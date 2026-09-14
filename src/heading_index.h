#pragma once

#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

class QTextDocument;

struct HeadingRecord
{
    QPointer<QWidget> editor;
    quint64 version = 0;
    int level = 0;
    QString text;
    int blockPosition = -1;
    int sourceLine = -1;
    int sourceOffset = -1; // Unicode scalars, zero based.
};

QVector<HeadingRecord> renderedHeadings(QTextDocument *document,
                                       QWidget *editor, quint64 version);
// All source targets stay invalid unless the complete ordered heading sequence
// agrees with the rendered snapshot. Unsupported syntax fails closed.
bool mapHeadingSource(QVector<HeadingRecord> *headings, const QString &source);
