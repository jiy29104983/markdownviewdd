#include "preview_controller.h"

#include "diagnostics.h"
#include "host_adapter.h"
#include "markdown_preview_dock.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QTextEdit>
#include <QTextDocument>
#include <QStringList>
#include <QTimer>
#include <QtMath>

namespace {
constexpr int kEditorFallbackPollIntervalMs = 1500;
constexpr int kMinimumRenderDebounceMs = 350;
constexpr int kMaximumRenderDebounceMs = 2000;
constexpr int kRenderDurationMultiplier = 3;
constexpr qint64 kSlowRenderThresholdMs = 750;
constexpr qint64 kLargeDocumentThresholdBytes = 1024 * 1024;
constexpr int kNativePreviewCacheCapacity = 3;
}

PreviewController::PreviewController(QWidget *notepad, HostAdapter *hostAdapter)
    : QObject(notepad),
      m_notepad(notepad),
      m_hostAdapter(hostAdapter ? hostAdapter : createNotepadHostAdapter(notepad)),
      m_ownsHostAdapter(!hostAdapter),
      m_mainWindow(qobject_cast<QMainWindow *>(notepad))
{
    Diagnostics::write(this, QStringLiteral("PreviewController constructor entered"));
    m_dock = new MarkdownPreviewDock(notepad);
    if (m_mainWindow) {
        m_mainWindow->addDockWidget(Qt::RightDockWidgetArea, m_dock);
    } else {
        m_dock->setFloating(true);
        m_dock->resize(520, 720);
    }
    m_dock->hide();

    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(kMinimumRenderDebounceMs);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kEditorFallbackPollIntervalMs);

    m_hostEventTimer = new QTimer(this);
    m_hostEventTimer->setSingleShot(true);
    m_hostEventTimer->setInterval(0);

    connect(m_renderTimer, &QTimer::timeout,
            this, &PreviewController::renderScheduled);
    connect(m_pollTimer, &QTimer::timeout, this, &PreviewController::pollEditor);
    connect(m_hostEventTimer, &QTimer::timeout,
            this, &PreviewController::synchronizeFromHostEvent);
    connect(m_dock, &MarkdownPreviewDock::refreshRequested,
            this, &PreviewController::renderNow);
    connect(m_dock, &MarkdownPreviewDock::displayedPreviewDestroyed, this, [this]() {
        m_previewEditor = nullptr;
        m_renderedVersion = 0;
        if (PreviewCacheEntry *entry = previewCacheEntry(m_editor.data())) {
            entry->renderedVersion = 0;
            entry->hasNativePreview = false;
        }
        m_hostAdapter->setPreviewCurrent(m_editor.data(), false);
        showCachedPreviewOrMessage();
        publishStatus();
    });
    connect(m_dock, &MarkdownPreviewDock::refreshModeChanged,
            this, &PreviewController::setRefreshMode);
    connect(m_dock, &MarkdownPreviewDock::syncScrollingChanged,
            this, &PreviewController::setSyncScrolling);
    connect(m_dock, &MarkdownPreviewDock::previewScrollRatioChanged,
            this, &PreviewController::scrollEditorToRatio);
    connect(m_dock, &MarkdownPreviewDock::previewScrollRangeChanged,
            this, [this]() {
        // Qt estimates the scrollbar range while a large rich-text document
        // is being laid out.  Reapply the ratio when that estimate changes so
        // the preview cannot remain at an obsolete blank offset.
        m_hasLastEditorScrollState = false;
        updateSynchronizedScroll();
    });
    connect(m_dock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        Diagnostics::write(this, QStringLiteral("dock visibilityChanged=%1")
                               .arg(visible));
        if (m_toggleAction && (visible || m_dock->isHidden())) {
            // visibilityChanged(false) is also emitted when the main window is
            // minimized.  In that case the dock is not explicitly hidden, so
            // keep the user's requested state and let Qt restore it together
            // with the main window.  Blocking the action also prevents a
            // visibility notification from calling setVisible() recursively.
            const QSignalBlocker blocker(m_toggleAction);
            m_toggleAction->setChecked(visible);
        }
        if (visible) {
            // Defer rich-text layout until the dock has finished showing.
            // Using the same debounce timer as editor updates also coalesces
            // a context-menu request with this visibility notification.
            pollEditor();
            scheduleRender();
        } else {
            m_renderTimer->stop();
            if (disconnectHostImmediateRefresh()) {
                Diagnostics::write(this,
                    QStringLiteral("host immediate refresh disconnected while dock hidden"));
            }
        }
        updatePollTimerState();
    });

    ensureHostEventConnection();
    pollEditor();
    updatePollTimerState();
    publishStatus();
    qApp->installEventFilter(this);
    Diagnostics::write(this, QStringLiteral("PreviewController constructor completed"));
}

PreviewController::~PreviewController()
{
    qApp->removeEventFilter(this);
    for (const PreviewCacheEntry &entry : m_previewCache) {
        disconnect(entry.textConnection);
        disconnect(entry.destroyedConnection);
    }
    if (m_ownsHostAdapter) {
        delete m_hostAdapter;
    }
}

