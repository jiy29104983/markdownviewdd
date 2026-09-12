#pragma once

#include "preview_status.h"

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
class QLabel;
class QComboBox;
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

signals:
    void nativeZoomChanged(qreal zoom);
    void refreshRequested();
    void refreshModeChanged(RefreshMode mode);
    void syncScrollingChanged(bool enabled);
    void previewScrollRatioChanged(double ratio);
    void previewScrollRangeChanged();
    void displayedPreviewDestroyed();

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void openLink(const QUrl &url);

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
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

    QTextBrowser *m_browser = nullptr;
    QLayout *m_contentLayout = nullptr;
    QLabel *m_documentLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_feedbackLabel = nullptr;
    QComboBox *m_modeCombo = nullptr;
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
