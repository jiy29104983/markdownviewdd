#include "diagnostics.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QTextStream>
#include <QVariant>
#include <QWidget>
#include <QtGlobal>

namespace {
constexpr qint64 kDefaultMaximumBytes = 1024 * 1024;
constexpr int kDefaultRetainedFiles = 3;
const char kWindowIdProperty[] = "markdownviewDiagnosticWindowId";

struct State {
    QMutex mutex;
    QString directory;
    qint64 maximumBytes = kDefaultMaximumBytes;
    int retainedFiles = kDefaultRetainedFiles;
    Diagnostics::Level minimumLevel = Diagnostics::Level::Info;
    bool initialized = false;
    quint64 nextWindowId = 1;
};

State &state()
{
    static State value;
    return value;
}

QString filePath(const State &value, int generation = 0)
{
    const QString name = QStringLiteral("markdownview-%1.log")
                             .arg(QCoreApplication::applicationPid());
    const QString base = QDir(value.directory.isEmpty()
                                  ? QDir::tempPath()
                                  : value.directory)
                             .filePath(name);
    return generation == 0 ? base : base + QStringLiteral(".%1").arg(generation);
}

void rotate(State &value)
{
    if (value.retainedFiles <= 0) {
        QFile::remove(filePath(value));
        return;
    }

    QFile::remove(filePath(value, value.retainedFiles));
    for (int generation = value.retainedFiles - 1; generation >= 1;
         --generation) {
        QFile::rename(filePath(value, generation),
                      filePath(value, generation + 1));
    }
    QFile::rename(filePath(value), filePath(value, 1));
}

QString levelName(Diagnostics::Level level)
{
    switch (level) {
    case Diagnostics::Level::Debug:
        return QStringLiteral("debug");
    case Diagnostics::Level::Info:
        return QStringLiteral("info");
    case Diagnostics::Level::Warning:
        return QStringLiteral("warning");
    case Diagnostics::Level::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("info");
}

QString windowId(State &value, const QObject *context)
{
    if (!context) {
        return QStringLiteral("process");
    }

    const QObject *candidate = context;
    if (const QWidget *widget = qobject_cast<const QWidget *>(context)) {
        candidate = widget->window();
    } else {
        const QObject *parent = context->parent();
        while (parent) {
            if (const QWidget *widget = qobject_cast<const QWidget *>(parent)) {
                candidate = widget->window();
                break;
            }
            parent = parent->parent();
        }
    }

    const QVariant existing = candidate->property(kWindowIdProperty);
    if (existing.isValid()) {
        return existing.toString();
    }
    const QString id = QStringLiteral("window-%1").arg(value.nextWindowId++);
    const_cast<QObject *>(candidate)->setProperty(kWindowIdProperty, id);
    return id;
}

void writeHeader(QFile &file)
{
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << "Markdown Preview diagnostic log\n";
    stream << "time=" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << "\n";
    stream << "process=" << QCoreApplication::applicationPid() << "\n";
    stream << "plugin=" << NDD_MARKDOWN_VIEW_VERSION << "\n";
    stream << "qt_compile=" << QT_VERSION_STR << "\n";
    stream << "qt_runtime=" << qVersion() << "\n";
    stream << "application="
           << QFileInfo(QCoreApplication::applicationFilePath()).fileName() << "\n";
    stream.flush();
}

bool ensureInitialized(State &value)
{
    if (value.initialized) {
        return true;
    }
    value.initialized = true;
    QDir().mkpath(value.directory.isEmpty() ? QDir::tempPath() : value.directory);
    if (QFileInfo::exists(filePath(value))) {
        rotate(value);
    }
    QFile file(filePath(value));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    writeHeader(file);
    return true;
}
} // namespace

namespace Diagnostics {

void initialize()
{
    State &value = state();
    const QMutexLocker locker(&value.mutex);
    ensureInitialized(value);
}

QString logFilePath()
{
    State &value = state();
    const QMutexLocker locker(&value.mutex);
    return filePath(value);
}

QString pathIdentity(const QString &path)
{
    if (path.isEmpty()) {
        return QStringLiteral("<empty>");
    }
    const QByteArray digest = QCryptographicHash::hash(
        QFileInfo(path).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256)
                                  .toHex()
                                  .left(12);
    return QStringLiteral("%1 [path-id:%2]")
        .arg(QFileInfo(path).fileName(), QString::fromLatin1(digest));
}

void write(const QObject *context, const QString &message, Level level)
{
    State &value = state();
    const QMutexLocker locker(&value.mutex);
    if (static_cast<int>(level) < static_cast<int>(value.minimumLevel)) {
        return;
    }
    if (!ensureInitialized(value)) {
        return;
    }

    const QString line = QStringLiteral("%1 | pid=%2 | %3 | %4 | %5\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                             .arg(QCoreApplication::applicationPid())
                             .arg(windowId(value, context), levelName(level), message);
    const QByteArray bytes = line.toUtf8();
    QFileInfo info(filePath(value));
    if (value.maximumBytes > 0 && info.exists() &&
        info.size() + bytes.size() > value.maximumBytes) {
        rotate(value);
        QFile header(filePath(value));
        if (!header.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return;
        }
        writeHeader(header);
    }

    QFile file(filePath(value));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        return;
    }
    file.write(bytes);
    file.flush();
}

void write(const QString &message, Level level)
{
    write(nullptr, message, level);
}

void configureForTesting(const QString &directory, qint64 maximumBytes,
                         int retainedFiles)
{
    State &value = state();
    const QMutexLocker locker(&value.mutex);
    value.directory = directory;
    value.maximumBytes = maximumBytes;
    value.retainedFiles = retainedFiles;
    value.initialized = false;
    value.nextWindowId = 1;
}

} // namespace Diagnostics
