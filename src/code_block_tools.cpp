#include "code_block_tools.h"
#include "preview_ui.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QTextEdit>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextFrame>
#include <QTextLayout>
#include <QToolTip>
#include <QtMath>
#include <memory>

namespace {
class CopyButton final : public QToolButton
{
public:
    explicit CopyButton(QWidget *parent) : QToolButton(parent)
    {
        setFixedSize(24, 24);
        setAutoRaise(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
    }
    int feedback = 0;
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || hasFocus()) {
            QColor hover = palette().color(QPalette::Highlight);
            hover.setAlpha(28);
            painter.setPen(Qt::NoPen);
            painter.setBrush(hover);
            painter.drawRoundedRect(QRectF(0.5, 0.5, 23, 23), 4, 4);
        }
        PreviewUi::icon(feedback == 1 ? PreviewUi::Symbol::Success : feedback == -1
            ? PreviewUi::Symbol::Error : PreviewUi::Symbol::Copy, this).paint(
                &painter, QRect(4, 4, 16, 16), Qt::AlignCenter,
                isEnabled() ? QIcon::Normal : QIcon::Disabled);
        if (hasFocus()) {
            painter.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DotLine));
            painter.drawRoundedRect(QRectF(1, 1, 22, 22), 3, 3);
        }
    }
};
}

CodeBlockTools::CodeBlockTools(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("NddMarkdownCodeTools"));
    m_layoutTimer = new QTimer(this);
    m_layoutTimer->setSingleShot(true);
    m_layoutTimer->setInterval(16);
    connect(m_layoutTimer, &QTimer::timeout, this, &CodeBlockTools::updatePositions);
    m_feedbackTimer = new QTimer(this);
    m_feedbackTimer->setSingleShot(true);
    m_feedbackTimer->setInterval(2500);
    connect(m_feedbackTimer, &QTimer::timeout, this, [this]() {
        m_feedbackOrdinal = -1;
        updateSelection();
    });
    setClipboardWriter({});
    hide(); // Only the viewport children are visible; no global toolbar.
}

CodeBlockTools::~CodeBlockTools()
{
    clearSnapshot();
}

void CodeBlockTools::setClipboardWriter(ClipboardWriter writer)
{
    m_writer = writer ? std::move(writer) : ClipboardWriter([](const QString &text) {
        auto *clipboard = QApplication::clipboard();
        if (!clipboard)
            return false;
        clipboard->setText(text, QClipboard::Clipboard);
        QString actual = clipboard->text(QClipboard::Clipboard);
        actual.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        return actual == text;
    });
}

void CodeBlockTools::clearSnapshot()
{
    ++m_generation;
    m_feedbackTimer->stop();
    m_layoutTimer->stop();
    m_feedbackOrdinal = -1;
    for (const auto &header : m_headers) {
        if (header) {
            header->hide();
            header->setEnabled(false);
            header->deleteLater();
        }
    }
    m_headers.clear();
    m_buttons.clear();
    if (m_menu)
        m_menu->close();
    m_menu = nullptr;
    for (const auto &connection : m_connections)
        disconnect(connection);
    m_connections.clear();
    if (m_edit) {
        m_edit->viewport()->removeEventFilter(this);
        m_edit->removeEventFilter(this);
    }
    m_edit = nullptr;
    m_document = nullptr;
    m_editor = nullptr;
    m_version = 0;
    m_records.clear();
    hide();
}

