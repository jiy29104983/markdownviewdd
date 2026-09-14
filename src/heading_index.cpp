#include "heading_index.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>

namespace {
struct SourceHeading
{
    int level;
    QString text;
    int line;
    int offset;
};

QString inlineText(const QString &markdown)
{
    static const QString markup = QStringLiteral("\\`*_~[<&");
    bool needsParsing = false;
    for (const QChar character : markdown) {
        if (markup.contains(character)) {
            needsParsing = true;
            break;
        }
    }
    if (!needsParsing) {
        return markdown.simplified();
    }
    // Parse only the small heading fragment for inline markup. The outline's
    // structure always comes from the already-rendered document, never this.
    QTextDocument fragment;
    fragment.setMarkdown(QStringLiteral("# ") + markdown, QTextDocument::MarkdownDialectGitHub);
    return fragment.toPlainText().simplified();
}
}

QVector<HeadingRecord> renderedHeadings(QTextDocument *document,
                                       QWidget *editor, quint64 version)
{
    QVector<HeadingRecord> result;
    if (!document || !editor || version == 0) {
        return result;
    }
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const int level = block.blockFormat().headingLevel();
        if (level >= 1 && level <= 6) {
            result.append({editor, version, level, block.text(), block.position(), -1, -1});
        }
    }
    return result;
}

bool mapHeadingSource(QVector<HeadingRecord> *headings, const QString &source)
{
    if (!headings) {
        return false;
    }
    for (HeadingRecord &heading : *headings) {
        heading.sourceLine = -1;
        heading.sourceOffset = -1;
    }
    if (headings->isEmpty()) {
        return true;
    }
    static const QRegularExpression atx(QStringLiteral("^ {0,3}(#{1,6})(?:[ \\t]+(.*)|$)"));
    static const QRegularExpression closing(QStringLiteral("[ \\t]+#+[ \\t]*$"));
    static const QRegularExpression fence(QStringLiteral("^ {0,3}(`{3,}|~{3,})(.*)$"));
    static const QRegularExpression setext(QStringLiteral("^ {0,3}(=+|-+)[ \\t]*$"));
    static const QRegularExpression quote(QStringLiteral("^ {0,3}>[ \\t]?"));
    static const QRegularExpression list(QStringLiteral("^ {0,3}(?:[-+*]|[0-9]{1,9}[.)])[ \\t]+"));
    QVector<SourceHeading> candidates;
    QChar fenceChar;
    int fenceLength = 0;
    QString paragraph;
    int paragraphLine = -1;
    int paragraphOffset = -1;
    int lineNumber = 0;
    int scalarOffset = 0;
    int start = 0;
    bool htmlBlock = false;
    while (start < source.size()) {
        int end = start;
        while (end < source.size() && source.at(end) != QLatin1Char('\n') &&
               source.at(end) != QLatin1Char('\r')) {
            ++end;
        }
        int next = end;
        if (next < source.size() && source.at(next++) == QLatin1Char('\r') &&
            next < source.size() && source.at(next) == QLatin1Char('\n')) {
            ++next;
        }
        QString line = source.mid(start, end - start);
        // Container prefixes are retained in the original source offset/line.
        for (;;) {
            const auto match = quote.match(line);
            if (!match.hasMatch()) {
                break;
            }
            line.remove(0, match.capturedLength());
        }
        const auto listMatch = list.match(line);
        if (listMatch.hasMatch()) {
            line.remove(0, listMatch.capturedLength());
        }
        const QString trimmed = line.trimmed();
        const auto fenceMatch = fence.match(line);
        if (!fenceChar.isNull()) {
            if (fenceMatch.hasMatch() && fenceMatch.captured(1).at(0) == fenceChar &&
                fenceMatch.capturedLength(1) >= fenceLength && fenceMatch.captured(2).trimmed().isEmpty()) {
                fenceChar = QChar();
            }
            paragraph.clear();
        } else if (fenceMatch.hasMatch() &&
                   !(fenceMatch.captured(1).startsWith(QLatin1Char('`')) &&
                     fenceMatch.captured(2).contains(QLatin1Char('`')))) {
            fenceChar = fenceMatch.captured(1).at(0);
            fenceLength = fenceMatch.capturedLength(1);
            paragraph.clear();
        } else if (trimmed.isEmpty()) {
            paragraph.clear();
            htmlBlock = false;
        } else if (htmlBlock || trimmed.startsWith(QLatin1Char('<'))) {
            // Raw HTML containers are deliberately not guessed as Markdown.
            htmlBlock = true;
            paragraph.clear();
        } else {
            const auto heading = atx.match(line);
            const auto underline = setext.match(line);
            if (heading.hasMatch()) {
                QString body = heading.captured(2);
                body.remove(closing);
                candidates.append({heading.capturedLength(1), inlineText(body), lineNumber, scalarOffset});
                paragraph.clear();
            } else if (underline.hasMatch() && !paragraph.isEmpty()) {
                candidates.append({underline.captured(1).startsWith(QLatin1Char('=')) ? 1 : 2,
                                   inlineText(paragraph), paragraphLine, paragraphOffset});
                paragraph.clear();
            } else if (!line.startsWith(QStringLiteral("    ")) && !line.startsWith(QLatin1Char('\t'))) {
                if (paragraph.isEmpty()) {
                    paragraphLine = lineNumber;
                    paragraphOffset = scalarOffset;
                } else {
                    paragraph += QLatin1Char('\n');
                }
                paragraph += line;
            } else {
                paragraph.clear();
            }
        }
        scalarOffset += source.mid(start, next - start).toUcs4().size();
        start = next;
        ++lineNumber;
    }
    if (candidates.size() != headings->size()) {
        return false;
    }
    for (int i = 0; i < candidates.size(); ++i) {
        if (candidates.at(i).level != headings->at(i).level ||
            candidates.at(i).text != headings->at(i).text.simplified()) {
            return false;
        }
    }
    for (int i = 0; i < candidates.size(); ++i) {
        (*headings)[i].sourceLine = candidates.at(i).line;
        (*headings)[i].sourceOffset = candidates.at(i).offset;
    }
    return true;
}
