#include "preview_controller.h"

#include "diagnostics.h"
#include "markdown_preview_dock.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
#include <QVariant>
#include <QtMath>

namespace {
constexpr int kEditorPollIntervalMs = 120;
constexpr int kMinimumRenderDebounceMs = 350;
constexpr int kMaximumRenderDebounceMs = 2000;
constexpr int kRenderDurationMultiplier = 3;
constexpr auto kNativePreviewProperty = "_markdownview_native_preview";
constexpr auto kNativePreviewOwnerHook = "_markdownview_owner_hook";
constexpr auto kContextActionBridge = "_markdownview_sidebar_bridge";
constexpr auto kFilePathProperty = "filePath";
}

PreviewController::PreviewController(QWidget *notepad)
    : QObject(notepad),
      m_notepad(notepad),
      m_mainWindow(qobject_cast<QMainWindow *>(notepad))
{
    Diagnostics::write(QStringLiteral("PreviewController constructor entered"));
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
    m_pollTimer->setInterval(kEditorPollIntervalMs);

    connect(m_renderTimer, &QTimer::timeout, this, &PreviewController::renderNow);
    connect(m_pollTimer, &QTimer::timeout, this, &PreviewController::pollEditor);
    connect(m_dock, &MarkdownPreviewDock::refreshRequested,
            this, &PreviewController::renderNow);
    connect(m_dock, &MarkdownPreviewDock::syncScrollingChanged,
            this, &PreviewController::setSyncScrolling);
    connect(m_dock, &MarkdownPreviewDock::previewScrollRatioChanged,
            this, &PreviewController::scrollEditorToRatio);
    connect(m_dock, &MarkdownPreviewDock::previewScrollRangeChanged,
            this, [this]() {
        // Qt estimates the scrollbar range while a large rich-text document
        // is being laid out.  Reapply the ratio when that estimate changes so
        // the preview cannot remain at an obsolete blank offset.
        m_lastEditorScrollValue = -1;
        updateSynchronizedScroll();
    });
    connect(m_dock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        Diagnostics::write(QStringLiteral("dock visibilityChanged=%1")
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
                Diagnostics::write(
                    QStringLiteral("host immediate refresh disconnected while dock hidden"));
            }
        }
    });

    m_pollTimer->start();
    pollEditor();
    qApp->installEventFilter(this);
    Diagnostics::write(QStringLiteral("PreviewController constructor completed"));
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
    if (event && event->type() == QEvent::DynamicPropertyChange &&
        watched == m_editor) {
        auto *propertyEvent = static_cast<QDynamicPropertyChangeEvent *>(event);
        if (propertyEvent->propertyName() == QByteArray(kFilePathProperty)) {
            handleFilePathChanged();
        }
    }

    if (event && event->type() == QEvent::Show) {
        QMenu *menu = qobject_cast<QMenu *>(watched);
        QWidget *current = resolveCurrentEditor();
        QWidget *menuParent = menu ? menu->parentWidget() : nullptr;
        const bool belongsToHostWindow = menuParent && current && m_notepad &&
            menuParent->window() == m_notepad->window() &&
            current->window() == m_notepad->window();
        if (belongsToHostWindow && menuParent == current) {
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
        if (!action || action->property(kContextActionBridge).toBool()) {
            continue;
        }

        QString text = action->text();
        text.remove(QLatin1Char('&'));
        if (!text.contains(QStringLiteral("markdown"), Qt::CaseInsensitive)) {
            continue;
        }

        action->setText(tr("在侧边栏预览 Markdown"));
        action->setProperty(kContextActionBridge, true);

        // notepad-- creates this QAction on every context-menu invocation and
        // connects it directly to ScintillaEditView::on_viewMarkdown().  The
        // plugin must be the sole receiver; otherwise the host slot can show a
        // top-level MarkdownView before (or after) the dock adopts it.
        const bool disconnected = m_editor && QObject::disconnect(
            action, nullptr, m_editor.data(), nullptr);
        connect(action, &QAction::triggered,
                this, &PreviewController::showPreviewFromNativeAction);
        Diagnostics::write(
            QStringLiteral("native context Markdown action bridged; host disconnected=%1")
                .arg(disconnected));
        break;
    }
}

void PreviewController::showPreviewFromNativeAction()
{
    Diagnostics::write(QStringLiteral("native context Markdown action triggered"));
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
    synchronizeActiveEditor();

    if (m_dock && m_dock->isVisible() && m_syncScrolling) {
        updateSynchronizedScroll();
    }
}

