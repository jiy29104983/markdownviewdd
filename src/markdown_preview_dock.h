#pragma once

#include "preview_status.h"
#include "heading_index.h"
#include "code_block_index.h"

#include <QByteArray>
#include <QDockWidget>
#include <QHash>
#include <QFont>
#include <QMetaObject>
#include <QPointer>
#include <QPoint>
#include <QUrl>

#include <functional>

class QAbstractScrollArea;
class HeadingOutline;
class PreviewSearch;
class CodeBlockTools;
class QSplitter;
class QLabel;
class QAction;
class QMainWindow;
class QMenu;
class QLayout;
class QScrollBar;
class QTextBrowser;
class QTextEdit;
class QTimer;
class QToolButton;
class QWidget;

class MarkdownPreviewDock final : public QDockWidget
{
    Q_OBJECT

public:
    using UrlOpener = std::function<bool(const QUrl &)>;

    explicit MarkdownPreviewDock(QWidget *parent = nullptr);
    ~MarkdownPreviewDock() override;

    bool adoptNativePreview(QWidget *previewWindow, QTextEdit *textEdit,
                            const QString &filePath,
                            QWidget *editor, quint64 contentVersion,
                            bool current = true);
    void invalidatePreview();
    void markPreviewStale();
    bool hasPreviewFor(QWidget *editor, quint64 contentVersion) const;
    QByteArray htmlSnapshotFor(QWidget *editor,
                               quint64 contentVersion) const;
    void renderMarkdown(const QString &markdown, const QString &filePath);
    void showMessage(const QString &title, const QString &message);
    void setDocumentInfo(const QString &filePath, int characterCount,
                         bool hasDocument = true);
    void setRefreshStatus(const QString &status, const QString &details,
                          bool failed = false);
    void setRefreshMode(RefreshMode mode);
    void setSyncScrolling(bool enabled);
    void setReadingFont(const QFont &font, qreal zoom);
    bool nativeScrollRatioFor(QWidget *editor, double *ratio) const;
    void preserveNativeScrollRatio(QWidget *editor, quint64 contentVersion,
                                   double ratio);
    void refreshDocumentStyle(QWidget *editor, quint64 contentVersion);
    void scrollToRatio(double ratio);
    double scrollRatio() const;
    bool saveHtmlSnapshot(QWidget *dialogParent, const QByteArray &html,
                          const QString &sourceFilePath);
    static bool writeHtmlSnapshot(const QByteArray &html,
                                  const QString &targetPath,
                                  QString *errorMessage = nullptr);
    void setUrlOpener(UrlOpener opener);
    void setHeadingSnapshot(const QVector<HeadingRecord> &headings,
                            QWidget *editor, quint64 version);
    void setOutlineStatus(const PreviewStatus &status);
    bool navigateHeading(const HeadingRecord &heading);
    bool navigatePreviewPosition(QWidget *editor, quint64 version, int position, int length = 0);
    void setCodeCopyValidator(std::function<bool(QWidget *, quint64)> validator);
    void setCodeSnapshot(const QVector<CodeBlockRecord> &records, QWidget *editor, quint64 version);
    void setSearchStatus(const PreviewStatus &status);
    bool hasNavigationTarget() const { return m_hasNavigationTarget; }
    void releaseNavigationTarget();
    void setNavigationFeedback(const QString &message);
    QVector<HeadingRecord> headings() const;
    QAction *outlineVisibleAction() const { return m_outlineVisibleAction; }
    QMenu *outlinePositionMenu() const { return m_outlinePositionMenu; }
    void openSearch();
    void explainCompactOutline();


signals:
    void navigationTargetReleased();
    void headingActivated(const HeadingRecord &heading);
    void nativeZoomChanged(qreal zoom);
    void refreshRequested();
    void refreshModeChanged(RefreshMode mode);
    void syncScrollingChanged(bool enabled);
    void previewScrollRatioChanged(double ratio);
    void previewScrollRangeChanged();
    void displayedPreviewDestroyed();

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void openLink(const QUrl &url);

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void updateCurrentHeading();
    void restoreNavigationTarget();
    void preserveLayoutTarget();
    void setOutlineOnRight(bool right);
    QAbstractScrollArea *activeScrollArea() const;
    void connectNativeScrollBar(QScrollBar *scrollBar);
    void trackNativePreview(QWidget *previewWindow);
    void handleNativePreviewDestroyed(QObject *previewObject);
    void disconnectNativePreviews();
    void emitPreviewScrollRatio(QScrollBar *scrollBar);
    void restorePreservedScrollRatio();
    void cancelPreservedScroll();
    bool applyDocumentStyle(QTextEdit *textEdit);
    void restyleNativePreview(double ratio);
    bool handleNativeWheel(QObject *watched, QEvent *event);
    void scheduleThemeStyleRefresh();
    bool scrollNativeToAnchor(const QString &anchor);
    void showLinkFailure(const QUrl &url, const QString &reason);
    bool hasDisplayedPreviewFor(QWidget *editor, quint64 contentVersion) const;
    QString loadStyleSheet() const;
    QUrl baseUrlForFile(const QString &filePath) const;

