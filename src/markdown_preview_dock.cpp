#include "markdown_preview_dock.h"

#include "diagnostics.h"
#include "heading_outline.h"
#include "preview_search.h"
#include "code_block_tools.h"
#include "saved_markdown_font.h"

#include <QAbstractSlider>
#include <QDialog>
#include <QPlainTextEdit>
#include <QActionGroup>
#include <QMenu>
#include <QSplitter>
#include <QSplitterHandle>
#include <QApplication>
#include <QDesktopServices>
#include <QClipboard>
#include "preview_ui.h"
#include <QMainWindow>
#include <QResizeEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QPalette>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QWheelEvent>
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
#include <QVBoxLayout>

#include <utility>
#include <algorithm>
#include <iterator>

namespace {
constexpr int kLayoutSyncDelayMs = 32;
constexpr auto kStyleRevision = "_markdownview_font_style_revision";
constexpr auto kDocumentRevision = "_markdownview_font_document_revision";
constexpr int kCodeFormatProperty = QTextFormat::UserProperty + 2;

qreal headingRatio(int level)
{
    static const qreal ratios[] = {1.0, 2.0, 1.75, 1.50, 1.30, 1.15, 1.05};
    return level >= 1 && level <= 6 ? ratios[level] : 1.0;
}

double barRatio(const QScrollBar *bar)
{
    return bar && bar->maximum() > bar->minimum()
        ? static_cast<double>(bar->value() - bar->minimum()) /
            static_cast<double>(bar->maximum() - bar->minimum()) : 0.0;
}

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

QByteArray portableHtmlSnapshot(const QTextDocument *document, const QObject *context)
{
    if (!document) {
        return QByteArray();
    }

    QString html = CodeBlockTools::htmlForExport(document);
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
        Diagnostics::write(context,
            QStringLiteral("HTML export kept %1 unavailable local image reference(s): %2")
                .arg(missingResources.size())
                .arg(missingResources.join(QStringLiteral(", "))));
    }
    Diagnostics::write(context,
        QStringLiteral("HTML export embedded %1 local image(s), source bytes=%2")
            .arg(replacements.size())
            .arg(embeddedBytes));
    return html.toUtf8();
}
}