bool PreviewController::installMenu(QMenu *rootMenu)
{
    if (!rootMenu || !m_dock) {
        return false;
    }
    if (m_rootMenu) {
        return m_rootMenu == rootMenu;
    }

    m_toggleAction = rootMenu->addAction(tr("显示/隐藏预览"));
    m_toggleAction->setCheckable(true);
    m_toggleAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    m_toggleAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_toggleAction, &QAction::toggled,
            this, &PreviewController::togglePreview);

    auto *refreshAction = rootMenu->addAction(tr("立即刷新"));
    connect(refreshAction, &QAction::triggered,
            this, &PreviewController::renderNow);

    auto *modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    m_automaticAction = rootMenu->addAction(tr("自动刷新"));
    m_manualAction = rootMenu->addAction(tr("手动刷新"));
    for (QAction *action : {m_automaticAction.data(), m_manualAction.data()}) {
        action->setCheckable(true);
        modeGroup->addAction(action);
    }
    connect(m_automaticAction, &QAction::triggered, this, [this]() {
        setRefreshMode(RefreshMode::Automatic);
    });
    connect(m_manualAction, &QAction::triggered, this, [this]() {
        setRefreshMode(RefreshMode::Manual);
    });
    publishStatus();

    m_syncAction = rootMenu->addAction(tr("同步编辑器滚动"));
    m_syncAction->setCheckable(true);
    m_syncAction->setChecked(m_syncScrolling);
    connect(m_syncAction, &QAction::toggled,
            this, &PreviewController::setSyncScrolling);

    m_exportAction = rootMenu->addAction(tr("导出 HTML…"));
    connect(m_exportAction, &QAction::triggered,
            this, &PreviewController::exportCurrentHtml);
    updateExportActionState();

    rootMenu->addSeparator();
    auto *aboutAction = rootMenu->addAction(tr("关于 Markdown 预览"));
    connect(aboutAction, &QAction::triggered,
            this, &PreviewController::showAbout);
    m_rootMenu = rootMenu;
    return true;
}

bool PreviewController::eventFilter(QObject *watched, QEvent *event)
{
    if (m_hostAdapter->isFilePathChangeEvent(event)) {
        if (watched == m_editor) {
            handleFilePathChanged();
        } else if (PreviewCacheEntry *entry = previewCacheEntry(
                       qobject_cast<QWidget *>(watched))) {
            entry->filePath = m_hostAdapter->filePath(entry->editor.data());
            entry->contentVersion = ++m_nextContentVersion;
            entry->error.clear();
            m_hostAdapter->setPreviewCurrent(entry->editor.data(), false);
        }
    }

    if (event && event->type() == QEvent::Show) {
        QMenu *menu = qobject_cast<QMenu *>(watched);
        QWidget *current = resolveCurrentEditor();
        if (m_hostAdapter->isEditorContextMenu(menu, current)) {
            if (current != m_editor) {
                attachEditor(current);
            }
            bridgeEditorContextMenu(menu);
        }
    }

    return QObject::eventFilter(watched, event);
}

void PreviewController::bridgeEditorContextMenu(QMenu *menu)
{
    if (!menu) {
        return;
    }

    const QList<QAction *> actions = menu->actions();
    for (QAction *action : actions) {
        if (!m_hostAdapter->isMarkdownContextAction(action)) {
            continue;
        }

        action->setText(tr("在侧边栏预览 Markdown"));

        // notepad-- creates this QAction on every context-menu invocation and
        // connects it directly to the host's native preview slot.  The plugin
        // must be the sole receiver; otherwise the host can show a
        // top-level MarkdownView before (or after) the dock adopts it.
        const bool disconnected = m_hostAdapter->bridgeMarkdownContextAction(
            action, m_editor.data());
        connect(action, &QAction::triggered,
                this, &PreviewController::showPreviewFromNativeAction);
        Diagnostics::write(this,
            QStringLiteral("native context Markdown action bridged; host disconnected=%1")
                .arg(disconnected));
        break;
    }
}

void PreviewController::showPreviewFromNativeAction()
{
    Diagnostics::write(this, QStringLiteral("native context Markdown action triggered"));
    pollEditor();
    if (!m_dock || !m_editor) {
        return;
    }

    m_dock->setVisible(true);
    m_dock->raise();

    // The original QAction connection has been removed.  Route the request
    // through the debounce timer so showing the dock and triggering the action
    // cannot render the document twice.
    scheduleRender();
}

void PreviewController::pollEditor()
{
    ensureHostEventConnection();
    synchronizeActiveEditor();

    if (m_dock && m_dock->isVisible() && m_syncScrolling) {
        updateSynchronizedScroll();
    }
}

void PreviewController::ensureHostEventConnection()
{
    if (m_activeEditorConnection || !m_hostAdapter) {
        return;
    }
    m_activeEditorConnection = m_hostAdapter->connectActiveEditorChanged(
        this, [this]() {
            // The host can emit currentChanged before it finishes updating
            // the new page's dynamic properties. Coalesce the event and read
            // the complete state after the current event has returned.
            m_hostEventTimer->start();
        });
}

void PreviewController::synchronizeFromHostEvent()
{
    synchronizeActiveEditor();
    if (m_dock && m_dock->isVisible() && m_syncScrolling) {
        updateSynchronizedScroll();
    }
}

void PreviewController::onEditorTextChanged()
{
    QWidget *changedEditor = qobject_cast<QWidget *>(sender());
    if (!changedEditor) {
        return;
    }
    // The plugin connection is installed before the host preview connection.
    // Recheck on every edit so a host action cannot silently restore its
    // synchronous full-document renderer.
    if (m_hostAdapter->disconnectImmediateRefresh(changedEditor)) {
        Diagnostics::write(this,
            QStringLiteral("host immediate refresh reconnected; disconnected on edit"));
    }
    m_hostAdapter->setPreviewCurrent(changedEditor, false);
    if (changedEditor != m_editor) {
        // Keep the displayed snapshot version, but advance the source version.
        if (PreviewCacheEntry *entry = previewCacheEntry(changedEditor)) {
            entry->contentVersion = ++m_nextContentVersion;
            entry->error.clear();
        }
        return;
    }
    markPreviewPending();
    scheduleAutomaticRender();
}

