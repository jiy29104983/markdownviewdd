#include "markdown_preview_dock.h"

#include "diagnostics.h"

#include <QAbstractSlider>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextEdit>
#include <QtMath>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
constexpr auto kDockDestroyHook = "_markdownview_dock_destroy_hook";
}

MarkdownPreviewDock::MarkdownPreviewDock(QWidget *parent)
    : QDockWidget(tr("Markdown 预览"), parent)
{
    setObjectName(QStringLiteral("NddMarkdownPreviewDock"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolbar = new QWidget(container);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 4, 5, 4);

    m_documentLabel = new QLabel(tr("没有活动文档"), toolbar);
    m_documentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_documentLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbarLayout->addWidget(m_documentLabel);

    auto *refreshButton = new QToolButton(toolbar);
    refreshButton->setText(tr("刷新"));
    refreshButton->setToolTip(tr("立即重新渲染当前文档"));
    toolbarLayout->addWidget(refreshButton);

    m_syncButton = new QToolButton(toolbar);
    m_syncButton->setText(tr("同步滚动"));
    m_syncButton->setCheckable(true);
    m_syncButton->setChecked(true);
    toolbarLayout->addWidget(m_syncButton);

    m_browser = new QTextBrowser(container);
    m_browser->setObjectName(QStringLiteral("NddMarkdownPreviewBrowser"));
    m_browser->setOpenLinks(false);
    m_browser->setOpenExternalLinks(false);
    m_browser->setFrameShape(QFrame::NoFrame);
    m_browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    layout->addWidget(toolbar);
    layout->addWidget(m_browser, 1);
    m_contentLayout = layout;
    setWidget(container);

    connect(refreshButton, &QToolButton::clicked,
            this, &MarkdownPreviewDock::refreshRequested);
    connect(m_syncButton, &QToolButton::toggled,
            this, &MarkdownPreviewDock::syncScrollingChanged);
    connect(m_browser, &QTextBrowser::anchorClicked,
            this, &MarkdownPreviewDock::openLink);
    QScrollBar *browserScrollBar = m_browser->verticalScrollBar();
    connect(browserScrollBar, &QAbstractSlider::actionTriggered, this,
            [this, browserScrollBar](int) {
        QTimer::singleShot(0, this, [this, browserScrollBar]() {
            emitPreviewScrollRatio(browserScrollBar);
        });
    });
}

bool MarkdownPreviewDock::adoptNativePreview(QWidget *previewWindow,
                                             const QString &filePath)
{
    if (!previewWindow || !m_contentLayout || !widget()) {
        return false;
    }

    QTextEdit *textEdit = previewWindow->findChild<QTextEdit *>(
        QStringLiteral("textEdit"));
    if (!textEdit) {
        Diagnostics::write(QStringLiteral("native MarkdownView textEdit was not found"));
        return false;
    }

    textEdit->document()->setBaseUrl(baseUrlForFile(filePath));
    textEdit->document()->setDefaultStyleSheet(loadStyleSheet());

    if (m_nativePreview && m_nativePreview != previewWindow) {
        m_contentLayout->removeWidget(m_nativePreview);
        m_nativePreview->hide();
    }

    if (m_nativePreview != previewWindow) {
        previewWindow->hide();
        previewWindow->setParent(widget(), Qt::Widget);

        if (QMainWindow *window = qobject_cast<QMainWindow *>(previewWindow)) {
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

        m_contentLayout->addWidget(previewWindow);
        m_nativePreview = previewWindow;
        m_nativeTextEdit = textEdit;
        connectNativeScrollBar(textEdit->verticalScrollBar());
        if (!previewWindow->property(kDockDestroyHook).toBool()) {
            connect(previewWindow, &QObject::destroyed, this,
                    [this, previewWindow]() {
                if (m_nativePreview == previewWindow) {
                    m_nativePreview = nullptr;
                    m_nativeTextEdit = nullptr;
                    m_nativeScrollConnection = QMetaObject::Connection();
                    if (m_browser) {
                        m_browser->show();
                    }
                }
            });
            previewWindow->setProperty(kDockDestroyHook, true);
        }
    }

    m_currentFilePath = filePath;
    m_browser->hide();
    previewWindow->show();
    Diagnostics::write(QStringLiteral("native MarkdownView embedded in dock"));
    return true;
}

void MarkdownPreviewDock::renderMarkdown(const QString &markdown,
                                         const QString &filePath)
{
    Diagnostics::write(QStringLiteral("renderMarkdown entered"));
    const double previousRatio = scrollRatio();
    m_currentFilePath = filePath;

    QTextDocument *document = m_browser->document();
    Diagnostics::write(QStringLiteral("setting document base URL"));
    document->setBaseUrl(baseUrlForFile(filePath));
    Diagnostics::write(QStringLiteral("setting document style sheet"));
    document->setDefaultStyleSheet(loadStyleSheet());
    Diagnostics::write(QStringLiteral("calling QTextDocument::setMarkdown"));
    document->setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
    Diagnostics::write(QStringLiteral("QTextDocument::setMarkdown returned"));

    if (!m_syncButton->isChecked()) {
        QTimer::singleShot(0, this, [this, previousRatio]() {
            scrollToRatio(previousRatio);
        });
    }
}

void MarkdownPreviewDock::showMessage(const QString &title,
                                      const QString &message)
{
    if (m_nativePreview) {
        m_nativePreview->hide();
    }
    m_browser->show();
    m_currentFilePath.clear();
    m_browser->document()->setDefaultStyleSheet(loadStyleSheet());
    m_browser->setHtml(QStringLiteral("<h3>%1</h3><p>%2</p>")
                           .arg(title.toHtmlEscaped(), message.toHtmlEscaped()));
}

void MarkdownPreviewDock::setDocumentInfo(const QString &filePath,
                                          int characterCount)
{
    const QString displayName = filePath.isEmpty()
        ? tr("未命名文档")
        : QFileInfo(filePath).fileName();
    m_documentLabel->setText(characterCount >= 0
        ? tr("%1 · 长度 %2").arg(displayName).arg(characterCount)
        : displayName);
    m_documentLabel->setToolTip(filePath);
}

void MarkdownPreviewDock::setSyncScrolling(bool enabled)
{
    const QSignalBlocker blocker(m_syncButton);
    m_syncButton->setChecked(enabled);
}

void MarkdownPreviewDock::scrollToRatio(double ratio)
{
    QAbstractScrollArea *area = activeScrollArea();
    QScrollBar *bar = area ? area->verticalScrollBar() : nullptr;
    if (!bar || bar->maximum() <= bar->minimum()) {
        return;
    }

    ratio = qBound(0.0, ratio, 1.0);
    const int value = bar->minimum() +
        qRound(ratio * static_cast<double>(bar->maximum() - bar->minimum()));
    bar->setValue(value);
}

double MarkdownPreviewDock::scrollRatio() const
{
    QAbstractScrollArea *area = activeScrollArea();
    const QScrollBar *bar = area ? area->verticalScrollBar() : nullptr;
    if (!bar || bar->maximum() <= bar->minimum()) {
        return 0.0;
    }

    return static_cast<double>(bar->value() - bar->minimum()) /
           static_cast<double>(bar->maximum() - bar->minimum());
}

bool MarkdownPreviewDock::exportHtml(QWidget *dialogParent)
{
    QTextDocument *document = activeDocument();
    if (!document || document->isEmpty()) {
        return false;
    }

    QFileInfo sourceInfo(m_currentFilePath);
    const QString suggestedName = sourceInfo.completeBaseName().isEmpty()
        ? QStringLiteral("preview.html")
        : sourceInfo.completeBaseName() + QStringLiteral(".html");
    const QString initialPath = sourceInfo.absoluteDir().exists()
        ? sourceInfo.absoluteDir().filePath(suggestedName)
        : suggestedName;

    const QString targetPath = QFileDialog::getSaveFileName(
        dialogParent, tr("导出 Markdown 预览"), initialPath,
        tr("HTML 文件 (*.html *.htm)"));
    if (targetPath.isEmpty()) {
        return false;
    }

    QSaveFile output(targetPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(dialogParent, tr("导出失败"),
                             tr("无法写入文件：%1").arg(targetPath));
        return false;
    }

    const QByteArray html = document->toHtml("UTF-8").toUtf8();
    if (output.write(html) != html.size() || !output.commit()) {
        QMessageBox::warning(dialogParent, tr("导出失败"),
                             tr("写入文件时发生错误：%1").arg(targetPath));
        return false;
    }

    return true;
}

QTextDocument *MarkdownPreviewDock::activeDocument() const
{
    return m_nativeTextEdit && m_nativePreview && m_nativePreview->isVisible()
        ? m_nativeTextEdit->document()
        : m_browser->document();
}

QAbstractScrollArea *MarkdownPreviewDock::activeScrollArea() const
{
    return m_nativeTextEdit && m_nativePreview && m_nativePreview->isVisible()
        ? static_cast<QAbstractScrollArea *>(m_nativeTextEdit.data())
        : static_cast<QAbstractScrollArea *>(m_browser);
}

void MarkdownPreviewDock::connectNativeScrollBar(QScrollBar *scrollBar)
{
    if (m_nativeScrollConnection) {
        disconnect(m_nativeScrollConnection);
    }
    m_nativeScrollConnection = QMetaObject::Connection();

    if (!scrollBar) {
        return;
    }

    m_nativeScrollConnection = connect(
        scrollBar, &QAbstractSlider::actionTriggered, this,
        [this, scrollBar](int) {
        const QPointer<QScrollBar> guardedScrollBar(scrollBar);
        QTimer::singleShot(0, this, [this, guardedScrollBar]() {
            if (guardedScrollBar) {
                emitPreviewScrollRatio(guardedScrollBar.data());
            }
        });
    });
}

void MarkdownPreviewDock::emitPreviewScrollRatio(QScrollBar *scrollBar)
{
    QAbstractScrollArea *area = activeScrollArea();
    if (!m_syncButton || !m_syncButton->isChecked() ||
        !area || !scrollBar || area->verticalScrollBar() != scrollBar ||
        scrollBar->maximum() <= scrollBar->minimum()) {
        return;
    }

    const double ratio =
        static_cast<double>(scrollBar->value() - scrollBar->minimum()) /
        static_cast<double>(scrollBar->maximum() - scrollBar->minimum());
    emit previewScrollRatioChanged(ratio);
}

void MarkdownPreviewDock::openLink(const QUrl &url)
{
    if (url.path().isEmpty() && !url.fragment().isEmpty()) {
        m_browser->scrollToAnchor(url.fragment());
        return;
    }

    QUrl resolved = url;
    if (resolved.isRelative()) {
        resolved = baseUrlForFile(m_currentFilePath).resolved(resolved);
    }

    const QString scheme = resolved.scheme().toLower();
    if (scheme == QStringLiteral("http") ||
        scheme == QStringLiteral("https") ||
        scheme == QStringLiteral("mailto") ||
        scheme == QStringLiteral("file")) {
        QDesktopServices::openUrl(resolved);
    }
}

QString MarkdownPreviewDock::loadStyleSheet() const
{
    QFile css(QStringLiteral(":/markdownview/markdown.css"));
    if (!css.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QString style = QString::fromUtf8(css.readAll());
    const QPalette colors = m_browser->palette();
    style.replace(QStringLiteral("@text@"), colors.color(QPalette::Text).name());
    style.replace(QStringLiteral("@base@"), colors.color(QPalette::Base).name());
    style.replace(QStringLiteral("@alternate-base@"),
                  colors.color(QPalette::AlternateBase).name());
    style.replace(QStringLiteral("@mid@"), colors.color(QPalette::Mid).name());
    style.replace(QStringLiteral("@midlight@"),
                  colors.color(QPalette::Midlight).name());
    style.replace(QStringLiteral("@link@"), colors.color(QPalette::Link).name());
    return style;
}

QUrl MarkdownPreviewDock::baseUrlForFile(const QString &filePath) const
{
    if (filePath.isEmpty()) {
        return QUrl();
    }

    const QFileInfo info(filePath);
    QString directory = info.absolutePath();
    if (!directory.endsWith(QDir::separator())) {
        directory += QDir::separator();
    }
    return QUrl::fromLocalFile(directory);
}
