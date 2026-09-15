#include "heading_outline.h"

#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

HeadingOutline::HeadingOutline(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("NddMarkdownOutline"));
    setMinimumWidth(100);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto *title = new QLabel(tr("大纲"), this);
    layout->addWidget(title);
    m_state = new QLabel(tr("尚无可用预览"), this);
    m_state->setObjectName(QStringLiteral("NddMarkdownOutlineStatus"));
    m_state->setTextFormat(Qt::PlainText);
    m_state->setWordWrap(true);
    layout->addWidget(m_state);
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("NddMarkdownOutlineTree"));
    m_tree->setAccessibleName(tr("标题大纲"));
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setIndentation(14);
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->resizeSection(1, 26);
    layout->addWidget(m_tree, 1);
    // itemActivated may also be emitted by a mouse double-click. Mouse clicks
    // activate here; keyboard Enter is handled by the tree's activation signal.
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        const int index = item->data(0, Qt::UserRole).toInt();
        if (index >= 0 && index < m_headings.size()) {
            const HeadingRecord target = m_headings.at(index);
            emit headingActivated(target);
        }
    });
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        const int index = item->data(0, Qt::UserRole).toInt();
        if (index >= 0 && index < m_headings.size()) {
            const HeadingRecord target = m_headings.at(index);
            emit headingActivated(target);
        }
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this]() { updateMarker(); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this]() { updateMarker(); });
}

void HeadingOutline::saveExpansion()
{
    if (!m_editor) {
        return;
    }
    QHash<QString, bool> states;
    for (int i = 0; i < m_items.size(); ++i) {
        states.insert(m_expansionKeys.at(i), m_items.at(i)->isExpanded());
    }
    m_expansion[m_editor.data()] = states;
}

void HeadingOutline::clearSnapshot()
{
    saveExpansion();
    m_editor = nullptr;
    m_version = 0;
    m_currentHeading = -1;
    m_headings.clear();
    m_items.clear();
    m_expansionKeys.clear();
    m_tree->clear();
    m_state->setText(tr("尚无可用预览"));
    m_state->show();
}

void HeadingOutline::setSnapshot(const QVector<HeadingRecord> &headings,
                                 QWidget *editor, quint64 version)
{
    clearSnapshot();
    if (!editor || version == 0) {
        return;
    }
    if (!m_expansion.contains(editor)) {
        m_expansion.insert(editor, {});
        connect(editor, &QObject::destroyed, this, [this, editor]() {
            m_expansion.remove(editor);
            // QPointer has already cleared by destroyed().
            if (!m_editor) {
                clearSnapshot();
            }
        });
    }
    m_editor = editor;
    m_version = version;
    m_headings = headings;
    const auto states = m_expansion.value(editor);
    const QSignalBlocker rebuilding(m_tree);
    m_tree->setUpdatesEnabled(false);
    QVector<int> parents;
    QHash<QString, int> occurrences;
    for (int i = 0; i < headings.size(); ++i) {
        const HeadingRecord &heading = headings.at(i);
        const QString key = QString::number(heading.level) + QLatin1Char(':') + heading.text;
        const int occurrence = occurrences.value(key, 0);
        occurrences[key] = occurrence + 1;
        m_expansionKeys.append(QString::number(occurrence) + QLatin1Char(':') + key);
        while (!parents.isEmpty() && headings.at(parents.last()).level >= heading.level) {
            parents.removeLast();
        }
        auto *item = parents.isEmpty() ? new QTreeWidgetItem(m_tree)
                                       : new QTreeWidgetItem(m_items.at(parents.last()));
        item->setText(0, heading.text.isEmpty() ? tr("（空标题）") : heading.text);
        item->setToolTip(0, heading.text);
        item->setData(0, Qt::UserRole, i);
        m_items.append(item);
        parents.append(i);
        item->setExpanded(states.value(m_expansionKeys.at(i), true));
    }
    m_tree->setUpdatesEnabled(true);
    setFreshness(false);
}

void HeadingOutline::setFreshness(bool stale)
{
    if (!m_editor || !m_version) {
        m_state->setText(tr("尚无可用预览"));
    } else if (stale) {
        m_state->setText(tr("基于当前预览，正文待刷新"));
    } else if (m_headings.isEmpty()) {
        m_state->setText(tr("当前文档没有标题"));
    } else {
        m_state->clear();
    }
    m_state->setVisible(!m_state->text().isEmpty());
}

void HeadingOutline::setCurrentBlock(int blockPosition)
{
    int current = -1;
    for (int i = 0; i < m_headings.size(); ++i) {
        if (m_headings.at(i).blockPosition > blockPosition) {
            break;
        }
        current = i;
    }
    if (m_currentHeading != current) {
        m_currentHeading = current;
        updateMarker();
    }
}

void HeadingOutline::updateMarker()
{
    // Never setCurrentItem(), scrollToItem(), setExpanded() or setFocus().
    for (QTreeWidgetItem *item : m_items) {
        item->setText(1, QString());
        item->setToolTip(1, QString());
    }
    if (m_currentHeading < 0 || m_currentHeading >= m_items.size()) {
        return;
    }
    QTreeWidgetItem *actual = m_items.at(m_currentHeading);
    QTreeWidgetItem *visible = actual;
    for (QTreeWidgetItem *parent = actual->parent(); parent; parent = parent->parent()) {
        if (!parent->isExpanded()) {
            visible = parent;
        }
    }
    visible->setText(1, visible == actual ? QStringLiteral("●") : QStringLiteral("◌"));
    visible->setToolTip(1, visible == actual ? tr("当前阅读章节") : tr("后代包含当前阅读章节"));
}