void CodeBlockTools::setSnapshot(QTextEdit *edit, const QVector<CodeBlockRecord> &records,
                                 QWidget *editor, quint64 version)
{
    clearSnapshot();
    if (!edit || !editor || !version || records.isEmpty())
        return;
    for (const auto &record : records) {
        if (record.editor != editor || record.version != version)
            return;
    }
    m_edit = edit;
    m_document = edit->document();
    m_editor = editor;
    m_version = version;
    m_records = records;
    m_headers.resize(records.size());
    m_buttons.resize(records.size());
    m_formatting = true;
    QTextCursor formatting(m_document);
    formatting.beginEditBlock();
    for (int i = 0; i < records.size(); ++i) {
        const auto &record = records.at(i);
        if (record.position < 0)
            continue; // Qt does not render empty fences; context menu remains available.
        const QTextBlock block = m_document->findBlock(record.position);
        if (!block.isValid())
            continue;
        auto format = block.blockFormat();
        if (!format.hasProperty(HeaderOriginalMargin))
            format.setProperty(HeaderOriginalMargin, format.topMargin());
        format.setTopMargin(format.property(HeaderOriginalMargin).toDouble() + HeaderHeight);
        if (format != block.blockFormat())
            QTextCursor(block).setBlockFormat(format);
        if (record.position == 0) {
            auto root = m_document->rootFrame()->frameFormat();
            if (!root.hasProperty(RootOriginalMargin))
                root.setProperty(RootOriginalMargin, root.topMargin());
            root.setTopMargin(root.property(RootOriginalMargin).toDouble() + HeaderHeight);
            if (root != m_document->rootFrame()->frameFormat())
                m_document->rootFrame()->setFrameFormat(root);
        }
    }
    formatting.endEditBlock();
    m_formatting = false;
    edit->viewport()->installEventFilter(this);
    edit->installEventFilter(this);
    m_connections.append(connect(m_document, &QTextDocument::contentsChange, this,
        [this](int, int, int) { if (!m_formatting) clearSnapshot(); }));
    m_connections.append(connect(m_document, &QObject::destroyed, this, &CodeBlockTools::clearSnapshot));
    m_connections.append(connect(editor, &QObject::destroyed, this, &CodeBlockTools::clearSnapshot));
    m_connections.append(connect(edit, &QObject::destroyed, this, &CodeBlockTools::clearSnapshot));
    m_connections.append(connect(edit->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        // Remove old hit targets immediately; position the new frame in one batch.
        for (const auto &header : m_headers) {
            if (header)
                header->hide();
        }
        schedulePositions();
    }));
    m_connections.append(connect(edit->horizontalScrollBar(), &QScrollBar::valueChanged,
                                 this, &CodeBlockTools::schedulePositions));
    m_connections.append(connect(m_document->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
                                 this, &CodeBlockTools::schedulePositions));
    updateSelection();
    schedulePositions();
}

QString CodeBlockTools::snapshotHint() const
{
    return m_manual || m_stale ? tr("复制当前预览内容") : tr("复制完整代码，保留缩进和换行");
}

void CodeBlockTools::setStatus(const PreviewStatus &status)
{
    if (m_editor && (status.activeEditor != m_editor || status.displayedEditor != m_editor ||
                     status.displayedVersion != m_version)) {
        clearSnapshot();
        return;
    }
    const bool manual = status.mode == RefreshMode::Manual;
    const bool stale = status.contentVersion != status.displayedVersion;
    if (m_manual == manual && m_stale == stale)
        return;
    m_manual = manual;
    m_stale = stale;
    if (m_editor && !m_feedbackTimer->isActive())
        updateSelection();
}

void CodeBlockTools::createHeader(int i)
{
    const auto &record = m_records.at(i);
    const quint64 generation = m_generation;
    auto *header = new QWidget(m_edit->viewport());
    header->setObjectName(QStringLiteral("NddMarkdownCodeHeader"));
    header->setAutoFillBackground(true);
    header->setBackgroundRole(QPalette::AlternateBase);
    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(4, 0, 2, 0);
    const QString language = record.language.isEmpty() ? tr("文本") : record.language;
    auto *label = new QLabel(language, header);
    label->setObjectName(QStringLiteral("NddMarkdownCodeLanguage"));
    label->setTextFormat(Qt::PlainText);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    label->setToolTip(language);
    row->addWidget(label, 1);
    auto *button = new CopyButton(header);
    button->setObjectName(QStringLiteral("NddMarkdownCopyCode"));
    button->setProperty("codeBlockOrdinal", i);
    button->setAccessibleName(tr("复制代码块 %1 · %2").arg(i + 1).arg(language));
    row->addWidget(button);
    connect(button, &QToolButton::clicked, this, [this, i, generation]() {
        if (generation == m_generation)
            copyBlock(i);
    });
    header->hide();
    m_headers[i] = header;
    m_buttons[i] = button;
}

