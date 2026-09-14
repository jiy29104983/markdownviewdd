#include "source_navigation.h"
#include <QJsonObject>
#include <QWidget>
extern "C" Q_DECL_EXPORT QJsonObject navigateSource(QWidget *widget, int line)
{
    const auto read = readAccessibleSource(widget);
    if (!read.available) return {{"ok", false}, {"error", read.error}};
    if (line < 0) return {{"ok", true}, {"source", read.text}};
    int offset = 0;
    int current = 0;
    int start = 0;
    while (current < line && start < read.text.size()) {
        const QChar ch = read.text.at(start++);
        if (ch == QLatin1Char('\r')) {
            if (start < read.text.size() && read.text.at(start) == QLatin1Char('\n')) ++start;
            ++current;
        } else if (ch == QLatin1Char('\n')) ++current;
    }
    if (current != line) return {{"ok", false}, {"error", "invalid line"}};
    offset = read.text.left(start).toUcs4().size();
    const auto result = navigateAccessibleSource(widget, line, offset, []() { return true; });
    return {{"ok", result.reached}, {"error", result.error}};
}
