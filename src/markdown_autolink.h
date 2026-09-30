#pragma once

#include <QRegularExpression>
#include <QString>

// CommonMark 6.5 URI/email autolinks are inline text, even at the start of a
// line. Keep actual HTML on the source indexers' conservative fallback path.
inline bool startsWithMarkdownAutolink(const QString &line)
{
    if (!line.startsWith(QLatin1Char('<')))
        return false;
    static const QRegularExpression autolink(QStringLiteral(
        "^<(?:[A-Za-z][A-Za-z0-9+.-]{1,31}:[^<>\\x00-\\x20\\x7f]*|"
        "[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@"
        "[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
        "(?:\\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*)>"));
    return autolink.match(line).hasMatch();
}