QWidget *PreviewController::resolveCurrentEditor() const
{
    return m_hostAdapter ? m_hostAdapter->currentEditor() : nullptr;
}

bool PreviewController::automaticRenderAllowed(bool allowInitialRender) const
{
    if (m_refreshMode != RefreshMode::Automatic || m_renderInProgress ||
        !m_editor || !isMarkdownDocument(m_editorFilePath) ||
        !m_dock || !m_dock->isVisible() || isPreviewCurrent()) {
        return false;
    }
    if (!m_performanceProtected) {
        return true;
    }
    // Preserve the existing first-open policy; buffer-size/first-render
    // protection is REQ-009. Mode changes and edits never use this exception.
    if (allowInitialRender) {
        for (const PreviewCacheEntry &entry : m_previewCache) {
            if (entry.editor == m_editor) {
                return !entry.hasRendered;
            }
        }
    }
    return false;
}

void PreviewController::scheduleRender()
{
    if (!automaticRenderAllowed(true)) {
        m_renderTimer->stop();
        return;
    }
    const qint64 adaptiveDelay = qBound<qint64>(
        kMinimumRenderDebounceMs,
        m_lastRenderDurationMs * kRenderDurationMultiplier,
        kMaximumRenderDebounceMs);
    m_scheduledEditor = m_editor;
    m_scheduledVersion = m_contentVersion;
    m_allowInitialAutomaticRender = true;
    m_renderTimer->start(static_cast<int>(adaptiveDelay));
}

void PreviewController::scheduleAutomaticRender()
{
    updateLargeDocumentPolicy(m_lastRenderDurationMs);
    if (!automaticRenderAllowed(false)) {
        m_renderTimer->stop();
    } else {
        scheduleRender();
        m_allowInitialAutomaticRender = false;
    }
    publishStatus();
}

void PreviewController::renderScheduled()
{
    const QPointer<QWidget> scheduledEditor = m_scheduledEditor;
    const quint64 scheduledVersion = m_scheduledVersion;
    const bool allowInitialRender = m_allowInitialAutomaticRender;
    synchronizeActiveEditor();
    if (!scheduledEditor || scheduledEditor != m_editor ||
        scheduledVersion != m_contentVersion ||
        !automaticRenderAllowed(allowInitialRender)) {
        return;
    }
    renderCurrentDocument(false);
}

void PreviewController::setRefreshMode(RefreshMode mode)
{
    if (m_refreshMode == mode) {
        return;
    }
    m_refreshMode = mode;
    m_renderTimer->stop();
    m_scheduledEditor = nullptr;
    synchronizeActiveEditor();
    if (mode == RefreshMode::Automatic) {
        scheduleAutomaticRender();
    } else {
        publishStatus();
    }
}

PreviewStatus PreviewController::previewStatus() const
{
    PreviewStatus status;
    status.mode = m_refreshMode;
    status.state = m_previewState;
    status.activeEditor = m_editor;
    status.displayedEditor = m_previewEditor;
    status.filePath = m_editorFilePath;
    status.contentVersion = m_contentVersion;
    status.displayedVersion = m_renderedVersion;
    status.performanceProtected = m_performanceProtected;
    status.protectionReason = protectionReason();
    status.error = m_lastHostError;
    if (status.state == PreviewState::Pending &&
        m_refreshMode == RefreshMode::Automatic && m_performanceProtected) {
        status.state = PreviewState::Paused;
    }
    return status;
}

QString PreviewController::protectionReason() const
{
    if (!m_performanceProtected) {
        return QString();
    }
    return m_lastRenderDurationMs >= kSlowRenderThresholdMs
        ? tr("最近完整渲染耗时达到 750 ms，后续自动刷新受性能保护限制。")
        : tr("文件大小达到 1 MiB，后续自动刷新受性能保护限制。");
}

void PreviewController::publishStatus()
{
    const PreviewStatus status = previewStatus();
    if (m_automaticAction && m_manualAction) {
        const QSignalBlocker automaticBlocker(m_automaticAction);
        const QSignalBlocker manualBlocker(m_manualAction);
        m_automaticAction->setChecked(status.mode == RefreshMode::Automatic);
        m_manualAction->setChecked(status.mode == RefreshMode::Manual);
    }
    if (m_dock) {
        m_dock->setRefreshMode(status.mode);
        m_dock->setDocumentInfo(status.filePath, -1, !status.activeEditor.isNull());
        QString text;
        QString details;
        switch (status.state) {
        case PreviewState::NoDocument:
            text = tr("没有活动文档");
            break;
        case PreviewState::Unsupported:
            text = tr("不支持当前文件类型");
            break;
        case PreviewState::Ready:
            text = tr("已同步");
            if (status.performanceProtected) {
                text += tr(" · 后续自动刷新受性能保护限制");
            }
            details = status.protectionReason;
            break;
        case PreviewState::Paused:
            text = tr("性能保护暂停 · 待手工刷新");
            details = status.protectionReason;
            break;
        case PreviewState::Pending:
            text = status.displayedEditor ? tr("待刷新 · 正在显示旧快照")
                                          : tr("待刷新 · 点击刷新生成预览");
            break;
        case PreviewState::Failed:
            text = status.displayedEditor ? tr("刷新失败 · 正在显示旧快照")
                                          : tr("刷新失败 · 点击重试");
            text += tr("：%1").arg(status.error);
            details = status.error;
            break;
        }
        m_dock->setRefreshStatus(text, details, status.state == PreviewState::Failed);
    }
    updateExportActionState();
    emit previewStatusChanged();
}

