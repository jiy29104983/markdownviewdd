#include "code_block_index.h"

#include <QRegularExpression>
#include <QStringList>
#include <QTextBlock>
#include <QTextDocument>

namespace {
bool isCode(const QTextBlock &block)
{
    const auto format = block.blockFormat();
    return format.hasProperty(QTextFormat::BlockCodeLanguage) ||
        format.hasProperty(QTextFormat::BlockCodeFence);
}
struct Candidate
{
    QString language;
    QString text;
    QString renderedText;
    QChar fence;
};
QString renderedIndent(const QString &line, int column)
{
    QString result;
    int i = 0;
    while (i < line.size()) {
        if (line.at(i) == QLatin1Char(' ')) {
            result += QLatin1Char(' ');
            ++column;
        } else if (line.at(i) == QLatin1Char('\t')) {
            const int width = 4 - column % 4;
            result += QString(width, QLatin1Char(' '));
            column += width;
        } else {
            break;
        }
        ++i;
    }
    return result + line.mid(i);
}

int spaces(const QString &line)
{
    int n = 0;
    while (n < line.size() && line.at(n) == QLatin1Char(' '))
        ++n;
    return n;
}
// Remove structural indentation only. A tab straddling the boundary leaves
// spaces; tabs after that boundary are code and remain literal tabs.
bool removeIndent(QString *line, int columns)
{
    int consumed = 0;
    int width = 0;
    while (consumed < line->size() && width < columns) {
        const QChar ch = line->at(consumed);
        if (ch == QLatin1Char(' '))
            ++width;
        else if (ch == QLatin1Char('\t'))
            width += 4 - width % 4;
        else
            break;
        ++consumed;
    }
    if (width < columns && !line->trimmed().isEmpty())
        return false;
    line->remove(0, consumed);
    if (width > columns)
        line->prepend(QString(width - columns, QLatin1Char(' ')));
    return true;
}

QVector<Candidate> sourceBlocks(const QString &source, bool *supported)
{
    static const QRegularExpression quote(QStringLiteral("^ {0,3}>[ \\t]?"));
    static const QRegularExpression list(QStringLiteral("^ {0,3}(?:[-+*]|[0-9]{1,9}[.)])[ ]{1,4}"));
    static const QRegularExpression fence(QStringLiteral("^( {0,3})(`{3,}|~{3,})(.*)$"));
    QVector<Candidate> result;
    Candidate active;
    bool fenced = false;
    bool indented = false;
    bool paragraph = false;
    int fenceLength = 0;
    int fenceIndent = 0;
    int quoteDepth = 0;
    int listIndent = 0;
    int activeQuotes = 0;
    int activeList = 0;
    QString pendingBlank;
    QString pendingDisplayBlank;
    QString normalized = source;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const auto lines = normalized.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const int count = lines.size() - (normalized.endsWith(QLatin1Char('\n')) ? 1 : 0);
    for (int i = 0; i < count; ++i) {
        QString line = lines.at(i);
        const QString newline = i < lines.size() - 1 ? QStringLiteral("\n") : QString();
        int depth = 0;
        while (!fenced || depth < activeQuotes) {
            const auto match = quote.match(line);
            if (!match.hasMatch())
                break;
            line.remove(0, match.capturedLength());
            ++depth;
        }
        if (fenced) {
            QString content = line;
            if (depth == activeQuotes && removeIndent(&content, activeList)) {
                const auto match = fence.match(content);
                if (match.hasMatch() && match.captured(2).at(0) == active.fence &&
                    match.capturedLength(2) >= fenceLength && match.captured(3).trimmed().isEmpty()) {
                    result.append(active);
                    fenced = false;
                } else {
                    QString display = renderedIndent(content, lines.at(i).size() - content.size());
                    display.remove(0, qMin(spaces(display), fenceIndent));
                    if (!removeIndent(&content, fenceIndent))
                        content.remove(0, spaces(content));
                    active.text += content + newline;
                    active.renderedText += display + newline;
                }
                paragraph = false;
                continue;
            }
            result.append(active); // Container ended implicitly.
            fenced = false;
            // Reprocess this line outside the old fence, including any extra quotes.
            --i;
            paragraph = false;
            continue;
        }
        if (depth != quoteDepth) {
            listIndent = 0;
            paragraph = false;
        }
        quoteDepth = depth;
        QString content = line;
        if (!removeIndent(&content, listIndent)) {
            listIndent = 0;
            content = line;
            paragraph = false;
        }
        auto listMatch = list.match(content);
        if (listMatch.hasMatch()) {
            if (indented) {
                result.append(active);
                indented = false;
            }
            listIndent += listMatch.capturedLength();
            content.remove(0, listMatch.capturedLength());
            paragraph = false;
        }
        if (indented) {
            QString code = content;
            if (content.trimmed().isEmpty()) {
                QString blank = content;
                removeIndent(&blank, 4);
                pendingBlank += blank + newline;
                pendingDisplayBlank += renderedIndent(blank, lines.at(i).size() - blank.size()) + newline;
                continue;
            }
            if (depth == activeQuotes && listIndent == activeList && removeIndent(&code, 4)) {
                active.text += pendingBlank + code + newline;
                active.renderedText += pendingDisplayBlank + renderedIndent(code, lines.at(i).size() - code.size()) + newline;
                pendingBlank.clear();
                pendingDisplayBlank.clear();
                continue;
            }
            result.append(active);
            indented = false;
            pendingBlank.clear();
            pendingDisplayBlank.clear();
        }
        const auto match = fence.match(content);
        if (match.hasMatch() && !(match.captured(2).startsWith(QLatin1Char('`')) &&
                                  match.captured(3).contains(QLatin1Char('`')))) {
            active = Candidate();
            active.fence = match.captured(2).at(0);
            active.language = match.captured(3).trimmed().section(QRegularExpression(QStringLiteral("\\s+")), 0, 0);
            fenceLength = match.capturedLength(2);
            fenceIndent = match.capturedLength(1);
            activeQuotes = depth;
            activeList = listIndent;
            fenced = true;
            paragraph = false;
        } else if (!paragraph && !content.trimmed().isEmpty() && removeIndent(&content, 4)) {
            active = Candidate();
            active.text = content + newline;
            active.renderedText = renderedIndent(content, lines.at(i).size() - content.size()) + newline;
            activeQuotes = depth;
            activeList = listIndent;
            indented = true;
            pendingBlank.clear();
            pendingDisplayBlank.clear();
        } else {
            // HTML block interpretation and unusual list/tab containers are not
            // guessed: matching displayed text alone would not prove identity.
            if (content.trimmed().startsWith(QLatin1Char('<')))
                *supported = false;
            paragraph = !content.trimmed().isEmpty();
        }
    }
    if (fenced || indented)
        result.append(active);
    return result;
}
}

