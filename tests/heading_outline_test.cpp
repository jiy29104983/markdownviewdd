#include "heading_index.h"
#include "heading_outline.h"
#include "host_adapter.h"
#include "markdown_preview_dock.h"
#include "preview_controller.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QScrollBar>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtTest>
#include <algorithm>
#include <memory>

namespace {
QString sample()
{
    return QStringList{QStringLiteral("引言 😀"), QString(), QStringLiteral("# 一级"),
        QStringLiteral("正文"), QString(), QStringLiteral("### 跳级"), QString(),
        QStringLiteral("## **重复** `code`"), QString(), QStringLiteral("```markdown"),
        QStringLiteral("## **重复** `code`"), QStringLiteral("```"), QString(),
        QStringLiteral("## **重复** `code`"), QString(), QStringLiteral("Setext _标题_"),
        QStringLiteral("==="), QString(), QStringLiteral("###### 六级"), QString(),
        QStringLiteral("## # 字面井号")}.join(QLatin1Char('\n'));
}

QString longSample(int count = 12)
{
    QString text = QStringLiteral("引言\n\n");
    for (int i = 0; i < count; ++i) {
        text += QStringLiteral("## 章节 %1\n\n").arg(i);
        text += QStringLiteral("正文包含中文、图片说明和很长的段落内容。\n\n").repeated(12);
    }
    return text;
}

class OutlineAdapter final : public HostAdapter
{
public:
    explicit OutlineAdapter(QTabWidget *tabs) : tabs(tabs) {}
    struct Cache { QPointer<QWidget> window; QPointer<QTextEdit> preview; bool current = false; };
    QTabWidget *tabs;
    QHash<QWidget *, Cache> cache;
    int renders = 0;
    mutable int reads = 0;
    int navigations = 0;
    int lastLine = -1;
    bool readable = true;
    bool navigable = true;
    std::function<void()> duringRead;
    std::function<void()> duringNavigate;
    QWidget *currentEditor() const override { return tabs->currentWidget(); }
    QMetaObject::Connection connectActiveEditorChanged(QObject *context, std::function<void()> fn) override
    { return QObject::connect(tabs, &QTabWidget::currentChanged, context, [fn]() { fn(); }); }
    ScrollConnections connectEditorScrollChanged(QWidget *editor, QObject *context, std::function<void()> fn) override
    {
        auto *bar = qobject_cast<QTextEdit *>(editor)->verticalScrollBar();
        return {QObject::connect(bar, &QScrollBar::valueChanged, context, [fn]() { fn(); }),
                QObject::connect(bar, &QScrollBar::rangeChanged, context, [fn]() { fn(); })};
    }
    QString filePath(QWidget *editor) const override { return editor->property("filePath").toString(); }
    bool isFilePathChangeEvent(QEvent *event) const override { return event->type() == QEvent::DynamicPropertyChange; }
    bool isEditorContextMenu(QMenu *, QWidget *) const override { return false; }
    bool isMarkdownContextAction(QAction *) const override { return false; }
    bool bridgeMarkdownContextAction(QAction *, QWidget *) override { return false; }
    PreviewResult ensurePreview(QWidget *editor) override
    {
        Cache &item = cache[editor];
        const bool created = !item.window;
        if (created) {
            item.window = new QWidget(editor);
            auto *layout = new QVBoxLayout(item.window);
            item.preview = new QTextEdit(item.window);
            item.preview->setReadOnly(true);
            layout->addWidget(item.preview);
            item.preview->setMarkdown(qobject_cast<QTextEdit *>(editor)->toPlainText());
            ++renders;
        }
        return {item.window, item.preview, QString(), 0, created};
    }
    bool refreshPreview(QWidget *editor, qint64 *, QString *) override
    {
        cache[editor].preview->setMarkdown(qobject_cast<QTextEdit *>(editor)->toPlainText());
        ++renders;
        return true;
    }
    bool disconnectImmediateRefresh(QWidget *, bool) override { return true; }
    bool previewIsCurrent(QWidget *editor) const override { return cache.value(editor).current; }
    void setPreviewCurrent(QWidget *editor, bool current) override { cache[editor].current = current; }
    bool releasePreview(QWidget *editor, QString *) override
    {
        if (cache[editor].window) cache[editor].window->deleteLater();
        cache.remove(editor);
        return true;
    }
    SourceReadResult readSource(QWidget *editor) const override
    {
        ++reads;
        const QString text = qobject_cast<QTextEdit *>(editor)->toPlainText();
        if (duringRead) duringRead();
        return {text, readable ? QString() : QStringLiteral("源码读取不可用"), readable};
    }
    SourceNavigationResult navigateSource(QWidget *editor, int line, int offset,
        const std::function<bool()> &valid) override
    {
        if (duringNavigate) duringNavigate();
        if (!valid()) return {false, QStringLiteral("expired")};
        if (!navigable) return {false, QStringLiteral("源码定位不可用")};
        auto *edit = qobject_cast<QTextEdit *>(editor);
        const QTextBlock block = edit->document()->findBlockByNumber(line);
        if (!block.isValid() || edit->toPlainText().left(block.position()).toUcs4().size() != offset)
            return {false, QStringLiteral("wrong source position")};
        ++navigations;
        lastLine = line;
        QTextCursor cursor(block);
        QScrollBar *bar = edit->verticalScrollBar();
        bar->setValue(bar->value() + edit->cursorRect(cursor).top());
        return {valid(), QString()};
    }
};

struct Fixture
{
    QMainWindow window;
    QTabWidget *tabs = new QTabWidget(&window);
    OutlineAdapter adapter{tabs};
    std::unique_ptr<PreviewController> controller;
    MarkdownPreviewDock *dock = nullptr;
    QTextEdit *editor = nullptr;
    explicit Fixture(const QString &text = longSample())
    {
        window.setCentralWidget(tabs);
        editor = add(text);
        controller.reset(new PreviewController(&window, &adapter));
        dock = window.findChild<MarkdownPreviewDock *>();
        window.resize(1100, 650);
        window.show();
        dock->show();
        refresh();
        QCoreApplication::processEvents();
    }
    QTextEdit *add(const QString &text)
    {
        auto *edit = new QTextEdit;
        edit->setPlainText(text);
        edit->setProperty("filePath", QStringLiteral("outline-%1.md").arg(tabs->count()));
        tabs->addTab(edit, QStringLiteral("Document %1").arg(tabs->count()));
        return edit;
    }
    void refresh() { QMetaObject::invokeMethod(controller.get(), "renderNow", Qt::DirectConnection); }
    void activate(int index) { dock->headingActivated(dock->headings().at(index)); }
    QTextEdit *preview() { return adapter.cache.value(tabs->currentWidget()).preview; }
    QTreeWidget *tree() { return dock->findChild<QTreeWidget *>(); }
};
}