void PreviewController::renderNow()
{
    renderCurrentDocument(true, true);
}

bool PreviewController::renderCurrentDocument(bool allowHiddenDock,
                                              bool forceHostUpdate)
{
    // Synchronous host callbacks can process events. Never start a nested render.
    if (m_renderInProgress) {
        return false;
    }
    synchronizeActiveEditor();
    m_renderTimer->stop();
    m_scheduledEditor = nullptr;
    QScopedValueRollback<bool> rendering(m_renderInProgress, true);
    Diagnostics::write(this, QStringLiteral("renderNow entered"),
                       Diagnostics::Level::Debug);
    if (!m_dock || (!allowHiddenDock && !m_dock->isVisible())) {
        Diagnostics::write(this, QStringLiteral("renderNow skipped: dock hidden"));
        return false;
    }

    if (!m_editor || !isMarkdownDocument(m_editorFilePath)) {
        showCachedPreviewOrMessage();
        publishStatus();
        return false;
    }
    const QString filePath = m_editorFilePath;

    QPointer<QWidget> renderEditor = m_editor;
    const quint64 renderVersion = m_contentVersion;
    double preservedScrollRatio = 0.0;
    const bool preserveScroll = !m_syncScrolling &&
        m_dock->nativeScrollRatioFor(renderEditor.data(),
                                     &preservedScrollRatio);
    QElapsedTimer elapsed;
    elapsed.start();
    bool performedFullRender = false;
    if (!activateNativePreview(forceHostUpdate, &performedFullRender)) {
        synchronizeActiveEditor();
        if (m_editor != renderEditor || m_contentVersion != renderVersion) {
            // A new document/version is scheduled only after this call unwinds.
            QTimer::singleShot(0, this, &PreviewController::scheduleAutomaticRender);
            return false;
        }
        if (m_lastHostError.isEmpty()) {
            m_lastHostError = tr("宿主未提供可用的原生 Markdown 预览能力。");
        }
        if (PreviewCacheEntry *entry = previewCacheEntry(renderEditor.data())) {
            entry->error = m_lastHostError;
        }
        showCachedPreviewOrMessage();
        m_previewState = PreviewState::Failed;
        m_hostAdapter->setPreviewCurrent(renderEditor.data(), false);
        m_dock->markPreviewStale();
        publishStatus();
        return false;
    }
    const qint64 operationDurationMs = elapsed.elapsed();
    Diagnostics::write(this, QStringLiteral("renderNow completed in %1 ms")
                           .arg(operationDurationMs));

    synchronizeActiveEditor();
    if (m_editor != renderEditor || m_contentVersion != renderVersion) {
        Diagnostics::write(this, QStringLiteral("renderNow discarded: editor state changed"));
        if (PreviewCacheEntry *entry = previewCacheEntry(renderEditor.data())) {
            // The native QTextDocument was mutated but has no accepted version.
            entry->renderedVersion = 0;
            m_hostAdapter->setPreviewCurrent(renderEditor.data(), false);
        }
        showCachedPreviewOrMessage();
        publishStatus();
        QTimer::singleShot(0, this, &PreviewController::scheduleAutomaticRender);
        return false;
    }

    if (performedFullRender) {
        m_lastRenderDurationMs = operationDurationMs;
        if (PreviewCacheEntry *entry = previewCacheEntry(renderEditor.data())) {
            entry->lastFullRenderDurationMs = m_lastRenderDurationMs;
        }
    }
    updateLargeDocumentPolicy(m_lastRenderDurationMs);

    m_previewEditor = renderEditor;
    m_renderedVersion = renderVersion;
    m_previewState = PreviewState::Ready;
    m_lastHostError.clear();
    if (PreviewCacheEntry *entry = previewCacheEntry(renderEditor.data())) {
        entry->renderedVersion = renderVersion;
        entry->snapshotFilePath = filePath;
        entry->hasRendered = true;
        entry->error.clear();
    }
    m_hostAdapter->setPreviewCurrent(renderEditor.data(), true);
    m_dock->setDocumentInfo(filePath, -1);
    m_hasLastEditorScrollState = false;
    if (m_syncScrolling) {
        QTimer::singleShot(0, this, &PreviewController::updateSynchronizedScroll);
    } else if (preserveScroll) {
        m_dock->preserveNativeScrollRatio(
            renderEditor.data(), renderVersion, preservedScrollRatio);
    }
    m_renderInProgress = false;
    publishStatus();
    return true;
}

void PreviewController::togglePreview(bool visible)
{
    Diagnostics::write(this, QStringLiteral("togglePreview entered: %1").arg(visible));
    if (!m_dock) {
        return;
    }
    m_dock->setVisible(visible);
    Diagnostics::write(this, QStringLiteral("dock setVisible returned"));
    if (visible) {
        Diagnostics::write(this, QStringLiteral("calling dock raise"));
        m_dock->raise();
        Diagnostics::write(this, QStringLiteral("dock raise returned"));
    }
}

void PreviewController::setSyncScrolling(bool enabled)
{
    m_syncScrolling = enabled;
    if (m_syncAction && m_syncAction->isChecked() != enabled) {
        m_syncAction->setChecked(enabled);
    }
    if (m_dock) {
        m_dock->setSyncScrolling(enabled);
    }
    m_hasLastEditorScrollState = false;
    if (enabled) {
        updateSynchronizedScroll();
    }
}

