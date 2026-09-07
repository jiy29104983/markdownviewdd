#pragma once

#include "ndd_plugin_api.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QtGlobal>

class QAction;
class QEvent;
class MarkdownPreviewDock;
class QMainWindow;
class QMenu;
class QTimer;
class HostAdapter;

class PreviewController final : public QObject
{
    Q_OBJECT

public:
    explicit PreviewController(QWidget *notepad, HostAdapter *hostAdapter = nullptr);
    ~PreviewController() override;

    bool installMenu(QMenu *rootMenu);
    bool currentHtmlSnapshot(QByteArray *html, QString *sourceFilePath);

private slots:
    void pollEditor();
    void onEditorTextChanged();
    void scheduleRender();
    void renderScheduled();
    void renderNow();
    void togglePreview(bool visible);
    void setSyncScrolling(bool enabled);
    void scrollEditorToRatio(double ratio);
    void exportCurrentHtml();
    void showAbout();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class PreviewState {
        NoDocument,
        Unsupported,
        Pending,
        Ready,
        Failed
    };

    void bridgeEditorContextMenu(QMenu *menu);
    void showPreviewFromNativeAction();
    QWidget *resolveCurrentEditor() const;
    void attachEditor(QWidget *editor);
    void synchronizeActiveEditor();
    void handleFilePathChanged();
    void markPreviewPending();
    void scheduleAutomaticRender();
    void updateLargeDocumentPolicy(qint64 renderDurationMs);
    bool fileExceedsAutomaticRefreshLimit() const;
    bool isPreviewCurrent() const;
    bool renderCurrentDocument(bool allowHiddenDock,
                               bool forceHostUpdate = false);
    void updateExportActionState();
    bool disconnectHostImmediateRefresh(bool force = false);
    bool activateNativePreview(bool forceHostUpdate);
    void updateSynchronizedScroll();
    QString currentFilePath() const;
    bool isMarkdownDocument(const QString &filePath) const;

    QPointer<QWidget> m_notepad;
    HostAdapter *m_hostAdapter = nullptr;
    bool m_ownsHostAdapter = false;
    QPointer<QMainWindow> m_mainWindow;
    QPointer<QWidget> m_editor;
    QPointer<QWidget> m_previewEditor;
    QPointer<MarkdownPreviewDock> m_dock;
    QPointer<QMenu> m_rootMenu;
    QPointer<QAction> m_toggleAction;
    QPointer<QAction> m_syncAction;
    QPointer<QAction> m_exportAction;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_renderTimer = nullptr;
    bool m_syncScrolling = true;
    int m_lastEditorScrollValue = -1;
    qint64 m_lastRenderDurationMs = 0;
    bool m_manualRefreshOnly = false;
    QString m_editorFilePath;
    QString m_lastHostError;
    quint64 m_contentVersion = 0;
    quint64 m_renderedVersion = 0;
    PreviewState m_previewState = PreviewState::NoDocument;
};
