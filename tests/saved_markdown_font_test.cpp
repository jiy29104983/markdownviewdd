#include "host_adapter.h"
#include "markdown_preview_dock.h"
#include "preview_controller.h"
#include "saved_markdown_font.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QScrollBar>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextFragment>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtTest>

#include <memory>

namespace {
QStringList fields(const QString &family, int size)
{
    return {family, QString::number(size), QStringLiteral("0"),
            QStringLiteral("0"), QStringLiteral("0")};
}

QByteArray bytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QMap<QString, QByteArray> hashes(const QString &root)
{
    QMap<QString, QByteArray> result;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        result.insert(QDir(root).relativeFilePath(path),
                      QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256));
    }
    return result;
}

struct Config
{
    QTemporaryDir directory;
    SavedFontPaths paths;
    Config()
    {
        paths.userConfigRoot = directory.filePath(QStringLiteral("user"));
        paths.applicationDirectory = directory.filePath(QStringLiteral("installation"));
        paths.snapshotDirectory = directory.filePath(QStringLiteral("snapshots"));
        QDir().mkpath(paths.snapshotDirectory);
    }
    QString mainPath() const
    {
        return paths.userConfigRoot + QStringLiteral("/notepad/nddsets.ini");
    }
    QString stylePath(const QString &theme = QStringLiteral("Default")) const
    {
        return paths.userConfigRoot + QStringLiteral("/notepad/userstyle/%1/markdown.ini").arg(theme);
    }
    QString templatePath(const QString &theme) const
    {
        return paths.applicationDirectory + QStringLiteral("/themes/%1/markdown.ini").arg(theme);
    }
    void theme(int id)
    {
        QSettings host(mainPath(), QSettings::IniFormat);
        host.setIniCodec("UTF-8");
        host.setValue(QStringLiteral("skinid"), id);
        host.sync();
    }
    void font(int points, const QString &family = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family())
    {
        writeFont(stylePath(), QStringLiteral("style0/font"), family, points);
    }
    static void writeFont(const QString &path, const QString &key, const QString &family, int points)
    {
        QSettings host(path, QSettings::IniFormat);
        host.setValue(QStringLiteral("Scintilla/Markdown/") + key, fields(family, points));
        host.sync();
    }
};

void wheel(QTextEdit *edit, int delta, Qt::KeyboardModifiers modifiers = Qt::ControlModifier)
{
    const QPointF pos(edit->viewport()->rect().center());
    QWheelEvent event(pos, edit->viewport()->mapToGlobal(pos.toPoint()),
                      QPoint(), QPoint(0, delta), Qt::NoButton, modifiers,
                      Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(edit->viewport(), &event);
}

QTextCharFormat formatOf(QTextEdit *edit, const QString &text)
{
    return edit->document()->find(text).charFormat();
}

QString sample()
{
    return QStringLiteral("# H1 `heading code`\n\n## H2\n\n### H3\n\n#### H4\n\n##### H5\n\n###### H6\n\n"
                          "Body **bold** *italic* ~~strike~~ [link](https://example.com) `inline code`\n\n"
                          "> Quote\n\n- List item\n\n| Header | Second |\n| --- | --- |\n| Cell | Value |\n\n"
                          "```cpp\nblock code\n```\n\n");
}
}

class QsciScintilla final : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit QsciScintilla(const QString &text = sample()) : markdown(text)
    {
        setProperty("filePath", QStringLiteral("sample.md"));
        verticalScrollBar()->setRange(0, 100);
    }
    void edit() { markdown += QStringLiteral("\nEdited\n"); emit textChanged(); }
    int renders = 0;
    QString markdown;
    QPointer<QWidget> preview;
    QPointer<QTextEdit> textEdit;
signals:
    void textChanged();
public slots:
    void on_viewMarkdown()
    {
        if (!preview) {
            preview = new QWidget(this);
            preview->setObjectName(QStringLiteral("MarkdownViewClass"));
            auto *layout = new QVBoxLayout(preview);
            textEdit = new QTextEdit(preview);
            textEdit->setReadOnly(true);
            textEdit->setObjectName(QStringLiteral("textEdit"));
            layout->addWidget(textEdit);
        }
        on_updataMarkdown();
    }
    void on_updataMarkdown()
    {
        ++renders;
        textEdit->setMarkdown(markdown);
    }
};

class CountingHostAdapter final : public HostAdapter
{
public:
    CountingHostAdapter(QWidget *window, const SavedFontPaths &paths)
        : host(createNotepadHostAdapter(window, paths)) {}
    SavedMarkdownFont savedMarkdownFont() const override
    {
        ++reads;
        return host->savedMarkdownFont();
    }
    QWidget *currentEditor() const override { return host->currentEditor(); }
    QMetaObject::Connection connectActiveEditorChanged(QObject *context, std::function<void()> callback) override
    { return host->connectActiveEditorChanged(context, callback); }
    ScrollConnections connectEditorScrollChanged(QWidget *editor, QObject *context, std::function<void()> callback) override
    { return host->connectEditorScrollChanged(editor, context, callback); }
    QString filePath(QWidget *editor) const override { return host->filePath(editor); }
    bool isFilePathChangeEvent(QEvent *event) const override { return host->isFilePathChangeEvent(event); }
    bool isEditorContextMenu(QMenu *menu, QWidget *editor) const override { return host->isEditorContextMenu(menu, editor); }
    bool isMarkdownContextAction(QAction *action) const override { return host->isMarkdownContextAction(action); }
    bool bridgeMarkdownContextAction(QAction *action, QWidget *editor) override
    { return host->bridgeMarkdownContextAction(action, editor); }
    PreviewResult ensurePreview(QWidget *editor) override { return host->ensurePreview(editor); }
    bool refreshPreview(QWidget *editor, qint64 *duration, QString *error) override
    { return host->refreshPreview(editor, duration, error); }
    bool disconnectImmediateRefresh(QWidget *editor, bool force) override
    { return host->disconnectImmediateRefresh(editor, force); }
    bool previewIsCurrent(QWidget *editor) const override { return host->previewIsCurrent(editor); }
    void setPreviewCurrent(QWidget *editor, bool current) override { host->setPreviewCurrent(editor, current); }
    bool releasePreview(QWidget *editor, QString *error) override { return host->releasePreview(editor, error); }
    mutable int reads = 0;
    std::unique_ptr<HostAdapter> host;
};