    CodeBlockTools *m_codeTools = nullptr;
    bool m_wrapCode = true;
    PreviewSearch *m_search = nullptr;
    HeadingOutline *m_outline = nullptr;
    QSplitter *m_splitter = nullptr;
    QWidget *m_previewContainer = nullptr;
    QTimer *m_headingTimer = nullptr;
    QMetaObject::Connection m_headingScrollConnection;
    HeadingRecord m_navigationTarget;
    int m_navigationLength = 0;
    bool m_hasNavigationTarget = false;
    bool m_outlineOnRight = false;
    int m_outlineWidth = 180;
    QTextBrowser *m_browser = nullptr;
    QLayout *m_contentLayout = nullptr;
    QLabel *m_documentLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_feedbackLabel = nullptr;
    QToolButton *m_modeButton = nullptr;
    QAction *m_autoMode = nullptr;
    QAction *m_manualMode = nullptr;
    QPointer<QMainWindow> m_dockOwner;
    QToolButton *m_floatButton = nullptr;
    Qt::DockWidgetArea m_lastDockArea = Qt::RightDockWidgetArea;
    QAction *m_outlineVisibleAction = nullptr;
    QMenu *m_outlinePositionMenu = nullptr;
    bool m_outlineWanted = true;
    bool m_compactOutline = false;
    bool m_updatingChrome = false;
    QList<QWidget *> m_chromeWidgets;
    QString m_documentName;
    QString m_documentDetails;
    QWidget *m_errorRow = nullptr;
    QLabel *m_errorLabel = nullptr;
    void updateChrome();
    void updateResponsiveLayout();
    QToolButton *m_retryButton = nullptr;
    QToolButton *m_detailsButton = nullptr;
    QString m_statusDetails;
    bool m_previewIsCurrent = false;
    QToolButton *m_syncButton = nullptr;
    QPointer<QWidget> m_nativePreview;
    QPointer<QTextEdit> m_nativeTextEdit;
    QPointer<QWidget> m_previewEditor;
    QPointer<QWidget> m_nativePreviewEditor;
    QPointer<QWidget> m_preservedScrollEditor;
    QObject *m_currentNativePreviewObject = nullptr;
    QMetaObject::Connection m_nativeScrollConnection;
    QMetaObject::Connection m_nativeHorizontalActionConnection;
    QMetaObject::Connection m_nativeScrollRangeConnection;
    QHash<QObject *, QMetaObject::Connection> m_nativePreviewDestroyConnections;
    QTimer *m_layoutSyncTimer = nullptr;
    QTimer *m_themeStyleTimer = nullptr;
    QString m_currentFilePath;
    UrlOpener m_urlOpener;
    QUrl m_pressedLink;
    QPoint m_linkPressPosition;
    quint64 m_previewContentVersion = 0;
    quint64 m_preservedScrollVersion = 0;
    quint64 m_scrollInteractionGeneration = 0;
    quint64 m_styleRevision = 1;
    quint64 m_styleUpdateGeneration = 0;
    quint64 m_preservedStyleGeneration = 0;
    quint64 m_preservedInteractionGeneration = 0;
    QFont m_bodyFont;
    QString m_codeFontFamily;
    qreal m_zoom = 1.0;
    bool m_handlingNativeWheel = false;
    double m_preservedScrollRatio = 0.0;
    bool m_hasPreservedScrollRatio = false;
    bool m_isDestroying = false;
};