void PreviewController::onEditorTextChanged()
{
    // The plugin connection is installed before the host preview connection.
    // Recheck on every edit so a host action cannot silently restore its
    // synchronous full-document renderer.
    if (disconnectHostImmediateRefresh()) {
        Diagnostics::write(
            QStringLiteral("host immediate refresh reconnected; disconnected on edit"));
    }
    markPreviewPending();
    scheduleRender();
}

QWidget *PreviewController::resolveCurrentEditor() const
{
    if (m_notepad) {
        QTabWidget *tabs = m_notepad->findChild<QTabWidget *>(
            QStringLiteral("editTabWidget"));
        if (tabs) {
            QWidget *page = tabs->currentWidget();
            if (page && page->inherits("QsciScintilla")) {
                return page;
            }
        }
    }

    return nullptr;
}

void PreviewController::scheduleRender()
{
    if (!m_renderTimer) {
        return;
    }

    if (!m_dock || !m_dock->isVisible()) {
        m_renderTimer->stop();
        return;
    }

    // Repeated start() calls form a trailing-edge debounce.  Let expensive
    // documents remain stale slightly longer so rendering never competes with
    // a continuous typing burst.
    const qint64 adaptiveDelay = qBound<qint64>(
        kMinimumRenderDebounceMs,
        m_lastRenderDurationMs * kRenderDurationMultiplier,
        kMaximumRenderDebounceMs);
    m_renderTimer->start(static_cast<int>(adaptiveDelay));
}

void PreviewController::renderNow()
{
    renderCurrentDocument(false);
}

bool PreviewController::renderCurrentDocument(bool allowHiddenDock)
{
    // Manual refreshes and explicit renders supersede any pending automatic
    // refresh for the same editor state.
    synchronizeActiveEditor();
    m_renderTimer->stop();
    Diagnostics::write(QStringLiteral("renderNow entered"));
    if (!m_dock || (!allowHiddenDock && !m_dock->isVisible())) {
        Diagnostics::write(QStringLiteral("renderNow skipped: dock hidden"));
        return false;
    }

    if (!m_editor) {
        m_previewState = PreviewState::NoDocument;
        m_previewEditor = nullptr;
        m_renderedVersion = 0;
        Diagnostics::write(QStringLiteral("renderNow: no active editor"));
        m_dock->showMessage(tr("没有活动文档"),
                            tr("打开一个 Markdown 文件后即可预览。"));
        updateExportActionState();
        return false;
    }

    const QString filePath = currentFilePath();
    Diagnostics::write(QStringLiteral("filePath read: %1").arg(filePath));
    if (!isMarkdownDocument(filePath)) {
        m_previewState = PreviewState::Unsupported;
        m_previewEditor = nullptr;
        m_renderedVersion = 0;
        m_dock->showMessage(
            tr("当前文档不是 Markdown 文件"),
            tr("支持 .md、.markdown、.mdown、.mkd、.mkdn 和 .mdwn 文件。"));
        m_dock->setDocumentInfo(filePath, -1);
        updateExportActionState();
        return false;
    }

    QPointer<QWidget> renderEditor = m_editor;
    const quint64 renderVersion = m_contentVersion;
    double preservedScrollRatio = 0.0;
    const bool preserveScroll = !m_syncScrolling &&
        m_dock->nativeScrollRatioFor(renderEditor.data(),
                                     &preservedScrollRatio);
    QElapsedTimer elapsed;
    elapsed.start();
    if (!activateNativePreview()) {
        synchronizeActiveEditor();
        if (m_editor != renderEditor || m_contentVersion != renderVersion) {
            Diagnostics::write(QStringLiteral(
                "renderNow failure discarded: editor state changed"));
            scheduleRender();
            return false;
        }
        if (m_editor == renderEditor && m_contentVersion == renderVersion) {
            m_previewState = PreviewState::Failed;
            m_previewEditor = nullptr;
            m_renderedVersion = 0;
        }
        m_dock->showMessage(
            tr("无法打开原生 Markdown 预览"),
            tr("notepad-- 没有响应 on_viewMarkdown 调用，或没有创建 MarkdownView。"));
        updateExportActionState();
        return false;
    }
    m_lastRenderDurationMs = elapsed.elapsed();
    Diagnostics::write(QStringLiteral("renderNow completed in %1 ms")
                           .arg(m_lastRenderDurationMs));

    synchronizeActiveEditor();
    if (m_editor != renderEditor || m_contentVersion != renderVersion) {
        Diagnostics::write(QStringLiteral("renderNow discarded: editor state changed"));
        scheduleRender();
        return false;
    }

    m_previewEditor = renderEditor;
    m_renderedVersion = renderVersion;
    m_previewState = PreviewState::Ready;
    m_dock->setDocumentInfo(filePath, -1);
    m_lastEditorScrollValue = -1;
    if (m_syncScrolling) {
        QTimer::singleShot(0, this, &PreviewController::updateSynchronizedScroll);
    } else if (preserveScroll) {
        m_dock->preserveNativeScrollRatio(
            renderEditor.data(), renderVersion, preservedScrollRatio);
    }
    updateExportActionState();
    return true;
}

