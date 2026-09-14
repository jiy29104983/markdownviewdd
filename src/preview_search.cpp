#include "preview_search.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <limits>

namespace {
constexpr int kDebounceMs = 75;
constexpr int kChunkCharacters = 4096;
constexpr int kBatchCharacters = 65536;
constexpr int kHighlightLimit = 256;
constexpr int kSearchHighlightProperty = QTextFormat::UserProperty + 41;

QString firstLine(const QString &text)
{
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\n') || text.at(i) == QLatin1Char('\r') ||
            text.at(i) == QChar::LineSeparator || text.at(i) == QChar::ParagraphSeparator) {
            return text.left(i);
        }
    }
    return text;
}

// QLineEdit normalizes pasted newlines before textChanged. Intercept paste/drop
// before that normalization, including the context menu and primary selection.
class SearchQueryEdit final : public QLineEdit
{
public:
    explicit SearchQueryEdit(QWidget *parent) : QLineEdit(parent) {}
    std::function<void()> multiline;
    bool composing = false;
    void insertQuery(const QString &text)
    {
        const QString line = firstLine(text);
        insert(line);
        if (line != text && multiline) multiline();
    }
protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->matches(QKeySequence::Paste)) {
            insertQuery(QApplication::clipboard()->text());
            event->accept();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent *event) override
    {
        composing = false;
        QLineEdit::focusOutEvent(event);
    }
    void inputMethodEvent(QInputMethodEvent *event) override
    {
        composing = !event->preeditString().isEmpty();
        QLineEdit::inputMethodEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        QMenu menu(this);
        auto *undoAction = menu.addAction(tr("撤销"), this, &QLineEdit::undo);
        undoAction->setEnabled(isUndoAvailable());
        auto *redoAction = menu.addAction(tr("重做"), this, &QLineEdit::redo);
        redoAction->setEnabled(isRedoAvailable());
        menu.addSeparator();
        menu.addAction(tr("剪切"), this, &QLineEdit::cut)->setEnabled(hasSelectedText());
        menu.addAction(tr("复制"), this, &QLineEdit::copy)->setEnabled(hasSelectedText());
        menu.addAction(tr("粘贴"), this, [this]() { insertQuery(QApplication::clipboard()->text()); });
        menu.addAction(tr("全选"), this, &QLineEdit::selectAll);
        menu.exec(event->globalPos());
    }
    void dropEvent(QDropEvent *event) override
    {
        if (event->mimeData()->hasText()) {
            setCursorPosition(cursorPositionAt(event->pos()));
            insertQuery(event->mimeData()->text());
            event->acceptProposedAction();
            return;
        }
        QLineEdit::dropEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::MiddleButton && QApplication::clipboard()->supportsSelection()) {
            setCursorPosition(cursorPositionAt(event->pos()));
            insertQuery(QApplication::clipboard()->text(QClipboard::Selection));
            event->accept();
            return;
        }
        QLineEdit::mouseReleaseEvent(event);
    }
};
}

PreviewSearch::PreviewSearch(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("NddMarkdownSearch"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    auto *row = new QHBoxLayout;
    auto *query = new SearchQueryEdit(this);
    m_query = query;
    query->setObjectName(QStringLiteral("NddMarkdownSearchQuery"));
    query->setPlaceholderText(tr("在预览中查找"));
    query->setAccessibleName(tr("预览搜索词"));
    query->setMinimumWidth(40);
    query->multiline = [this]() { m_notice = tr("多行查询仅使用第一行"); updateUi(); };
    row->addWidget(query, 1);
    m_case = new QCheckBox(tr("区分大小写"), this);
    m_case->setObjectName(QStringLiteral("NddMarkdownSearchCase"));
    row->addWidget(m_case);
    layout->addLayout(row);
    auto *actions = new QHBoxLayout;
    m_count = new QLabel(QStringLiteral("0/0"), this);
    m_count->setObjectName(QStringLiteral("NddMarkdownSearchCount"));
    actions->addWidget(m_count);
    actions->addStretch();
    auto button = [this, actions](const QString &text, const QString &name) {
        auto *item = new QToolButton(this);
        item->setText(text);
        item->setObjectName(name);
        actions->addWidget(item);
        return item;
    };
    m_previous = button(tr("上一处"), QStringLiteral("NddMarkdownSearchPrevious"));
    m_next = button(tr("下一处"), QStringLiteral("NddMarkdownSearchNext"));
    auto *close = button(tr("关闭"), QStringLiteral("NddMarkdownSearchClose"));
    m_previous->setToolTip(tr("上一处（Shift+Enter）"));
    m_next->setToolTip(tr("下一处（Enter）"));
    close->setToolTip(tr("关闭搜索（Esc）"));
    layout->addLayout(actions);
    m_message = new QLabel(this);
    m_message->setObjectName(QStringLiteral("NddMarkdownSearchMessage"));
    m_message->setTextFormat(Qt::PlainText);
    m_message->setWordWrap(true);
    layout->addWidget(m_message);
    connect(query, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_notice.clear();
        const QString line = firstLine(text);
        if (line != text) {
            const QSignalBlocker blocker(m_query);
            m_query->setText(line);
            m_notice = tr("多行查询仅使用第一行");
        }
        restart(false);
    });
    connect(m_case, &QCheckBox::toggled, this, [this]() { m_notice.clear(); restart(false); });
    connect(m_previous, &QToolButton::clicked, this, [this]() { navigate(-1); });
    connect(m_next, &QToolButton::clicked, this, [this]() { navigate(1); });
    connect(close, &QToolButton::clicked, this, &PreviewSearch::closeSearch);
    for (auto *child : findChildren<QWidget *>()) child->installEventFilter(this);
    hide();
    updateUi();
}

