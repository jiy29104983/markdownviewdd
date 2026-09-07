#pragma once

#include "ndd_plugin_api.h"

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
    void synchronizeFromHostEvent();
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
    struct PreviewCacheEntry {
        QPointer<QWidget> editor;
        double scrollRatio = 0.0;
        bool hasScrollRatio = false;
        bool hasNativePreview = false;
    };

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
    bool m_manualRefreshOnly = false;
    QString m_editorFilePath;
    QString m_lastHostError;
    quint64 m_contentVersion = 0;
    quint64 m_renderedVersion = 0;
    PreviewState m_previewState = PreviewState::NoDocument;
    QList<PreviewCacheEntry> m_previewCache;
};