struct Fixture
{
    QMainWindow window;
    QMenu menu;
    CountingHostAdapter adapter;
    PreviewController controller;
    QTabWidget *tabs = nullptr;
    QsciScintilla *editor = nullptr;
    Fixture(const Config &config, const QString &text = sample())
        : menu(&window), adapter(&window, config.paths), controller(&window, &adapter)
    {
        tabs = new QTabWidget(&window);
        tabs->setObjectName(QStringLiteral("editTabWidget"));
        window.setCentralWidget(tabs);
        editor = new QsciScintilla(text);
        tabs->addTab(editor, QStringLiteral("A"));
        controller.installMenu(&menu);
        window.resize(1000, 700);
        window.show();
        QCoreApplication::processEvents();
    }
    MarkdownPreviewDock *dock() { return window.findChild<MarkdownPreviewDock *>(); }
    void open() { QMetaObject::invokeMethod(&controller, "togglePreview", Q_ARG(bool, true)); }
    void refresh() { QMetaObject::invokeMethod(&controller, "renderNow"); }
    void sync(bool enabled) { QMetaObject::invokeMethod(&controller, "setSyncScrolling", Q_ARG(bool, enabled)); }
    bool exportHtml(QByteArray *html)
    {
        QString source;
        return controller.currentHtmlSnapshot(html, &source);
    }
};

class SavedMarkdownFontTest final : public QObject
{
    Q_OBJECT
private slots:
    void hostGeneratedFixtures();
    void themeMapping_data();
    void themeMapping();
    void missingFilesAndFields();
    void malformedIniAndMissingThemeKey();
    void userFileNeverMergesTemplate();
    void fontFieldDecoding_data();
    void fontFieldDecoding();
    void invalidTheme_data();
    void invalidTheme();
    void readonlySnapshotsAndPendingHostWrites();
    void qtUserScopePathDoesNotCreateOrFlushFiles();
    void unreadableSourcesUseDefaultAndCleanSnapshots();
    void fontAvailabilityAndGlobalSavedValues();
    void openingReadsOnceAndOtherEventsReuse();
    void hiddenExportDoesNotReadConfiguration();
    void nativeWheelAndSemanticFormats_data();
    void nativeWheelAndSemanticFormats();
    void reopenKeepsZoomAndRefreshPreservesRatios();
    void failureResetsBaseAndOnlyReopenRecovers();
    void tabsCacheAndWindowsKeepIndependentState();
    void staleSnapshotAndUserScrollCancelOldLayout();
    void consecutiveStylesAndDestroyedPreviewCancelOldWork();
    void fontScreenshots();
    void importedCodeWithUnsetFixedPitchRemainsMonospaced();
    void synchronizedFontChangeKeepsEditorInControl();
    void savedEditorZoomAndUnrelatedStylesAreIgnored();
    void manualContextOpenAndPendingReopenDoNotParse();
    void invalidStyleInputDoesNotMutateDocument();
};

void SavedMarkdownFontTest::hostGeneratedFixtures()
{
    const QString directory = QFINDTESTDATA("fixtures/saved-host-font/expected.json");
    QVERIFY(!directory.isEmpty());
    const QJsonArray cases = QJsonDocument::fromJson(bytes(directory)).array();
    QCOMPARE(cases.size(), 8);
    for (const QJsonValue &value : cases) {
        const QJsonObject row = value.toObject();
        Config config;
        config.theme(row.value(QStringLiteral("themeId")).toInt());
        const QString target = config.stylePath(row.value(QStringLiteral("theme")).toString());
        QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath()));
        const QString input = QFileInfo(directory).dir().filePath(row.value(QStringLiteral("file")).toString());
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(bytes(input), QCryptographicHash::Sha256).toHex()),
                 row.value(QStringLiteral("sha256")).toString());
        QVERIFY(QFile::copy(input, target));
        const SavedMarkdownFont font = readSavedMarkdownFont(config.paths);
        QCOMPARE(font.pointSize, qreal(row.value(QStringLiteral("hostPointSize")).toInt()));
        if (row.value(QStringLiteral("source")).toString().startsWith(QStringLiteral("builtin"))) {
            QCOMPARE(font.family, defaultMarkdownFont().family);
            QCOMPARE(font.source, SavedMarkdownFont::Source::Builtin);
        } else {
            QCOMPARE(font.configuredFamily, row.value(QStringLiteral("hostFamily")).toString());
            QCOMPARE(font.result, SavedMarkdownFont::Result::Configured);
        }
    }
}

void SavedMarkdownFontTest::themeMapping_data()
{
    QTest::addColumn<int>("id");
    QTest::addColumn<QString>("name");
    const QStringList names = {QStringLiteral("Default"), QStringLiteral("Bespin"), QStringLiteral("Black board"),
        QStringLiteral("Blue light"), QStringLiteral("Choco"), QStringLiteral("DansLeRuSH-Dark"),
        QStringLiteral("Deep Black"), QStringLiteral("lavender"), QStringLiteral("HotFudgeSundae"),
        QStringLiteral("misty rose"), QStringLiteral("Mono Industrial"), QStringLiteral("Monokai"),
        QStringLiteral("Obsidian"), QStringLiteral("Plastic Code Wrap"), QStringLiteral("Ruby Blue"),
        QStringLiteral("Twilight"), QStringLiteral("Vibrant Ink"), QStringLiteral("yellow rice")};
    for (int id = 0; id < names.size(); ++id) {
        QTest::newRow(qPrintable(names.at(id))) << id << names.at(id);
    }
}