MarkdownPreviewDock::MarkdownPreviewDock(QWidget *parent)
    : QDockWidget(tr("预览"), parent),
      m_urlOpener([](const QUrl &url) {
          return QDesktopServices::openUrl(url);
      })
{
    const SavedMarkdownFont font = defaultMarkdownFont();
    m_bodyFont = QFont(font.family);
    m_bodyFont.setPointSizeF(font.pointSize);
    m_codeFontFamily = markdownCodeFontFamily(font.family);
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

    m_dockOwner = qobject_cast<QMainWindow *>(parent);
    auto *title = new QWidget(this);
    title->setObjectName(QStringLiteral("NddMarkdownTitleBar"));
    auto *titleLayout = new QHBoxLayout(title);
    titleLayout->setContentsMargins(10, 2, 4, 2);
    titleLayout->setSpacing(4);
    auto *caption = new QLabel(tr("预览"), title);
    caption->setAttribute(Qt::WA_TransparentForMouseEvents);
    titleLayout->addWidget(caption);
    m_documentLabel = new QLabel(title);
    m_documentLabel->setTextFormat(Qt::PlainText);
    m_documentLabel->setObjectName(QStringLiteral("NddMarkdownPreviewDocumentLabel"));
    m_documentLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    titleLayout->addWidget(m_documentLabel, 1);
    m_floatButton = new QToolButton(title);
    m_floatButton->setObjectName(QStringLiteral("NddMarkdownFloatButton"));
    titleLayout->addWidget(m_floatButton);
    auto *closeButton = new QToolButton(title);
    closeButton->setObjectName(QStringLiteral("NddMarkdownCloseButton"));
    PreviewUi::setup(closeButton, PreviewUi::Symbol::Close, tr("关闭预览"));
    titleLayout->addWidget(closeButton);
    setTitleBarWidget(title);
    connect(closeButton, &QToolButton::clicked, this, &QWidget::close);
    connect(m_floatButton, &QToolButton::clicked, this, [this]() {
        if (!m_dockOwner) return;
        preserveLayoutTarget();
        if (isFloating()) {
            m_dockOwner->addDockWidget(m_lastDockArea, this);
            setFloating(false);
        } else {
            setFloating(true);
        }
    });
    connect(this, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea area) {
        if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea)
            m_lastDockArea = area;
    });
    connect(this, &QDockWidget::topLevelChanged, this, [this]() { updateChrome(); });

    auto *refreshGroup = new QWidget(toolbar);
    refreshGroup->setObjectName(QStringLiteral("NddMarkdownRefreshGroup"));
    auto *refreshLayout = new QHBoxLayout(refreshGroup);
    refreshLayout->setContentsMargins(1, 1, 1, 1);
    refreshLayout->setSpacing(0);
    auto *refreshButton = new QToolButton(refreshGroup);
    refreshButton->setObjectName(QStringLiteral("NddMarkdownRefreshButton"));
    PreviewUi::setup(refreshButton, PreviewUi::Symbol::Refresh, tr("立即刷新当前文档"));
    refreshLayout->addWidget(refreshButton);
    m_modeButton = new QToolButton(refreshGroup);
    m_modeButton->setObjectName(QStringLiteral("NddMarkdownRefreshMode"));
    m_modeButton->setAccessibleName(tr("刷新模式"));
    m_modeButton->setToolTip(tr("选择当前窗口的刷新模式"));
    m_modeButton->setFocusPolicy(Qt::StrongFocus);
    m_modeButton->setMinimumHeight(28);
    m_modeButton->setPopupMode(QToolButton::InstantPopup);
    auto *modeMenu = new QMenu(m_modeButton);
    auto *modeGroup = new QActionGroup(modeMenu);
    m_autoMode = modeMenu->addAction(tr("自动刷新"));
    m_manualMode = modeMenu->addAction(tr("手动刷新"));
    m_autoMode->setObjectName(QStringLiteral("NddMarkdownAutomaticMode"));
    m_manualMode->setObjectName(QStringLiteral("NddMarkdownManualMode"));
    for (auto *action : {m_autoMode, m_manualMode}) {
        action->setCheckable(true);
        modeGroup->addAction(action);
    }
    connect(m_autoMode, &QAction::triggered, this, [this]() { emit refreshModeChanged(RefreshMode::Automatic); });
    connect(m_manualMode, &QAction::triggered, this, [this]() { emit refreshModeChanged(RefreshMode::Manual); });
    m_modeButton->setMenu(modeMenu);
    setRefreshMode(RefreshMode::Automatic);
    refreshLayout->addWidget(m_modeButton);
    toolbarLayout->addWidget(refreshGroup);
    toolbarLayout->setContentsMargins(10, 4, 10, 4);
    toolbarLayout->setSpacing(4);
    toolbarLayout->addSpacing(8);

    m_syncButton = new QToolButton(toolbar);
    m_syncButton->setText(tr("同步滚动"));
    m_syncButton->setObjectName(QStringLiteral("NddMarkdownSyncButton"));
    PreviewUi::setup(m_syncButton, PreviewUi::Symbol::Sync, tr("同步滚动"));
    m_syncButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_syncButton->setCheckable(true);
    m_syncButton->setChecked(true);
    toolbarLayout->addWidget(m_syncButton);
    toolbarLayout->addStretch();

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
    m_search = new PreviewSearch(container);
    layout->addWidget(m_search);
    m_codeTools = new CodeBlockTools(container);
    auto *searchButton = new QToolButton(toolbar);
    PreviewUi::setup(searchButton, PreviewUi::Symbol::Search, tr("在预览中查找（Ctrl+F）"));
    searchButton->setObjectName(QStringLiteral("NddMarkdownSearchOpen"));
    searchButton->setToolTip(tr("在预览中查找（Ctrl+F）"));
    toolbarLayout->addWidget(searchButton);
    connect(searchButton, &QToolButton::clicked, m_search, &PreviewSearch::openSearch);
    connect(m_search, &PreviewSearch::navigateRequested,
            this, &MarkdownPreviewDock::navigatePreviewPosition);
    connect(m_search, &PreviewSearch::closed, this, [this]() {
        activeScrollArea()->setFocus(Qt::ShortcutFocusReason);
    });
    m_browser->installEventFilter(this);
    m_browser->viewport()->installEventFilter(this);
    auto *statusRow = new QWidget(container);
    auto *statusLayout = new QHBoxLayout(statusRow);
    statusRow->setFixedHeight(30);
    statusLayout->setContentsMargins(8, 4, 8, 4);
    m_statusLabel = new QLabel(tr("没有活动文档"), statusRow);
    m_statusLabel->setObjectName(QStringLiteral("NddMarkdownPreviewStatusLabel"));
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setWordWrap(false);
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                         Qt::TextSelectableByKeyboard);
    statusLayout->addWidget(m_statusLabel);
    m_errorRow = new QWidget(container);
    m_errorRow->setObjectName(QStringLiteral("NddMarkdownErrorRow"));
    auto *errorLayout = new QHBoxLayout(m_errorRow);
    errorLayout->setContentsMargins(10, 2, 10, 2);
    m_errorLabel = new QLabel(m_errorRow);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setTextFormat(Qt::PlainText);
    errorLayout->addWidget(m_errorLabel, 1);
    m_retryButton = new QToolButton(m_errorRow);
    m_retryButton->setObjectName(QStringLiteral("NddMarkdownRetryButton"));
    m_retryButton->setText(tr("重试"));
    m_retryButton->setFocusPolicy(Qt::StrongFocus);
    errorLayout->addWidget(m_retryButton);
    m_detailsButton = new QToolButton(m_errorRow);
    m_detailsButton->setText(tr("详情"));
    m_detailsButton->setFocusPolicy(Qt::StrongFocus);
    m_detailsButton->setObjectName(QStringLiteral("NddMarkdownStatusDetails"));
    errorLayout->addWidget(m_detailsButton);
    m_errorRow->hide();
    layout->addWidget(m_errorRow);
    m_feedbackLabel = new QLabel(container);
    m_feedbackLabel->setObjectName(QStringLiteral("NddMarkdownLinkFeedback"));
    m_feedbackLabel->setTextFormat(Qt::PlainText);
    m_feedbackLabel->setWordWrap(false);
    m_feedbackLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_feedbackLabel->setMargin(0);
    m_feedbackLabel->hide();

    m_splitter = new QSplitter(Qt::Horizontal, container);
    m_splitter->setObjectName(QStringLiteral("NddMarkdownOutlineSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_outline = new HeadingOutline(m_splitter);
    m_previewContainer = new QWidget(m_splitter);
    auto *previewLayout = new QVBoxLayout(m_previewContainer);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->addWidget(m_browser);
    m_contentLayout = previewLayout;
    m_splitter->addWidget(m_outline);
    m_splitter->addWidget(m_previewContainer);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({m_outlineWidth, 400});
    m_splitter->handle(1)->installEventFilter(this);
    layout->addWidget(m_splitter, 1);
    layout->addWidget(statusRow);
    statusLayout->addWidget(m_feedbackLabel);
    m_headingTimer = new QTimer(this);
    m_headingTimer->setSingleShot(true);
    m_headingTimer->setInterval(kLayoutSyncDelayMs);
    connect(m_headingTimer, &QTimer::timeout, this, &MarkdownPreviewDock::updateCurrentHeading);
    connect(m_outline, &HeadingOutline::headingActivated, this,
            &MarkdownPreviewDock::headingActivated);
    connect(m_splitter, &QSplitter::splitterMoved, this, [this]() {
        m_outlineWidth = m_outline->width();
        m_layoutSyncTimer->start();
        m_headingTimer->start();
    });
    auto *settings = new QToolButton(toolbar);
    PreviewUi::setup(settings, PreviewUi::Symbol::Settings, tr("预览设置"));
    settings->setObjectName(QStringLiteral("NddMarkdownOutlineSettings"));
    settings->setAccessibleName(tr("预览设置"));
    settings->setPopupMode(QToolButton::InstantPopup);
    auto *settingsMenu = new QMenu(settings);
    settingsMenu->addSection(tr("大纲"));
    auto *visible = settingsMenu->addAction(tr("显示大纲"));
    visible->setObjectName(QStringLiteral("NddMarkdownOutlineVisible"));
    visible->setCheckable(true);
    visible->setChecked(true);
    connect(visible, &QAction::toggled, this, [this](bool show) {
        preserveLayoutTarget();
        m_outlineWanted = show;
        updateResponsiveLayout();
        m_layoutSyncTimer->start();
    });
    auto *positionMenu = settingsMenu->addMenu(tr("位置"));
    auto *sideGroup = new QActionGroup(positionMenu);
    for (bool right : {false, true}) {
        auto *side = positionMenu->addAction(right ? tr("右侧") : tr("左侧"));
        side->setObjectName(right ? QStringLiteral("NddMarkdownOutlineRight") : QStringLiteral("NddMarkdownOutlineLeft"));
        side->setCheckable(true);
        side->setChecked(!right);
        sideGroup->addAction(side);
        connect(side, &QAction::triggered, this, [this, right]() { setOutlineOnRight(right); });
    }
    settingsMenu->addSeparator();
    settingsMenu->addSection(tr("阅读"));
    QSettings codeSettings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("markdownviewdd"), QStringLiteral("reading"));
    m_wrapCode = codeSettings.value(QStringLiteral("codeBlocks/visualWrap"), true).toBool();
    auto *wrap = settingsMenu->addAction(tr("代码块自动换行"));
    wrap->setObjectName(QStringLiteral("NddMarkdownCodeWrap"));
    wrap->setCheckable(true);
    wrap->setChecked(m_wrapCode);
    wrap->setToolTip(tr("关闭后保持长行，按需使用预览水平滚动条"));
    connect(wrap, &QAction::toggled, this, [this](bool enabled) {
        preserveLayoutTarget();
        m_wrapCode = enabled;
        QSettings saved(QSettings::IniFormat, QSettings::UserScope,
                        QStringLiteral("markdownviewdd"), QStringLiteral("reading"));
        saved.setValue(QStringLiteral("codeBlocks/visualWrap"), enabled);
        saved.sync();
        ++m_styleRevision;
        refreshDocumentStyle(m_previewEditor, m_previewContentVersion);
        m_layoutSyncTimer->start();
        if (saved.status() != QSettings::NoError)
            setNavigationFeedback(tr("长行显示已应用，但无法保存偏好；重开后可能恢复默认。"));
    });
    settings->setMenu(settingsMenu);
    toolbarLayout->addWidget(settings);
    auto *documentDetails = settingsMenu->addAction(tr("文档与状态详情"));
    connect(documentDetails, &QAction::triggered, m_detailsButton, &QToolButton::click);
    setWidget(container);
    m_chromeWidgets = {title, toolbar, m_search, m_errorRow, statusRow};
    updateChrome();

    connect(m_retryButton, &QToolButton::clicked,
            this, &MarkdownPreviewDock::refreshRequested);
    connect(m_detailsButton, &QToolButton::clicked, this, [this]() {
        const QString details = m_documentDetails + QStringLiteral("\n") + m_statusDetails;
        auto *dialog = new QDialog(this);
        dialog->setObjectName(QStringLiteral("NddMarkdownDetailsDialog"));
        dialog->setWindowTitle(tr("预览详情"));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        auto *detailsLayout = new QVBoxLayout(dialog);
        auto *text = new QPlainTextEdit(dialog);
        text->setReadOnly(true);
        text->setPlainText(details);
        text->setAccessibleName(tr("文档与状态详情"));
        detailsLayout->addWidget(text);
        auto *actions = new QHBoxLayout;
        auto *copy = new QToolButton(dialog);
        copy->setObjectName(QStringLiteral("NddMarkdownCopyDetails"));
        copy->setText(tr("复制详情"));
        PreviewUi::setup(copy, PreviewUi::Symbol::Copy, tr("复制详情"));
        copy->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        actions->addWidget(copy);
        actions->addStretch();
        auto *close = new QToolButton(dialog);
        close->setText(tr("关闭"));
        PreviewUi::setup(close, PreviewUi::Symbol::Close, tr("关闭详情"));
        actions->addWidget(close);
        detailsLayout->addLayout(actions);
        dialog->resize(460, 260);
        connect(close, &QToolButton::clicked, dialog, &QDialog::accept);
        connect(copy, &QToolButton::clicked, dialog, [details, copy]() {
            QApplication::clipboard()->setText(details);
            copy->setToolTip(tr("已复制详情"));
            copy->setIcon(PreviewUi::icon(PreviewUi::Symbol::Success, copy));
        });
        dialog->open();
    });
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
        m_search->updateHighlights();
        if (m_hasNavigationTarget) {
            restoreNavigationTarget();
            return;
        }
        if (m_hasPreservedScrollRatio) {
            restorePreservedScrollRatio();
            return;
        }
        if (!m_syncButton->isChecked()) {
            return;
        }
        if (m_nativeTextEdit && m_nativePreview && m_nativePreview->isVisible()) {
            m_nativeTextEdit->viewport()->update();
        }
        emit previewScrollRangeChanged();
    });
    connect(m_themeStyleTimer, &QTimer::timeout, this, [this]() {
        updateChrome();
        ++m_styleRevision;
        if (m_nativeTextEdit && m_previewEditor && m_previewContentVersion != 0) {
            refreshDocumentStyle(m_previewEditor.data(), m_previewContentVersion);
        }
        if (m_browser) {
            applyDocumentStyle(m_browser);
        }
        m_search->updateHighlights();
    });
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        m_search->setActive(visible);
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
    delete m_codeTools;
    m_codeTools = nullptr;
    delete m_search;
    m_search = nullptr;
    m_headingTimer->stop();
    disconnect(m_headingScrollConnection);
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
    disconnect(m_nativeHorizontalActionConnection);
    m_nativeHorizontalActionConnection = QMetaObject::Connection();
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
                                             QTextEdit *textEdit,
                                             const QString &filePath,
                                             QWidget *editor,
                                             quint64 contentVersion,
                                             bool current)
{
    if (!previewWindow || !textEdit || !editor || !m_contentLayout || !widget()) {
        return false;
    }

    const double previousRatio = barRatio(textEdit->verticalScrollBar());
    textEdit->document()->setBaseUrl(baseUrlForFile(filePath));

    if (m_nativePreview && m_nativePreview != previewWindow) {
        m_contentLayout->removeWidget(m_nativePreview);
        m_nativePreview->hide();
    }

    if (m_nativePreview != previewWindow) {
        if (m_nativeTextEdit) {
            m_nativeTextEdit->viewport()->removeEventFilter(this);
            m_nativeTextEdit->verticalScrollBar()->removeEventFilter(this);
            m_nativeTextEdit->horizontalScrollBar()->removeEventFilter(this);
            m_nativeTextEdit->removeEventFilter(this);
        }
        previewWindow->hide();
        previewWindow->setParent(m_previewContainer, Qt::Widget);

        m_contentLayout->addWidget(previewWindow);
        m_nativePreview = previewWindow;
        m_nativeTextEdit = textEdit;
        m_nativeTextEdit->viewport()->installEventFilter(this);
        m_nativeTextEdit->verticalScrollBar()->installEventFilter(this);
        m_nativeTextEdit->horizontalScrollBar()->installEventFilter(this);
        m_nativeTextEdit->installEventFilter(this);
        m_currentNativePreviewObject = previewWindow;
        connectNativeScrollBar(textEdit->verticalScrollBar());
        trackNativePreview(previewWindow);
    }

    if (m_hasPreservedScrollRatio && m_preservedScrollEditor != editor) {
        m_preservedScrollEditor = nullptr;
        m_preservedScrollVersion = 0;
        m_hasPreservedScrollRatio = false;
    }
    if (m_previewEditor != editor || m_previewContentVersion != contentVersion) {
        releaseNavigationTarget();
        m_outline->clearSnapshot();
    }
    m_nativePreviewEditor = editor;
    m_currentFilePath = filePath;
    m_previewEditor = editor;
    m_previewContentVersion = contentVersion;
    m_previewIsCurrent = current;
    if (current && m_syncButton->isChecked()) {
        m_hasPreservedScrollRatio = false;
        m_preservedScrollEditor = nullptr;
        m_preservedScrollVersion = 0;
    }
    restyleNativePreview(previousRatio);
    m_browser->hide();
    previewWindow->show();
    Diagnostics::write(this, QStringLiteral("native MarkdownView embedded in dock"));
    return true;
}

