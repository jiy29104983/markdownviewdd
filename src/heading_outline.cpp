#include "heading_outline.h"
#include "preview_ui.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int kHeadingLevelRole = Qt::UserRole + 1;

class OutlineItemDelegate final : public QStyledItemDelegate
{
public:
    explicit OutlineItemDelegate(QTreeWidget *tree)
        : QStyledItemDelegate(tree), m_tree(tree)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 26));
        return size;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const QPalette &pal = option.palette;
        const QModelIndex textIndex = index.sibling(index.row(), 0);
        const QModelIndex markerIndex = index.sibling(index.row(), 1);
        const QString marker = markerIndex.data(Qt::DisplayRole).toString();
        const bool isCurrent = (marker == QStringLiteral("●"));
        const bool isAncestor = (marker == QStringLiteral("◌"));
        const bool isSelected = (option.state & QStyle::State_Selected);
        const bool isHover = (option.state & QStyle::State_MouseOver);
        const int level = textIndex.data(kHeadingLevelRole).toInt();

        QColor rowBg = Qt::transparent;
        if (isSelected) {
            rowBg = PreviewUi::mix(pal.color(QPalette::Base),
                                   pal.color(QPalette::Highlight),
                                   PreviewUi::isDark(pal) ? 0.28 : 0.18);
        } else if (isCurrent) {
            rowBg = PreviewUi::mix(pal.color(QPalette::Base),
                                   pal.color(QPalette::Highlight),
                                   PreviewUi::isDark(pal) ? 0.18 : 0.11);
        } else if (isHover) {
            rowBg = PreviewUi::mix(pal.color(QPalette::Base),
                                   pal.color(QPalette::Text),
                                   PreviewUi::isDark(pal) ? 0.10 : 0.06);
        }

        if (rowBg.alpha() > 0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(rowBg);
            QRectF bgRect = option.rect.adjusted(
                index.column() == 0 ? 2 : -4, 1,
                index.column() == 0 ? 4 : -2, -1);
            painter->drawRoundedRect(bgRect, 4.0, 4.0);
        }

        if (index.column() == 0) {
            if (isCurrent || isAncestor) {
                QColor accent = pal.color(QPalette::Highlight);
                if (isAncestor) {
                    accent.setAlpha(150);
                }
                painter->setPen(Qt::NoPen);
                painter->setBrush(accent);
                const QRectF barRect(option.rect.left() + 2.0,
                                     option.rect.top() + 4.0,
                                     3.0,
                                     qMax(6, option.rect.height() - 8));
                painter->drawRoundedRect(barRect, 1.5, 1.5);
            }

            QFont font = option.font;
            if (isCurrent || level == 1) {
                font.setWeight(QFont::DemiBold);
            } else if (level == 2) {
                font.setWeight(QFont::Medium);
            }
            painter->setFont(font);

            QColor textColor = pal.color(QPalette::Text);
            if (isCurrent) {
                textColor = PreviewUi::mix(textColor, pal.color(QPalette::Highlight), 0.35);
            } else if (level >= 4 && !isSelected && !isHover) {
                textColor.setAlpha(195);
            }
            painter->setPen(textColor);

            const QRect textRect = option.rect.adjusted(isCurrent || isAncestor ? 8 : 6, 0, -4, 0);
            const QString elided = painter->fontMetrics().elidedText(
                textIndex.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width());
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, elided);

            if ((option.state & QStyle::State_HasFocus) && m_tree && m_tree->hasFocus()) {
                QPen focusPen(pal.color(QPalette::Highlight), 1.0, Qt::DotLine);
                painter->setPen(focusPen);
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(QRectF(option.rect.adjusted(2, 1, -1, -1)), 3.5, 3.5);
            }
        } else if (index.column() == 1) {
            const QPointF center(option.rect.center());
            const QColor accent = pal.color(QPalette::Highlight);
            if (isCurrent) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(accent);
                painter->drawEllipse(center, 3.5, 3.5);
            } else if (isAncestor) {
                QColor ring = accent;
                ring.setAlpha(185);
                painter->setPen(QPen(ring, 1.5));
                painter->setBrush(Qt::NoBrush);
                painter->drawEllipse(center, 3.2, 3.2);
            }
        }

        painter->restore();
    }

private:
    QTreeWidget *m_tree = nullptr;
};
}