void PreviewController::scrollEditorToRatio(double ratio)
{
    synchronizeActiveEditor();
    if (!isPreviewCurrent() || !m_dock->isVisible() || !m_syncScrolling) {
        return;
    }

    QAbstractScrollArea *editorArea =
        qobject_cast<QAbstractScrollArea *>(m_editor.data());
    QScrollBar *editorBar = editorArea ? editorArea->verticalScrollBar() : nullptr;
    if (!editorBar || editorBar->maximum() <= editorBar->minimum()) {
        return;
    }

    ratio = qBound(0.0, ratio, 1.0);
    const int value = editorBar->minimum() +
        qRound(ratio * static_cast<double>(
            editorBar->maximum() - editorBar->minimum()));
    m_syncingEditorScroll = true;
    // QScintilla consumes valueChanged to scroll its document. Only suppress
    // our own feedback callback, never the host's scrollbar signal receivers.
    editorBar->setValue(value);
    m_syncingEditorScroll = false;

    // Remember the complete programmatic state so the event callback and the
    // fallback poll cannot echo this write back to the preview.
    m_lastScrollEditor = m_editor;
    m_lastEditorScrollMinimum = editorBar->minimum();
    m_lastEditorScrollMaximum = editorBar->maximum();
    m_lastEditorScrollValue = editorBar->value();
    m_hasLastEditorScrollState = true;
}

bool PreviewController::currentHtmlSnapshot(QByteArray *html,
                                            QString *sourceFilePath)
{
    if (!html || !sourceFilePath) {
        return false;
    }

    html->clear();
    sourceFilePath->clear();
    synchronizeActiveEditor();
    updateExportActionState();
    if (!m_editor || !isMarkdownDocument(currentFilePath())) {
        return false;
    }

    if (!isPreviewCurrent() && !renderCurrentDocument(true)) {
        return false;
    }

    synchronizeActiveEditor();
    if (!isPreviewCurrent()) {
        Diagnostics::write(this,
            QStringLiteral("export snapshot discarded: editor state changed"));
        return false;
    }

    const QPointer<QWidget> snapshotEditor = m_editor;
    const quint64 snapshotVersion = m_contentVersion;
    const QString snapshotPath = currentFilePath();
    const QByteArray snapshot = m_dock->htmlSnapshotFor(
        snapshotEditor.data(), snapshotVersion);
    if (snapshot.isEmpty()) {
        Diagnostics::write(this, QStringLiteral("export snapshot unavailable"));
        return false;
    }

    *html = snapshot;
    *sourceFilePath = snapshotPath;
    return true;
}

void PreviewController::exportCurrentHtml()
{
    QByteArray html;
    QString sourceFilePath;
    if (!currentHtmlSnapshot(&html, &sourceFilePath)) {
        QMessageBox::warning(
            m_notepad, tr("无法导出 HTML"),
            tr("当前活动 Markdown 文档没有可导出的最新有效预览。请确认文档类型后重试。"));
        return;
    }

    m_dock->saveHtmlSnapshot(m_notepad, html, sourceFilePath);
}

void PreviewController::showAbout()
{
    QMessageBox::about(
        m_notepad,
        tr("关于 Markdown 预览"),
        tr("Markdown 预览 %1\n\n"
           "面向 notepad-- 的实时 Markdown 文件预览插件。\n"
           "复用 notepad-- v3.8 原生 Markdown 渲染并嵌入侧边栏。")
            .arg(QStringLiteral(NDD_MARKDOWN_VIEW_VERSION)));
}

void PreviewController::attachEditor(QWidget *editor)
{
    rememberCurrentPreviewScroll();
    if (m_editorScrollValueConnection) {
        disconnect(m_editorScrollValueConnection);
        m_editorScrollValueConnection = QMetaObject::Connection();
    }
    if (m_editorScrollRangeConnection) {
        disconnect(m_editorScrollRangeConnection);
        m_editorScrollRangeConnection = QMetaObject::Connection();
    }

    m_editor = editor;
    observeEditor(editor);
    m_editorFilePath = currentFilePath();
    const PreviewCacheEntry *entry = previewCacheEntry(editor);
    m_contentVersion = entry ? entry->contentVersion : 0;
    m_previewEditor = nullptr;
    m_renderedVersion = 0;
    m_previewState = m_editor ? PreviewState::Pending : PreviewState::NoDocument;
    if (m_dock) {
        m_dock->invalidatePreview();
    }
    m_lastScrollEditor = nullptr;
    m_hasLastEditorScrollState = false;
    m_lastRenderDurationMs = entry ? entry->lastFullRenderDurationMs : 0;
    m_lastHostError = entry ? entry->error : QString();
    updateLargeDocumentPolicy(m_lastRenderDurationMs);

    if (m_editor) {
        Diagnostics::write(this, QStringLiteral("attaching editor class=%1")
                               .arg(QString::fromLatin1(m_editor->metaObject()->className())));
        const HostAdapter::ScrollConnections scrollConnections =
            m_hostAdapter->connectEditorScrollChanged(
            m_editor.data(), this, [this]() {
                if (!m_syncingEditorScroll) {
                    updateSynchronizedScroll();
                }
            });
        m_editorScrollValueConnection = scrollConnections.valueChanged;
        m_editorScrollRangeConnection = scrollConnections.rangeChanged;
        if (disconnectHostImmediateRefresh()) {
            Diagnostics::write(this,
                QStringLiteral("existing host immediate refresh disconnected on attach"));
        }
    }
    showCachedPreviewOrMessage();
    publishStatus();
}

