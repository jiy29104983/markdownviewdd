#pragma once

#include <QByteArray>
#include <QDockWidget>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QPoint>
#include <QUrl>

#include <functional>

class QAbstractScrollArea;
class QLabel;
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
                            QWidget *editor, quint64 contentVersion);
    void invalidatePreview();
    bool hasPreviewFor(QWidget *editor, quint64 contentVersion) const;
    QByteArray htmlSnapshotFor(QWidget *editor,
                               quint64 contentVersion) const;
    void renderMarkdown(const QString &markdown, const QString &filePath);
    void showMessage(const QString &title, const QString &message);
    void setDocumentInfo(const QString &filePath, int characterCount);
    void setRefreshStatus(const QString &status, const QString &toolTip);
    void setSyncScrolling(bool enabled);
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
    void refreshRequested();
    void syncScrollingChanged(bool enabled);
    void previewScrollRatioChanged(double ratio);
    void previewScrollRangeChanged();

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
    void applyDocumentStyle(QTextEdit *textEdit);
    void scheduleThemeStyleRefresh();
    bool scrollNativeToAnchor(const QString &anchor);
    void showLinkFailure(const QUrl &url, const QString &reason);
    QString loadStyleSheet() const;
    QUrl baseUrlForFile(const QString &filePath) const;

    QTextBrowser *m_browser = nullptr;
    QLayout *m_contentLayout = nullptr;
    QLabel *m_documentLabel = nullptr;
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
    double m_preservedScrollRatio = 0.0;
    bool m_hasPreservedScrollRatio = false;
    bool m_isDestroying = false;
};