class HeadingOutlineTest final : public QObject
{
    Q_OBJECT
private slots:
    void sourceMapping_data();
    void sourceMapping();
    void sourceMappingFailsClosed();
    void repositoryExampleMaps();
    void mappingEdgeCases_data();
    void mappingEdgeCases();
    void imageTableLayoutAndAllLevels();
    void renderReadModificationInvalidatesSnapshot();
    void hierarchyAndIndependentSelection();
    void twoSidedNavigationSurvivesLayoutAndPolling();
    void staleSnapshotOnlyNavigatesPreview();
    void statusUsesVersions();
    void layoutAndWindowIsolation();
    void snapshotsInvalidateAndRestoreExpansion();
    void missingCapabilityAndReentrantNavigation();
    void sourceReadReentrancy();
    void keyboardAndSectionHighlight();
    void sourceUserScrollResumesSync();
    void nullSourceCapability();
    void delayedSourceLayoutKeepsHeading();
    void layoutDoesNotReviveOldSourceTarget();
    void queuedLayoutCannotReplaceUserScroll();
    void fiveHundredHeadings();
};

void HeadingOutlineTest::sourceMapping_data()
{
    QTest::addColumn<QString>("eol");
    QTest::newRow("LF") << QStringLiteral("\n");
    QTest::newRow("CRLF") << QStringLiteral("\r\n");
    QTest::newRow("CR") << QStringLiteral("\r");
}