void PreviewController::observeEditor(QWidget *editor)
{
    if (!editor) {
        return;
    }
    if (PreviewCacheEntry *entry = previewCacheEntry(editor)) {
        const QString filePath = m_hostAdapter->filePath(editor);
        if (entry->filePath != filePath) {
            entry->filePath = filePath;
            entry->contentVersion = ++m_nextContentVersion;
            entry->error.clear();
            m_hostAdapter->setPreviewCurrent(editor, false);
        }
        return;
    }

    PreviewCacheEntry entry;
    entry.editor = editor;
    entry.filePath = m_hostAdapter->filePath(editor);
    entry.contentVersion = ++m_nextContentVersion;
    entry.textConnection = connect(editor, SIGNAL(textChanged()),
                                   this, SLOT(onEditorTextChanged()),
                                   Qt::UniqueConnection);
    entry.destroyedConnection = connect(editor, &QObject::destroyed,
                                        this, &PreviewController::onEditorDestroyed);
    m_previewCache.append(entry);
    Diagnostics::write(this, QStringLiteral("runtime textChanged connection=%1")
                               .arg(static_cast<bool>(entry.textConnection)));
}

void PreviewController::onEditorDestroyed(QObject *editor)
{
    // QWidget can emit destroyed before its QPointer is cleared. Remove both
    // that exact entry and entries whose guards have already become null.
    for (int i = m_previewCache.size() - 1; i >= 0; --i) {
        if (!m_previewCache.at(i).editor || m_previewCache.at(i).editor == editor) {
            m_previewCache.removeAt(i);
        }
    }
    if (m_editor && m_editor != editor) {
        return;
    }
    m_editor = nullptr;
    m_editorFilePath.clear();
    m_contentVersion = 0;
    m_previewEditor = nullptr;
    m_renderedVersion = 0;
    m_previewState = PreviewState::NoDocument;
    m_lastScrollEditor = nullptr;
    m_hasLastEditorScrollState = false;
    if (m_dock) {
        m_dock->invalidatePreview();
    }
    m_renderTimer->stop();
    m_lastRenderDurationMs = 0;
    m_performanceProtected = false;
    m_lastHostError.clear();
    showCachedPreviewOrMessage();
    publishStatus();
    m_hostEventTimer->start();
}

void PreviewController::handleFilePathChanged()
{
    if (!m_editor) {
        return;
    }

    const QString filePath = currentFilePath();
    if (filePath == m_editorFilePath) {
        return;
    }

    Diagnostics::write(this, QStringLiteral("editor filePath changed: %1 -> %2")
                           .arg(Diagnostics::pathIdentity(m_editorFilePath),
                                Diagnostics::pathIdentity(filePath)));
    m_editorFilePath = filePath;
    if (PreviewCacheEntry *entry = previewCacheEntry(m_editor.data())) {
        entry->filePath = filePath;
    }
    markPreviewPending();
    if (m_dock) {
        m_dock->setDocumentInfo(filePath, -1);
    }
    showCachedPreviewOrMessage();
    scheduleAutomaticRender();
}

void PreviewController::updateLargeDocumentPolicy(qint64 renderDurationMs)
{
    const bool fileIsLarge = fileExceedsAutomaticRefreshLimit();
    const bool renderWasSlow = renderDurationMs >= kSlowRenderThresholdMs;
    const bool wasProtected = m_performanceProtected;
    m_performanceProtected = fileIsLarge || renderWasSlow;
    if (m_performanceProtected && !wasProtected) {
        Diagnostics::write(this,
            QStringLiteral("performance protection enabled: fileBytes=%1, renderMs=%2")
                .arg(QFileInfo(currentFilePath()).size())
                .arg(renderDurationMs));
    }
}

bool PreviewController::fileExceedsAutomaticRefreshLimit() const
{
    const QString filePath = currentFilePath();
    if (filePath.isEmpty()) {
        return false;
    }
    const QFileInfo info(filePath);
    return info.exists() && info.isFile() &&
        info.size() >= kLargeDocumentThresholdBytes;
}

void PreviewController::synchronizeActiveEditor()
{
    QWidget *current = resolveCurrentEditor();
    if (current == m_editor) {
        handleFilePathChanged();
        return;
    }

    Diagnostics::write(this, QStringLiteral("active editor changed: 0x%1")
                           .arg(reinterpret_cast<quintptr>(current), 0, 16));
    attachEditor(current);
    if (m_dock && m_dock->isVisible()) {
        scheduleRender();
    }
}

void PreviewController::markPreviewPending()
{
    if (!m_editor) {
        return;
    }
    m_contentVersion = ++m_nextContentVersion;
    if (PreviewCacheEntry *entry = previewCacheEntry(m_editor.data())) {
        entry->contentVersion = m_contentVersion;
        entry->error.clear();
    }
    m_lastHostError.clear();
    m_previewState = isMarkdownDocument(m_editorFilePath)
        ? PreviewState::Pending : PreviewState::Unsupported;
    m_hostAdapter->setPreviewCurrent(m_editor.data(), false);
    if (m_dock) {
        m_dock->markPreviewStale();
    }
    // scheduleAutomaticRender publishes the complete mode/protection state.
}

