#pragma once

#include <QString>

namespace Diagnostics {

QString logFilePath();
void resetLog();
void write(const QString &message);

} // namespace Diagnostics
