#pragma once

#include <QDockWidget>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QUrl>

class QAbstractScrollArea;
class QLabel;
class QLayout;
class QScrollBar;
class QTextBrowser;
class QTextDocument;
class QTextEdit;
class QTimer;
class QToolButton;
class QWidget;

class MarkdownPreviewDock final : public QDockWidget
{
    Q_OBJECT

public:
    explicit MarkdownPreviewDock(QWidget *parent = nullptr);
    ~MarkdownPreviewDock() override;

    bool adoptNativePreview(QWidget *previewWindow, const QString &filePath);
    void renderMarkdown(const QString &markdown, const QString &filePath);
    void showMessage(const QString &title, const QString &message);
    void setDocumentInfo(const QString &filePath, int characterCount);
    void setSyncScrolling(bool enabled);
    void scrollToRatio(double ratio);
    double scrollRatio() const;
    bool exportHtml(QWidget *dialogParent);

signals:
    void refreshRequested();
    void syncScrollingChanged(bool enabled);
    void previewScrollRatioChanged(double ratio);
    void previewScrollRangeChanged();

private slots:
    void openLink(const QUrl &url);

private:
    QTextDocument *activeDocument() const;
    QAbstractScrollArea *activeScrollArea() const;
    void connectNativeScrollBar(QScrollBar *scrollBar);
    void trackNativePreview(QWidget *previewWindow);
    void handleNativePreviewDestroyed(QObject *previewObject);
    void disconnectNativePreviews();
    void emitPreviewScrollRatio(QScrollBar *scrollBar);
    QString loadStyleSheet() const;
    QUrl baseUrlForFile(const QString &filePath) const;

    QTextBrowser *m_browser = nullptr;
    QLayout *m_contentLayout = nullptr;
    QLabel *m_documentLabel = nullptr;
    QToolButton *m_syncButton = nullptr;
    QPointer<QWidget> m_nativePreview;
    QPointer<QTextEdit> m_nativeTextEdit;
    QObject *m_currentNativePreviewObject = nullptr;
    QMetaObject::Connection m_nativeScrollConnection;
    QMetaObject::Connection m_nativeScrollRangeConnection;
    QHash<QObject *, QMetaObject::Connection> m_nativePreviewDestroyConnections;
    QTimer *m_layoutSyncTimer = nullptr;
    QString m_currentFilePath;
    bool m_isDestroying = false;
};
