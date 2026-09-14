#pragma once

#include "heading_index.h"
#include <QHash>
#include <QWidget>

class QLabel;
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

signals:
    void headingActivated(const HeadingRecord &heading);

private:
    void saveExpansion();
    void updateMarker();


    QTreeWidget *m_tree = nullptr;
    QLabel *m_state = nullptr;
    QVector<HeadingRecord> m_headings;
    QVector<QTreeWidgetItem *> m_items;
    QVector<QString> m_expansionKeys;
    QPointer<QWidget> m_editor;
    quint64 m_version = 0;
    int m_currentHeading = -1;
    QHash<QWidget *, QHash<QString, bool>> m_expansion;
};
