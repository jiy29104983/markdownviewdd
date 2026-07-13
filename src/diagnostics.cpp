#include "diagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QtGlobal>

namespace Diagnostics {

QString logFilePath()
{
    return QDir::temp().filePath(QStringLiteral("markdownview.log"));
}

void resetLog()
{
    QFile file(logFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << "Markdown Preview diagnostic log\n";
    stream << "time=" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << "\n";
    stream << "plugin=" << NDD_MARKDOWN_VIEW_VERSION << "\n";
    stream << "qt_compile=" << QT_VERSION_STR << "\n";
    stream << "qt_runtime=" << qVersion() << "\n";
    stream << "application=" << QCoreApplication::applicationFilePath() << "\n";
    stream.flush();
}

void write(const QString &message)
{
    QFile file(logFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        return;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << " | " << message << "\n";
    stream.flush();
}

} // namespace Diagnostics