void MarkdownPreviewDock::invalidatePreview()
{
    m_codeTools->clearSnapshot();
    m_search->setSnapshot(nullptr, nullptr, 0);
    markPreviewStale();
    m_pressedLink = QUrl();
    m_previewEditor = nullptr;
    m_previewContentVersion = 0;
    releaseNavigationTarget();
    m_outline->clearSnapshot();
}

void MarkdownPreviewDock::markPreviewStale()
{
    cancelPreservedScroll();
    m_previewIsCurrent = false;
}

bool MarkdownPreviewDock::hasPreviewFor(QWidget *editor,
                                       quint64 contentVersion) const
{
    return m_previewIsCurrent && hasDisplayedPreviewFor(editor, contentVersion);
}

bool MarkdownPreviewDock::hasDisplayedPreviewFor(QWidget *editor,
                                                quint64 contentVersion) const
{
    return editor && m_previewEditor == editor &&
        m_previewContentVersion == contentVersion &&
        m_nativePreview && m_nativeTextEdit && !m_nativePreview->isHidden();
}

QByteArray MarkdownPreviewDock::htmlSnapshotFor(
    QWidget *editor, quint64 contentVersion) const
{
    if (!hasPreviewFor(editor, contentVersion)) {
        return QByteArray();
    }

    return portableHtmlSnapshot(m_nativeTextEdit->document(), this);
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
    disconnect(m_nativeHorizontalActionConnection);
    m_nativeHorizontalActionConnection = QMetaObject::Connection();
    m_nativeScrollConnection = QMetaObject::Connection();
    m_nativeScrollRangeConnection = QMetaObject::Connection();
    m_codeTools->clearSnapshot();
    m_search->setSnapshot(nullptr, nullptr, 0);
    m_nativePreview = nullptr;
    m_nativeTextEdit = nullptr;
    m_previewEditor = nullptr;
    m_nativePreviewEditor = nullptr;
    m_preservedScrollEditor = nullptr;
    m_previewContentVersion = 0;
    m_preservedScrollVersion = 0;
    m_hasPreservedScrollRatio = false;
    m_currentNativePreviewObject = nullptr;
    releaseNavigationTarget();
    m_outline->clearSnapshot();
    m_pressedLink = QUrl();
    if (m_layoutSyncTimer) {
        m_layoutSyncTimer->stop();
    }
    if (m_browser) {
        m_browser->show();
    }
    emit displayedPreviewDestroyed();
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
    Diagnostics::write(this, QStringLiteral("renderMarkdown entered"));
    const double previousRatio = scrollRatio();
    m_currentFilePath = filePath;

    QTextDocument *document = m_browser->document();
    Diagnostics::write(this, QStringLiteral("setting document base URL"));
    document->setBaseUrl(baseUrlForFile(filePath));
    Diagnostics::write(this, QStringLiteral("setting document style sheet"));
    document->setDefaultStyleSheet(loadStyleSheet());
    Diagnostics::write(this, QStringLiteral("calling QTextDocument::setMarkdown"));
    document->setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
    applyDocumentStyle(m_browser);
    Diagnostics::write(this, QStringLiteral("QTextDocument::setMarkdown returned"));

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
                                          int characterCount,
                                          bool hasDocument)
{
    m_documentName = !hasDocument ? QString()
        : filePath.isEmpty() ? tr("未命名文档") : QFileInfo(filePath).fileName();
    m_documentDetails = filePath.isEmpty() ? m_documentName : filePath;
    if (characterCount >= 0) m_documentDetails += tr(" · 长度 %1").arg(characterCount);
    m_documentLabel->setToolTip(m_documentDetails);
    setWindowTitle(m_documentName.isEmpty() ? tr("预览") : tr("预览 · %1").arg(m_documentName));
    updateResponsiveLayout();
    m_feedbackLabel->hide();
}

