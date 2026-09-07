#include "host_adapter.h"

#include <QAction>
#include <QElapsedTimer>
#include <QDynamicPropertyChangeEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QPointer>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QVariant>
#include <QWidget>

namespace {
constexpr auto kEditorTabsObjectName = "editTabWidget";
constexpr auto kFilePathProperty = "filePath";
constexpr auto kNativePreviewClassName = "MarkdownViewClass";
constexpr auto kNativeTextEditObjectName = "textEdit";
constexpr auto kNativePreviewProperty = "_markdownview_native_preview";
constexpr auto kNativePreviewOwnerHook = "_markdownview_owner_hook";
constexpr auto kNativePreviewCurrent = "_markdownview_preview_current";
constexpr auto kContextActionBridge = "_markdownview_sidebar_bridge";
constexpr auto kViewMarkdownSlot = "on_viewMarkdown";
constexpr auto kUpdateMarkdownSlot = "on_updataMarkdown";

class NotepadHostAdapter final : public HostAdapter
{
public:
    explicit NotepadHostAdapter(QWidget *notepad)
        : m_notepad(notepad)
    {
    }

    QWidget *currentEditor() const override
    {
        if (!m_notepad) {
            return nullptr;
        }
        QTabWidget *tabs = m_notepad->findChild<QTabWidget *>(
            QString::fromLatin1(kEditorTabsObjectName));
        QWidget *page = tabs ? tabs->currentWidget() : nullptr;
        return page && page->inherits("QsciScintilla") ? page : nullptr;
    }

    QString filePath(QWidget *editor) const override
    {
        return editor ? editor->property(kFilePathProperty).toString() : QString();
    }

    bool isFilePathChangeEvent(QEvent *event) const override
    {
        if (!event || event->type() != QEvent::DynamicPropertyChange) {
            return false;
        }
        auto *propertyEvent = static_cast<QDynamicPropertyChangeEvent *>(event);
        return propertyEvent->propertyName() == QByteArray(kFilePathProperty);
    }

    bool isEditorContextMenu(QMenu *menu, QWidget *editor) const override
    {
        QWidget *menuParent = menu ? menu->parentWidget() : nullptr;
        return menuParent && editor && m_notepad && menuParent == editor &&
            menuParent->window() == m_notepad->window() &&
            editor->window() == m_notepad->window();
    }

    bool isMarkdownContextAction(QAction *action) const override
    {
        if (!action || action->property(kContextActionBridge).toBool()) {
            return false;
        }
        QString text = action->text();
        text.remove(QLatin1Char('&'));
        return text.contains(QStringLiteral("markdown"), Qt::CaseInsensitive);
    }

    bool bridgeMarkdownContextAction(QAction *action,
                                     QWidget *editor) override
    {
        if (!action || !editor) {
            return false;
        }
        action->setProperty(kContextActionBridge, true);
        return QObject::disconnect(action, nullptr, editor, nullptr);
    }

    PreviewResult ensurePreview(QWidget *editor) override
    {
        PreviewResult result;
        if (!editor) {
            result.error = QStringLiteral("没有活动编辑器");
            return result;
        }

        QWidget *preview = nativePreview(editor);
        if (!preview) {
            QElapsedTimer elapsed;
            elapsed.start();
            const bool invoked = QMetaObject::invokeMethod(
                editor, kViewMarkdownSlot, Qt::DirectConnection);
            result.durationMs = elapsed.elapsed();
            if (!invoked) {
                result.error = QStringLiteral("宿主未响应 on_viewMarkdown");
                return result;
            }
            result.created = true;
            preview = editor->findChild<QWidget *>(
                QString::fromLatin1(kNativePreviewClassName));
        }

        disconnectImmediateRefresh(editor, result.created || preview);
        if (!preview) {
            result.error = QStringLiteral("宿主未创建 MarkdownView");
            return result;
        }

        editor->setProperty(kNativePreviewProperty,
                            QVariant::fromValue(static_cast<QObject *>(preview)));
        installOwnershipHooks(editor, preview);
        preparePreviewWindow(preview);

        result.window = preview;
        result.textEdit = preview->findChild<QTextEdit *>(
            QString::fromLatin1(kNativeTextEditObjectName));
        if (!result.textEdit) {
            result.error = QStringLiteral("宿主 MarkdownView 缺少 textEdit");
        }
        return result;
    }

    bool refreshPreview(QWidget *editor, qint64 *durationMs,
                        QString *error) override
    {
        if (!editor) {
            if (error) {
                *error = QStringLiteral("没有活动编辑器");
            }
            return false;
        }
        QElapsedTimer elapsed;
        elapsed.start();
        const bool updated = QMetaObject::invokeMethod(
            editor, kUpdateMarkdownSlot, Qt::DirectConnection);
        if (durationMs) {
            *durationMs = elapsed.elapsed();
        }
        if (!updated && error) {
            *error = QStringLiteral("宿主未响应 on_updataMarkdown");
        }
        return updated;
    }

    bool disconnectImmediateRefresh(QWidget *editor, bool force) override
    {
        if (!editor || (!force && !nativePreview(editor))) {
            return false;
        }
        return QObject::disconnect(editor, SIGNAL(textChanged()), editor, nullptr);
    }

    bool previewIsCurrent(QWidget *editor) const override
    {
        return editor && editor->property(kNativePreviewCurrent).toBool();
    }

    void setPreviewCurrent(QWidget *editor, bool current) override
    {
        if (editor) {
            editor->setProperty(kNativePreviewCurrent, current);
        }
    }

private:
    QWidget *nativePreview(QWidget *editor) const
    {
        if (!editor) {
            return nullptr;
        }
        QObject *stored = editor->property(kNativePreviewProperty).value<QObject *>();
        QWidget *preview = qobject_cast<QWidget *>(stored);
        return preview ? preview : editor->findChild<QWidget *>(
            QString::fromLatin1(kNativePreviewClassName));
    }

    static void installOwnershipHooks(QWidget *editor, QWidget *preview)
    {
        if (preview->property(kNativePreviewOwnerHook).toBool()) {
            return;
        }
        QPointer<QWidget> guardedEditor(editor);
        QObject::connect(preview, &QObject::destroyed, editor, [guardedEditor]() {
            if (guardedEditor) {
                guardedEditor->setProperty(kNativePreviewProperty, QVariant());
            }
        });
        QObject::connect(editor, &QObject::destroyed,
                         preview, &QObject::deleteLater);
        preview->setProperty(kNativePreviewOwnerHook, true);
    }

    static void preparePreviewWindow(QWidget *preview)
    {
        QMainWindow *window = qobject_cast<QMainWindow *>(preview);
        if (!window) {
            return;
        }
        if (QMenuBar *menu = window->findChild<QMenuBar *>(
                QStringLiteral("menuBar"))) {
            menu->hide();
        }
        if (QStatusBar *status = window->findChild<QStatusBar *>(
                QStringLiteral("statusBar"))) {
            status->hide();
        }
        const QList<QToolBar *> toolBars = window->findChildren<QToolBar *>();
        for (QToolBar *toolBar : toolBars) {
            toolBar->hide();
        }
    }

    QPointer<QWidget> m_notepad;
};
}

HostAdapter *createNotepadHostAdapter(QWidget *notepad)
{
    return new NotepadHostAdapter(notepad);
}