PreviewSearch::~PreviewSearch()
{
    disconnect(m_contentConnection);
    disconnect(m_destroyConnection);
    disconnect(m_scrollConnection);
    stop();
}

bool PreviewSearch::usable() const
{
    return m_snapshotValid && m_view && m_document && m_view->document() == m_document &&
        m_editor && m_version != 0;
}

void PreviewSearch::setSnapshot(QTextEdit *view, QWidget *editor, quint64 version)
{
    QTextDocument *document = view ? view->document() : nullptr;
    if (!view && !m_view && !editor && !m_editor && version == 0 && m_version == 0) return;
    if (view == m_view && document == m_document && editor == m_editor && version == m_version &&
        m_snapshotValid && document && document->revision() == m_revision) return;
    const bool sameEditor = editor && editor == m_editor;
    stop();
    disconnect(m_contentConnection);
    disconnect(m_destroyConnection);
    disconnect(m_scrollConnection);
    m_view = view;
    m_document = document;
    m_editor = editor;
    m_version = version;
    m_revision = document ? document->revision() : -1;
    m_snapshotValid = view && editor && version != 0;
    if (document) {
        m_contentConnection = connect(document, &QTextDocument::contentsChange, this,
            [this](int, int removed, int added) {
                if (m_formatting || (removed == 0 && added == 0)) return;
                m_snapshotValid = false;
                // Do not ask QTextEdit to lay out or repaint while Qt is in the
                // middle of replacing its document. Invalidate synchronously,
                // then clear display selections after the mutation unwinds.
                stop(false);
                const quint64 generation = m_generation;
                QTimer::singleShot(0, this, [this, generation]() {
                    if (generation != m_generation) return;
                    clearHighlights();
                    updateUi();
                });
            });
        m_destroyConnection = connect(document, &QObject::destroyed, this, [this]() {
            m_snapshotValid = false;
            // Unlike contentsChange, destruction must clear ExtraSelections
            // synchronously: Qt 5.15 setDocument() retains those cursors, and
            // setExtraSelections() later dereferences their dead document.
            // QObject::destroyed runs before the document's private data dies.
            stop();
            updateUi();
        });
        m_scrollConnection = connect(view->verticalScrollBar(), &QScrollBar::valueChanged,
                                    this, &PreviewSearch::updateHighlights);
    }
    restart(sameEditor);
}

void PreviewSearch::setStatus(const PreviewStatus &status)
{
    m_status = status;
    updateUi();
}

void PreviewSearch::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    if (active) restart(true);
    else { stop(); updateUi(); }
}

void PreviewSearch::openSearch()
{
    if (!m_open) {
        m_open = true;
        show();
        restart(false);
    }
    m_query->setFocus(Qt::ShortcutFocusReason);
    m_query->selectAll();
}

void PreviewSearch::closeSearch()
{
    m_open = false;
    stop();
    {
        const QSignalBlocker queryBlocker(m_query);
        const QSignalBlocker caseBlocker(m_case);
        m_query->clear();
        m_case->setChecked(false);
    }
    m_anchorContext.clear();
    m_anchorPosition = -1;
    m_matches.squeeze();
    m_prefix.clear();
    m_pattern.clear();
    m_notice.clear();
    hide();
    if (m_view && m_view->isVisible()) m_view->setFocus(Qt::ShortcutFocusReason);
    updateUi();
    emit closed();
}

bool PreviewSearch::handleKey(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    const bool inSearch = widget && (widget == this || isAncestorOf(widget));
    const bool inPreview = m_view && (widget == m_view || widget == m_view->viewport());
    if (!m_active || (!inSearch && !inPreview) || !event ||
        (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress)) return false;
    auto *key = static_cast<QKeyEvent *>(event);
    if (static_cast<SearchQueryEdit *>(m_query)->composing) return false;
    const bool find = key->matches(QKeySequence::Find);
    const bool enter = m_open && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
        (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::ShiftModifier);
    const bool escape = m_open && key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier;
    if (!find && !enter && !escape) return false;
    key->accept();
    if (event->type() == QEvent::KeyPress) {
        if (find) openSearch();
        else if (escape) closeSearch();
        else navigate(key->modifiers() == Qt::ShiftModifier ? -1 : 1);
    }
    return true;
}

