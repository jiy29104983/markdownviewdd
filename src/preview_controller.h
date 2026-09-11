#pragma once

#include "ndd_plugin_api.h"
#include "preview_status.h"

#include <QByteArray>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QtGlobal>

class QAction;
class QEvent;
class MarkdownPreviewDock;
class QMainWindow;
class QMenu;
class QTimer;
class QTextEdit;
class HostAdapter;

class PreviewController final : public QObject
{
    Q_OBJECT

public:
    explicit PreviewController(QWidget *notepad, HostAdapter *hostAdapter = nullptr);
    ~PreviewController() override;

    bool installMenu(QMenu *rootMenu);
    bool currentHtmlSnapshot(QByteArray *html, QString *sourceFilePath);
    PreviewStatus previewStatus() const;

public slots:
    void setRefreshMode(RefreshMode mode);

signals:
    void previewStatusChanged();

private slots:
    void pollEditor();
    void synchronizeFromHostEvent();
    void onEditorTextChanged();
    void onEditorDestroyed(QObject *editor);
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
    struct PreviewCacheEntry {
        QPointer<QWidget> editor;
        QString filePath;
        QMetaObject::Connection textConnection;
        QMetaObject::Connection destroyedConnection;
        qint64 lastFullRenderDurationMs = 0;
        double scrollRatio = 0.0;
        bool hasScrollRatio = false;
        bool hasNativePreview = false;
        bool hasRendered = false;
        QPointer<QWidget> previewWindow;
        QPointer<QTextEdit> previewTextEdit;
        QString snapshotFilePath;
        QString error;
        quint64 contentVersion = 0;
        quint64 renderedVersion = 0;
    };

    void bridgeEditorContextMenu(QMenu *menu);
    void showPreviewFromNativeAction();
    QWidget *resolveCurrentEditor() const;
    void attachEditor(QWidget *editor);
    void observeEditor(QWidget *editor);
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
    void publishStatus();
    void showCachedPreviewOrMessage();
    bool automaticRenderAllowed(bool allowInitialRender) const;
    QString protectionReason() const;
    bool disconnectHostImmediateRefresh(bool force = false);
    bool activateNativePreview(bool forceHostUpdate, bool *performedFullRender);
    void rememberCurrentPreviewScroll();
    void touchPreviewCache(QWidget *editor);
    void enforcePreviewCacheLimit();
    PreviewCacheEntry *previewCacheEntry(QWidget *editor);
    void prunePreviewCache();
    void updateSynchronizedScroll();
    void ensureHostEventConnection();
    void updatePollTimerState();
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
    QPointer<QAction> m_automaticAction;
    QPointer<QAction> m_manualAction;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_hostEventTimer = nullptr;
    QTimer *m_renderTimer = nullptr;
    bool m_syncScrolling = true;
    QPointer<QWidget> m_lastScrollEditor;
    int m_lastEditorScrollMinimum = 0;
    int m_lastEditorScrollMaximum = 0;
    int m_lastEditorScrollValue = 0;
    bool m_hasLastEditorScrollState = false;
    bool m_syncingEditorScroll = false;
    QMetaObject::Connection m_activeEditorConnection;
    QMetaObject::Connection m_editorScrollValueConnection;
    QMetaObject::Connection m_editorScrollRangeConnection;
    qint64 m_lastRenderDurationMs = 0;
    RefreshMode m_refreshMode = RefreshMode::Automatic;
    bool m_performanceProtected = false;
    bool m_renderInProgress = false;
    bool m_allowInitialAutomaticRender = false;
    QPointer<QWidget> m_scheduledEditor;
    quint64 m_scheduledVersion = 0;
    quint64 m_nextContentVersion = 0;
    QString m_editorFilePath;
    QString m_lastHostError;
    quint64 m_contentVersion = 0;
    quint64 m_renderedVersion = 0;
    PreviewState m_previewState = PreviewState::NoDocument;
    QList<PreviewCacheEntry> m_previewCache;
};