void PreviewController::showCachedPreviewOrMessage()
{
    if (!m_dock) {
        return;
    }
    m_previewEditor = nullptr;
    m_renderedVersion = 0;
    m_dock->invalidatePreview();
    if (!m_editor) {
        m_previewState = PreviewState::NoDocument;
        m_dock->showMessage(tr("没有活动文档"), tr("打开一个 Markdown 文件后即可预览。"));
        return;
    }
    if (!isMarkdownDocument(m_editorFilePath)) {
        m_previewState = PreviewState::Unsupported;
        m_dock->showMessage(tr("当前文档不是 Markdown 文件"),
                           tr("支持 .md、.markdown、.mdown、.mkd、.mkdn 和 .mdwn 文件。"));
        return;
    }
    const PreviewCacheEntry *cached = previewCacheEntry(m_editor.data());
    if (cached && cached->renderedVersion && cached->previewWindow && cached->previewTextEdit) {
        // Copy before adoption: Qt signals may re-enter the controller.
        const PreviewCacheEntry entry = *cached;
        if (m_dock->adoptNativePreview(entry.previewWindow, entry.previewTextEdit,
                                      entry.snapshotFilePath, m_editor.data(),
                                      entry.renderedVersion)) {
            m_previewEditor = m_editor;
            m_renderedVersion = entry.renderedVersion;
            const bool current = entry.error.isEmpty() &&
                entry.renderedVersion == m_contentVersion &&
                m_hostAdapter->previewIsCurrent(m_editor.data());
            m_previewState = !entry.error.isEmpty() ? PreviewState::Failed
                : current ? PreviewState::Ready : PreviewState::Pending;
            if (!current) {
                m_dock->markPreviewStale();
            }
            touchPreviewCache(m_editor.data());
            return;
        }
    }
    m_previewState = m_lastHostError.isEmpty() ? PreviewState::Pending : PreviewState::Failed;
    m_dock->showMessage(m_lastHostError.isEmpty() ? tr("尚无可用预览") : tr("刷新失败"),
                       m_lastHostError.isEmpty() ? tr("点击“刷新”生成当前文档的预览。")
                                                 : m_lastHostError);
}

bool PreviewController::isPreviewCurrent() const
{
    return !m_renderInProgress && m_previewState == PreviewState::Ready && m_editor &&
        m_previewEditor == m_editor && m_renderedVersion == m_contentVersion &&
        m_dock && m_dock->hasPreviewFor(m_editor.data(), m_contentVersion);
}

void PreviewController::updateExportActionState()
{
    if (!m_exportAction) {
        return;
    }

    m_exportAction->setEnabled(
        m_editor && isMarkdownDocument(currentFilePath()));
}

bool PreviewController::disconnectHostImmediateRefresh(bool force)
{
    return m_hostAdapter && m_hostAdapter->disconnectImmediateRefresh(
        m_editor.data(), force);
}

bool PreviewController::activateNativePreview(bool forceHostUpdate,
                                               bool *performedFullRender)
{
    *performedFullRender = false;
    const QPointer<QWidget> editor = m_editor;
    const quint64 version = m_contentVersion;
    if (!editor || !m_dock) {
        return false;
    }
    m_lastHostError.clear();
    const HostAdapter::PreviewResult preview = m_hostAdapter->ensurePreview(editor.data());
    Diagnostics::write(this,
        QStringLiteral("host preview ensure created=%1, duration=%2 ms, error=%3")
            .arg(preview.created).arg(preview.durationMs).arg(preview.error));
    synchronizeActiveEditor();
    if (!preview.isValid() || !editor || editor != m_editor || version != m_contentVersion) {
        m_lastHostError = preview.error;
        if (preview.isValid() && editor) {
            // A first host render may have processed events and changed source.
            m_hostAdapter->setPreviewCurrent(editor.data(), false);
        }
        return false;
    }
    const bool reusablePreview = m_hostAdapter->previewIsCurrent(editor.data());
    QPointer<QTextEdit> textEdit = preview.textEdit;
    QPointer<QWidget> previewWindow = preview.window;
    if (!m_dock->adoptNativePreview(previewWindow, textEdit, m_editorFilePath,
                                    editor.data(), version)) {
        return false;
    }
    touchPreviewCache(editor.data());
    if (PreviewCacheEntry *entry = previewCacheEntry(editor.data())) {
        entry->previewWindow = previewWindow;
        entry->previewTextEdit = textEdit;
    }
    if (preview.created || (reusablePreview && !forceHostUpdate)) {
        *performedFullRender = preview.created;
        if (preview.created && !m_syncScrolling) {
            const PreviewCacheEntry *entry = previewCacheEntry(editor.data());
            if (entry && entry->hasScrollRatio) {
                m_dock->preserveNativeScrollRatio(editor.data(), version, entry->scrollRatio);
            }
        }
        enforcePreviewCacheLimit();
        return true;
    }

    // Do not clone the entire rich-text document on every refresh. Keep the
    // last successful snapshot only if a failed host call left it untouched.
    const QPointer<QTextDocument> previousDocument = textEdit->document();
    bool documentChanged = previousDocument->signalsBlocked();
    const QMetaObject::Connection contentConnection = connect(
        previousDocument, &QTextDocument::contentsChanged, this,
        [&documentChanged]() { documentChanged = true; }, Qt::DirectConnection);
    qint64 hostRenderDuration = 0;
    QString hostError;
    const bool updated = m_hostAdapter->refreshPreview(
        editor.data(), &hostRenderDuration, &hostError);
    disconnect(contentConnection);
    Diagnostics::write(this,
        QStringLiteral("host preview refresh returned: %1, duration=%2 ms, error=%3")
            .arg(updated).arg(hostRenderDuration).arg(hostError));
    *performedFullRender = updated;
    if (!updated) {
        if (!textEdit || !previousDocument || textEdit->document() != previousDocument ||
            documentChanged) {
            if (PreviewCacheEntry *entry = previewCacheEntry(editor.data())) {
                entry->renderedVersion = 0;
            }
        }
        m_lastHostError = hostError;
        m_hostAdapter->setPreviewCurrent(editor.data(), false);
    } else if (editor && editor == m_editor && version == m_contentVersion &&
               previewWindow && textEdit) {
        QElapsedTimer styleElapsed;
        styleElapsed.start();
        m_dock->refreshDocumentStyle(editor.data(), version);
        Diagnostics::write(this, QStringLiteral("document style refresh duration=%1 ms")
                               .arg(styleElapsed.elapsed()));
    }
    enforcePreviewCacheLimit();
    return updated && previewWindow && textEdit;
}

