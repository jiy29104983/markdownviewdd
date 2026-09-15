#pragma once

#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

class QTextDocument;

// Positions and identities belong only to one accepted display snapshot.
struct CodeBlockRecord
{
    QPointer<QWidget> editor;
    quint64 version = 0;
    int ordinal = 0;
    int position = -1; // Empty fences can have no rendered position in Qt 5.15.
    int endPosition = -1;
    QString language;
    QString text;
    QString error;
    bool reliable = false;
};

QVector<CodeBlockRecord> indexCodeBlocks(QTextDocument *document, QWidget *editor,
                                       quint64 version, const QString *source);