void HeadingOutlineTest::sourceMapping()
{
    QFETCH(QString, eol);
    QString source = sample();
    source.replace(QStringLiteral("\n"), eol);
    QTextDocument document;
    document.setMarkdown(source);
    QWidget editor;
    auto records = renderedHeadings(&document, &editor, 7);
    QCOMPARE(records.size(), 7);
    QVERIFY(mapHeadingSource(&records, source));
    const QVector<int> expectedLines{2, 5, 7, 13, 15, 18, 20};
    const QVector<int> expectedLevels{1, 3, 2, 2, 1, 6, 2};
    const auto sourceLines = source.split(eol);
    for (int i = 0; i < records.size(); ++i) {
        QCOMPARE(records.at(i).sourceLine, expectedLines.at(i));
        QCOMPARE(records.at(i).level, expectedLevels.at(i));
        QCOMPARE(records.at(i).editor.data(), &editor);
        QCOMPARE(records.at(i).version, quint64(7));
        const QString prefix = sourceLines.mid(0, expectedLines.at(i)).join(eol) + eol;
        QCOMPARE(records.at(i).sourceOffset, prefix.toUcs4().size());
    }
    QCOMPARE(records.at(2).text, QStringLiteral("重复 code"));
    QCOMPARE(records.at(3).text, QStringLiteral("重复 code"));
    QVERIFY(records.at(2).blockPosition != records.at(3).blockPosition);
}

void HeadingOutlineTest::sourceMappingFailsClosed()
{
    QWidget editor;
    QTextDocument document;
    document.setMarkdown(QStringLiteral("# A\n\n## A"));
    auto records = renderedHeadings(&document, &editor, 2);
    QVERIFY(!mapHeadingSource(&records, QStringLiteral("# A\n\n```\n## A\n```")));
    for (const auto &record : records) QCOMPARE(record.sourceLine, -1);
    QVERIFY(!mapHeadingSource(&records, QStringLiteral("# B\n\n## A")));
    QVERIFY(mapHeadingSource(&records, QStringLiteral("~~~\n# fake\n~~~\n\n# A\n\n## A")));
    QCOMPARE(records.first().sourceLine, 4);
    QCOMPARE(records.last().sourceLine, 6);
}

void HeadingOutlineTest::hierarchyAndIndependentSelection()
{
    HeadingOutline outline;
    QWidget editor;
    QTextDocument document;
    document.setMarkdown(sample());
    outline.setSnapshot(renderedHeadings(&document, &editor, 1), &editor, 1);
    auto *tree = outline.findChild<QTreeWidget *>();
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->childCount(), 3);
    auto *parent = tree->topLevelItem(0);
    auto *child = parent->child(2);
    tree->setCurrentItem(tree->topLevelItem(1));
    auto *selected = tree->currentItem();
    parent->setExpanded(false);
    outline.setCurrentBlock(outline.headings().at(3).blockPosition);
    QCOMPARE(tree->currentItem(), selected);
    QVERIFY(!parent->isExpanded());
    QCOMPARE(parent->text(1), QStringLiteral("◌"));
    QCOMPARE(outline.currentHeading(), 3);
    parent->setExpanded(true);
    QCOMPARE(child->text(1), QStringLiteral("●"));
}