void SavedMarkdownFontTest::themeMapping()
{
    QFETCH(int, id);
    QFETCH(QString, name);
    Config config;
    config.theme(id);
    const QString path = config.templatePath(name);
    Config::writeFont(path, QStringLiteral("style0/font"), QStringLiteral("Theme Font"), 19);
    const SavedMarkdownFont font = readSavedMarkdownFont(config.paths);
    QCOMPARE(font.themeId, id);
    QCOMPARE(font.sourcePath, path);
    QCOMPARE(font.configuredFamily, QStringLiteral("Theme Font"));
    QCOMPARE(font.pointSize, 19.0);
}

void SavedMarkdownFontTest::missingFilesAndFields()
{
    Config config;
    const auto before = hashes(config.directory.path());
    SavedMarkdownFont font = readSavedMarkdownFont(config.paths);
    QCOMPARE(font.result, SavedMarkdownFont::Result::Builtin);
    QCOMPARE(font.pointSize, 12.0);
    QCOMPARE(font.themeId, 0);
    QCOMPARE(hashes(config.directory.path()), before);
    Config::writeFont(config.stylePath(), QStringLiteral("defaultfont"), QStringLiteral("Late Default"), 25);
    font = readSavedMarkdownFont(config.paths);
    QCOMPARE(font.pointSize, 12.0);
    config.theme(6);
    Config::writeFont(config.stylePath(QStringLiteral("Deep Black")), QStringLiteral("defaultfont"), QStringLiteral("Theme Default"), 17);
    font = readSavedMarkdownFont(config.paths);
    QCOMPARE(font.pointSize, 17.0);
    QCOMPARE(font.source, SavedMarkdownFont::Source::DefaultFont);
    Config::writeFont(config.stylePath(QStringLiteral("Deep Black")), QStringLiteral("style0/font"), QStringLiteral("Body"), 14);
    QCOMPARE(readSavedMarkdownFont(config.paths).pointSize, 14.0);
}

