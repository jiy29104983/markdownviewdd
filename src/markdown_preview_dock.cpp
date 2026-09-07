#include "markdown_preview_dock.h"

#include "diagnostics.h"

#include <QAbstractSlider>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFragment>
#include <QtMath>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QVBoxLayout>

#include <utility>

namespace {
constexpr int kLayoutSyncDelayMs = 32;
}

MarkdownPreviewDock::MarkdownPreviewDock(QWidget *parent)
    : QDockWidget(tr("Markdown 预览"), parent),
      m_urlOpener([](const QUrl &url) {
          return QDesktopServices::openUrl(url);
      })
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

    m_layoutSyncTimer = new QTimer(this);
    m_layoutSyncTimer->setSingleShot(true);
    m_layoutSyncTimer->setInterval(kLayoutSyncDelayMs);

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
    connect(m_layoutSyncTimer, &QTimer::timeout, this, [this]() {
        if (!isVisible() || !m_syncButton) {
            return;
        }
        if (!m_syncButton->isChecked()) {
            restorePreservedScrollRatio();
            return;
        }
        if (m_nativeTextEdit && m_nativePreview && m_nativePreview->isVisible()) {
            m_nativeTextEdit->viewport()->update();
        }
        emit previewScrollRangeChanged();
    });
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (!visible) {
            m_layoutSyncTimer->stop();
        }
    });
    QScrollBar *browserScrollBar = m_browser->verticalScrollBar();
    connect(browserScrollBar, &QAbstractSlider::actionTriggered, this,
            [this, browserScrollBar](int) {
        QTimer::singleShot(0, this, [this, browserScrollBar]() {
            emitPreviewScrollRatio(browserScrollBar);
        });
    });
}

MarkdownPreviewDock::~MarkdownPreviewDock()
{
    m_isDestroying = true;
    if (m_layoutSyncTimer) {
        m_layoutSyncTimer->stop();
    }
    if (m_nativeScrollConnection) {
        disconnect(m_nativeScrollConnection);
    }
    if (m_nativeScrollRangeConnection) {
        disconnect(m_nativeScrollRangeConnection);
    }
    m_nativeScrollConnection = QMetaObject::Connection();
    m_nativeScrollRangeConnection = QMetaObject::Connection();
    disconnectNativePreviews();
    m_nativePreview = nullptr;
    m_nativeTextEdit = nullptr;
    m_previewEditor = nullptr;
    m_nativePreviewEditor = nullptr;
    m_preservedScrollEditor = nullptr;
    m_previewContentVersion = 0;
    m_preservedScrollVersion = 0;
    m_hasPreservedScrollRatio = false;
    m_currentNativePreviewObject = nullptr;
}

void MarkdownPreviewDock::setUrlOpener(UrlOpener opener)
{
    m_urlOpener = opener ? std::move(opener) : UrlOpener();
}

bool MarkdownPreviewDock::adoptNativePreview(QWidget *previewWindow,
                                             const QString &filePath,
                                             QWidget *editor,
                                             quint64 contentVersion)
{
    if (!previewWindow || !editor || !m_contentLayout || !widget()) {
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
        if (m_nativeTextEdit) {
            m_nativeTextEdit->viewport()->removeEventFilter(this);
        }
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
        m_nativeTextEdit->viewport()->installEventFilter(this);
        m_currentNativePreviewObject = previewWindow;
        connectNativeScrollBar(textEdit->verticalScrollBar());
        trackNativePreview(previewWindow);
    }

    if (m_hasPreservedScrollRatio && m_preservedScrollEditor != editor) {
        m_preservedScrollEditor = nullptr;
        m_preservedScrollVersion = 0;
        m_hasPreservedScrollRatio = false;
    }
    m_nativePreviewEditor = editor;
    m_currentFilePath = filePath;
    m_previewEditor = editor;
    m_previewContentVersion = contentVersion;
    m_browser->hide();
    previewWindow->show();
    Diagnostics::write(QStringLiteral("native MarkdownView embedded in dock"));
    return true;
}

void MarkdownPreviewDock::invalidatePreview()
{
    m_previewEditor = nullptr;
    m_previewContentVersion = 0;
}

bool MarkdownPreviewDock::hasPreviewFor(QWidget *editor,
                                       quint64 contentVersion) const
{
    return editor && m_previewEditor == editor &&
        m_previewContentVersion == contentVersion &&
        m_nativePreview && m_nativeTextEdit;
}

QByteArray MarkdownPreviewDock::htmlSnapshotFor(
    QWidget *editor, quint64 contentVersion) const
{
    if (!hasPreviewFor(editor, contentVersion)) {
        return QByteArray();
    }

    return m_nativeTextEdit->document()->toHtml("UTF-8").toUtf8();
}

void MarkdownPreviewDock::trackNativePreview(QWidget *previewWindow)
{
    if (!previewWindow || m_nativePreviewDestroyConnections.contains(previewWindow)) {
        return;
    }

    const QMetaObject::Connection connection = connect(
        previewWindow, &QObject::destroyed, this,
        [this](QObject *previewObject) {
            handleNativePreviewDestroyed(previewObject);
        });
    m_nativePreviewDestroyConnections.insert(previewWindow, connection);
}

