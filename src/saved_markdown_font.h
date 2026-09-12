#pragma once

#include <QString>
#include <QtGlobal>

struct SavedFontPaths
{
    // Empty paths use Qt's IniFormat/UserScope root and the host executable dir.
    QString userConfigRoot;
    QString applicationDirectory;
    QString snapshotDirectory;
};

struct SavedMarkdownFont
{
    enum class Result { Builtin, Configured, Failed };
    enum class Source { Builtin, Style0, DefaultFont };

    QString family;
    qreal pointSize = 12.0;
    int themeId = 0;
    Source source = Source::Builtin;
    Result result = Result::Builtin;
    QString sourcePath;
    QString configuredFamily;
    QString reason;
    bool familyFallback = false;
};

SavedMarkdownFont defaultMarkdownFont();
SavedMarkdownFont readSavedMarkdownFont(const SavedFontPaths &paths = SavedFontPaths());
QString markdownCodeFontFamily(const QString &bodyFamily);
