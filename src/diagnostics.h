#pragma once

#include <QString>
#include <QtGlobal>

class QObject;

namespace Diagnostics {

enum class Level {
    Debug,
    Info,
    Warning,
    Error
};

void initialize();
QString logFilePath();
QString pathIdentity(const QString &path);
void write(const QObject *context, const QString &message,
           Level level = Level::Info);
void write(const QString &message, Level level = Level::Info);

// Intended for the isolated diagnostics test executable only.
void configureForTesting(const QString &directory, qint64 maximumBytes,
                         int retainedFiles);

} // namespace Diagnostics