HeadingOutline::HeadingOutline(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("NddMarkdownOutline"));
    setMinimumWidth(100);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(2, 2, 0, 2);
    headerRow->setSpacing(4);
    auto *title = new QLabel(tr("大纲"), this);
    title->setObjectName(QStringLiteral("NddMarkdownOutlineHeader"));
    QFont titleFont = title->font();
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    headerRow->addWidget(title, 1);

    m_expandCollapseButton = new QToolButton(this);
    m_expandCollapseButton->setObjectName(QStringLiteral("NddMarkdownOutlineExpandToggle"));
    PreviewUi::setup(m_expandCollapseButton, PreviewUi::Symbol::CollapseAll, tr("折叠全部章节"));
    m_expandCollapseButton->setMinimumSize(24, 24);
    m_expandCollapseButton->setFixedSize(24, 24);
    m_expandCollapseButton->setIconSize(QSize(14, 14));
    m_expandCollapseButton->hide();
    headerRow->addWidget(m_expandCollapseButton);
    layout->addLayout(headerRow);

    connect(m_expandCollapseButton, &QToolButton::clicked, this, [this]() {
        bool anyExpanded = false;
        for (QTreeWidgetItem *item : m_items) {
            if (item->childCount() > 0 && item->isExpanded()) {
                anyExpanded = true;
                break;
            }
        }
        {
            // Recompute the marker once after the batch instead of scanning
            // every heading for each individual expanded/collapsed signal.
            const QSignalBlocker changingExpansion(m_tree);
            for (QTreeWidgetItem *item : m_items) {
                if (item->childCount() > 0) {
                    item->setExpanded(!anyExpanded);
                }
            }
        }
        saveExpansion();
        updateMarker();
        updateExpandCollapseButton();
    });

    m_state = new QLabel(tr("尚无可用预览"), this);
    m_state->setObjectName(QStringLiteral("NddMarkdownOutlineStatus"));
    m_state->setTextFormat(Qt::PlainText);
    m_state->setWordWrap(true);
    m_state->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(m_state);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("NddMarkdownOutlineTree"));
    m_tree->setAccessibleName(tr("标题大纲"));
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setIndentation(12);
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setMouseTracking(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->resizeSection(1, 22);
    m_tree->setItemDelegate(new OutlineItemDelegate(m_tree));
    layout->addWidget(m_tree, 1);
    // Mouse clicks and keyboard activation share the same guarded navigation.
    const auto activate = [this](QTreeWidgetItem *item, int) {
        const int index = item->data(0, Qt::UserRole).toInt();
        if (index >= 0 && index < m_headings.size()) {
            const HeadingRecord target = m_headings.at(index);
            emit headingActivated(target);
        }
    };
    connect(m_tree, &QTreeWidget::itemClicked, this, activate);
    connect(m_tree, &QTreeWidget::itemActivated, this, activate);
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this]() {
        updateMarker();
        updateExpandCollapseButton();
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this]() {
        updateMarker();
        updateExpandCollapseButton();
    });
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
    m_currentHeading = -1;
    m_headings.clear();
    m_items.clear();
    m_expansionKeys.clear();
    m_tree->clear();
    m_state->setText(tr("尚无可用预览"));
    m_state->show();
    updateExpandCollapseButton();
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
        item->setData(0, kHeadingLevelRole, heading.level);
        m_items.append(item);
        parents.append(i);
        item->setExpanded(states.value(m_expansionKeys.at(i), true));
    }
    m_tree->setUpdatesEnabled(true);
    setFreshness(false);
    updateExpandCollapseButton();
}

void HeadingOutline::setFreshness(bool stale)
{
    if (!m_editor) {
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
        m_tree->viewport()->update();
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
    m_tree->viewport()->update();
}

void HeadingOutline::updateExpandCollapseButton()
{
    if (!m_expandCollapseButton) {
        return;
    }
    bool hasChildren = false;
    bool anyExpanded = false;
    for (QTreeWidgetItem *item : m_items) {
        if (item->childCount() > 0) {
            hasChildren = true;
            if (item->isExpanded()) {
                anyExpanded = true;
                break;
            }
        }
    }
    m_expandCollapseButton->setVisible(hasChildren);
    if (hasChildren) {
        const QString tip = anyExpanded ? tr("折叠全部章节") : tr("展开全部章节");
        m_expandCollapseButton->setIcon(
            PreviewUi::icon(anyExpanded ? PreviewUi::Symbol::CollapseAll
                                        : PreviewUi::Symbol::ExpandAll,
                            m_expandCollapseButton));
        m_expandCollapseButton->setToolTip(tip);
        m_expandCollapseButton->setAccessibleName(tip);
    }
}

void HeadingOutline::populatePopupMenu(QMenu *menu)
{
    if (!menu) {
        return;
    }
    menu->clear();
    if (!m_editor) {
        QAction *empty = menu->addAction(tr("尚无可用预览"));
        empty->setEnabled(false);
        return;
    }
    if (m_headings.isEmpty()) {
        QAction *empty = menu->addAction(tr("当前文档没有标题"));
        empty->setEnabled(false);
        return;
    }
    for (int i = 0; i < m_headings.size(); ++i) {
        const HeadingRecord &heading = m_headings.at(i);
        const QString indent(qMax(0, heading.level - 1) * 2, QLatin1Char(' '));
        QString title = heading.text.isEmpty() ? tr("（空标题）") : heading.text;
        title.replace(QLatin1Char('&'), QStringLiteral("&&"));
        QAction *action = menu->addAction(indent + title);
        action->setCheckable(true);
        action->setChecked(i == m_currentHeading);
        connect(action, &QAction::triggered, this, [this, heading]() {
            emit headingActivated(heading);
        });
    }
}