void MarkdownPreviewDock::handleNativePreviewDestroyed(QObject *previewObject)
{
    m_nativePreviewDestroyConnections.remove(previewObject);
    if (m_isDestroying || m_currentNativePreviewObject != previewObject) {
        return;
    }

    if (m_nativeScrollConnection) {
        disconnect(m_nativeScrollConnection);
    }
    if (m_nativeScrollRangeConnection) {
        disconnect(m_nativeScrollRangeConnection);
    }
    m_nativeScrollConnection = QMetaObject::Connection();
    m_nativeScrollRangeConnection = QMetaObject::Connection();
    m_nativePreview = nullptr;
    m_nativeTextEdit = nullptr;
    m_previewEditor = nullptr;
    m_nativePreviewEditor = nullptr;
    m_preservedScrollEditor = nullptr;
    m_previewContentVersion = 0;
    m_preservedScrollVersion = 0;
    m_hasPreservedScrollRatio = false;
    m_currentNativePreviewObject = nullptr;
    m_pressedLink = QUrl();
    if (m_layoutSyncTimer) {
        m_layoutSyncTimer->stop();
    }
    if (m_browser) {
        m_browser->show();
    }
}

void MarkdownPreviewDock::disconnectNativePreviews()
{
    const QList<QMetaObject::Connection> connections =
        m_nativePreviewDestroyConnections.values();
    for (const QMetaObject::Connection &connection : connections) {
        disconnect(connection);
    }
    m_nativePreviewDestroyConnections.clear();
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
    invalidatePreview();
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
    if (enabled) {
        m_preservedScrollEditor = nullptr;
        m_preservedScrollVersion = 0;
        m_hasPreservedScrollRatio = false;
    } else if (m_layoutSyncTimer) {
        m_layoutSyncTimer->stop();
    }
}

bool MarkdownPreviewDock::nativeScrollRatioFor(QWidget *editor,
                                               double *ratio) const
{
    if (!editor || !ratio || m_nativePreviewEditor != editor ||
        !m_nativePreview || !m_nativeTextEdit) {
        return false;
    }

    const QScrollBar *bar = m_nativeTextEdit->verticalScrollBar();
    if (!bar || bar->maximum() <= bar->minimum()) {
        return false;
    }

    *ratio = static_cast<double>(bar->value() - bar->minimum()) /
        static_cast<double>(bar->maximum() - bar->minimum());
    return true;
}

void MarkdownPreviewDock::preserveNativeScrollRatio(
    QWidget *editor, quint64 contentVersion, double ratio)
{
    if (!editor || m_nativePreviewEditor != editor || !m_nativeTextEdit ||
        !m_syncButton || m_syncButton->isChecked()) {
        return;
    }

    m_preservedScrollEditor = editor;
    m_preservedScrollVersion = contentVersion;
    m_preservedScrollRatio = qBound(0.0, ratio, 1.0);
    m_hasPreservedScrollRatio = true;
    m_layoutSyncTimer->start();
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

bool MarkdownPreviewDock::saveHtmlSnapshot(QWidget *dialogParent,
                                           const QByteArray &html,
                                           const QString &sourceFilePath)
{
    if (html.isEmpty()) {
        return false;
    }

    const QFileInfo sourceInfo(sourceFilePath);
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

    QString errorMessage;
    if (!writeHtmlSnapshot(html, targetPath, &errorMessage)) {
        QMessageBox::warning(dialogParent, tr("导出失败"), errorMessage);
        return false;
    }

    return true;
}

bool MarkdownPreviewDock::writeHtmlSnapshot(const QByteArray &html,
                                            const QString &targetPath,
                                            QString *errorMessage)
{
    if (html.isEmpty() || targetPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("没有可写入的 HTML 快照或目标路径。");
        }
        return false;
    }

    QSaveFile output(targetPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = tr("无法写入文件：%1").arg(targetPath);
        }
        return false;
    }

    const qint64 expectedSize = static_cast<qint64>(html.size());
    if (output.write(html) != expectedSize) {
        output.cancelWriting();
        if (errorMessage) {
            *errorMessage = tr("写入文件时发生错误：%1").arg(targetPath);
        }
        return false;
    }
    if (!output.commit()) {
        if (errorMessage) {
            *errorMessage = tr("提交导出文件时发生错误：%1").arg(targetPath);
        }
        return false;
    }

    return true;
}

QAbstractScrollArea *MarkdownPreviewDock::activeScrollArea() const
{
    return m_nativeTextEdit && m_nativePreview && m_nativePreview->isVisible()
        ? static_cast<QAbstractScrollArea *>(m_nativeTextEdit.data())
        : static_cast<QAbstractScrollArea *>(m_browser);
}

