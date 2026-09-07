#include "markdown_preview_dock.h"

#include "diagnostics.h"

#include <QAbstractSlider>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QPalette>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFrame>
#include <QTextFragment>
#include <QTextTable>
#include <QtMath>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QVBoxLayout>

#include <utility>

namespace {
constexpr int kLayoutSyncDelayMs = 32;

void applyFrameStyle(QTextFrame *frame, const QPalette &palette)
{
    if (!frame) {
        return;
    }

    if (QTextTable *table = dynamic_cast<QTextTable *>(frame)) {
        QTextTableFormat format = table->format();
        format.setBorder(1.0);
        format.setBorderBrush(palette.brush(QPalette::Midlight));
        format.setCellPadding(6.0);
        format.setCellSpacing(0.0);
        table->setFormat(format);

        for (int row = 0; row < table->rows(); ++row) {
            for (int column = 0; column < table->columns(); ++column) {
                QTextTableCell cell = table->cellAt(row, column);
                QTextTableCellFormat cellFormat = cell.format().toTableCellFormat();
                cellFormat.setBackground(row == 0
                    ? palette.brush(QPalette::AlternateBase)
                    : palette.brush(QPalette::Base));
                cell.setFormat(cellFormat);
            }
        }
    }

    for (QTextFrame::iterator it = frame->begin(); !it.atEnd(); ++it) {
        if (QTextFrame *child = it.currentFrame()) {
            applyFrameStyle(child, palette);
        }
    }
}

QString decodeHtmlAttribute(const QString &value)
{
    QTextDocument document;
    document.setHtml(QStringLiteral("<span>%1</span>").arg(value));
    return document.toPlainText();
}

QString exportDiagnosticComment(const QStringList &resources)
{
    if (resources.isEmpty()) {
        return QString();
    }

    QStringList escapedResources;
    for (const QString &resource : resources) {
        QString escaped = resource;
        escaped.replace(QStringLiteral("--"), QStringLiteral("- -"));
        escapedResources.append(escaped);
    }
    return QStringLiteral("\n<!-- markdownview-export: local images not embedded: %1 -->\n")
        .arg(escapedResources.join(QStringLiteral(", ")));
}

QByteArray portableHtmlSnapshot(const QTextDocument *document)
{
    if (!document) {
        return QByteArray();
    }

    QString html = document->toHtml("UTF-8");
    const QRegularExpression imageSourcePattern(
        QStringLiteral("(<img\\b[^>]*\\bsrc\\s*=\\s*)([\\\"'])([^\\\"']+)\\2"),
        QRegularExpression::CaseInsensitiveOption);
    QList<QPair<int, QPair<int, QString>>> replacements;
    QStringList missingResources;
    qint64 embeddedBytes = 0;

    QRegularExpressionMatchIterator matches = imageSourcePattern.globalMatch(html);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString source = decodeHtmlAttribute(match.captured(3));
        const QUrl sourceUrl(source);
        if (sourceUrl.scheme().compare(QStringLiteral("data"), Qt::CaseInsensitive) == 0 ||
            sourceUrl.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0 ||
            sourceUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 ||
            (!sourceUrl.scheme().isEmpty() &&
             sourceUrl.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) != 0)) {
            continue;
        }

        QUrl resolvedUrl = sourceUrl.isRelative()
            ? document->baseUrl().resolved(sourceUrl)
            : sourceUrl;
        resolvedUrl.setFragment(QString());
        resolvedUrl.setQuery(QString());
        const QString localPath = resolvedUrl.toLocalFile();
        QFile imageFile(localPath);
        if (localPath.isEmpty() || !imageFile.open(QIODevice::ReadOnly)) {
            missingResources.append(source);
            continue;
        }

        const QByteArray imageBytes = imageFile.readAll();
        const QString mimeType = QMimeDatabase().mimeTypeForFile(
            localPath, QMimeDatabase::MatchContent).name();
        if (imageBytes.isEmpty() || !mimeType.startsWith(QStringLiteral("image/"))) {
            missingResources.append(source);
            continue;
        }