void HeadingOutlineTest::twoSidedNavigationSurvivesLayoutAndPolling()
{
    Fixture f;
    QCOMPARE(f.dock->headings().size(), 12);
    const int renders = f.adapter.renders;
    QMetaObject::invokeMethod(f.controller.get(), "setSyncScrolling", Q_ARG(bool, false));
    const QString source = f.editor->toPlainText();
    QTextCursor selected(f.editor->document());
    selected.setPosition(1); selected.setPosition(5, QTextCursor::KeepAnchor);
    f.editor->setTextCursor(selected);
    const auto target = f.dock->headings().at(8);
    f.activate(8);
    QCOMPARE(f.adapter.navigations, 1);
    QCOMPARE(f.adapter.lastLine, 2 + 8 * 26);
    QVERIFY(f.dock->hasNavigationTarget());
    f.window.resize(970, 620);
    QTest::qWait(80);
    QMetaObject::invokeMethod(f.controller.get(), "pollEditor");
    QTextCursor previewCursor(f.preview()->document()); previewCursor.setPosition(target.blockPosition);
    QVERIFY(qAbs(f.preview()->cursorRect(previewCursor).top()) <= 2);
    QTextCursor sourceCursor(f.editor->document()->findBlockByNumber(target.sourceLine));
    QVERIFY(qAbs(f.editor->cursorRect(sourceCursor).top()) <= 2);
    QCOMPARE(f.editor->toPlainText(), source);
    QCOMPARE(f.editor->textCursor().selectionStart(), 1);
    QCOMPARE(f.editor->textCursor().selectionEnd(), 5);
    QCOMPARE(f.adapter.renders, renders);
    QMetaObject::invokeMethod(f.controller.get(), "setSyncScrolling", Q_ARG(bool, true));
    QMetaObject::invokeMethod(f.controller.get(), "pollEditor");
    QVERIFY(qAbs(f.preview()->cursorRect(previewCursor).top()) <= 2);
}

void HeadingOutlineTest::staleSnapshotOnlyNavigatesPreview()
{
    Fixture f;
    f.controller->setRefreshMode(RefreshMode::Manual);
    const int renders = f.adapter.renders;
    f.editor->append(QStringLiteral("changed"));
    const int sourceScroll = f.editor->verticalScrollBar()->value();
    f.activate(8);
    QTest::qWait(80);
    QCOMPARE(f.adapter.navigations, 0);
    QCOMPARE(f.adapter.renders, renders);
    QCOMPARE(f.editor->verticalScrollBar()->value(), sourceScroll);
    QVERIFY(f.dock->findChild<QLabel *>(QStringLiteral("NddMarkdownOutlineStatus"))->text().contains(QStringLiteral("待刷新")));
    QVERIFY(f.dock->findChild<QLabel *>(QStringLiteral("NddMarkdownLinkFeedback"))->text().contains(QStringLiteral("请先刷新")));
    f.refresh();
    f.activate(8);
    QCOMPARE(f.adapter.navigations, 1);
}

void HeadingOutlineTest::statusUsesVersions()
{
    Fixture f;
    f.controller->setRefreshMode(RefreshMode::Manual);
    auto *label = f.dock->findChild<QLabel *>(QStringLiteral("NddMarkdownOutlineStatus"));
    QVERIFY(label->text().isEmpty());
    PreviewStatus status = f.controller->previewStatus();
    status.performanceProtected = true;
    status.state = PreviewState::Paused;
    f.dock->setOutlineStatus(status);
    QVERIFY(label->text().isEmpty());
    ++status.contentVersion;
    f.dock->setOutlineStatus(status);
    QVERIFY(label->text().contains(QStringLiteral("待刷新")));
}

