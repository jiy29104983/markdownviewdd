#include "saved_markdown_font.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QSettings>
#include <QStringList>
#include <QTemporaryFile>
#include <QUuid>

#include <memory>

namespace {
const QStringList kThemes = {
    QStringLiteral("Default"), QStringLiteral("Bespin"), QStringLiteral("Black board"),
    QStringLiteral("Blue light"), QStringLiteral("Choco"), QStringLiteral("DansLeRuSH-Dark"),
    QStringLiteral("Deep Black"), QStringLiteral("lavender"), QStringLiteral("HotFudgeSundae"),
    QStringLiteral("misty rose"), QStringLiteral("Mono Industrial"), QStringLiteral("Monokai"),
    QStringLiteral("Obsidian"), QStringLiteral("Plastic Code Wrap"), QStringLiteral("Ruby Blue"),
    QStringLiteral("Twilight"), QStringLiteral("Vibrant Ink"), QStringLiteral("yellow rice")
};

QString builtinFamily()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Courier New");
#elif defined(Q_OS_MAC)
    return QStringLiteral("Menlo");
#else
    return QStringLiteral("Courier 10 Pitch");
#endif
}

bool familyAvailable(const QString &family)
{
    return QFontDatabase().families().contains(family, Qt::CaseInsensitive);
}

QString userConfigRoot()
{
    // Never instantiate a reader on a live host path: destruction of such a
    // QSettings reader can flush the host's pending, shared-cache writes.
    const QString organization = QStringLiteral("notepad/markdownview-path-%1")
        .arg(QUuid::createUuid().toString(QUuid::Id128));
    QSettings locator(QSettings::IniFormat, QSettings::UserScope, organization);
    QDir directory = QFileInfo(locator.fileName()).absoluteDir();
    directory.cdUp();
    return directory.absolutePath();
}

struct Snapshot
{
    explicit Snapshot(const QString &directory)
        : file(QDir(directory.isEmpty() ? QDir::tempPath() : directory)
                   .filePath(QStringLiteral("markdownview-font-XXXXXX.ini")))
    {
    }

    bool load(const QString &path, bool utf8 = false)
    {
        QFile source(path);
        if (!source.open(QIODevice::ReadOnly) || !file.open()) {
            return false;
        }
        const QByteArray bytes = source.readAll();
        if (source.error() != QFile::NoError || file.write(bytes) != bytes.size() ||
            !file.flush()) {
            return false;
        }
        file.close();
        settings.reset(new QSettings(file.fileName(), QSettings::IniFormat));
        settings->setFallbacksEnabled(false);
        if (utf8) {
            settings->setIniCodec("UTF-8");
        }
        return true;
    }

    // Reverse destruction order closes the parser before removing its private file.
    QTemporaryFile file;
    std::unique_ptr<QSettings> settings;
};

bool decodeFont(const QVariant &value, QString *family, qreal *pointSize)
{
    const QStringList fields = value.toStringList();
    if (fields.size() != 5 || fields.at(0).trimmed().isEmpty()) {
        return false;
    }
    bool valid = false;
    const int points = fields.at(1).toInt(&valid);
    if (!valid || points <= 0) {
        return false;
    }
    for (int index = 2; index < 5; ++index) {
        const int flag = fields.at(index).toInt(&valid);
        if (!valid || (flag != 0 && flag != 1)) {
            return false;
        }
    }
    *family = fields.at(0);
    *pointSize = points;
    return true;
}
}

SavedMarkdownFont defaultMarkdownFont()
{
    SavedMarkdownFont font;
    font.family = builtinFamily();
    if (!familyAvailable(font.family)) {
        font.family = QFontInfo(QFontDatabase::systemFont(QFontDatabase::GeneralFont)).family();
        font.familyFallback = true;
    }
    return font;
}

QString markdownCodeFontFamily(const QString &bodyFamily)
{
    const QFontDatabase database;
    if (familyAvailable(bodyFamily) && database.isFixedPitch(bodyFamily)) {
        return bodyFamily;
    }
    for (const QString &family : {QStringLiteral("Consolas"), builtinFamily(),
                                  QStringLiteral("DejaVu Sans Mono")}) {
        if (familyAvailable(family) && database.isFixedPitch(family)) {
            return family;
        }
    }
    return QFontInfo(QFontDatabase::systemFont(QFontDatabase::FixedFont)).family();
}

SavedMarkdownFont readSavedMarkdownFont(const SavedFontPaths &paths)
{
    SavedMarkdownFont font = defaultMarkdownFont();
    const auto fail = [&font](const QString &reason) {
        font.result = SavedMarkdownFont::Result::Failed;
        font.source = SavedMarkdownFont::Source::Builtin;
        font.reason = reason;
        return font;
    };
    const QDir root(paths.userConfigRoot.isEmpty() ? userConfigRoot() : paths.userConfigRoot);
    const QString generalPath = root.filePath(QStringLiteral("notepad/nddsets.ini"));
    Snapshot general(paths.snapshotDirectory);
    if (QFileInfo::exists(generalPath)) {
        if (!general.load(generalPath, true)) {
            return fail(QStringLiteral("main-config-read-failed"));
        }
        bool valid = false;
        font.themeId = general.settings->value(QStringLiteral("skinid"), 0).toString().toInt(&valid);
        if (!valid || font.themeId < 0 || font.themeId >= kThemes.size() ||
            general.settings->status() != QSettings::NoError) {
            font.themeId = -1;
            return fail(QStringLiteral("invalid-saved-theme"));
        }
    }

    const QString theme = kThemes.at(font.themeId);
    const QString userPath = root.filePath(
        QStringLiteral("notepad/userstyle/%1/markdown.ini").arg(theme));
    const QDir application(paths.applicationDirectory.isEmpty()
        ? QCoreApplication::applicationDirPath() : paths.applicationDirectory);
    const QString templatePath = application.filePath(
        QStringLiteral("themes/%1/markdown.ini").arg(theme));
    if (QFileInfo::exists(userPath)) {
        font.sourcePath = userPath;
    } else if (QFileInfo::exists(templatePath)) {
        font.sourcePath = templatePath;
    } else {
        return font;
    }

    Snapshot selected(paths.snapshotDirectory);
    if (!selected.load(font.sourcePath)) {
        return fail(QStringLiteral("markdown-config-read-failed"));
    }
    QSettings &settings = *selected.settings;
    QString key = QStringLiteral("Scintilla/Markdown/style0/font");
    SavedMarkdownFont::Source source = SavedMarkdownFont::Source::Style0;
    if (!settings.contains(key)) {
        // Default initializes style0 before loading defaultfont. Other themes
        // load defaultfont first. Do not merge missing fields from a template.
        key = font.themeId == 0 ? QString()
            : QStringLiteral("Scintilla/Markdown/defaultfont");
        source = SavedMarkdownFont::Source::DefaultFont;
    }
    if (key.isEmpty() || !settings.contains(key)) {
        return settings.status() == QSettings::NoError ? font
            : fail(QStringLiteral("invalid-markdown-ini"));
    }
    QString family;
    qreal points = 0.0;
    if (!decodeFont(settings.value(key), &family, &points) ||
        settings.status() != QSettings::NoError) {
        return fail(QStringLiteral("invalid-markdown-font"));
    }
    font.configuredFamily = family;
    font.pointSize = points;
    font.source = source;
    font.result = SavedMarkdownFont::Result::Configured;
    font.familyFallback = !familyAvailable(family);
    if (!font.familyFallback) {
        font.family = family;
    } else {
        font.reason = QStringLiteral("font-family-unavailable");
    }
    return font;
}