void SavedMarkdownFontTest::malformedIniAndMissingThemeKey()
{
    Config config;
    config.font(17);
    {
        QSettings host(config.mainPath(), QSettings::IniFormat);
        host.setValue(QStringLiteral("unrelated"), 123);
        host.sync();
    }
    QCOMPARE(readSavedMarkdownFont(config.paths).themeId, 0);
    QCOMPARE(readSavedMarkdownFont(config.paths).pointSize, 17.0);
    QFile malformed(config.mainPath());
    QVERIFY(malformed.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(malformed.write("[broken section\nskinid=0\n") > 0);
    malformed.close();
    QCOMPARE(readSavedMarkdownFont(config.paths).result, SavedMarkdownFont::Result::Failed);
    QVERIFY(QFile::remove(config.mainPath()));
    QFile style(config.stylePath());
    QVERIFY(style.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(style.write("[broken section\nfont=hello\n") > 0);
    style.close();
    QCOMPARE(readSavedMarkdownFont(config.paths).result, SavedMarkdownFont::Result::Failed);
}

void SavedMarkdownFontTest::userFileNeverMergesTemplate()
{
    Config config;
    config.theme(6);
    const QString theme = QStringLiteral("Deep Black");
    Config::writeFont(config.templatePath(theme), QStringLiteral("style0/font"), QStringLiteral("Template"), 30);
    QCOMPARE(readSavedMarkdownFont(config.paths).pointSize, 30.0);
    Config::writeFont(config.stylePath(theme), QStringLiteral("defaultfont"), QStringLiteral("User"), 16);
    QCOMPARE(readSavedMarkdownFont(config.paths).pointSize, 16.0);
    {
        QSettings host(config.stylePath(theme), QSettings::IniFormat);
        host.remove(QStringLiteral("Scintilla/Markdown/defaultfont"));
        host.setValue(QStringLiteral("Other"), 1);
        host.sync();
    }
    QCOMPARE(readSavedMarkdownFont(config.paths).pointSize, 12.0);
    {
        QSettings host(config.stylePath(theme), QSettings::IniFormat);
        host.setValue(QStringLiteral("Scintilla/Markdown/defaultfont"), fields(QStringLiteral("Default"), 18));
        host.setValue(QStringLiteral("Scintilla/Markdown/style0/font"), QStringList{QStringLiteral("broken")});
        host.sync();
    }
    const SavedMarkdownFont font = readSavedMarkdownFont(config.paths);
    QCOMPARE(font.result, SavedMarkdownFont::Result::Failed);
    QCOMPARE(font.pointSize, 12.0);
}

void SavedMarkdownFontTest::fontFieldDecoding_data()
{
    QTest::addColumn<QStringList>("input");
    QTest::addColumn<bool>("valid");
    QTest::newRow("chinese") << fields(QStringLiteral("微软雅黑"), 14) << true;
    QTest::newRow("space-comma-escape") << fields(QStringLiteral("字体, Family \\ Name"), 16) << true;
    QTest::newRow("empty-family") << fields(QStringLiteral("  "), 14) << false;
    QTest::newRow("zero") << fields(QStringLiteral("Font"), 0) << false;
    QTest::newRow("negative") << fields(QStringLiteral("Font"), -3) << false;
    QTest::newRow("short-list") << QStringList{QStringLiteral("Font"), QStringLiteral("12")} << false;
    for (const QString &value : {QStringLiteral("12.5"), QStringLiteral("nan"), QStringLiteral("inf"), QStringLiteral("2147483648")}) {
        QStringList input = fields(QStringLiteral("Font"), 12);
        input[1] = value;
        QTest::newRow(qPrintable(value)) << input << false;
    }
    QStringList badFlag = fields(QStringLiteral("Font"), 12);
    badFlag[2] = QStringLiteral("bad");
    QTest::newRow("bad-flag") << badFlag << false;
}

void SavedMarkdownFontTest::fontFieldDecoding()
{
    QFETCH(QStringList, input);
    QFETCH(bool, valid);
    Config config;
    {
        QSettings host(config.stylePath(), QSettings::IniFormat);
        host.setValue(QStringLiteral("Scintilla/Markdown/style0/font"), input);
        host.setValue(QStringLiteral("Scintilla/Markdown/style0/font2"), fields(QStringLiteral("Wrong"), 40));
        host.sync();
    }
    const SavedMarkdownFont font = readSavedMarkdownFont(config.paths);
    QCOMPARE(font.result, valid ? SavedMarkdownFont::Result::Configured : SavedMarkdownFont::Result::Failed);
    QCOMPARE(font.pointSize, valid ? input.at(1).toDouble() : 12.0);
    if (valid) {
        QCOMPARE(font.configuredFamily, input.at(0));
    }
    QVERIFY(QDir(config.paths.snapshotDirectory).entryList(QDir::Files).isEmpty());
}

void SavedMarkdownFontTest::invalidTheme_data()
{
    QTest::addColumn<QString>("value");
    for (const QString &value : {QStringLiteral("-1"), QStringLiteral("18"), QStringLiteral("999"),
                                QStringLiteral("abc"), QStringLiteral("1.5"), QStringLiteral("")}) {
        QTest::newRow(qPrintable(value.isEmpty() ? QStringLiteral("empty") : value)) << value;
    }
}

void SavedMarkdownFontTest::invalidTheme()
{
    QFETCH(QString, value);
    Config config;
    config.font(20);
    {
        QSettings host(config.mainPath(), QSettings::IniFormat);
        host.setValue(QStringLiteral("skinid"), value);
        host.sync();
    }
    const SavedMarkdownFont font = readSavedMarkdownFont(config.paths);
    QCOMPARE(font.result, SavedMarkdownFont::Result::Failed);
    QCOMPARE(font.themeId, -1);
    QCOMPARE(font.pointSize, 12.0);
}

void SavedMarkdownFontTest::readonlySnapshotsAndPendingHostWrites()
{
    Config config;
    config.theme(0);
    config.font(14);
    const auto before = hashes(config.directory.path());
    QSettings hostTheme(config.mainPath(), QSettings::IniFormat);
    QSettings hostStyle(config.stylePath(), QSettings::IniFormat);
    hostTheme.setValue(QStringLiteral("skinid"), 6);
    hostStyle.setValue(QStringLiteral("Scintilla/Markdown/style0/font"), fields(QStringLiteral("Pending"), 22));
    const SavedMarkdownFont font = readSavedMarkdownFont(config.paths);
    QCOMPARE(font.pointSize, 14.0);
    QCOMPARE(font.themeId, 0);
    QCOMPARE(hashes(config.directory.path()), before);
    QCOMPARE(hostTheme.value(QStringLiteral("skinid")).toInt(), 6);
    QCOMPARE(hostStyle.value(QStringLiteral("Scintilla/Markdown/style0/font")).toStringList().at(1), QStringLiteral("22"));
    // A failed private snapshot must not flush pending host data either.
    SavedFontPaths failedPaths = config.paths;
    failedPaths.snapshotDirectory += QStringLiteral("/does-not-exist");
    QCOMPARE(readSavedMarkdownFont(failedPaths).result, SavedMarkdownFont::Result::Failed);
    QCOMPARE(hashes(config.directory.path()), before);
    hostStyle.sync();
    QCOMPARE(readSavedMarkdownFont(config.paths).pointSize, 22.0);
}

void SavedMarkdownFontTest::qtUserScopePathDoesNotCreateOrFlushFiles()
{
    Config config;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.paths.userConfigRoot);
    QSettings host(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("notepad/nddsets"));
    QCOMPARE(host.fileName(), config.mainPath());
    host.setValue(QStringLiteral("skinid"), 0);
    host.sync();
    config.font(16);
    const auto before = hashes(config.directory.path());
    host.setValue(QStringLiteral("skinid"), 6);
    SavedFontPaths paths = config.paths;
    paths.userConfigRoot.clear();
    QCOMPARE(readSavedMarkdownFont(paths).pointSize, 16.0);
    QCOMPARE(hashes(config.directory.path()), before);
    // Subsequent cases always inject paths; the temporary global root is never a real user directory.
}

void SavedMarkdownFontTest::unreadableSourcesUseDefaultAndCleanSnapshots()
{
    Config config;
    config.theme(0);
    QVERIFY(QDir().mkpath(config.stylePath())); // A directory is an unreadable INI on every test platform.
    Config::writeFont(config.templatePath(QStringLiteral("Default")), QStringLiteral("style0/font"), QStringLiteral("Template"), 30);
    SavedMarkdownFont font = readSavedMarkdownFont(config.paths);
    QCOMPARE(font.result, SavedMarkdownFont::Result::Failed);
    QCOMPARE(font.pointSize, 12.0);
    QVERIFY(QDir(config.paths.snapshotDirectory).entryList(QDir::Files).isEmpty());
    QVERIFY(QFile::remove(config.mainPath()));
    QVERIFY(QDir().mkpath(config.mainPath()));
    QCOMPARE(readSavedMarkdownFont(config.paths).result, SavedMarkdownFont::Result::Failed);
    QVERIFY(QDir(config.paths.snapshotDirectory).entryList(QDir::Files).isEmpty());
}

void SavedMarkdownFontTest::fontAvailabilityAndGlobalSavedValues()
{
    Config config;
    const QString missing = QStringLiteral("REQ002 Missing Font 31c9e2");
    config.font(13, missing);
    SavedMarkdownFont font = readSavedMarkdownFont(config.paths);
    QVERIFY(font.familyFallback);
    QCOMPARE(font.family, defaultMarkdownFont().family);
    QCOMPARE(font.pointSize, 13.0);
    Config::writeFont(config.paths.userConfigRoot + QStringLiteral("/notepad/userstyle/Default/AllGlobal.ini"),
                      QStringLiteral("style0/font"), QStringLiteral("Wrong Global"), 35);
    config.font(21, missing); // Final saved Markdown result of global size-only update.
    font = readSavedMarkdownFont(config.paths);
    QCOMPARE(font.pointSize, 21.0);
    QCOMPARE(font.configuredFamily, missing);
    const QString family = QFontDatabase().families().first();
    config.font(21, family); // Final saved Markdown result of global family-only update.
    font = readSavedMarkdownFont(config.paths);
    QVERIFY(!font.familyFallback);
    QCOMPARE(font.family, family);
    QCOMPARE(font.pointSize, 21.0);
    QVERIFY(QFontDatabase().isFixedPitch(markdownCodeFontFamily(family)));
}

void SavedMarkdownFontTest::openingReadsOnceAndOtherEventsReuse()
{
    Config config;
    config.font(12);
    Fixture fixture(config);
    QCOMPARE(fixture.adapter.reads, 0);
    fixture.open();
    QCOMPARE(fixture.adapter.reads, 1);
    fixture.refresh();
    fixture.open();
    fixture.dock()->show();
    fixture.refresh();
    config.font(18);
    fixture.dock()->setPalette(QPalette(Qt::darkGray));
    fixture.controller.restoreHostFontSize();
    QByteArray html;
    QVERIFY(fixture.exportHtml(&html));
    fixture.window.showMinimized();
    QCoreApplication::processEvents();
    fixture.window.showNormal();
    QTest::qWait(1700);
    QCOMPARE(fixture.adapter.reads, 1);
    QCOMPARE(fixture.controller.readingFont().pointSize, 12.0);
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 12.0);
    fixture.dock()->hide();
    fixture.open();
    QCOMPARE(fixture.adapter.reads, 2);
    QCOMPARE(fixture.controller.readingFont().pointSize, 18.0);
    fixture.dock()->close();
    fixture.dock()->show();
    QCOMPARE(fixture.adapter.reads, 3);
}