void HeadingOutlineTest::layoutAndWindowIsolation()
{
    Fixture first;
    Fixture second;
    const int renders = first.adapter.renders;
    first.activate(6);
    const auto heading = first.dock->headings().at(6);
    first.dock->findChild<QAction *>(QStringLiteral("NddMarkdownOutlineRight"))->trigger();
    auto *splitter = first.dock->findChild<QSplitter *>();
    QCOMPARE(splitter->indexOf(first.dock->findChild<HeadingOutline *>()), 1);
    QCOMPARE(second.dock->findChild<QSplitter *>()->indexOf(second.dock->findChild<HeadingOutline *>()), 0);
    auto *visible = first.dock->findChild<QAction *>(QStringLiteral("NddMarkdownOutlineVisible"));
    visible->setChecked(false);
    QVERIFY(first.dock->findChild<HeadingOutline *>()->isHidden());
    QVERIFY(!second.dock->findChild<HeadingOutline *>()->isHidden());
    visible->setChecked(true);
    splitter->setSizes({350, 220});
    const int previousWidth = first.dock->findChild<HeadingOutline *>()->width();
    first.dock->findChild<QAction *>(QStringLiteral("NddMarkdownOutlineWider"))->trigger();
    QVERIFY(first.dock->findChild<HeadingOutline *>()->width() > previousWidth);
    QTest::qWait(80);
    QTextCursor cursor(first.preview()->document()); cursor.setPosition(heading.blockPosition);
    QVERIFY(qAbs(first.preview()->cursorRect(cursor).top()) <= 2);
    QCOMPARE(first.adapter.renders, renders);
    QCOMPARE(first.adapter.reads, 1);
}

void HeadingOutlineTest::snapshotsInvalidateAndRestoreExpansion()
{
    Fixture f(sample());
    f.controller->setRefreshMode(RefreshMode::Manual);
    f.tree()->topLevelItem(0)->setExpanded(false);
    const auto old = f.dock->headings().first();
    QTextEdit *other = f.add(QStringLiteral("no headings"));
    f.tabs->setCurrentWidget(other);
    QMetaObject::invokeMethod(f.controller.get(), "pollEditor");
    QVERIFY(f.dock->headings().isEmpty());
    f.dock->headingActivated(old);
    QCOMPARE(f.adapter.navigations, 0);
    f.refresh();
    QVERIFY(f.dock->headings().isEmpty());
    QVERIFY(f.dock->findChild<QLabel *>(QStringLiteral("NddMarkdownOutlineStatus"))->text().contains(QStringLiteral("没有标题")));
    f.tabs->setCurrentWidget(f.editor);
    QMetaObject::invokeMethod(f.controller.get(), "pollEditor");
    QVERIFY(!f.tree()->topLevelItem(0)->isExpanded());
    f.editor->setPlainText(sample().replace(QStringLiteral("一级"), QStringLiteral("新标题")));
    f.refresh();
    QVERIFY(f.tree()->topLevelItem(0)->isExpanded());
    QVERIFY(!f.dock->navigateHeading(old));
    delete f.editor;
    f.editor = nullptr;
    QCoreApplication::processEvents();
    QVERIFY(f.dock->headings().isEmpty());
}

void HeadingOutlineTest::missingCapabilityAndReentrantNavigation()
{
    Fixture f;
    f.adapter.navigable = false;
    f.activate(4);
    QCOMPARE(f.adapter.navigations, 0);
    QVERIFY(f.dock->findChild<QLabel *>(QStringLiteral("NddMarkdownLinkFeedback"))->text().contains(QStringLiteral("不可用")));
    f.adapter.navigable = true;
    f.controller->setRefreshMode(RefreshMode::Manual);
    QTextEdit *other = f.add(QStringLiteral("# other"));
    f.adapter.duringNavigate = [&]() { f.tabs->setCurrentWidget(other); };
    f.activate(6);
    QCOMPARE(f.adapter.navigations, 0);
    QVERIFY(f.dock->headings().isEmpty());
    f.tabs->setCurrentWidget(f.editor);
    QMetaObject::invokeMethod(f.controller.get(), "pollEditor");
    f.adapter.duringNavigate = [&]() { f.editor->append(QStringLiteral("modified")); };
    f.activate(3);
    QCOMPARE(f.adapter.navigations, 0);
}