bool PreviewSearch::eventFilter(QObject *watched, QEvent *event)
{
    if (handleKey(watched, event)) return true;
    return QWidget::eventFilter(watched, event);
}

void PreviewSearch::stop(bool repaint)
{
    ++m_generation;
    m_searching = false;
    m_matches.clear();
    m_current = -1;
    m_block = QTextBlock();
    if (repaint) clearHighlights();
}

void PreviewSearch::restart(bool keepAnchor)
{
    stop();
    if (!keepAnchor) { m_anchorContext.clear(); m_anchorPosition = -1; }
    m_pattern = m_query->text();
    if (!m_case->isChecked()) {
        for (QChar &ch : m_pattern) ch = ch.toCaseFolded();
    }
    if (!m_open || !m_active || !usable() || m_pattern.isEmpty()) { updateUi(); return; }
    m_prefix.fill(0, m_pattern.size());
    for (int i = 1, matched = 0; i < m_pattern.size(); ++i) {
        while (matched > 0 && m_pattern.at(i) != m_pattern.at(matched)) matched = m_prefix.at(matched - 1);
        if (m_pattern.at(i) == m_pattern.at(matched)) ++matched;
        m_prefix[i] = matched;
    }
    m_revision = m_document->revision();
    m_block = m_document->begin();
    m_offset = 0;
    m_matched = 0;
    m_bestAnchor = -1;
    m_bestDistance = std::numeric_limits<int>::max();
    m_searching = true;
    m_maxBatchNs = 0;
    m_elapsed.start();
    m_paintElapsed.start();
    updateUi();
    const quint64 generation = m_generation;
    QTimer::singleShot(kDebounceMs, this, [this, generation]() { scan(generation); });
}

void PreviewSearch::scan(quint64 generation)
{
    if (generation != m_generation || !m_searching || !m_active || !usable()) return;
    if (m_document->revision() != m_revision) { stop(); updateUi(); return; }
    QElapsedTimer batch;
    batch.start();
    int processed = 0;
    while (m_block.isValid() && processed < kBatchCharacters && batch.elapsed() < 4) {
        const int length = m_block.length() - 1;
        if (m_offset >= length) {
            m_block = m_block.next();
            m_offset = 0;
            m_matched = 0; // never cross paragraphs/table cells
            ++processed;
            continue;
        }
        const int start = m_block.position() + m_offset;
        const int count = qMin(kChunkCharacters, length - m_offset);
        QTextCursor cursor(m_document);
        cursor.setPosition(start);
        cursor.setPosition(start + count, QTextCursor::KeepAnchor);
        const QString text = cursor.selectedText();
        for (int i = 0; i < text.size(); ++i) {
            const QChar original = text.at(i);
            if (original == QChar::ObjectReplacementCharacter || original == QChar::LineSeparator) {
                m_matched = 0;
                continue;
            }
            const QChar ch = m_case->isChecked() ? original : original.toCaseFolded();
            while (m_matched > 0 && ch != m_pattern.at(m_matched)) m_matched = m_prefix.at(m_matched - 1);
            if (ch == m_pattern.at(m_matched)) ++m_matched;
            if (m_matched == m_pattern.size()) {
                const int position = start + i + 1 - m_pattern.size();
                m_matches.append(position);
                if (!m_anchorContext.isEmpty()) {
                    const int distance = qAbs(position - m_anchorPosition);
                    if (distance < m_bestDistance && contextAt(position) == m_anchorContext) {
                        m_bestAnchor = m_matches.size() - 1;
                        m_bestDistance = distance;
                    }
                }
                m_matched = 0; // non-overlapping matches
            }
        }
        m_offset += count;
        processed += count;
    }
    m_maxBatchNs = qMax(m_maxBatchNs, batch.nsecsElapsed());
    const bool complete = !m_block.isValid();
    if (complete) {
        m_searching = false;
        m_current = m_matches.isEmpty() ? -1 : (m_bestAnchor >= 0 ? m_bestAnchor : 0);
        rememberCurrent();
    } else if (!m_matches.isEmpty()) {
        m_current = 0;
    }
    if (complete || m_paintElapsed.elapsed() >= 40) {
        updateUi();
        updateHighlights();
        m_paintElapsed.restart();
    }
    if (complete) {
        m_lastSearchNs = m_elapsed.nsecsElapsed();
        emit searchFinished();
    } else {
        QTimer::singleShot(5, this, [this, generation]() { scan(generation); });
    }
}