void PreviewController::togglePreview(bool visible)
{
    Diagnostics::write(QStringLiteral("togglePreview entered: %1").arg(visible));
    if (!m_dock) {
        return;
    }
    m_dock->setVisible(visible);
    Diagnostics::write(QStringLiteral("dock setVisible returned"));
    if (visible) {
        Diagnostics::write(QStringLiteral("calling dock raise"));
        m_dock->raise();
        Diagnostics::write(QStringLiteral("dock raise returned"));
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
    m_lastEditorScrollValue = -1;
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
    editorBar->setValue(value);

    // The editor-to-preview direction is polled.  Remember the value written
    // here so the next poll does not immediately echo it back to the preview.
    m_lastEditorScrollValue = editorBar->value();
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
        Diagnostics::write(
            QStringLiteral("export snapshot discarded: editor state changed"));
        return false;
    }

    const QPointer<QWidget> snapshotEditor = m_editor;
    const quint64 snapshotVersion = m_contentVersion;
    const QString snapshotPath = currentFilePath();
    const QByteArray snapshot = m_dock->htmlSnapshotFor(
        snapshotEditor.data(), snapshotVersion);
    if (snapshot.isEmpty()) {
        Diagnostics::write(QStringLiteral("export snapshot unavailable"));
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
    if (m_editor) {
        disconnect(m_editor, nullptr, this, nullptr);
    }

    m_editor = editor;
    m_editorFilePath = currentFilePath();
    ++m_contentVersion;
    m_previewEditor = nullptr;
    m_renderedVersion = 0;
    m_previewState = m_editor ? PreviewState::Pending : PreviewState::NoDocument;
    if (m_dock) {
        m_dock->invalidatePreview();
    }
    m_lastEditorScrollValue = -1;
    m_lastRenderDurationMs = 0;

    if (m_editor) {
        Diagnostics::write(QStringLiteral("attaching editor class=%1")
                               .arg(QString::fromLatin1(m_editor->metaObject()->className())));
        const QMetaObject::Connection textChangedConnection = connect(
            m_editor.data(), SIGNAL(textChanged()),
            this, SLOT(onEditorTextChanged()),
            Qt::UniqueConnection);
        Diagnostics::write(QStringLiteral("runtime textChanged connection=%1")
                               .arg(static_cast<bool>(textChangedConnection)));
        connect(m_editor, &QObject::destroyed, this, [this]() {
            m_editor = nullptr;
            m_editorFilePath.clear();
            ++m_contentVersion;
            m_previewEditor = nullptr;
            m_renderedVersion = 0;
            m_previewState = PreviewState::NoDocument;
            if (m_dock) {
                m_dock->invalidatePreview();
            }
            updateExportActionState();
            scheduleRender();
        });
        if (disconnectHostImmediateRefresh()) {
            Diagnostics::write(
                QStringLiteral("existing host immediate refresh disconnected on attach"));
        }
    }
    updateExportActionState();
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

    Diagnostics::write(QStringLiteral("editor filePath changed: %1 -> %2")
                           .arg(m_editorFilePath, filePath));
    m_editorFilePath = filePath;
    markPreviewPending();
    if (m_dock) {
        m_dock->setDocumentInfo(filePath, -1);
    }
    if (m_dock && m_dock->isVisible()) {
        scheduleRender();
    }
}

void PreviewController::synchronizeActiveEditor()
{
    QWidget *current = resolveCurrentEditor();
    if (current == m_editor) {
        return;
    }

    Diagnostics::write(QStringLiteral("active editor changed: 0x%1")
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

    ++m_contentVersion;
    m_previewState = PreviewState::Pending;
    m_previewEditor = nullptr;
    m_renderedVersion = 0;
    if (m_dock) {
        m_dock->invalidatePreview();
    }
    updateExportActionState();
}

bool PreviewController::isPreviewCurrent() const
{
    return m_previewState == PreviewState::Ready && m_editor &&
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

QWidget *PreviewController::nativePreviewForEditor() const
{
    if (!m_editor) {
        return nullptr;
    }

    QObject *stored = m_editor->property(kNativePreviewProperty).value<QObject *>();
    QWidget *nativePreview = qobject_cast<QWidget *>(stored);
    if (!nativePreview) {
        nativePreview = m_editor->findChild<QWidget *>(
            QStringLiteral("MarkdownViewClass"));
    }
    return nativePreview;
}

bool PreviewController::disconnectHostImmediateRefresh(bool force)
{
    if (!m_editor || (!force && !nativePreviewForEditor())) {
        return false;
    }

    // notepad-- v3.8.3 has exactly one textChanged connection whose receiver
    // is the editor itself: on_updataMarkdown().  Disconnecting by receiver
    // avoids relying on the host's pointer-to-member connection syntax while
    // preserving the editor -> controller and editor -> main-window signals.
    return QObject::disconnect(
        m_editor.data(), SIGNAL(textChanged()),
        m_editor.data(), nullptr);
}

bool PreviewController::activateNativePreview()
{
    if (!m_editor || !m_dock) {
        return false;
    }

    QWidget *nativePreview = nativePreviewForEditor();
    bool renderedWhileCreating = false;
    if (!nativePreview) {
        Diagnostics::write(QStringLiteral("invoking host on_viewMarkdown"));
        const bool invoked = QMetaObject::invokeMethod(
            m_editor.data(), "on_viewMarkdown", Qt::DirectConnection);
        Diagnostics::write(QStringLiteral("host on_viewMarkdown returned: %1")
                               .arg(invoked));
        if (!invoked) {
            return false;
        }
        renderedWhileCreating = true;

        nativePreview = m_editor->findChild<QWidget *>(
            QStringLiteral("MarkdownViewClass"));
    }

    // on_viewMarkdown() installs a direct editor self-connection.  Remove any
    // such connection before control returns to the event loop.
    const bool disconnected = disconnectHostImmediateRefresh(
        renderedWhileCreating || nativePreview);
    if (renderedWhileCreating || disconnected) {
        Diagnostics::write(QStringLiteral("host immediate refresh disconnected=%1")
                               .arg(disconnected));
    }

    if (!nativePreview) {
        Diagnostics::write(QStringLiteral("host MarkdownView was not found"));
        return false;
    }

    m_editor->setProperty(
        kNativePreviewProperty,
        QVariant::fromValue(static_cast<QObject *>(nativePreview)));
    if (!nativePreview->property(kNativePreviewOwnerHook).toBool()) {
        QPointer<QWidget> editor = m_editor;
        connect(nativePreview, &QObject::destroyed, m_editor.data(), [editor]() {
            if (editor) {
                editor->setProperty(kNativePreviewProperty, QVariant());
            }
        });
        connect(m_editor.data(), &QObject::destroyed,
                nativePreview, &QObject::deleteLater);
        nativePreview->setProperty(kNativePreviewOwnerHook, true);
    }

    if (!m_dock->adoptNativePreview(nativePreview, currentFilePath(),
                                    m_editor.data(), m_contentVersion)) {
        return false;
    }

    // on_viewMarkdown() has already rendered the initial document.  Reuse that
    // result instead of immediately parsing and laying out the whole document
    // a second time.
    if (renderedWhileCreating) {
        Diagnostics::write(QStringLiteral("host initial render reused"));
        return true;
    }

    // Render through the host module after embedding.  This is also the only
    // render performed for debounced editor textChanged notifications.
    const bool updated = QMetaObject::invokeMethod(
        m_editor.data(), "on_updataMarkdown", Qt::DirectConnection);
    Diagnostics::write(QStringLiteral("host on_updataMarkdown returned: %1")
                           .arg(updated));
    if (updated) {
        m_dock->refreshDocumentStyle(m_editor.data(), m_contentVersion);
    }
    return updated;
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

    const int scrollValue = editorBar->value();
    if (scrollValue == m_lastEditorScrollValue) {
        return;
    }

    m_lastEditorScrollValue = scrollValue;
    const double ratio = static_cast<double>(scrollValue - editorBar->minimum()) /
        static_cast<double>(editorBar->maximum() - editorBar->minimum());
    m_dock->scrollToRatio(ratio);
}

QString PreviewController::currentFilePath() const
{
    return m_editor ? m_editor->property(kFilePathProperty).toString() : QString();
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
