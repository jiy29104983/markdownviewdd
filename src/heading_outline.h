#pragma once

#include "heading_index.h"
#include <QHash>
#include <QWidget>

class QLabel;
class QMenu;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

class HeadingOutline final : public QWidget
{
    Q_OBJECT
public:
    explicit HeadingOutline(QWidget *parent = nullptr);
    void setSnapshot(const QVector<HeadingRecord> &headings, QWidget *editor, quint64 version);
    void clearSnapshot();
    void setFreshness(bool stale);
    void setCurrentBlock(int blockPosition);
    const QVector<HeadingRecord> &headings() const { return m_headings; }
    int currentHeading() const { return m_currentHeading; }
    void populatePopupMenu(QMenu *menu);

signals:
    void headingActivated(const HeadingRecord &heading);

private:
    void saveExpansion();
    void updateMarker();
    void updateExpandCollapseButton();

    QTreeWidget *m_tree = nullptr;
    QLabel *m_state = nullptr;
    QToolButton *m_expandCollapseButton = nullptr;
    QVector<HeadingRecord> m_headings;
    QVector<QTreeWidgetItem *> m_items;
    QVector<QString> m_expansionKeys;
    QPointer<QWidget> m_editor;
    int m_currentHeading = -1;
    QHash<QWidget *, QHash<QString, bool>> m_expansion;
};