void PreviewController::rememberCurrentPreviewScroll()
{
    if (!m_editor || !m_dock) {
        return;
    }
    double ratio = 0.0;
    if (!m_dock->nativeScrollRatioFor(m_editor.data(), &ratio)) {
        return;
    }
    PreviewCacheEntry *entry = previewCacheEntry(m_editor.data());
    if (!entry) {
        PreviewCacheEntry newEntry;
        newEntry.editor = m_editor;
        m_previewCache.append(newEntry);
        entry = &m_previewCache.last();
    }
    entry->scrollRatio = ratio;
    entry->hasScrollRatio = true;
}

void PreviewController::touchPreviewCache(QWidget *editor)
{
    if (!editor) {
        return;
    }
    prunePreviewCache();
    for (int i = 0; i < m_previewCache.size(); ++i) {
        if (m_previewCache.at(i).editor == editor) {
            PreviewCacheEntry entry = m_previewCache.takeAt(i);
            entry.hasNativePreview = true;
            m_previewCache.append(entry);
            return;
        }
    }
    PreviewCacheEntry entry;
    entry.editor = editor;
    entry.hasNativePreview = true;
    m_previewCache.append(entry);
}

void PreviewController::enforcePreviewCacheLimit()
{
    prunePreviewCache();
    int cachedPreviewCount = 0;
    for (const PreviewCacheEntry &entry : m_previewCache) {
        if (entry.hasNativePreview) {
            ++cachedPreviewCount;
        }
    }
    while (cachedPreviewCount > kNativePreviewCacheCapacity) {
        int evictionIndex = -1;
        for (int i = 0; i < m_previewCache.size(); ++i) {
            if (m_previewCache.at(i).hasNativePreview &&
                m_previewCache.at(i).editor != m_editor) {
                evictionIndex = i;
                break;
            }
        }
        if (evictionIndex < 0) {
            return;
        }

        PreviewCacheEntry &entry = m_previewCache[evictionIndex];
        QString error;
        const bool released = m_hostAdapter->releasePreview(entry.editor.data(), &error);
        if (released) {
            entry.hasNativePreview = false;
            entry.previewWindow = nullptr;
            entry.previewTextEdit = nullptr;
            entry.renderedVersion = 0;
            --cachedPreviewCount;
        }
        Diagnostics::write(this,
            QStringLiteral("native preview cache evicted: released=%1, remaining=%2, error=%3")
                .arg(released).arg(cachedPreviewCount).arg(error));
        if (!released) {
            return;
        }
    }
}

PreviewController::PreviewCacheEntry *PreviewController::previewCacheEntry(
    QWidget *editor)
{
    prunePreviewCache();
    for (PreviewCacheEntry &entry : m_previewCache) {
        if (entry.editor == editor) {
            return &entry;
        }
    }
    return nullptr;
}

void PreviewController::prunePreviewCache()
{
    for (int i = m_previewCache.size() - 1; i >= 0; --i) {
        if (m_previewCache.at(i).editor.isNull()) {
            m_previewCache.removeAt(i);
        }
    }
}

void PreviewController::updateSynchronizedScroll()
{
    synchronizeActiveEditor();
    if (!isPreviewCurrent() || !m_dock->isVisible() || !m_syncScrolling) {
        return;
    }

    QAbstractScrollArea *editorArea =
        qobject_cast<QAbstractScrollArea *>(m_editor.data());
    QScrollBar *editorBar = editorArea ? editorArea->verticalScrollBar() : nullptr;
    if (!editorBar || editorBar->maximum() <= editorBar->minimum()) {
        return;
    }

    const int scrollMinimum = editorBar->minimum();
    const int scrollMaximum = editorBar->maximum();
    const int scrollValue = editorBar->value();
    if (m_hasLastEditorScrollState && m_lastScrollEditor == m_editor &&
        scrollMinimum == m_lastEditorScrollMinimum &&
        scrollMaximum == m_lastEditorScrollMaximum &&
        scrollValue == m_lastEditorScrollValue) {
        return;
    }

    m_lastScrollEditor = m_editor;
    m_lastEditorScrollMinimum = scrollMinimum;
    m_lastEditorScrollMaximum = scrollMaximum;
    m_lastEditorScrollValue = scrollValue;
    m_hasLastEditorScrollState = true;
    const double ratio = static_cast<double>(scrollValue - editorBar->minimum()) /
        static_cast<double>(editorBar->maximum() - editorBar->minimum());
    m_dock->scrollToRatio(ratio);
}

void PreviewController::updatePollTimerState()
{
    if (!m_pollTimer || !m_dock) {
        return;
    }
    if (m_dock->isVisible()) {
        if (!m_pollTimer->isActive()) {
            m_pollTimer->start();
        }
    } else {
        m_pollTimer->stop();
    }
}

QString PreviewController::currentFilePath() const
{
    return m_hostAdapter ? m_hostAdapter->filePath(m_editor.data()) : QString();
}

bool PreviewController::isMarkdownDocument(const QString &filePath) const
{
    if (filePath.isEmpty()) {
        return true;
    }

    const QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();
    if (suffix.isEmpty() && !info.exists()) {
        return true;
    }

    static const QStringList extensions = {
        QStringLiteral("md"), QStringLiteral("markdown"),
        QStringLiteral("mdown"), QStringLiteral("mkd"),
        QStringLiteral("mkdn"), QStringLiteral("mdwn")
    };
    return extensions.contains(suffix);
}