void MarkdownPreviewDock::setRefreshStatus(const QString &status,
                                           const QString &details,
                                           bool failed)
{
    m_statusLabel->setText(failed ? tr("待刷新 · 请处理上方错误") : status);
    m_statusLabel->setToolTip(details);
    m_statusDetails = details;
    m_errorLabel->setText(status.section(QChar(0xff1a), 0, 0));
    m_errorRow->setVisible(failed);
    m_retryButton->setVisible(failed);
}

void MarkdownPreviewDock::setRefreshMode(RefreshMode mode)
{
    m_autoMode->setChecked(mode == RefreshMode::Automatic);
    m_manualMode->setChecked(mode == RefreshMode::Manual);
    m_modeButton->setText(mode == RefreshMode::Automatic ? tr("自动") : tr("手动"));
}

void MarkdownPreviewDock::updateChrome()
{
    if (!m_floatButton || m_updatingChrome) return;
    const QScopedValueRollback<bool> updating(m_updatingChrome, true);
    PreviewUi::setup(m_floatButton, isFloating() ? PreviewUi::Symbol::Dock : PreviewUi::Symbol::Float,
        isFloating() ? tr("停靠回主窗口") : tr("浮动窗口"));
    m_floatButton->setEnabled(!m_dockOwner.isNull());
    if (!m_dockOwner) m_floatButton->setToolTip(tr("当前宿主没有可用的主窗口停靠区域"));
    const QString chromeStyle = QStringLiteral(
        "QToolButton { border: 1px solid transparent; border-radius: 4px; padding: 3px; }"
        "QToolButton:hover, QToolButton:pressed { background: palette(midlight); }"
        "QToolButton:checked { background: palette(alternate-base); border-color: palette(highlight); }"
        "QToolButton:focus { border-color: palette(highlight); }"
        "QWidget#NddMarkdownRefreshGroup { border: 1px solid palette(mid); border-radius: 4px; }"
        "QWidget#NddMarkdownTitleBar { border-bottom: 1px solid palette(midlight); }");
    // Re-polish only chrome: touching the Dock stylesheet also re-polishes the
    // native QTextEdit and can reset its palette and reading position.
    for (auto *chrome : m_chromeWidgets) {
        chrome->setStyleSheet(QString());
        chrome->setPalette(palette());
        chrome->setStyleSheet(chromeStyle);
    }
}

