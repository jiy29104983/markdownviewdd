#pragma once

#include <QString>
#include <QtGlobal>

class QAction;
class QEvent;
class QMenu;
class QTextEdit;
class QWidget;

class HostAdapter
{
public:
    struct PreviewResult
    {
        QWidget *window = nullptr;
        QTextEdit *textEdit = nullptr;
        QString error;
        qint64 durationMs = 0;
        bool created = false;

        bool isValid() const
        {
            return window && textEdit && error.isEmpty();
        }
    };

    virtual ~HostAdapter() = default;

    virtual QWidget *currentEditor() const = 0;
    virtual QString filePath(QWidget *editor) const = 0;
    virtual bool isFilePathChangeEvent(QEvent *event) const = 0;
    virtual bool isEditorContextMenu(QMenu *menu, QWidget *editor) const = 0;
    virtual bool isMarkdownContextAction(QAction *action) const = 0;
    virtual bool bridgeMarkdownContextAction(QAction *action,
                                             QWidget *editor) = 0;
    virtual PreviewResult ensurePreview(QWidget *editor) = 0;
    virtual bool refreshPreview(QWidget *editor, qint64 *durationMs,
                                QString *error) = 0;
    virtual bool disconnectImmediateRefresh(QWidget *editor,
                                            bool force = false) = 0;
    virtual bool previewIsCurrent(QWidget *editor) const = 0;
    virtual void setPreviewCurrent(QWidget *editor, bool current) = 0;
    virtual bool releasePreview(QWidget *editor, QString *error) = 0;
};

HostAdapter *createNotepadHostAdapter(QWidget *notepad);