void HeadingOutlineTest::sourceReadReentrancy()
{
    Fixture f;
    f.controller->setRefreshMode(RefreshMode::Manual);
    QTextEdit *other = f.add(QStringLiteral("# Other"));
    f.adapter.duringRead = [&]() { f.tabs->setCurrentWidget(other); };
    f.refresh();
    QVERIFY(f.dock->headings().isEmpty());
    QCOMPARE(f.controller->previewStatus().activeEditor.data(), other);
}

void HeadingOutlineTest::keyboardAndSectionHighlight()
{
    Fixture f;
    auto *tree = f.tree();
    tree->setCurrentItem(tree->topLevelItem(5));
    tree->setFocus();
    QTest::keyClick(tree, Qt::Key_Return);
    QCOMPARE(f.adapter.navigations, 1);
    auto *selected = tree->currentItem();
    auto *outline = f.dock->findChild<HeadingOutline *>();
    f.preview()->verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMinimum);
    QTRY_VERIFY(!f.dock->hasNavigationTarget());
    QTRY_COMPARE(outline->currentHeading(), -1);
    f.preview()->verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMaximum);
    QTRY_COMPARE(outline->currentHeading(), 11);
    QCOMPARE(tree->currentItem(), selected);
    QVERIFY(tree->hasFocus());
}

void HeadingOutlineTest::sourceUserScrollResumesSync()
{
    Fixture f;
    f.activate(7);
    QVERIFY(f.dock->hasNavigationTarget());
    f.editor->setFocus();
    QTest::keyClick(f.editor, Qt::Key_PageDown);
    QTest::qWait(60);
    QVERIFY(!f.dock->hasNavigationTarget());
    f.activate(6);
    QMetaObject::invokeMethod(f.controller.get(), "setSyncScrolling", Q_ARG(bool, false));
    QTest::keyClick(f.editor, Qt::Key_PageDown);
    QVERIFY(f.dock->hasNavigationTarget());
}

void HeadingOutlineTest::nullSourceCapability()
{
    QVERIFY(!readAccessibleSource(nullptr).available);
    QTextEdit unsupported;
    QVERIFY(!readAccessibleSource(&unsupported).available);
    QVERIFY(!navigateAccessibleSource(nullptr, 0, 0, []() { return true; }).reached);
    QVERIFY(!navigateAccessibleSource(&unsupported, 0, 0, []() { return false; }).reached);
    Fixture f;
    f.adapter.readable = false;
    f.refresh();
    QVERIFY(!f.dock->headings().isEmpty());
    f.activate(3);
    QCOMPARE(f.adapter.navigations, 0);
}