QVector<CodeBlockRecord> indexCodeBlocks(QTextDocument *document, QWidget *editor,
                                       quint64 version, const QString *source)
{
    QVector<CodeBlockRecord> records;
    if (!document || !editor || !version)
        return records;
    QVector<QTextBlock> rendered;
    for (auto block = document->begin(); block.isValid(); block = block.next()) {
        if (isCode(block))
            rendered.append(block);
    }
    bool supported = source != nullptr;
    const auto candidates = source ? sourceBlocks(*source, &supported) : QVector<Candidate>();
    int cursor = 0;
    for (const auto &candidate : candidates) {
        CodeBlockRecord record;
        record.editor = editor;
        record.version = version;
        record.ordinal = records.size();
        record.language = candidate.language;
        record.text = candidate.text;
        if (!candidate.text.isEmpty()) {
            QString body = candidate.renderedText;
            if (body.endsWith(QLatin1Char('\n')))
                body.chop(1);
            QStringList expected = body.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
            expected.append(QString()); // Qt's synthetic terminal code paragraph.
            for (const auto &line : expected) {
                if (cursor >= rendered.size()) {
                    supported = false;
                    break;
                }
                const auto block = rendered.at(cursor++);
                const auto format = block.blockFormat();
                if (record.endPosition >= 0 && record.endPosition != block.position())
                    supported = false;
                if (record.position < 0)
                    record.position = block.position();
                record.endPosition = block.position() + block.length();
                if (block.text() != line ||
                    format.property(QTextFormat::BlockCodeLanguage).toString() != candidate.language ||
                    format.property(QTextFormat::BlockCodeFence).toString() !=
                        (candidate.fence.isNull() ? QString() : QString(candidate.fence)))
                    supported = false;
            }
        }
        records.append(record);
    }
    if (cursor != rendered.size())
        supported = false;
    if (!supported) {
        // Without a proven ordered mapping, expose only disabled rendered runs.
        records.clear();
        int previousEnd = -1;
        for (const auto &block : rendered) {
            const QString language = block.blockFormat().property(QTextFormat::BlockCodeLanguage).toString();
            if (records.isEmpty() || previousEnd != block.position() || records.last().language != language) {
                CodeBlockRecord record;
                record.editor = editor;
                record.version = version;
                record.ordinal = records.size();
                record.position = block.position();
                record.language = language;
                record.error = source ? QObject::tr("代码块与当前预览不能可靠对应，复制不可用。")
                                      : QObject::tr("无法读取同版本源码，复制不可用。");
                records.append(record);
            }
            previousEnd = block.position() + block.length();
            records.last().endPosition = previousEnd;
        }
    } else {
        for (auto &record : records)
            record.reliable = true;
    }
    return records;
}