void MarkdownPreviewDock::resizeEvent(QResizeEvent *event)
{
    QDockWidget::resizeEvent(event);
    updateResponsiveLayout();
}

void MarkdownPreviewDock::updateResponsiveLayout()
{
    if (!m_outline) return;
    if (width() < 440) m_compactOutline = true;
    else if (width() >= 500) m_compactOutline = false;
    m_outline->setVisible(m_outlineWanted && !m_compactOutline);
    m_syncButton->setToolButtonStyle(width() < 380 ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    m_documentLabel->setText(m_documentLabel->fontMetrics().elidedText(
        m_documentName, Qt::ElideRight, qMax(0, m_documentLabel->width())));
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
    if (m_hasNavigationTarget || !editor || m_nativePreviewEditor != editor || !m_nativeTextEdit ||
        !m_syncButton || (m_syncButton->isChecked() && m_previewIsCurrent) ||
        !qIsFinite(ratio)) {
        return;
    }

    m_preservedScrollEditor = editor;
    m_preservedScrollVersion = contentVersion;
    m_preservedScrollRatio = qBound(0.0, ratio, 1.0);
    m_preservedStyleGeneration = m_styleUpdateGeneration;
    m_preservedInteractionGeneration = m_scrollInteractionGeneration;
    m_hasPreservedScrollRatio = true;
    m_layoutSyncTimer->start();
}

void MarkdownPreviewDock::setReadingFont(const QFont &font, qreal zoom)
{
    const qreal points = font.pointSizeF() * zoom;
    if (font.family().isEmpty() || !qIsFinite(zoom) || zoom <= 0.0 ||
        !qIsFinite(points * 2.0) || points <= 0.0) {
        return;
    }
    if (m_bodyFont == font && m_zoom == zoom) {
        return;
    }
    m_bodyFont = font;
    m_codeFontFamily = markdownCodeFontFamily(font.family());
    m_zoom = zoom;
    ++m_styleRevision;
    refreshDocumentStyle(m_previewEditor.data(), m_previewContentVersion);
}

void MarkdownPreviewDock::refreshDocumentStyle(QWidget *editor,
                                               quint64 contentVersion)
{
    if (hasDisplayedPreviewFor(editor, contentVersion) && m_nativeTextEdit) {
        restyleNativePreview(barRatio(m_nativeTextEdit->verticalScrollBar()));
    }
}

void MarkdownPreviewDock::restyleNativePreview(double ratio)
{
    if (!m_nativeTextEdit || !m_previewEditor || !m_previewContentVersion) {
        return;
    }
    m_codeTools->setFormatting(true);
    m_search->setFormatting(true);
    const bool changed = applyDocumentStyle(m_nativeTextEdit);
    m_search->setFormatting(false);
    m_codeTools->setFormatting(false);
    if (!changed) {
        return;
    }
    const quint64 styleGeneration = ++m_styleUpdateGeneration;
    m_hasPreservedScrollRatio = false;
    m_preservedScrollEditor = nullptr;
    m_preservedScrollVersion = 0;
    const quint64 interactionGeneration = m_scrollInteractionGeneration;
    const quint64 version = m_previewContentVersion;
    const QPointer<QWidget> editor = m_previewEditor;
    const QPointer<QTextEdit> textEdit = m_nativeTextEdit;
    // Old snapshots retain their own position even when the sync toggle is on.
    // A current synchronized snapshot remains driven by the editor's position.
    preserveNativeScrollRatio(editor.data(), version, ratio);
    QTimer::singleShot(0, this, [this, editor, textEdit, version,
                                 styleGeneration, interactionGeneration]() {
        if (textEdit != m_nativeTextEdit ||
            !hasDisplayedPreviewFor(editor.data(), version) ||
            styleGeneration != m_styleUpdateGeneration ||
            interactionGeneration != m_scrollInteractionGeneration) {
            return;
        }
        if (m_hasNavigationTarget) {
            restoreNavigationTarget();
        } else if (m_hasPreservedScrollRatio) {
            restorePreservedScrollRatio();
        } else if (m_previewIsCurrent && m_syncButton->isChecked()) {
            emit previewScrollRangeChanged();
        }
    });
}

bool MarkdownPreviewDock::applyDocumentStyle(QTextEdit *textEdit)
{
    if (!textEdit || !textEdit->document()) {
        return false;
    }
    QFont bodyFont = m_bodyFont;
    bodyFont.setPointSizeF(m_bodyFont.pointSizeF() * m_zoom);
    textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    const QPalette colors = palette();
    const QString nativeCodeFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    QTextDocument *document = textEdit->document();
    // QTextDocument::clear can retain the root frame's display-only margin.
    // Remove it when a newly imported first block has no attached header.
    auto rootFormat = document->rootFrame()->frameFormat();
    if (rootFormat.hasProperty(CodeBlockTools::RootOriginalMargin) &&
        !document->begin().blockFormat().hasProperty(CodeBlockTools::HeaderOriginalMargin)) {
        rootFormat.setTopMargin(rootFormat.property(CodeBlockTools::RootOriginalMargin).toDouble());
        rootFormat.clearProperty(CodeBlockTools::RootOriginalMargin);
        document->rootFrame()->setFrameFormat(rootFormat);
    }
    if (textEdit->palette() == colors &&
        textEdit->font().pointSizeF() == bodyFont.pointSizeF() &&
        document->defaultFont() == bodyFont &&
        document->property(kStyleRevision).toULongLong() == m_styleRevision &&
        document->property(kDocumentRevision).toInt() == document->revision()) {
        return false;
    }
    if (textEdit->palette() != colors) {
        textEdit->setPalette(colors);
    }

    // Batch formatting so QTextDocument does not lay out the entire document
    // after each fragment. Preserve the user's cursor/selection and all semantic
    // properties; use absolute sizes, never multiply previously styled values.
    QTextCursor edit(document);
    edit.beginEditBlock();
    // QTextEdit's native zoom starts from QWidget::font(), not the document
    // default. Keep both at the displayed size, including after a zoom reset.
    if (textEdit->font().pointSizeF() != bodyFont.pointSizeF()) {
        textEdit->setFont(bodyFont);
    }
    document->setDefaultFont(bodyFont);
    document->setDefaultStyleSheet(loadStyleSheet());
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat blockFormat = block.blockFormat();
        const int headingLevel = blockFormat.headingLevel();
        const int quoteLevel = blockFormat.property(QTextFormat::BlockQuoteLevel).toInt();
        const bool codeBlock = blockFormat.hasProperty(QTextFormat::BlockCodeFence) ||
            blockFormat.hasProperty(QTextFormat::BlockCodeLanguage);
        const qreal points = bodyFont.pointSizeF() * headingRatio(headingLevel);
        if (headingLevel > 0) {
            blockFormat.setTopMargin(12.0);
            blockFormat.setBottomMargin(6.0);
        }
        if (quoteLevel > 0) {
            blockFormat.setLeftMargin(12.0 * quoteLevel);
            blockFormat.setBackground(colors.brush(QPalette::AlternateBase));
        }
        if (headingLevel > 0) {
            // Recompute from semantic levels, so refresh/theme/zoom never
            // accumulate indentation. Keep the surrounding quote's margin.
            blockFormat.setLeftMargin(12.0 * qMax(0, quoteLevel) +
                12.0 * (headingLevel - 1) * m_zoom);
        }
        if (codeBlock) {
            blockFormat.setNonBreakableLines(!m_wrapCode);
            blockFormat.setBackground(colors.brush(QPalette::AlternateBase));
            blockFormat.setLeftMargin(10.0);
            blockFormat.setRightMargin(10.0);
            blockFormat.setTopMargin(6.0 + (blockFormat.hasProperty(CodeBlockTools::HeaderOriginalMargin)
                ? CodeBlockTools::HeaderHeight : 0));
            blockFormat.setBottomMargin(6.0);
        }
        QTextCursor blockCursor(block);
        if (block.blockFormat() != blockFormat) {
            blockCursor.setBlockFormat(blockFormat);
        }
        auto styledFormat = [&](QTextCharFormat format) {
            // Qt 5.15.2 imports code with setFont(systemFont(FixedFont)); on
            // Windows that QFont can have fixedPitch()==false. Its explicit
            // font properties still distinguish it from ordinary Markdown text.
            // Remember that distinction before normalizing the font family.
            const bool code = codeBlock || (format.hasProperty(kCodeFormatProperty)
                ? format.boolProperty(kCodeFormatProperty)
                : format.fontFixedPitch() ||
                    (format.hasProperty(QTextFormat::FontFixedPitch) &&
                     format.hasProperty(QTextFormat::FontStyleHint) &&
                     (format.fontFamily() == nativeCodeFamily ||
                      format.fontFamilies().toStringList().contains(nativeCodeFamily))));
            format.setProperty(kCodeFormatProperty, code);
            format.setFontFamily(code ? m_codeFontFamily : bodyFont.family());
            // setFont() can also store FontFamilies/StyleName. They must not
            // override our selected family or Markdown's bold/italic semantics.
            format.clearProperty(QTextFormat::FontFamilies);
            format.clearProperty(QTextFormat::FontStyleName);
            format.clearProperty(QTextFormat::FontSizeAdjustment);
            format.clearProperty(QTextFormat::FontPixelSize);
            format.setFontPointSize(points);
            format.setForeground(colors.brush(QPalette::Text));
            if (headingLevel > 0) {
                format.setFontWeight(QFont::Bold);
            }
            if (code) {
                format.setFontFixedPitch(true);
                format.setBackground(colors.brush(QPalette::AlternateBase));
            }
            if (format.isAnchor()) {
                format.setForeground(colors.brush(QPalette::Link));
                format.setFontUnderline(true);
            }
            return format;
        };
        const QTextCharFormat blockChars = styledFormat(block.charFormat());
        if (block.charFormat() != blockChars) {
            blockCursor.setBlockCharFormat(blockChars);
        }
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) {
                continue;
            }
            const QTextCharFormat format = styledFormat(fragment.charFormat());
            if (format != fragment.charFormat()) {
                QTextCursor cursor(document);
                cursor.setPosition(fragment.position());
                cursor.setPosition(fragment.position() + fragment.length(),
                                   QTextCursor::KeepAnchor);
                cursor.setCharFormat(format);
            }
        }
    }
    applyFrameStyle(document->rootFrame(), colors);
    edit.endEditBlock();
    document->setProperty(kStyleRevision, QVariant::fromValue(m_styleRevision));
    document->setProperty(kDocumentRevision, document->revision());
    textEdit->viewport()->update();
    return true;
}