void SavedMarkdownFontTest::hiddenExportDoesNotReadConfiguration()
{
    Config config;
    config.font(20);
    Fixture fixture(config);
    fixture.controller.setRefreshMode(RefreshMode::Manual);
    QByteArray html;
    QVERIFY(fixture.exportHtml(&html));
    QCOMPARE(fixture.adapter.reads, 0);
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 12.0);
    QVERIFY(fixture.dock()->isHidden());
    fixture.open();
    QCOMPARE(fixture.adapter.reads, 1);
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 20.0);
    wheel(fixture.editor->textEdit, 120);
    const qreal zoom = fixture.controller.previewZoom();
    fixture.dock()->hide();
    config.font(30);
    fixture.editor->edit();
    QVERIFY(fixture.exportHtml(&html));
    QCOMPARE(fixture.adapter.reads, 1);
    QCOMPARE(fixture.controller.previewZoom(), zoom);
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 20.0 * zoom);
    QVERIFY(fixture.dock()->isHidden());
    QCOMPARE(fixture.controller.previewStatus().mode, RefreshMode::Manual);
}

void SavedMarkdownFontTest::nativeWheelAndSemanticFormats_data()
{
    QTest::addColumn<QString>("markdown");
    QTest::newRow("all-formats") << sample();
    QTest::newRow("empty") << QString();
    QTest::newRow("heading-only") << QStringLiteral("# Only heading");
    QTest::newRow("code-only") << QStringLiteral("```\nOnly code\n```");
}

void SavedMarkdownFontTest::nativeWheelAndSemanticFormats()
{
    QFETCH(QString, markdown);
    Config config;
    config.font(12);
    Fixture fixture(config, markdown);
    fixture.open();
    fixture.refresh();
    QTextEdit *edit = fixture.editor->textEdit;
    QTextEdit native;
    native.setReadOnly(true);
    native.setMarkdown(markdown);
    native.document()->setDefaultFont(edit->document()->defaultFont());
    native.show();
    QTextCursor selection = edit->document()->find(QStringLiteral("H1"));
    if (!selection.isNull()) {
        edit->setTextCursor(selection);
    }
    const int selectionStart = edit->textCursor().selectionStart();
    const int selectionEnd = edit->textCursor().selectionEnd();
    for (int delta : {30, 60, 120, -120, 240, -60}) {
        wheel(&native, delta);
        wheel(edit, delta);
        QCOMPARE(edit->document()->defaultFont().pointSizeF(), native.document()->defaultFont().pointSizeF());
        QCOMPARE(fixture.controller.previewZoom(), native.document()->defaultFont().pointSizeF() / 12.0);
    }
    QCOMPARE(edit->textCursor().selectionStart(), selectionStart);
    QCOMPARE(edit->textCursor().selectionEnd(), selectionEnd);
    QCOMPARE(fixture.editor->renders, 1);
    QCOMPARE(fixture.adapter.reads, 1);
    const qreal points = edit->document()->defaultFont().pointSizeF();
    if (markdown == sample()) {
        const qreal expected[] = {2.0, 1.75, 1.50, 1.30, 1.15, 1.05};
        for (int level = 1; level <= 6; ++level) {
            const auto format = formatOf(edit, QStringLiteral("H%1").arg(level));
            QCOMPARE(format.fontPointSize(), points * expected[level - 1]);
            QVERIFY(format.fontWeight() >= QFont::Bold);
        }
        for (const QString &word : {QStringLiteral("Body"), QStringLiteral("Quote"), QStringLiteral("List item"), QStringLiteral("Cell")}) {
            QCOMPARE(formatOf(edit, word).fontPointSize(), points);
        }
        QVERIFY(formatOf(edit, QStringLiteral("bold")).fontWeight() >= QFont::Bold);
        QVERIFY(formatOf(edit, QStringLiteral("italic")).fontItalic());
        QVERIFY(formatOf(edit, QStringLiteral("strike")).fontStrikeOut());
        QVERIFY(formatOf(edit, QStringLiteral("link")).isAnchor());
        QVERIFY(formatOf(edit, QStringLiteral("Header")).fontWeight() >= QFont::Bold);
        for (const QString &code : {QStringLiteral("inline code"), QStringLiteral("block code"), QStringLiteral("heading code")}) {
            const auto format = formatOf(edit, code);
            QVERIFY2(format.fontFixedPitch(), qPrintable(code));
            QVERIFY(QFontDatabase().isFixedPitch(format.fontFamily()));
            QCOMPARE(format.fontPointSize(), code == QStringLiteral("heading code") ? points * 2.0 : points);
        }
    }
    fixture.refresh();
    QCOMPARE(edit->document()->defaultFont().pointSizeF(), points);
    fixture.controller.restoreHostFontSize();
    QCOMPARE(edit->document()->defaultFont().pointSizeF(), 12.0);
    QCOMPARE(fixture.adapter.reads, 1);
}

