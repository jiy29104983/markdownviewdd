#include "preview_controller.h"

#include "diagnostics.h"
#include "markdown_preview_dock.h"

#include <QAbstractScrollArea>
#include <QAction>
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
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
#include <QVariant>
#include <QtMath>

namespace {
constexpr int kEditorPollIntervalMs = 120;
constexpr int kRenderDebounceMs = 350;
constexpr int kLargeDocumentDebounceMs = 900;
constexpr qint64 kLargeDocumentBytes = 64 * 1024;
constexpr auto kNativePreviewProperty = "_markdownview_native_preview";
constexpr auto kNativePreviewOwnerHook = "_markdownview_owner_hook";
constexpr auto kContextActionBridge = "_markdownview_sidebar_bridge";
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
    m_renderTimer->setInterval(kRenderDebounceMs);

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

    m_toggleAction = rootMenu->addAction(tr("显示/隐藏预览"));
    m_toggleAction->setCheckable(true);
    m_toggleAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    m_toggleAction->setShortcutContext(Qt::ApplicationShortcut);
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

    auto *exportAction = rootMenu->addAction(tr("导出 HTML…"));
    connect(exportAction, &QAction::triggered, this, [this]() {
        if (m_dock) {
            m_dock->exportHtml(m_notepad);
        }
    });

    rootMenu->addSeparator();
    auto *aboutAction = rootMenu->addAction(tr("关于 Markdown 预览"));
    connect(aboutAction, &QAction::triggered,
            this, &PreviewController::showAbout);
    return true;
}

bool PreviewController::eventFilter(QObject *watched, QEvent *event)
{
    if (event && event->type() == QEvent::Show) {
        QMenu *menu = qobject_cast<QMenu *>(watched);
        QWidget *current = resolveCurrentEditor();
        if (menu && current && menu->parentWidget() == current) {
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
    QWidget *current = resolveCurrentEditor();
    if (current != m_editor) {
        Diagnostics::write(QStringLiteral("active editor changed: 0x%1")
                               .arg(reinterpret_cast<quintptr>(current), 0, 16));
        attachEditor(current);
        if (m_dock && m_dock->isVisible()) {
            scheduleRender();
        }
    }

    if (m_dock && m_dock->isVisible() && m_syncScrolling) {
        updateSynchronizedScroll();
    }
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
    if (m_dock && m_dock->isVisible()) {
        int interval = kRenderDebounceMs;
        const QFileInfo info(currentFilePath());
        if (info.exists() && info.size() >= kLargeDocumentBytes) {
            interval = kLargeDocumentDebounceMs;
        }
        m_renderTimer->start(interval);
    }
}

void PreviewController::renderNow()
{
    // Manual refreshes and explicit renders supersede any pending automatic
    // refresh for the same editor state.
    m_renderTimer->stop();
    Diagnostics::write(QStringLiteral("renderNow entered"));
    if (!m_dock || !m_dock->isVisible()) {
        Diagnostics::write(QStringLiteral("renderNow skipped: dock hidden"));
        return;
    }

    if (!m_editor) {
        Diagnostics::write(QStringLiteral("renderNow: no active editor"));
        m_dock->showMessage(tr("没有活动文档"),
                            tr("打开一个 Markdown 文件后即可预览。"));
        return;
    }

    const QString filePath = currentFilePath();
    Diagnostics::write(QStringLiteral("filePath read: %1").arg(filePath));
    if (!isMarkdownDocument(filePath)) {
        m_dock->showMessage(
            tr("当前文档不是 Markdown 文件"),
            tr("支持 .md、.markdown、.mdown、.mkd、.mkdn 和 .mdwn 文件。"));
        m_dock->setDocumentInfo(filePath, -1);
        return;
    }

    QElapsedTimer elapsed;
    elapsed.start();
    if (!activateNativePreview()) {
        m_dock->showMessage(
            tr("无法打开原生 Markdown 预览"),
            tr("notepad-- 没有响应 on_viewMarkdown 调用，或没有创建 MarkdownView。"));
        return;
    }
    Diagnostics::write(QStringLiteral("renderNow completed in %1 ms")
                           .arg(elapsed.elapsed()));

    m_dock->setDocumentInfo(filePath, -1);
    m_lastEditorScrollValue = -1;
    if (m_syncScrolling) {
        QTimer::singleShot(0, this, &PreviewController::updateSynchronizedScroll);
    }
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
    if (!m_editor || !m_dock || !m_dock->isVisible() || !m_syncScrolling) {
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
    m_lastEditorScrollValue = -1;

    if (m_editor) {
        Diagnostics::write(QStringLiteral("attaching editor class=%1")
                               .arg(QString::fromLatin1(m_editor->metaObject()->className())));
        const QMetaObject::Connection textChangedConnection = connect(
            m_editor.data(), SIGNAL(textChanged()),
            this, SLOT(scheduleRender()),
            Qt::UniqueConnection);
        Diagnostics::write(QStringLiteral("runtime textChanged connection=%1")
                               .arg(static_cast<bool>(textChangedConnection)));
        connect(m_editor, &QObject::destroyed, this, [this]() {
            m_editor = nullptr;
            scheduleRender();
        });
    }
}

bool PreviewController::activateNativePreview()
{
    if (!m_editor || !m_dock) {
        return false;
    }

    QObject *stored = m_editor->property(kNativePreviewProperty).value<QObject *>();
    QWidget *nativePreview = qobject_cast<QWidget *>(stored);

    bool renderedWhileCreating = false;
    if (!nativePreview) {
        nativePreview = m_editor->findChild<QWidget *>(
            QStringLiteral("MarkdownViewClass"));
    }

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

    // on_viewMarkdown() installs a direct editor textChanged ->
    // on_updataMarkdown() connection.  That bypasses the plugin's debounce and
    // makes a large document render once per keystroke.  Remove only that host
    // self-connection; the editor -> PreviewController connection remains.
    const bool disconnected = QObject::disconnect(
        m_editor.data(), SIGNAL(textChanged()),
        m_editor.data(), SLOT(on_updataMarkdown()));
    Diagnostics::write(QStringLiteral("host immediate refresh disconnected=%1")
                           .arg(disconnected));

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

    if (!m_dock->adoptNativePreview(nativePreview, currentFilePath())) {
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
    return updated;
}

void PreviewController::updateSynchronizedScroll()
{
    if (!m_editor || !m_dock || !m_dock->isVisible() || !m_syncScrolling) {
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
    return m_editor ? m_editor->property("filePath").toString() : QString();
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