bool MarkdownPreviewDock::handleNativeWheel(QObject *watched, QEvent *event)
{
    if (!m_nativeTextEdit || watched != m_nativeTextEdit->viewport() ||
        event->type() != QEvent::Wheel || !m_nativeTextEdit->isReadOnly()) {
        return false;
    }
    auto *wheel = static_cast<QWheelEvent *>(event);
    if (!(wheel->modifiers() & Qt::ControlModifier)) {
        return false;
    }
    const QPointer<QTextEdit> textEdit = m_nativeTextEdit;
    const QPointer<QWidget> editor = m_previewEditor;
    const quint64 version = m_previewContentVersion;
    const quint64 styleRevision = m_styleRevision;
    const double ratio = barRatio(textEdit->verticalScrollBar());
    // Deliver this very event once to QTextEdit. Its native implementation owns
    // angle/pixel deltas, fractional steps and limits. Read the document default
    // font afterwards, which also works for empty and heading-only documents.
    {
        QScopedValueRollback<bool> handling(m_handlingNativeWheel, true);
        QCoreApplication::sendEvent(watched, event);
    }
    if (!textEdit || textEdit != m_nativeTextEdit || editor != m_previewEditor ||
        version != m_previewContentVersion || styleRevision != m_styleRevision) {
        return true;
    }
    const qreal points = textEdit->document()->defaultFont().pointSizeF();
    const qreal zoom = points / m_bodyFont.pointSizeF();
    if (qIsFinite(zoom) && zoom > 0.0 && qIsFinite(points * 2.0) && m_zoom != zoom) {
        m_zoom = zoom;
        ++m_styleRevision;
        emit nativeZoomChanged(zoom);
        restyleNativePreview(ratio);
    }
    return true;
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
    if (!m_updatingChrome && event && (event->type() == QEvent::PaletteChange ||
                  event->type() == QEvent::ApplicationPaletteChange ||
                  event->type() == QEvent::StyleChange)) {
        scheduleThemeStyleRefresh();
    }
}