void CodeBlockTools::updateSelection()
{
    for (int i = 0; i < m_buttons.size(); ++i) {
        auto *button = static_cast<CopyButton *>(m_buttons.at(i).data());
        if (!button)
            continue;
        const auto &record = m_records.at(i);
        button->setEnabled(record.reliable);
        if (i == m_feedbackOrdinal) {
            button->feedback = m_feedbackValue;
            button->setToolTip(m_feedbackText);
            button->setAccessibleDescription(m_feedbackText);
        } else {
            button->feedback = 0;
            const QString hint = record.reliable ? snapshotHint() : record.error;
            button->setToolTip(hint);
            button->setAccessibleDescription(hint);
        }
        button->update();
    }
}

void CodeBlockTools::setFormatting(bool formatting)
{
    m_formatting = formatting;
    if (!formatting)
        schedulePositions();
}

void CodeBlockTools::schedulePositions()
{
    if (m_edit && m_edit->isVisible() && !m_layoutTimer->isActive())
        m_layoutTimer->start();
}

void CodeBlockTools::updatePositions()
{
    if (!m_edit || !m_document || !m_editor || !m_edit->isVisible() || m_edit->document() != m_document)
        return;
    // Do not force Qt's unfinished long-document layout from an overlay timer.
    const auto last = m_document->lastBlock();
    if (last.layout() && last.layout()->lineCount() == 0) {
        m_layoutTimer->start(50);
        return;
    }
    m_layoutTimer->setInterval(16);
    const int width = m_edit->viewport()->width();
    for (int i = 0; i < m_headers.size(); ++i) {
        if (m_records.at(i).position < 0)
            continue;
        auto *header = m_headers.at(i).data();
        QTextCursor cursor(m_document);
        cursor.setPosition(m_records.at(i).position);
        const QRect text = m_edit->cursorRect(cursor);
        const int top = text.top() - HeaderHeight;
        if (top < 0 || top >= m_edit->viewport()->height()) {
            if (header) {
                header->hide();
                header->setEnabled(false);
                header->deleteLater();
                m_headers[i] = nullptr;
                m_buttons[i] = nullptr;
            }
            continue;
        }
        if (!header) {
            createHeader(i);
            header = m_headers.at(i);
        }
        const int left = qMax(8, qRound(m_document->documentMargin() + cursor.blockFormat().leftMargin()));
        header->setGeometry(left, top, qMax(0, width - left - 12), HeaderHeight - 2);
        QPalette colors = m_edit->palette();
        header->setPalette(colors);
        if (auto *label = header->findChild<QLabel *>()) {
            QFont font = this->font();
            if (font.pointSizeF() > 0)
                font.setPointSizeF(qMax(8.0, font.pointSizeF() - 1.0));
            label->setFont(font);
            QColor muted = colors.color(QPalette::Text);
            muted.setAlpha(155);
            colors.setColor(QPalette::WindowText, muted);
            label->setPalette(colors);
        }
        header->setVisible(m_edit->isVisible() && top >= 0 && top < m_edit->viewport()->height());
        header->raise();
    }
    updateSelection();
}

QString CodeBlockTools::htmlForExport(const QTextDocument *document)
{
    bool decorated = document->rootFrame()->frameFormat().hasProperty(RootOriginalMargin);
    for (auto block = document->begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().hasProperty(HeaderOriginalMargin)) {
            decorated = true;
            break;
        }
    }
    if (!decorated)
        return document->toHtml("UTF-8");
    std::unique_ptr<QTextDocument> clean(document->clone());
    auto root = clean->rootFrame()->frameFormat();
    if (root.hasProperty(RootOriginalMargin)) {
        root.setTopMargin(root.property(RootOriginalMargin).toDouble());
        root.clearProperty(RootOriginalMargin);
        clean->rootFrame()->setFrameFormat(root);
    }
    QTextCursor edit(clean.get());
    edit.beginEditBlock();
    for (auto block = clean->begin(); block.isValid(); block = block.next()) {
        auto format = block.blockFormat();
        if (format.hasProperty(HeaderOriginalMargin)) {
            format.setTopMargin(format.property(HeaderOriginalMargin).toDouble());
            format.clearProperty(HeaderOriginalMargin);
            QTextCursor(block).setBlockFormat(format);
        }
    }
    edit.endEditBlock();
    return clean->toHtml("UTF-8");
}