int PreviewSearch::currentPosition() const
{
    return m_current >= 0 && m_current < m_matches.size() ? m_matches.at(m_current) : -1;
}

QString PreviewSearch::contextAt(int position) const
{
    if (!m_document) return QString();
    const QTextBlock block = m_document->findBlock(position);
    if (!block.isValid()) return QString();
    QTextCursor cursor(m_document);
    cursor.setPosition(qMax(block.position(), position - 24));
    cursor.setPosition(qMin(block.position() + block.length() - 1,
                           position + m_pattern.size() + 24), QTextCursor::KeepAnchor);
    return cursor.selectedText();
}

void PreviewSearch::rememberCurrent()
{
    m_anchorPosition = currentPosition();
    m_anchorContext = m_anchorPosition < 0 ? QString() : contextAt(m_anchorPosition);
}

void PreviewSearch::navigate(int direction)
{
    if (m_searching || !usable() || m_matches.isEmpty()) return;
    const int next = m_current + direction;
    const bool wrapped = next < 0 || next >= m_matches.size();
    m_current = (next + m_matches.size()) % m_matches.size();
    m_notice = wrapped ? (direction > 0 ? tr("已从末尾回到开头") : tr("已从开头回到末尾")) : QString();
    rememberCurrent();
    updateUi();
    emit navigateRequested(m_editor, m_version, currentPosition());
    updateHighlights();
}

void PreviewSearch::clearHighlights()
{
    if (!m_view) return;
    auto selections = m_view->extraSelections();
    const int oldSize = selections.size();
    for (auto it = selections.begin(); it != selections.end();) {
        if (it->format.boolProperty(kSearchHighlightProperty)) it = selections.erase(it);
        else ++it;
    }
    if (oldSize != selections.size()) m_view->setExtraSelections(selections);
}

void PreviewSearch::setFormatting(bool formatting)
{
    m_formatting = formatting;
    if (!formatting && usable()) {
        m_revision = m_document->revision();
        updateHighlights();
    }
}

void PreviewSearch::updateHighlights()
{
    if (!m_open || !m_active || !usable() || m_updatingHighlights) return;
    const QScopedValueRollback<bool> updating(m_updatingHighlights, true);
    auto selections = m_view->extraSelections();
    for (auto it = selections.begin(); it != selections.end();) {
        if (it->format.boolProperty(kSearchHighlightProperty)) it = selections.erase(it);
        else ++it;
    }
    const int top = m_view->cursorForPosition(QPoint(0, 0)).position();
    auto first = std::lower_bound(m_matches.cbegin(), m_matches.cend(), top);
    const int begin = qMax(0, int(first - m_matches.cbegin()) - 1);
    const int end = qMin(m_matches.size(), begin + kHighlightLimit - 1);
    const QPalette colors = m_view->palette();
    auto add = [this, &selections, &colors](int index, bool current) {
        QTextEdit::ExtraSelection item;
        item.cursor = QTextCursor(m_document);
        item.cursor.setPosition(m_matches.at(index));
        item.cursor.setPosition(m_matches.at(index) + m_pattern.size(), QTextCursor::KeepAnchor);
        item.format.setProperty(kSearchHighlightProperty, true);
        QColor background = colors.color(QPalette::Highlight);
        if (!current) background.setAlpha(65);
        item.format.setBackground(background);
        item.format.setForeground(colors.color(current ? QPalette::HighlightedText : QPalette::Text));
        selections.append(item);
    };
    for (int i = begin; i < end; ++i) if (i != m_current) add(i, false);
    if (m_current >= 0) add(m_current, true);
    m_view->setExtraSelections(selections);
}

void PreviewSearch::updateUi()
{
    if (!m_open) return;
    m_count->setText(m_searching ? tr("已找到 %1 · 计算中…").arg(m_matches.size())
        : QStringLiteral("%1/%2").arg(m_current + 1).arg(m_matches.size()));
    m_previous->setEnabled(!m_searching && usable() && !m_matches.isEmpty());
    m_next->setEnabled(m_previous->isEnabled());
    QStringList messages;
    if (!usable()) messages.append(tr("没有可搜索的预览，请先刷新"));
    else if (m_status.activeEditor == m_editor && m_status.displayedEditor == m_editor &&
             m_status.displayedVersion == m_version && m_status.contentVersion != m_version)
        messages.append(tr("待刷新 · 搜索当前展示的旧快照"));
    if (m_matches.size() > kHighlightLimit)
        messages.append(tr("完整计数；仅高亮阅读位置附近的结果和当前项（最多 %1 项）").arg(kHighlightLimit));
    if (!m_notice.isEmpty()) messages.append(m_notice);
    m_message->setText(messages.join(QStringLiteral(" · ")));
    m_message->setVisible(!messages.isEmpty());
}