void MarkdownPreviewDock::connectNativeScrollBar(QScrollBar *scrollBar)
{
    m_layoutSyncTimer->stop();
    if (m_nativeScrollConnection) {
        disconnect(m_nativeScrollConnection);
    }
    if (m_nativeScrollRangeConnection) {
        disconnect(m_nativeScrollRangeConnection);
    }
    m_nativeScrollConnection = QMetaObject::Connection();
    m_nativeScrollRangeConnection = QMetaObject::Connection();

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

    // QTextDocumentLayout lays out large documents incrementally.  QTextEdit
    // adjusts its scrollbar range while that work advances, so reapply the
    // editor ratio after each coalesced range update.
    m_nativeScrollRangeConnection = connect(
        scrollBar, &QAbstractSlider::rangeChanged, this,
        [this](int, int) {
            if (isVisible() && m_syncButton &&
                (m_syncButton->isChecked() || m_hasPreservedScrollRatio)) {
                m_layoutSyncTimer->start();
            }
        });
}

void MarkdownPreviewDock::restorePreservedScrollRatio()
{
    if (!m_hasPreservedScrollRatio || !m_nativeTextEdit || !m_nativePreview ||
        !m_nativePreview->isVisible() ||
        m_nativePreviewEditor != m_preservedScrollEditor ||
        m_previewEditor != m_preservedScrollEditor ||
        m_previewContentVersion != m_preservedScrollVersion) {
        return;
    }

    QScrollBar *bar = m_nativeTextEdit->verticalScrollBar();
    if (!bar || bar->maximum() <= bar->minimum()) {
        return;
    }

    const int value = bar->minimum() + qRound(
        m_preservedScrollRatio *
        static_cast<double>(bar->maximum() - bar->minimum()));
    const QSignalBlocker blocker(bar);
    bar->setValue(value);
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
        if (m_nativeTextEdit && m_nativePreview && m_nativePreview->isVisible()) {
            if (!scrollNativeToAnchor(url.fragment())) {
                showLinkFailure(url, tr("页内锚点不存在"));
            }
        } else {
            m_browser->scrollToAnchor(url.fragment());
        }
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
        if (!m_urlOpener || !m_urlOpener(resolved)) {
            showLinkFailure(resolved, tr("系统未能打开此链接"));
        }
        return;
    }

    showLinkFailure(resolved, tr("不支持协议 %1").arg(scheme));
}

bool MarkdownPreviewDock::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_nativeTextEdit || watched != m_nativeTextEdit->viewport()) {
        return QDockWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        m_pressedLink = QUrl();
        if (mouseEvent->button() == Qt::LeftButton &&
            mouseEvent->modifiers() == Qt::NoModifier) {
            m_pressedLink = QUrl(m_nativeTextEdit->anchorAt(mouseEvent->pos()));
            m_linkPressPosition = mouseEvent->pos();
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const QUrl releasedLink(m_nativeTextEdit->anchorAt(mouseEvent->pos()));
        const QTextCursor cursor = m_nativeTextEdit->textCursor();
        const bool isClick = mouseEvent->button() == Qt::LeftButton &&
            mouseEvent->modifiers() == Qt::NoModifier &&
            !m_pressedLink.isEmpty() && releasedLink == m_pressedLink &&
            (mouseEvent->pos() - m_linkPressPosition).manhattanLength() <
                QApplication::startDragDistance() &&
            !cursor.hasSelection();
        const QUrl activatedLink = m_pressedLink;
        m_pressedLink = QUrl();
        if (isClick) {
            openLink(activatedLink);
            return true;
        }
    }

    return QDockWidget::eventFilter(watched, event);
}

bool MarkdownPreviewDock::scrollNativeToAnchor(const QString &anchor)
{
    if (!m_nativeTextEdit || anchor.isEmpty()) {
        return false;
    }

    for (QTextBlock block = m_nativeTextEdit->document()->begin();
         block.isValid(); block = block.next()) {
        int anchorPosition = -1;
        if (block.charFormat().anchorNames().contains(anchor)) {
            anchorPosition = block.position();
        }
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (anchorPosition < 0 && fragment.isValid() &&
                fragment.charFormat().anchorNames().contains(anchor)) {
                anchorPosition = fragment.position();
            }
        }
        if (anchorPosition < 0) {
            continue;
        }

        QTextCursor cursor(m_nativeTextEdit->document());
        cursor.setPosition(anchorPosition);
        QScrollBar *bar = m_nativeTextEdit->verticalScrollBar();
        if (bar) {
            const int target = bar->value() +
                m_nativeTextEdit->cursorRect(cursor).top();
            bar->setValue(qBound(bar->minimum(), target, bar->maximum()));
        }
        return true;
    }
    return false;
}

void MarkdownPreviewDock::showLinkFailure(const QUrl &url,
                                          const QString &reason)
{
    Diagnostics::write(
        QStringLiteral("link open rejected or failed: %1 (%2)")
            .arg(url.toDisplayString(), reason));
    if (m_documentLabel) {
        m_documentLabel->setText(tr("链接未打开：%1").arg(reason));
        m_documentLabel->setToolTip(url.toDisplayString());
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