bool CodeBlockTools::copyBlock(int ordinal)
{
    if (!m_edit || !m_document || !m_editor || m_edit->document() != m_document ||
        ordinal < 0 || ordinal >= m_records.size() || !m_records.at(ordinal).reliable)
        return false;
    const auto record = m_records.at(ordinal);
    if (record.editor != m_editor || record.version != m_version)
        return false;
    const quint64 generation = m_generation;
    const QPointer<CodeBlockTools> guard(this);
    const auto validator = m_validator;
    const auto writer = m_writer;
    if (validator && !validator(record.editor, record.version)) {
        if (guard)
            clearSnapshot();
        return false;
    }
    if (!guard || generation != m_generation)
        return false;
    const bool copied = writer(record.text);
    if (!guard || generation != m_generation)
        return copied;
    const QString feedback = copied
        ? ((m_manual || m_stale) ? tr("已复制当前预览内容") : tr("已复制完整代码块"))
        : tr("复制失败：剪贴板未接受代码内容。");
    m_feedbackOrdinal = ordinal;
    m_feedbackText = feedback;
    m_feedbackValue = copied ? 1 : -1;
    updateSelection();
    if (ordinal < m_buttons.size() && m_buttons.at(ordinal)) {
        auto *button = static_cast<CopyButton *>(m_buttons.at(ordinal).data());
        button->feedback = copied ? 1 : -1;
        button->setToolTip(feedback);
        button->setAccessibleDescription(feedback);
        button->update();
        if (!copied || m_manual || m_stale)
            QToolTip::showText(button->mapToGlobal(QPoint(0, button->height())), feedback, button);
    } else if (m_edit) {
        QToolTip::showText(m_edit->mapToGlobal(QPoint(12, 12)), feedback, m_edit);
    }
    m_feedbackTimer->start();
    return copied;
}

bool CodeBlockTools::eventFilter(QObject *watched, QEvent *event)
{
    if (m_edit && watched == m_edit && event->type() == QEvent::Hide) {
        m_layoutTimer->stop();
        for (const auto &header : m_headers) {
            if (header)
                header->hide();
        }
    }
    if (m_edit && (watched == m_edit || watched == m_edit->viewport()) &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show ||
         event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange))
        schedulePositions();
    if (m_edit && (watched == m_edit || watched == m_edit->viewport()) &&
        event->type() == QEvent::ContextMenu) {
        auto *context = static_cast<QContextMenuEvent *>(event);
        const int position = context->reason() == QContextMenuEvent::Keyboard
            ? m_edit->textCursor().position()
            : m_edit->cursorForPosition(m_edit->viewport()->mapFromGlobal(context->globalPos())).position();
        int ordinal = -1;
        for (int i = 0; i < m_records.size(); ++i) {
            if (m_records.at(i).position >= 0 && position >= m_records.at(i).position &&
                position < m_records.at(i).endPosition) {
                ordinal = i;
                break;
            }
        }
        bool hasEmpty = false;
        for (const auto &record : m_records)
            hasEmpty |= record.reliable && record.position < 0;
        if (ordinal < 0 && !hasEmpty)
            return false;
        // Parent the menu to this control; queued actions carry a generation,
        // so document replacement while the menu is open cannot copy a new block.
        auto *menu = m_edit->createStandardContextMenu();
        menu->setParent(this, menu->windowFlags());
        m_menu = menu;
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->addSeparator();
        const quint64 generation = m_generation;
        for (int i = 0; i < m_records.size(); ++i) {
            const auto &record = m_records.at(i);
            const bool empty = record.reliable && record.position < 0;
            if (i != ordinal && !empty)
                continue;
            const QString language = record.language.isEmpty() ? tr("文本") : record.language;
            auto *action = menu->addAction(empty ? tr("复制空代码块 %1 · %2").arg(i + 1).arg(language)
                                                 : tr("复制完整代码块 · %1").arg(language));
            action->setEnabled(record.reliable);
            action->setToolTip(record.reliable ? snapshotHint() : record.error);
            connect(action, &QAction::triggered, this, [this, i, generation]() {
                if (generation == m_generation)
                    copyBlock(i);
            });
        }
        menu->popup(context->reason() == QContextMenuEvent::Keyboard
            ? m_edit->viewport()->mapToGlobal(m_edit->cursorRect().bottomLeft()) : context->globalPos());
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