void MarkdownPreviewDock::scrollToRatio(double ratio)
{
    if (m_hasNavigationTarget) {
        return;
    }
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
    disconnect(m_headingScrollConnection);
    m_layoutSyncTimer->stop();
    if (m_nativeScrollConnection) {
        disconnect(m_nativeScrollConnection);
    }
    if (m_nativeScrollRangeConnection) {
        disconnect(m_nativeScrollRangeConnection);
    }
    disconnect(m_nativeHorizontalActionConnection);
    m_nativeHorizontalActionConnection = QMetaObject::Connection();
    m_nativeScrollConnection = QMetaObject::Connection();
    m_nativeScrollRangeConnection = QMetaObject::Connection();

    if (!scrollBar) {
        return;
    }

    if (m_nativeTextEdit) {
        m_nativeHorizontalActionConnection = connect(
            m_nativeTextEdit->horizontalScrollBar(), &QAbstractSlider::actionTriggered,
            this, [this](int) { cancelPreservedScroll(); });
    }

    m_headingScrollConnection = connect(scrollBar, &QAbstractSlider::valueChanged,
        this, [this]() { m_headingTimer->start(); });
    m_nativeScrollConnection = connect(
        scrollBar, &QAbstractSlider::actionTriggered, this,
        [this, scrollBar](int) {
            cancelPreservedScroll();
            const QPointer<QScrollBar> guardedScrollBar(scrollBar);
            const QPointer<QWidget> editor = m_previewEditor;
            const quint64 version = m_previewContentVersion;
            const quint64 interaction = m_scrollInteractionGeneration;
            // actionTriggered precedes value propagation. Capture the intended
            // slider position now; a queued source/layout update must not turn
            // this user action into a read of an older programmatic value.
            const int range = scrollBar->maximum() - scrollBar->minimum();
            const double ratio = range > 0
                ? double(scrollBar->sliderPosition() - scrollBar->minimum()) / range : 0.0;
            QTimer::singleShot(0, this, [this, guardedScrollBar, editor, version,
                                         interaction, ratio]() {
                if (!guardedScrollBar || interaction != m_scrollInteractionGeneration ||
                    !hasDisplayedPreviewFor(editor, version)) {
                    return;
                }
                scrollToRatio(ratio);
                emitPreviewScrollRatio(guardedScrollBar.data());
            });
        });

    // QTextDocumentLayout lays out large documents incrementally.  QTextEdit
    // adjusts its scrollbar range while that work advances, so reapply the
    // editor ratio after each coalesced range update.
    m_nativeScrollRangeConnection = connect(
        scrollBar, &QAbstractSlider::rangeChanged, this,
        [this](int, int) {
            if (isVisible() && m_syncButton &&
                (m_syncButton->isChecked() || m_hasPreservedScrollRatio || m_hasNavigationTarget)) {
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
        m_previewContentVersion != m_preservedScrollVersion ||
        m_preservedStyleGeneration != m_styleUpdateGeneration ||
        m_preservedInteractionGeneration != m_scrollInteractionGeneration) {
        return;
    }

    QScrollBar *bar = m_nativeTextEdit->verticalScrollBar();
    if (!bar || bar->maximum() <= bar->minimum()) {
        return;
    }

    const int value = bar->minimum() + qRound(
        m_preservedScrollRatio *
        static_cast<double>(bar->maximum() - bar->minimum()));
    bar->setValue(value);
}

void MarkdownPreviewDock::cancelPreservedScroll()
{
    releaseNavigationTarget();
    ++m_scrollInteractionGeneration;
    m_preservedScrollEditor = nullptr;
    m_preservedScrollVersion = 0;
    m_hasPreservedScrollRatio = false;
    if (m_layoutSyncTimer && m_syncButton && !m_syncButton->isChecked()) {
        m_layoutSyncTimer->stop();
    }
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
    if (m_search && m_search->handleKey(
            watched == m_browser || watched == m_browser->viewport() ? m_search : watched, event)) {
        return true;
    }
    if (event && m_splitter && watched == m_splitter->handle(1) &&
        event->type() == QEvent::MouseButtonPress) {
        preserveLayoutTarget();
    }
    if (event && m_nativeTextEdit && watched == m_nativeTextEdit->viewport() &&
        event->type() == QEvent::Resize) {
        m_layoutSyncTimer->start();
        m_headingTimer->start();
    }
    if (m_handlingNativeWheel) {
        return QDockWidget::eventFilter(watched, event);
    }
    if (!m_updatingChrome && event && (event->type() == QEvent::PaletteChange ||
                  event->type() == QEvent::ApplicationPaletteChange ||
                  event->type() == QEvent::StyleChange)) {
        scheduleThemeStyleRefresh();
    }
    if (event && m_nativeTextEdit &&
        (watched == m_nativeTextEdit || watched == m_nativeTextEdit->viewport() ||
         watched == m_nativeTextEdit->verticalScrollBar() ||
         watched == m_nativeTextEdit->horizontalScrollBar()) &&
        (event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::Wheel || event->type() == QEvent::KeyPress)) {
        cancelPreservedScroll();
    }
    if (event && handleNativeWheel(watched, event)) {
        return true;
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
    Diagnostics::write(this,
        QStringLiteral("link open rejected or failed: %1 (%2)")
            .arg(url.toDisplayString(), reason));
    if (m_feedbackLabel) {
        m_feedbackLabel->setText(tr("链接未打开：%1").arg(reason));
        m_feedbackLabel->setToolTip(url.toDisplayString());
        m_feedbackLabel->show();
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

QVector<HeadingRecord> MarkdownPreviewDock::headings() const
{
    return m_outline->headings();
}

void MarkdownPreviewDock::setHeadingSnapshot(const QVector<HeadingRecord> &headings,
                                             QWidget *editor, quint64 version)
{
    if (!hasDisplayedPreviewFor(editor, version)) {
        return;
    }
    releaseNavigationTarget();
    m_outline->setSnapshot(headings, editor, version);
    m_headingTimer->start();
}

void MarkdownPreviewDock::setOutlineStatus(const PreviewStatus &status)
{
    m_outline->setFreshness(status.activeEditor &&
        status.activeEditor == status.displayedEditor && status.displayedVersion != 0 &&
        status.contentVersion != status.displayedVersion);
}

bool MarkdownPreviewDock::navigateHeading(const HeadingRecord &heading)
{
    if (!hasDisplayedPreviewFor(heading.editor, heading.version)) {
        return false;
    }
    bool found = false;
    for (const HeadingRecord &item : m_outline->headings()) {
        if (item.blockPosition == heading.blockPosition && item.editor == heading.editor &&
            item.version == heading.version && item.text == heading.text && item.level == heading.level) {
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }
    return navigatePreviewPosition(heading.editor, heading.version, heading.blockPosition);
}

bool MarkdownPreviewDock::navigatePreviewPosition(QWidget *editor, quint64 version, int position, int length)
{
    if (!hasDisplayedPreviewFor(editor, version) || position < 0 ||
        position >= m_nativeTextEdit->document()->characterCount() - 1 || length < 0 ||
        length > m_nativeTextEdit->document()->characterCount() - 1 - position) {
        return false;
    }
    cancelPreservedScroll();
    m_navigationTarget.editor = editor;
    m_navigationTarget.version = version;
    m_navigationTarget.blockPosition = position;
    m_navigationLength = length;
    m_hasNavigationTarget = true;
    restoreNavigationTarget();
    m_layoutSyncTimer->start();
    return true;
}

void MarkdownPreviewDock::setCodeCopyValidator(std::function<bool(QWidget *, quint64)> validator)
{
    m_codeTools->setSnapshotValidator(std::move(validator));
}

void MarkdownPreviewDock::setCodeSnapshot(const QVector<CodeBlockRecord> &records,
                                          QWidget *editor, quint64 version)
{
    if (hasDisplayedPreviewFor(editor, version)) {
        m_search->setFormatting(true);
        m_codeTools->setSnapshot(m_nativeTextEdit, records, editor, version);
        // Header spacing is a known formatting-only change, not new Markdown.
        m_nativeTextEdit->document()->setProperty(kDocumentRevision, m_nativeTextEdit->document()->revision());
        m_search->setFormatting(false);
    }
    else
        m_codeTools->clearSnapshot();
}

void MarkdownPreviewDock::setSearchStatus(const PreviewStatus &status)
{
    m_codeTools->setStatus(status);
    if (status.activeEditor == status.displayedEditor &&
        hasDisplayedPreviewFor(status.displayedEditor, status.displayedVersion)) {
        m_search->setSnapshot(m_nativeTextEdit, status.displayedEditor, status.displayedVersion);
    } else {
        m_codeTools->clearSnapshot();
        m_search->setSnapshot(nullptr, nullptr, 0);
    }
    m_search->setStatus(status);
}

void MarkdownPreviewDock::releaseNavigationTarget()
{
    const bool hadTarget = m_hasNavigationTarget;
    m_hasNavigationTarget = false;
    m_navigationTarget = HeadingRecord();
    m_navigationLength = 0;
    if (hadTarget) {
        emit navigationTargetReleased();
    }
}

void MarkdownPreviewDock::restoreNavigationTarget()
{
    if (!m_hasNavigationTarget ||
        !hasDisplayedPreviewFor(m_navigationTarget.editor, m_navigationTarget.version)) {
        return;
    }
    QTextDocument *document = m_nativeTextEdit->document();
    if (m_navigationTarget.blockPosition < 0 ||
        m_navigationTarget.blockPosition >= document->characterCount()) {
        releaseNavigationTarget();
        return;
    }
    QTextCursor cursor(document);
    cursor.setPosition(m_navigationTarget.blockPosition);
    QScrollBar *bar = m_nativeTextEdit->verticalScrollBar();
    const int target = bar->value() + m_nativeTextEdit->cursorRect(cursor).top();
    bar->setValue(qBound(bar->minimum(), target, bar->maximum()));
    // Wide tables can place a search result outside the horizontal viewport.
    // Move only as far as needed and never replace the user's text selection.
    QRect targetRect = m_nativeTextEdit->cursorRect(cursor);
    if (m_navigationLength > 0 &&
        m_navigationLength <= document->characterCount() - 1 - cursor.position()) {
        cursor.setPosition(cursor.position() + m_navigationLength);
        targetRect = targetRect.united(m_nativeTextEdit->cursorRect(cursor));
    }
    const QRect viewportRect = m_nativeTextEdit->viewport()->rect();
    QScrollBar *horizontal = m_nativeTextEdit->horizontalScrollBar();
    if (targetRect.width() > viewportRect.width() || targetRect.left() < viewportRect.left()) {
        horizontal->setValue(horizontal->value() + targetRect.left() - viewportRect.left());
    } else if (targetRect.right() > viewportRect.right()) {
        horizontal->setValue(horizontal->value() + targetRect.right() - viewportRect.right());
    }
    m_headingTimer->start();
}

void MarkdownPreviewDock::updateCurrentHeading()
{
    if (!m_nativeTextEdit || !hasDisplayedPreviewFor(m_previewEditor, m_previewContentVersion)) {
        return;
    }
    QScrollBar *bar = m_nativeTextEdit->verticalScrollBar();
    int position = m_nativeTextEdit->cursorForPosition(QPoint(0, 0)).position();
    if (bar->maximum() > bar->minimum() && bar->value() == bar->maximum()) {
        position = m_nativeTextEdit->document()->characterCount() - 1;
    } else if (!m_outline->headings().isEmpty()) {
        // cursorForPosition() chooses the nearest block in a top margin. Do not
        // mark a heading which has not yet crossed the viewport top.
        const auto &records = m_outline->headings();
        auto next = std::upper_bound(records.cbegin(), records.cend(), position,
            [](int value, const HeadingRecord &record) { return value < record.blockPosition; });
        if (next != records.cbegin()) {
            const HeadingRecord &heading = *std::prev(next);
            QTextCursor cursor(m_nativeTextEdit->document());
            cursor.setPosition(heading.blockPosition);
            if (m_nativeTextEdit->cursorRect(cursor).top() > 0) {
                position = heading.blockPosition - 1;
            }
        }
    }
    m_outline->setCurrentBlock(position);
}

void MarkdownPreviewDock::preserveLayoutTarget()
{
    if (m_hasNavigationTarget || !hasDisplayedPreviewFor(m_previewEditor, m_previewContentVersion)) {
        return;
    }
    cancelPreservedScroll();
    m_navigationTarget.editor = m_previewEditor;
    m_navigationTarget.version = m_previewContentVersion;
    m_navigationTarget.blockPosition = m_nativeTextEdit->cursorForPosition(QPoint(0, 0)).position();
    m_hasNavigationTarget = true;
}

void MarkdownPreviewDock::setOutlineOnRight(bool right)
{
    if (m_outlineOnRight == right) {
        return;
    }
    preserveLayoutTarget();
    m_outlineOnRight = right;
    m_splitter->insertWidget(right ? 1 : 0, m_outline);
    m_splitter->setStretchFactor(right ? 0 : 1, 1);
    m_splitter->setStretchFactor(right ? 1 : 0, 0);
    const int previewWidth = qMax(100, m_splitter->width() - m_outlineWidth);
    m_splitter->setSizes(right ? QList<int>{previewWidth, m_outlineWidth}
                              : QList<int>{m_outlineWidth, previewWidth});
    m_splitter->handle(1)->installEventFilter(this);
    m_layoutSyncTimer->start();
}

void MarkdownPreviewDock::setNavigationFeedback(const QString &message)
{
    m_feedbackLabel->setText(message);
    m_feedbackLabel->setToolTip(message);
    m_feedbackLabel->setVisible(!message.isEmpty());
}