void SavedMarkdownFontTest::reopenKeepsZoomAndRefreshPreservesRatios()
{
    Config config;
    config.font(12);
    Fixture fixture(config);
    fixture.open();
    fixture.refresh();
    wheel(fixture.editor->textEdit, 360);
    QCOMPARE(fixture.controller.previewZoom(), 1.25);
    QCOMPARE(formatOf(fixture.editor->textEdit, QStringLiteral("H4")).fontPointSize(), 19.5);
    config.font(14);
    fixture.refresh();
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 15.0);
    const int renders = fixture.editor->renders;
    const auto status = fixture.controller.previewStatus();
    fixture.dock()->hide();
    fixture.open();
    QCOMPARE(fixture.adapter.reads, 2);
    QCOMPARE(fixture.controller.previewZoom(), 1.25);
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 17.5);
    QCOMPARE(fixture.editor->renders, renders);
    QCOMPARE(fixture.controller.previewStatus().contentVersion, status.contentVersion);
    QCOMPARE(fixture.controller.previewStatus().displayedVersion, status.displayedVersion);
    QPalette dark = fixture.dock()->palette();
    dark.setColor(QPalette::Base, Qt::black);
    fixture.dock()->setPalette(dark);
    QTest::qWait(40);
    QCOMPARE(formatOf(fixture.editor->textEdit, QStringLiteral("H4")).fontPointSize(), 22.75);
    fixture.editor->edit();
    QTRY_COMPARE_WITH_TIMEOUT(fixture.editor->renders, renders + 1, 3000);
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 17.5);
    const auto actions = fixture.menu.actions();
    QAction *restore = nullptr;
    for (QAction *action : actions) {
        if (action->objectName() == QStringLiteral("NddMarkdownRestoreHostFontSize")) {
            restore = action;
        }
    }
    QVERIFY(restore && restore->isEnabled());
    QVERIFY(restore->text().contains(QLatin1Char('&')));
    restore->trigger();
    QCOMPARE(fixture.controller.previewZoom(), 1.0);
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 14.0);
}

void SavedMarkdownFontTest::failureResetsBaseAndOnlyReopenRecovers()
{
    Config config;
    config.font(20);
    Fixture fixture(config);
    fixture.open();
    fixture.refresh();
    wheel(fixture.editor->textEdit, 120);
    const qreal zoom = fixture.controller.previewZoom();
    config.theme(999);
    fixture.dock()->hide();
    fixture.open();
    QCOMPARE(fixture.controller.readingFont().result, SavedMarkdownFont::Result::Failed);
    QCOMPARE(fixture.controller.readingFont().pointSize, 12.0);
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 12.0 * zoom);
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::Ready);
    QCOMPARE(fixture.adapter.reads, 2);
    config.theme(0);
    config.font(16);
    QTest::qWait(1700);
    QCOMPARE(fixture.adapter.reads, 2);
    fixture.controller.restoreHostFontSize();
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 12.0);
    fixture.dock()->hide();
    fixture.open();
    QCOMPARE(fixture.adapter.reads, 3);
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 16.0);
}

void SavedMarkdownFontTest::tabsCacheAndWindowsKeepIndependentState()
{
    Config config;
    config.font(12);
    Fixture first(config);
    Fixture second(config);
    first.open(); first.refresh();
    second.open(); second.refresh();
    wheel(first.editor->textEdit, 360);
    wheel(second.editor->textEdit, 120);
    first.controller.setRefreshMode(RefreshMode::Manual);
    first.sync(false);
    const qreal secondZoom = second.controller.previewZoom();
    config.font(14);
    first.dock()->hide(); first.open();
    QCOMPARE(first.controller.readingFont().pointSize, 14.0);
    QCOMPARE(second.controller.readingFont().pointSize, 12.0);
    for (int index = 0; index < 4; ++index) {
        auto *editor = new QsciScintilla;
        first.tabs->addTab(editor, QString::number(index));
        first.tabs->setCurrentWidget(editor);
        first.refresh();
        QCOMPARE(editor->textEdit->document()->defaultFont().pointSizeF(), 17.5);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    QVERIFY(!first.editor->preview);
    first.tabs->setCurrentWidget(first.editor);
    QCoreApplication::processEvents();
    QVERIFY(!first.editor->preview); // Manual cache miss must not parse.
    first.refresh();
    QCOMPARE(first.editor->textEdit->document()->defaultFont().pointSizeF(), 17.5);
    QCOMPARE(first.adapter.reads, 2);
    first.controller.restoreHostFontSize();
    QCOMPARE(second.controller.previewZoom(), secondZoom);
    QCOMPARE(second.adapter.reads, 1);
    Fixture third(config);
    QCOMPARE(third.controller.previewZoom(), 1.0);
    QCOMPARE(third.adapter.reads, 0);
    third.open();
    QCOMPARE(third.controller.readingFont().pointSize, 14.0);
}

void SavedMarkdownFontTest::staleSnapshotAndUserScrollCancelOldLayout()
{
    Config config;
    config.font(12);
    Fixture fixture(config, sample() + QStringLiteral("Paragraph for reading.\n\n").repeated(300));
    fixture.open(); fixture.refresh();
    QTest::qWait(100);
    fixture.controller.setRefreshMode(RefreshMode::Manual);
    fixture.editor->edit();
    const auto status = fixture.controller.previewStatus();
    const int renders = fixture.editor->renders;
    QScrollBar *bar = fixture.editor->textEdit->verticalScrollBar();
    bar->setValue(qRound(bar->maximum() * 0.6));
    const int editorPosition = fixture.editor->verticalScrollBar()->value();
    wheel(fixture.editor->textEdit, 120);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(fixture.dock()->scrollRatio() - 0.6) < 0.01, 1000);
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::Pending);
    QCOMPARE(fixture.controller.previewStatus().displayedVersion, status.displayedVersion);
    QCOMPARE(fixture.editor->renders, renders);
    QCOMPARE(fixture.editor->verticalScrollBar()->value(), editorPosition);
    wheel(fixture.editor->textEdit, 120);
    bar->setPageStep(qMax(1, bar->maximum() / 3));
    bar->triggerAction(QAbstractSlider::SliderPageStepSub);
    const double userRatio = fixture.dock()->scrollRatio();
    QVERIFY(userRatio < 0.5);
    fixture.window.resize(1100, 750);
    QTest::qWait(120);
    QVERIFY(fixture.dock()->scrollRatio() < 0.5);
    QCOMPARE(fixture.editor->verticalScrollBar()->value(), editorPosition);
    fixture.sync(false);
    fixture.dock()->setPalette(QPalette(Qt::darkGray));
    fixture.controller.restoreHostFontSize();
    bar->triggerAction(QAbstractSlider::SliderToMinimum);
    QTest::qWait(100);
    QVERIFY(fixture.dock()->scrollRatio() < 0.01);
}