        const QString dataUrl = QStringLiteral("data:%1;base64,%2")
            .arg(mimeType, QString::fromLatin1(imageBytes.toBase64()));
        replacements.append(qMakePair(
            match.capturedStart(3),
            qMakePair(match.capturedLength(3), dataUrl)));
        embeddedBytes += imageBytes.size();
    }

    for (auto it = replacements.crbegin(); it != replacements.crend(); ++it) {
        html.replace(it->first, it->second.first, it->second.second);
    }

    if (!missingResources.isEmpty()) {
        missingResources.removeDuplicates();
        const QString comment = exportDiagnosticComment(missingResources);
        const int bodyEnd = html.lastIndexOf(QStringLiteral("</body>"), -1,
                                             Qt::CaseInsensitive);
        html.insert(bodyEnd >= 0 ? bodyEnd : html.size(), comment);
        Diagnostics::write(
            QStringLiteral("HTML export kept %1 unavailable local image reference(s): %2")
                .arg(missingResources.size())
                .arg(missingResources.join(QStringLiteral(", "))));
    }
    Diagnostics::write(
        QStringLiteral("HTML export embedded %1 local image(s), source bytes=%2")
            .arg(replacements.size())
            .arg(embeddedBytes));
    return html.toUtf8();
}
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
    m_documentLabel->setObjectName(
        QStringLiteral("NddMarkdownPreviewDocumentLabel"));
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

    m_themeStyleTimer = new QTimer(this);
    m_themeStyleTimer->setSingleShot(true);
    m_themeStyleTimer->setInterval(0);

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
    connect(m_themeStyleTimer, &QTimer::timeout, this, [this]() {
        if (m_nativeTextEdit && m_previewEditor && m_previewContentVersion != 0) {
            refreshDocumentStyle(m_previewEditor.data(), m_previewContentVersion);
        }
        if (m_browser) {
            applyDocumentStyle(m_browser);
        }
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
    if (m_themeStyleTimer) {
        m_themeStyleTimer->stop();
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
    applyDocumentStyle(textEdit);
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

    return portableHtmlSnapshot(m_nativeTextEdit->document());
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

void MarkdownPreviewDock::setRefreshStatus(const QString &status,
                                           const QString &toolTip)
{
    if (!m_documentLabel) {
        return;
    }
    m_documentLabel->setText(status);
    m_documentLabel->setToolTip(toolTip);
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

void MarkdownPreviewDock::refreshDocumentStyle(QWidget *editor,
                                               quint64 contentVersion)
{
    if (!hasPreviewFor(editor, contentVersion) || !m_nativeTextEdit) {
        return;
    }

    const double ratio = scrollRatio();
    applyDocumentStyle(m_nativeTextEdit);
    QTimer::singleShot(0, this, [this, editor = QPointer<QWidget>(editor),
                                 contentVersion, ratio]() {
        if (hasPreviewFor(editor.data(), contentVersion)) {
            scrollToRatio(ratio);
        }
    });
}

void MarkdownPreviewDock::applyDocumentStyle(QTextEdit *textEdit)
{
    if (!textEdit || !textEdit->document()) {
        return;
    }

    const QPalette colors = palette();
    if (textEdit->palette() != colors) {
        textEdit->setPalette(colors);
    }
    QTextDocument *document = textEdit->document();
    document->setDefaultStyleSheet(loadStyleSheet());

    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat blockFormat = block.blockFormat();
        const int headingLevel = blockFormat.property(QTextFormat::HeadingLevel).toInt();
        const int quoteLevel = blockFormat.property(QTextFormat::BlockQuoteLevel).toInt();
        const bool codeBlock = blockFormat.hasProperty(QTextFormat::BlockCodeFence) ||
            blockFormat.hasProperty(QTextFormat::BlockCodeLanguage);

        if (headingLevel > 0) {
            QTextCharFormat headingFormat;
            headingFormat.setFontWeight(QFont::DemiBold);
            headingFormat.setFontPointSize(qMax(11.0, 22.0 - headingLevel * 2.0));
            headingFormat.setForeground(colors.brush(QPalette::Text));
            QTextCursor cursor(block);
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.mergeCharFormat(headingFormat);
            blockFormat.setTopMargin(12.0);
            blockFormat.setBottomMargin(6.0);
        }
        if (quoteLevel > 0) {
            blockFormat.setLeftMargin(12.0 * quoteLevel);
            blockFormat.setBackground(colors.brush(QPalette::AlternateBase));
        }
        if (codeBlock) {
            blockFormat.setBackground(colors.brush(QPalette::AlternateBase));
            blockFormat.setLeftMargin(10.0);
            blockFormat.setRightMargin(10.0);
            blockFormat.setTopMargin(6.0);
            blockFormat.setBottomMargin(6.0);
            QTextCharFormat codeFormat;
            codeFormat.setFontFamily(QStringLiteral("Consolas"));
            codeFormat.setFontFixedPitch(true);
            codeFormat.setForeground(colors.brush(QPalette::Text));
            QTextCursor cursor(block);
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.mergeCharFormat(codeFormat);
        }
        QTextCursor blockCursor(block);
        blockCursor.setBlockFormat(blockFormat);

        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) {
                continue;
            }
            QTextCharFormat format = fragment.charFormat();
            bool changed = false;
            if (format.isAnchor()) {
                format.setForeground(colors.brush(QPalette::Link));
                format.setFontUnderline(true);
                changed = true;
            }
            if (!codeBlock && format.fontFixedPitch()) {
                format.setBackground(colors.brush(QPalette::AlternateBase));
                format.setForeground(colors.brush(QPalette::Text));
                changed = true;
            }
            if (changed) {
                QTextCursor cursor(document);
                cursor.setPosition(fragment.position());
                cursor.setPosition(fragment.position() + fragment.length(),
                                   QTextCursor::KeepAnchor);
                cursor.mergeCharFormat(format);
            }
        }
    }

    applyFrameStyle(document->rootFrame(), colors);
    textEdit->viewport()->update();
}

void MarkdownPreviewDock::scheduleThemeStyleRefresh()
{
    if (m_themeStyleTimer) {
        m_themeStyleTimer->start();
    }
}

void MarkdownPreviewDock::changeEvent(QEvent *event)
{
    QDockWidget::changeEvent(event);
    if (event && (event->type() == QEvent::PaletteChange ||
                  event->type() == QEvent::ApplicationPaletteChange ||
                  event->type() == QEvent::StyleChange)) {
        scheduleThemeStyleRefresh();
    }
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
    if (event && (event->type() == QEvent::PaletteChange ||
                  event->type() == QEvent::ApplicationPaletteChange ||
                  event->type() == QEvent::StyleChange)) {
        scheduleThemeStyleRefresh();
    }
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