void HeadingOutlineTest::fiveHundredHeadings()
{
    QString text;
    for (int i = 0; i < 500; ++i)
        text += QStringLiteral("## 标题 %1\n\n段落内容\n\n").arg(i);
    Fixture f(text);
    QCOMPARE(f.dock->headings().size(), 500);
    QList<double> indexTimes;
    HeadingOutline measuredOutline;
    for (int i = -1; i < 20; ++i) {
        QElapsedTimer timer; timer.start();
        auto records = renderedHeadings(f.preview()->document(), f.editor,
                                        f.controller->previewStatus().displayedVersion);
        QVERIFY(mapHeadingSource(&records, text));
        measuredOutline.setSnapshot(records, f.editor,
                                    f.controller->previewStatus().displayedVersion);
        if (i >= 0) indexTimes.append(double(timer.nsecsElapsed()) / 1e6);
    }
    std::sort(indexTimes.begin(), indexTimes.end());
    QList<double> times;
    for (int i = -1; i < 20; ++i) {
        QElapsedTimer timer; timer.start();
        f.activate((i + 2) * 17 % 500);
        QCoreApplication::processEvents();
        if (i >= 0) times.append(double(timer.nsecsElapsed()) / 1e6);
    }
    const int renders = f.adapter.renders;
    for (int i = 0; i < 20; ++i) {
        f.preview()->verticalScrollBar()->setValue(i * 20);
        QCoreApplication::processEvents();
    }
    QCOMPARE(f.adapter.renders, renders);
    QCOMPARE(f.adapter.reads, 1);
    QCOMPARE(f.adapter.navigations, 21);
    std::sort(times.begin(), times.end());
    QJsonObject metrics{{QStringLiteral("headings"), 500}, {QStringLiteral("samples"), 20},
        {QStringLiteral("median_ms"), (times.at(9) + times.at(10)) / 2},
        {QStringLiteral("p95_ms"), times.at(18)},
        {QStringLiteral("index_median_ms"), (indexTimes.at(9) + indexTimes.at(10)) / 2},
        {QStringLiteral("index_p95_ms"), indexTimes.at(18)}};
    qInfo().noquote() << QJsonDocument(metrics).toJson(QJsonDocument::Compact);
    f.tree()->setCurrentItem(f.tree()->topLevelItem(6));
    f.activate(6);
    QTRY_COMPARE(f.dock->findChild<HeadingOutline *>()->currentHeading(), 6);
    QCOMPARE(f.tree()->topLevelItem(6)->text(1), QStringLiteral("●"));
    const QString directory = qEnvironmentVariable("MARKDOWNVIEW_SCREENSHOT_DIR");
    if (!directory.isEmpty()) {
        QDir().mkpath(directory);
        QVERIFY(f.window.grab().save(QDir(directory).filePath(QStringLiteral("req003-outline-left.png"))));
        f.dock->findChild<QAction *>(QStringLiteral("NddMarkdownOutlineRight"))->trigger();
        QTest::qWait(100);
        QTRY_COMPARE(f.dock->findChild<HeadingOutline *>()->currentHeading(), 6);
        QCOMPARE(f.tree()->topLevelItem(6)->text(1), QStringLiteral("●"));
        QVERIFY(f.window.grab().save(QDir(directory).filePath(QStringLiteral("req003-outline-right.png"))));
    }
}


void HeadingOutlineTest::mappingEdgeCases_data()
{
    QTest::addColumn<QString>("markdown");
    QTest::addColumn<QVector<int>>("lines");
    QTest::newRow("atx-closing-escape") << QStringLiteral("## **粗体** 与 `代码` ###\n\n## 转义 \\*星号\\*\n") << QVector<int>{0, 2};
    QTest::newRow("fence-longer-close") << QStringLiteral("````md\n# 假标题\n```\n# 仍是假标题\n`````\n\n# 真标题\n") << QVector<int>{6};
    QTest::newRow("quote-and-list") << QStringLiteral("> # 引用标题\n>\n> 内容\n\n- ## 列表标题\n\n# 正文标题\n") << QVector<int>{0, 4, 6};
    QTest::newRow("indented-code") << QStringLiteral("    # 代码内容\n\n# 真标题\n") << QVector<int>{2};
    QTest::newRow("link-inline") << QStringLiteral("## [链接](https://example.invalid) 与 ~~删除~~\n") << QVector<int>{0};
    QTest::newRow("multiline-setext") << QStringLiteral("多行\n标题\n===\n\n# 尾部\n") << QVector<int>{0, 4};
}

void HeadingOutlineTest::mappingEdgeCases()
{
    QFETCH(QString, markdown);
    QFETCH(QVector<int>, lines);
    QWidget editor;
    QTextDocument doc; doc.setMarkdown(markdown);
    auto records = renderedHeadings(&doc, &editor, 1);
    QCOMPARE(records.size(), lines.size());
    QVERIFY(mapHeadingSource(&records, markdown));
    for (int i = 0; i < lines.size(); ++i) QCOMPARE(records.at(i).sourceLine, lines.at(i));
}

