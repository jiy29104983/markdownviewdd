#include "code_block_tools.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

CodeBlockTools::CodeBlockTools(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("NddMarkdownCodeTools"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(2);
    auto *row = new QHBoxLayout;
    layout->addLayout(row);
    m_blocks = new QComboBox(this);
    m_blocks->setObjectName(QStringLiteral("NddMarkdownCodeBlocks"));
    m_blocks->setAccessibleName(tr("代码块及语言"));
    m_blocks->setMinimumContentsLength(8);
    m_blocks->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    row->addWidget(m_blocks, 1);
    m_copy = new QToolButton(this);
    m_copy->setObjectName(QStringLiteral("NddMarkdownCopyCode"));
    m_copy->setText(tr("复制代码块"));
    m_copy->setAccessibleName(tr("复制所选完整代码块"));
    m_copy->setFocusPolicy(Qt::StrongFocus);
    row->addWidget(m_copy);
    m_feedback = new QLabel(this);
    m_feedback->setObjectName(QStringLiteral("NddMarkdownCodeFeedback"));
    m_feedback->setTextFormat(Qt::PlainText);
    m_feedback->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_feedback->setFixedHeight(fontMetrics().height() + 6);
    layout->addWidget(m_feedback);
    m_feedbackTimer = new QTimer(this);
    m_feedbackTimer->setSingleShot(true);
    m_feedbackTimer->setInterval(2500);
    connect(m_feedbackTimer, &QTimer::timeout, this, &CodeBlockTools::updateSelection);
    connect(m_copy, &QToolButton::clicked, this, [this]() { copyBlock(m_blocks->currentIndex()); });
    connect(m_blocks, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]() { m_feedbackTimer->stop(); updateSelection(); });
    connect(m_blocks, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (i >= 0 && i < m_records.size() && m_records.at(i).position >= 0) {
            const auto record = m_records.at(i);
            emit navigateRequested(record.editor, record.version, record.position, 0);
        }
    });
    setClipboardWriter({});
    hide();
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
    m_blocks->clear();
    m_copy->setEnabled(false);
    m_feedback->clear();
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
    const QSignalBlocker blocker(m_blocks);
    for (const auto &record : records) {
        const QString language = record.language.isEmpty() ? tr("文本") : record.language;
        m_blocks->addItem(tr("代码块 %1 · %2%3").arg(record.ordinal + 1).arg(language,
                         record.reliable && record.text.isEmpty() ? tr("（空）") : QString()));
        m_blocks->setItemData(m_blocks->count() - 1, language, Qt::ToolTipRole);
    }
    edit->viewport()->installEventFilter(this);
    edit->installEventFilter(this);
    m_connections.append(connect(m_document, &QTextDocument::contentsChange, this,
        [this](int, int, int) { if (!m_formatting) clearSnapshot(); }));
    m_connections.append(connect(m_document, &QObject::destroyed, this, &CodeBlockTools::clearSnapshot));
    m_connections.append(connect(editor, &QObject::destroyed, this, &CodeBlockTools::clearSnapshot));
    m_connections.append(connect(edit, &QObject::destroyed, this, &CodeBlockTools::clearSnapshot));
    updateSelection();
    show();
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

void CodeBlockTools::updateSelection()
{
    const int i = m_blocks->currentIndex();
    const bool valid = m_edit && m_document && m_editor && i >= 0 && i < m_records.size();
    const QString hint = valid && !m_records.at(i).reliable ? m_records.at(i).error : snapshotHint();
    m_copy->setEnabled(valid && m_records.at(i).reliable);
    m_copy->setToolTip(hint);
    m_feedback->setText(hint);
    m_feedback->setToolTip(hint);
    m_feedback->setAccessibleName(hint);
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
    m_feedback->setText(feedback);
    m_feedback->setToolTip(feedback);
    m_feedback->setAccessibleName(feedback);
    m_feedbackTimer->start();
    return copied;
}

bool CodeBlockTools::eventFilter(QObject *watched, QEvent *event)
{
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
        if (ordinal < 0)
            return false;
        m_blocks->setCurrentIndex(ordinal);
        // Parent the menu to this control; queued actions carry a generation,
        // so document replacement while the menu is open cannot copy a new block.
        auto *menu = m_edit->createStandardContextMenu();
        menu->setParent(this, menu->windowFlags());
        m_menu = menu;
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->addSeparator();
        const auto record = m_records.at(ordinal);
        const QString language = record.language.isEmpty() ? tr("文本") : record.language;
        auto *action = menu->addAction(tr("复制完整代码块 · %1").arg(language));
        action->setEnabled(record.reliable);
        action->setToolTip(record.reliable ? snapshotHint() : record.error);
        const quint64 generation = m_generation;
        connect(action, &QAction::triggered, this, [this, ordinal, generation]() {
            if (generation == m_generation)
                copyBlock(ordinal);
        });
        menu->popup(context->reason() == QContextMenuEvent::Keyboard
            ? m_edit->viewport()->mapToGlobal(m_edit->cursorRect().bottomLeft()) : context->globalPos());
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