void SavedMarkdownFontTest::consecutiveStylesAndDestroyedPreviewCancelOldWork()
{
    Config config;
    config.font(12);
    Fixture fixture(config, QStringLiteral("Paragraph\n\n").repeated(300));
    fixture.open(); fixture.refresh(); fixture.sync(false);
    QTest::qWait(100);
    wheel(fixture.editor->textEdit, 120);
    fixture.controller.restoreHostFontSize();
    auto *other = new QsciScintilla(QStringLiteral("# Other\n\n") + QStringLiteral("Paragraph\n\n").repeated(100));
    fixture.tabs->addTab(other, QStringLiteral("B"));
    fixture.tabs->setCurrentWidget(other);
    fixture.refresh();
    other->textEdit->verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMinimum);
    QTest::qWait(100);
    QVERIFY(fixture.dock()->scrollRatio() < 0.01);
    QCOMPARE(other->textEdit->document()->defaultFont().pointSizeF(), 12.0);
    fixture.controller.setRefreshMode(RefreshMode::Manual);
    wheel(other->textEdit, 120);
    delete other->preview.data();
    QTest::qWait(100);
    QVERIFY(!fixture.controller.previewStatus().displayedEditor);
    QCOMPARE(fixture.adapter.reads, 1);
}

void SavedMarkdownFontTest::synchronizedFontChangeKeepsEditorInControl()
{
    Config config;
    config.font(12);
    Fixture fixture(config, QStringLiteral("Paragraph for layout\n\n").repeated(300));
    fixture.open(); fixture.refresh();
    QTest::qWait(100);
    fixture.editor->verticalScrollBar()->setValue(70);
    QTRY_VERIFY(qAbs(fixture.dock()->scrollRatio() - 0.7) < 0.01);
    const int renders = fixture.editor->renders;
    wheel(fixture.editor->textEdit, 120);
    QTRY_VERIFY(qAbs(fixture.dock()->scrollRatio() - 0.7) < 0.01);
    QCOMPARE(fixture.editor->verticalScrollBar()->value(), 70);
    QCOMPARE(fixture.editor->renders, renders);
    fixture.controller.setRefreshMode(RefreshMode::Manual);
    fixture.editor->edit();
    fixture.editor->textEdit->verticalScrollBar()->setValue(
        qRound(fixture.editor->textEdit->verticalScrollBar()->maximum() * 0.3));
    wheel(fixture.editor->textEdit, 120);
    fixture.refresh(); // Cancel a stale snapshot's queued preservation target.
    QTRY_VERIFY(qAbs(fixture.dock()->scrollRatio() - 0.7) < 0.01);
    fixture.editor->verticalScrollBar()->setValue(40);
    QTRY_VERIFY(qAbs(fixture.dock()->scrollRatio() - 0.4) < 0.01);
    fixture.controller.restoreHostFontSize();
    QTRY_VERIFY(qAbs(fixture.dock()->scrollRatio() - 0.4) < 0.01);
}

void SavedMarkdownFontTest::savedEditorZoomAndUnrelatedStylesAreIgnored()
{
    Config config;
    config.font(12);
    {
        QSettings host(config.stylePath(), QSettings::IniFormat);
        host.setValue(QStringLiteral("Scintilla/Markdown/zoom"), 9);
        host.setValue(QStringLiteral("Scintilla/Markdown/style1/font"), fields(QStringLiteral("Heading font"), 35));
        host.setValue(QStringLiteral("Scintilla/CPP/style0/font"), fields(QStringLiteral("CPP font"), 26));
        host.sync();
    }
    Fixture fixture(config);
    fixture.editor->setFont(QFont(QStringLiteral("Wrong Editor Font"), 40));
    fixture.editor->setProperty("language", QStringLiteral("cpp"));
    fixture.open(); fixture.refresh();
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 12.0);
    fixture.editor->setFont(QFont(QStringLiteral("Wrong Editor Font"), 50));
    wheel(fixture.editor->textEdit, -120, Qt::NoModifier);
    QCOMPARE(fixture.controller.previewZoom(), 1.0);
    fixture.dock()->hide(); fixture.open();
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 12.0);
}

