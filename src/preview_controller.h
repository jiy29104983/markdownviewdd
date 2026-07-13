#pragma once

#include "ndd_plugin_api.h"

#include <QObject>
#include <QPointer>

class QAction;
class QEvent;
class MarkdownPreviewDock;
class QMainWindow;
class QMenu;
class QTimer;

class PreviewController final : public QObject
{
    Q_OBJECT

public:
    explicit PreviewController(QWidget *notepad);

    bool installMenu(QMenu *rootMenu);

private slots:
    void pollEditor();
    void scheduleRender();
    void renderNow();
    void togglePreview(bool visible);
    void setSyncScrolling(bool enabled);
    void scrollEditorToRatio(double ratio);
    void showAbout();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void bridgeEditorContextMenu(QMenu *menu);
    void showPreviewFromNativeAction();
    QWidget *resolveCurrentEditor() const;
    void attachEditor(QWidget *editor);
    bool activateNativePreview();
    void updateSynchronizedScroll();
    QString currentFilePath() const;
    bool isMarkdownDocument(const QString &filePath) const;

    QPointer<QWidget> m_notepad;
    QPointer<QMainWindow> m_mainWindow;
    QPointer<QWidget> m_editor;
    QPointer<MarkdownPreviewDock> m_dock;
    QPointer<QAction> m_toggleAction;
    QPointer<QAction> m_syncAction;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_renderTimer = nullptr;
    bool m_syncScrolling = true;
    int m_lastEditorScrollValue = -1;
};