void HeadingOutlineTest::imageTableLayoutAndAllLevels()
{
    QString markdown;
    for (int level = 1; level <= 6; ++level) {
        markdown += QString(level, QLatin1Char('#')) + QStringLiteral(" 同名 **标题**\n\n");
        markdown += QStringLiteral("| 表头 | 内容 |\n| --- | --- |\n| A | 很长的表格说明 |\n\n"
            "![图片](missing-local-image.png)\n\n").repeated(20);
    }
    Fixture f(markdown);
    QCOMPARE(f.dock->headings().size(), 6);
    QFont font = f.preview()->font(); font.setPointSizeF(18.0);
    f.dock->setReadingFont(font, 1.3);
    for (int i = 0; i < 6; ++i) {
        QCOMPARE(f.dock->headings().at(i).level, i + 1);
        f.activate(i);
        QCOMPARE(f.adapter.lastLine, i * 122);
        QTest::qWait(40);
        QTextCursor cursor(f.preview()->document());
        cursor.setPosition(f.dock->headings().at(i).blockPosition);
        QVERIFY(f.preview()->cursorRect(cursor).top() >= -2);
        QVERIFY(f.preview()->cursorRect(cursor).top() < f.preview()->viewport()->height());
    }
}

void HeadingOutlineTest::renderReadModificationInvalidatesSnapshot()
{
    Fixture f;
    f.controller->setRefreshMode(RefreshMode::Manual);
    f.editor->setPlainText(QStringLiteral("# Completely different\n\nNew content"));
    f.adapter.duringRead = [&]() { f.editor->append(QStringLiteral("edit while reading")); };
    f.refresh();
    QVERIFY(f.dock->headings().isEmpty());
    QCOMPARE(f.controller->previewStatus().displayedVersion, quint64(0));
}


void HeadingOutlineTest::delayedSourceLayoutKeepsHeading()
{
    Fixture f;
    f.activate(8);
    const auto target = f.dock->headings().at(8);
    QFont font = f.editor->font(); font.setPointSize(19);
    f.editor->setFont(font);
    QTextCursor cursor(f.editor->document()->findBlockByNumber(target.sourceLine));
    QTRY_VERIFY(qAbs(f.editor->cursorRect(cursor).top()) <= 2);
    QVERIFY(f.dock->hasNavigationTarget());
    QCOMPARE(f.adapter.lastLine, target.sourceLine);
}


void HeadingOutlineTest::repositoryExampleMaps()
{
    QFile file(QFINDTESTDATA("../examples/preview-demo.md"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll());
    QTextDocument doc; doc.setMarkdown(text);
    QWidget editor;
    auto records = renderedHeadings(&doc, &editor, 1);
    QVERIFY(!records.isEmpty());
    QVERIFY(mapHeadingSource(&records, text));
}


void HeadingOutlineTest::layoutDoesNotReviveOldSourceTarget()
{
    Fixture f;
    f.activate(8);
    f.preview()->verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMinimum);
    QTest::qWait(60);
    QVERIFY(!f.dock->hasNavigationTarget());
    f.dock->findChild<QAction *>(QStringLiteral("NddMarkdownOutlineRight"))->trigger();
    const int calls = f.adapter.navigations;
    QFont font = f.editor->font(); font.setPointSize(18);
    f.editor->setFont(font);
    QTest::qWait(80);
    QCOMPARE(f.adapter.navigations, calls);
}


void HeadingOutlineTest::queuedLayoutCannotReplaceUserScroll()
{
    Fixture f;
    f.activate(5);
    QScrollBar *bar = f.preview()->verticalScrollBar();
    const int oldPosition = bar->value();
    bar->triggerAction(QAbstractSlider::SliderToMaximum);
    // Simulate a queued compatibility/layout write overtaking value delivery.
    bar->setValue(oldPosition);
    QTRY_COMPARE(bar->value(), bar->maximum());
    QTRY_COMPARE(f.dock->findChild<HeadingOutline *>()->currentHeading(), 11);
}

QTEST_MAIN(HeadingOutlineTest)
#include "heading_outline_test.moc"