void SavedMarkdownFontTest::manualContextOpenAndPendingReopenDoNotParse()
{
    Config config;
    config.font(12);
    Fixture fixture(config);
    fixture.controller.setRefreshMode(RefreshMode::Manual);
    QMenu context(fixture.editor);
    QAction *action = context.addAction(QStringLiteral("Markdown"));
    QObject::connect(action, &QAction::triggered, fixture.editor, &QsciScintilla::on_viewMarkdown);
    context.show();
    QCoreApplication::processEvents();
    action->trigger();
    QCOMPARE(fixture.adapter.reads, 1);
    QCOMPARE(fixture.editor->renders, 0);
    action->trigger();
    QCOMPARE(fixture.adapter.reads, 1);
    fixture.refresh();
    fixture.editor->edit();
    const PreviewStatus before = fixture.controller.previewStatus();
    config.font(16);
    fixture.dock()->hide(); fixture.open();
    QCOMPARE(fixture.adapter.reads, 2);
    QCOMPARE(fixture.editor->renders, 1);
    QCOMPARE(fixture.controller.previewStatus().state, before.state);
    QCOMPARE(fixture.controller.previewStatus().displayedVersion, before.displayedVersion);
    QCOMPARE(fixture.editor->textEdit->document()->defaultFont().pointSizeF(), 16.0);
    QByteArray html;
    QVERIFY(fixture.exportHtml(&html));
    QVERIFY(html.contains("Edited"));
    QCOMPARE(fixture.adapter.reads, 2);
    QCOMPARE(fixture.controller.previewStatus().mode, RefreshMode::Manual);
}

void SavedMarkdownFontTest::invalidStyleInputDoesNotMutateDocument()
{
    Config config;
    config.font(12);
    Fixture fixture(config);
    fixture.open(); fixture.refresh();
    const QFont font = fixture.editor->textEdit->document()->defaultFont();
    const QString before = fixture.editor->textEdit->toHtml();
    fixture.dock()->setReadingFont(font, qQNaN());
    fixture.dock()->setReadingFont(font, qInf());
    fixture.dock()->setReadingFont(font, -1.0);
    fixture.dock()->setReadingFont(font, 0.0);
    QCOMPARE(fixture.editor->textEdit->toHtml(), before);
    const int revision = fixture.editor->textEdit->document()->revision();
    const auto status = fixture.controller.previewStatus();
    for (int i = 0; i < 3; ++i) {
        fixture.dock()->refreshDocumentStyle(fixture.editor, status.displayedVersion);
    }
    QCOMPARE(fixture.editor->textEdit->document()->revision(), revision);
    QCOMPARE(fixture.editor->renders, 1);
}

void SavedMarkdownFontTest::importedCodeWithUnsetFixedPitchRemainsMonospaced()
{
    MarkdownPreviewDock dock;
    QWidget editor;
    auto *preview = new QWidget;
    auto *layout = new QVBoxLayout(preview);
    auto *edit = new QTextEdit(preview);
    edit->setReadOnly(true);
    layout->addWidget(edit);
    edit->setMarkdown(sample());
    // Reproduce the Windows Qt 5.15.2 importer without relying on the Linux
    // font database to happen to construct the same system FixedFont QFont.
    for (const QString &code : {QStringLiteral("inline code"), QStringLiteral("heading code")}) {
        QTextCursor cursor = edit->document()->find(code);
        QFont nativeCode = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        nativeCode.setFixedPitch(false);
        QTextCharFormat format;
        format.setFont(nativeCode);
        cursor.setCharFormat(format);
    }
    const QString bodyFamily = markdownCodeFontFamily(defaultMarkdownFont().family);
    dock.setReadingFont(QFont(bodyFamily, 12), 1.0);
    QVERIFY(dock.adoptNativePreview(preview, edit, QStringLiteral("sample.md"), &editor, 1));
    dock.show();
    for (const QString &code : {QStringLiteral("inline code"), QStringLiteral("heading code")}) {
        const auto format = formatOf(edit, code);
        QVERIFY(format.fontFixedPitch());
        QVERIFY(QFontDatabase().isFixedPitch(format.font().family()));
        QCOMPARE(format.font().family(), format.fontFamily());
    }
    QVERIFY(!formatOf(edit, QStringLiteral("Body")).fontFixedPitch());
    QVERIFY(formatOf(edit, QStringLiteral("Body")).background().style() == Qt::NoBrush);
    wheel(edit, 120);
    dock.setReadingFont(QFont(bodyFamily, 14), 1.25);
    QCOMPARE(formatOf(edit, QStringLiteral("heading code")).fontPointSize(), 35.0);
    QVERIFY(formatOf(edit, QStringLiteral("inline code")).fontFixedPitch());
    QVERIFY(!formatOf(edit, QStringLiteral("Body")).fontFixedPitch());
}

void SavedMarkdownFontTest::fontScreenshots()
{
    const QString output = qEnvironmentVariable("MARKDOWNVIEW_SCREENSHOT_DIR");
    if (output.isEmpty()) {
        return;
    }
    QVERIFY(QDir().mkpath(output));
    Config config;
    config.font(12);
    Fixture fixture(config);
    fixture.open(); fixture.refresh(); fixture.sync(false);
    QTest::qWait(100);
    QVERIFY(fixture.window.grab().save(QDir(output).filePath(QStringLiteral("req002-font-100.png"))));
    wheel(fixture.editor->textEdit, 360);
    QTest::qWait(100);
    QVERIFY(fixture.window.grab().save(QDir(output).filePath(QStringLiteral("req002-font-125.png"))));
    QPalette dark = fixture.dock()->palette();
    dark.setColor(QPalette::Base, QColor(QStringLiteral("#202124")));
    dark.setColor(QPalette::Text, QColor(QStringLiteral("#f1f3f4")));
    dark.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#303134")));
    dark.setColor(QPalette::Mid, QColor(QStringLiteral("#5f6368")));
    dark.setColor(QPalette::Midlight, QColor(QStringLiteral("#5f6368")));
    dark.setColor(QPalette::Link, QColor(QStringLiteral("#8ab4f8")));
    fixture.dock()->setPalette(dark);
    QTest::qWait(100);
    QVERIFY(fixture.window.grab().save(QDir(output).filePath(QStringLiteral("req002-font-dark.png"))));
}

QTEST_MAIN(SavedMarkdownFontTest)
#include "saved_markdown_font_test.moc"
