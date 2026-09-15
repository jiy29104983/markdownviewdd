#include "code_block_index.h"
#include "code_block_tools.h"
#include "host_adapter.h"
#include "markdown_preview_dock.h"
#include "preview_controller.h"
#include "preview_search.h"
#include <QAction>
#include <QAbstractTextDocumentLayout>
#include <algorithm>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QWheelEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QScrollBar>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextLayout>
#include <QTextFrame>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>
#include <memory>

namespace {
class CodeAdapter final : public HostAdapter
{
public:
    explicit CodeAdapter(QTabWidget *tabs) : tabs(tabs) {}
    struct Cache { QPointer<QWidget> window; QPointer<QTextEdit> preview; bool current = false; };
    QTabWidget *tabs;
    QHash<QWidget *, Cache> cache;
    int renders = 0;
    mutable int reads = 0;
    int navigations = 0;
    int lastLine = -1;
    bool readable = true;
    bool navigable = true;
    bool failRefresh = false;
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
        return !failRefresh;
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
    CodeAdapter adapter{tabs};
    std::unique_ptr<PreviewController> controller;
    MarkdownPreviewDock *dock = nullptr;
    QTextEdit *editor = nullptr;
    explicit Fixture(const QString &text = QStringLiteral("needle middle needle"))
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
    QTextEdit *preview() { return adapter.cache.value(tabs->currentWidget()).preview; }
};
CodeBlockTools *tools(Fixture &f) { return f.dock->findChild<CodeBlockTools *>(); }
QToolButton *copyButton(Fixture &f, int ordinal = 0)
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto buttons = f.preview()->findChildren<QToolButton *>(QStringLiteral("NddMarkdownCopyCode"));
        for (auto i = buttons.crbegin(); i != buttons.crend(); ++i) {
            if ((*i)->property("codeBlockOrdinal").toInt() == ordinal && (*i)->parentWidget()->isEnabled())
                return *i;
        }
        QTest::qWait(25);
    }
    return nullptr;
}
QString feedback(Fixture &f) { return copyButton(f)->accessibleDescription(); }
}

class CodeBlockTest final : public QObject
{
    Q_OBJECT
private slots:
    void extraction_data();
    void extraction();
    void uncertainMapping();
    void clipboardAndKeyboard();
    void manualAndCacheIdentity();
    void sourceReadReentry();
    void failureAndInvalidation();
    void wrapStyleSearchAndExport();
    void preferenceAndWindows();
    void contextMenuAndReentrantClipboard();
    void screenshotsAndMeasurements();
    void attachedButtonsFollowLayout();
    void attachedButtonsVirtualized();
};

void CodeBlockTest::extraction_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QStringList>("expected");
    QTest::addColumn<QStringList>("languages");
    QTest::newRow("space-tab-blank-special") << QStringLiteral("```cpp\n  a\t b  \n\n*[]<>`中文😀\n```\n")
        << QStringList{QStringLiteral("  a\t b  \n\n*[]<>`中文😀\n")} << QStringList{QStringLiteral("cpp")};
    QTest::newRow("leading-tabs") << QStringLiteral("```\n\ta\n  \tb\n```")
        << QStringList{QStringLiteral("\ta\n  \tb\n")} << QStringList{QString()};
    QTest::newRow("empty") << QStringLiteral("```\n```") << QStringList{QString()} << QStringList{QString()};
    QTest::newRow("blank-line") << QStringLiteral("```\n\n```") << QStringList{QStringLiteral("\n")} << QStringList{QString()};
    QTest::newRow("empty-between") << QStringLiteral("before\n\n```\n```\n\nafter") << QStringList{QString()} << QStringList{QString()};
    QTest::newRow("multiple-adjacent") << QStringLiteral("```x\na\n```\n```x\na\n```\n```\n```\n```odd-lang extra\nb\n```\n")
        << QStringList{QStringLiteral("a\n"), QStringLiteral("a\n"), QString(), QStringLiteral("b\n")}
        << QStringList{QStringLiteral("x"), QStringLiteral("x"), QString(), QStringLiteral("odd-lang")};
    QTest::newRow("unclosed-no-newline") << QStringLiteral("```\na") << QStringList{QStringLiteral("a")} << QStringList{QString()};
    QTest::newRow("unclosed-with-newline") << QStringLiteral("```\na\n") << QStringList{QStringLiteral("a\n")} << QStringList{QString()};
    QTest::newRow("trailing-blank") << QStringLiteral("```\na\n\n```\n") << QStringList{QStringLiteral("a\n\n")} << QStringList{QString()};
    QTest::newRow("embedded-fence") << QStringLiteral("````md\n```cpp\n> - x\n```\n````\n")
        << QStringList{QStringLiteral("```cpp\n> - x\n```\n")} << QStringList{QStringLiteral("md")};
    QTest::newRow("tilde") << QStringLiteral("~~~mystery\n ~~~ literal\n~~~\n")
        << QStringList{QStringLiteral(" ~~~ literal\n")} << QStringList{QStringLiteral("mystery")};
    QTest::newRow("fence-indent") << QStringLiteral("  ```\n    a\n b\n  ```\n")
        << QStringList{QStringLiteral("  a\nb\n")} << QStringList{QString()};
    QTest::newRow("fence-indent-tab") << QStringLiteral("  ```\n\ta\n  ```")
        << QStringList{QStringLiteral("  a\n")} << QStringList{QString()};
    QTest::newRow("indented") << QStringLiteral("    a\t b\n\n    c\n\nparagraph\n")
        << QStringList{QStringLiteral("a\t b\n\nc\n")} << QStringList{QString()};
    QTest::newRow("indented-blank-spaces") << QStringLiteral("    a\n      \n    b\n")
        << QStringList{QStringLiteral("a\n  \nb\n")} << QStringList{QString()};
    QTest::newRow("tab-indent") << QStringLiteral("\ta\tb\n\t  c\n")
        << QStringList{QStringLiteral("a\tb\n  c\n")} << QStringList{QString()};
    QTest::newRow("quote") << QStringLiteral("> ```js\n>   a\n> \tb\n> ```\n")
        << QStringList{QStringLiteral("  a\n\tb\n")} << QStringList{QStringLiteral("js")};
    QTest::newRow("nested-quote") << QStringLiteral("> > ```\n> > > literal\n> > ```\n")
        << QStringList{QStringLiteral("> literal\n")} << QStringList{QString()};
    QTest::newRow("list") << QStringLiteral("- ```cpp\n  a\n  ```\n\nend")
        << QStringList{QStringLiteral("a\n")} << QStringList{QStringLiteral("cpp")};
    QTest::newRow("list-indented") << QStringLiteral("- item\n\n      a\n      b\n")
        << QStringList{QStringLiteral("a\nb\n")} << QStringList{QString()};
    QTest::newRow("quote-implicit-end") << QStringLiteral("> ```\n> a\n\n```\nb\n```\n")
        << QStringList{QStringLiteral("a\n"), QStringLiteral("b\n")} << QStringList{QString(), QString()};
    QTest::newRow("crlf") << QStringLiteral("```\r\n  a\r\n\r\nb\r\n```\r\n")
        << QStringList{QStringLiteral("  a\n\nb\n")} << QStringList{QString()};
    QTest::newRow("cr") << QStringLiteral("```\r  a\rb\r```\r")
        << QStringList{QStringLiteral("  a\nb\n")} << QStringList{QString()};
    QTest::newRow("inline-excluded") << QStringLiteral("`x` and text\n    continuation\n") << QStringList{} << QStringList{};
}

void CodeBlockTest::extraction()
{
    QFETCH(QString, source);
    QFETCH(QStringList, expected);
    QFETCH(QStringList, languages);
    QWidget editor;
    QTextDocument document;
    document.setMarkdown(source);
    const auto records = indexCodeBlocks(&document, &editor, 7, &source);
    QCOMPARE(records.size(), expected.size());
    for (int i = 0; i < records.size(); ++i) {
        QVERIFY2(records.at(i).reliable, qPrintable(records.at(i).error));
        QCOMPARE(records.at(i).text, expected.at(i));
        QCOMPARE(records.at(i).language, languages.at(i));
        QCOMPARE(records.at(i).editor.data(), &editor);
        QCOMPARE(records.at(i).version, quint64(7));
        QCOMPARE(records.at(i).ordinal, i);
    }
}

void CodeBlockTest::uncertainMapping()
{
    QWidget editor;
    QTextDocument doc;
    doc.setMarkdown(QStringLiteral("```\nold\n```"));
    const QString changed = QStringLiteral("```\nnew\n```");
    auto records = indexCodeBlocks(&doc, &editor, 1, &changed);
    QCOMPARE(records.size(), 1);
    QVERIFY(!records.first().reliable);
    QVERIFY(records.first().text.isEmpty());
    records = indexCodeBlocks(&doc, &editor, 1, nullptr);
    QVERIFY(!records.first().reliable);
    QVERIFY(records.first().error.contains(QStringLiteral("源码")));
}

void CodeBlockTest::clipboardAndKeyboard()
{
    Fixture f(QStringLiteral("```cpp\n  a\tb\n\n[]*\n```\n\n```\n```"));
    auto *code = tools(f);
    QCOMPARE(code->records().size(), 2);
    QString copied;
    code->setClipboardWriter([&](const QString &text) { copied = text; return true; });
    auto *button = copyButton(f);
    QVERIFY(button);
    QTRY_VERIFY(button->isVisible());
    button->setFocus();
    QTest::keyClick(button, Qt::Key_Space);
    QCOMPARE(copied, QStringLiteral("  a\tb\n\n[]*\n"));
    QVERIFY(!f.dock->findChild<QComboBox *>(QStringLiteral("NddMarkdownCodeBlocks")));
    QContextMenuEvent context(QContextMenuEvent::Keyboard, QPoint(), QPoint());
    QCoreApplication::sendEvent(f.preview(), &context);
    auto *menu = code->findChild<QMenu *>();
    QVERIFY(menu);
    QVERIFY(menu->actions().last()->text().contains(QStringLiteral("空代码块")));
    menu->actions().last()->trigger();
    menu->close();
    QCOMPARE(copied, QString());
    code->setClipboardWriter({});
    QVERIFY(code->copyBlock(0));
    QString actual = QApplication::clipboard()->text();
    actual.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    QCOMPARE(actual, QStringLiteral("  a\tb\n\n[]*\n"));
    const QSize before = f.preview()->viewport()->size();
    QVERIFY(code->copyBlock(1));
    QCoreApplication::processEvents();
    QCOMPARE(f.preview()->viewport()->size(), before);
}

void CodeBlockTest::manualAndCacheIdentity()
{
    Fixture f(QStringLiteral("```\nA old\n```"));
    auto *code = tools(f);
    QString copied;
    code->setClipboardWriter([&](const QString &text) { copied = text; return true; });
    f.controller->setRefreshMode(RefreshMode::Manual);
    f.editor->setPlainText(QStringLiteral("```\nA new\n```"));
    const int reads = f.adapter.reads;
    const int renders = f.adapter.renders;
    QVERIFY(code->copyBlock(0));
    QCOMPARE(copied, QStringLiteral("A old\n"));
    QVERIFY(feedback(f).contains(QStringLiteral("当前预览")));
    QCOMPARE(f.adapter.reads, reads);
    QCOMPARE(f.adapter.renders, renders);
    f.add(QStringLiteral("```\nB\n```"));
    f.tabs->setCurrentIndex(1);
    QTRY_VERIFY(code->records().isEmpty());
    QVERIFY(!code->copyBlock(0));
    f.refresh();
    QVERIFY(code->copyBlock(0));
    QCOMPARE(copied, QStringLiteral("B\n"));
    f.tabs->setCurrentIndex(0);
    QVERIFY(!code->copyBlock(0)); // Deferred tab notification cannot authorize B.
    QTRY_VERIFY(!code->records().isEmpty() && code->records().first().editor == f.editor);
    QVERIFY(code->copyBlock(0));
    QCOMPARE(copied, QStringLiteral("A old\n"));
    f.refresh();
    QVERIFY(code->copyBlock(0));
    QCOMPARE(copied, QStringLiteral("A new\n"));
    for (int i = 0; i < 3; ++i) {
        f.add(QStringLiteral("```\nother\n```"));
        f.tabs->setCurrentIndex(f.tabs->count() - 1);
        QCoreApplication::processEvents();
        f.refresh();
    }
    f.tabs->setCurrentIndex(0);
    QTRY_VERIFY(code->records().isEmpty());
    QVERIFY(!code->copyBlock(0));
}

void CodeBlockTest::sourceReadReentry()
{
    Fixture f(QStringLiteral("```\nold\n```"));
    f.controller->setRefreshMode(RefreshMode::Manual);
    f.editor->setPlainText(QStringLiteral("```\nnew\n```"));
    f.adapter.duringRead = [&]() { f.editor->setPlainText(QStringLiteral("```\nchanged during read\n```")); };
    f.refresh();
    QVERIFY(tools(f)->records().isEmpty());
    QVERIFY(!tools(f)->copyBlock(0));
    f.adapter.duringRead = {};
    f.refresh();
    QCOMPARE(tools(f)->records().first().text, QStringLiteral("changed during read\n"));
}

void CodeBlockTest::failureAndInvalidation()
{
    Fixture f(QStringLiteral("```\nx\n```"));
    auto *code = tools(f);
    code->setClipboardWriter([](const QString &) { return false; });
    QVERIFY(!code->copyBlock(0));
    QVERIFY(feedback(f).contains(QStringLiteral("失败")));
    f.adapter.readable = false;
    f.refresh();
    QVERIFY(!code->records().first().reliable);
    QVERIFY(!copyButton(f)->isEnabled());
    QVERIFY(!code->copyBlock(0));
    f.adapter.readable = true;
    f.refresh();
    f.preview()->setMarkdown(QStringLiteral("```\nmutated externally\n```"));
    QVERIFY(code->records().isEmpty());
    f.refresh();
    QVERIFY(!code->records().isEmpty());
    delete f.preview()->document();
    QVERIFY(code->records().isEmpty());
    QVERIFY(!code->copyBlock(0));
}

void CodeBlockTest::wrapStyleSearchAndExport()
{
    const QString line(500, QLatin1Char('x'));
    Fixture f(QStringLiteral("ordinary paragraph\n\n```cpp\n  ") + line + QStringLiteral("\t end\n```\n\n| a | b |\n|---|---|\n| c | d |"));
    auto *code = tools(f);
    const QString expected = QStringLiteral("  ") + line + QStringLiteral("\t end\n");
    auto *wrap = f.dock->findChild<QAction *>(QStringLiteral("NddMarkdownCodeWrap"));
    wrap->setChecked(true);
    const int position = code->records().first().position;
    QTextCursor selection(f.preview()->document());
    selection.setPosition(position + 2);
    selection.setPosition(position + 12, QTextCursor::KeepAnchor);
    f.preview()->setTextCursor(selection);
    const QString selected = selection.selectedText();
    const int renders = f.adapter.renders;
    const int reads = f.adapter.reads;
    auto *search = f.dock->findChild<PreviewSearch *>();
    search->openSearch();
    search->findChild<QLineEdit *>()->setText(QStringLiteral("xxx"));
    QTest::qWait(150);
    for (bool enabled : {false, true}) {
        wrap->setChecked(enabled);
        QCoreApplication::processEvents();
        const auto block = f.preview()->document()->findBlock(position);
        QCOMPARE(block.blockFormat().nonBreakableLines(), !enabled);
        f.preview()->document()->documentLayout()->documentSize();
        if (enabled)
            QVERIFY(block.layout()->lineCount() > 1);
        else {
            QCOMPARE(block.layout()->lineCount(), 1);
            QVERIFY(f.preview()->horizontalScrollBar()->maximum() > 0);
        }
        QCOMPARE(f.preview()->textCursor().selectedText(), selected);
        QCOMPARE(code->records().first().text, expected);
        QVERIFY(!f.preview()->document()->begin().blockFormat().nonBreakableLines());
    }
    const QPointF point(f.preview()->viewport()->rect().center());
    QWheelEvent wheel(point, f.preview()->viewport()->mapToGlobal(point.toPoint()),
                      QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                      Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(f.preview()->viewport(), &wheel);
    QVERIFY(!code->records().isEmpty());
    QCOMPARE(code->records().first().text, expected);
    f.dock->setReadingFont(QFont(QStringLiteral("monospace"), 14), 1.25);
    QPalette dark = f.dock->palette();
    dark.setColor(QPalette::Base, Qt::black);
    dark.setColor(QPalette::Text, Qt::white);
    f.dock->setPalette(dark);
    QTest::qWait(80);
    QVERIFY(!code->records().isEmpty());
    QCOMPARE(code->records().first().text, expected);
    QString copied;
    code->setClipboardWriter([&](const QString &text) { copied = text; return true; });
    QVERIFY(code->copyBlock(0));
    QCOMPARE(copied, expected);
    const QByteArray html = f.dock->htmlSnapshotFor(f.editor, code->records().first().version);
    QVERIFY(!html.isEmpty());
    QVERIFY(!html.contains("NddMarkdownCopyCode"));
    QVERIFY(!html.contains(QStringLiteral("复制").toUtf8()));
    QVERIFY(!html.contains("cpp"));
    QCOMPARE(f.adapter.renders, renders);
    QCOMPARE(f.adapter.reads, reads);
}

void CodeBlockTest::preferenceAndWindows()
{
    Fixture first(QStringLiteral("```\nfirst\n```"));
    auto *wrap = first.dock->findChild<QAction *>(QStringLiteral("NddMarkdownCodeWrap"));
    wrap->setChecked(false);
    Fixture second(QStringLiteral("```\nsecond\n```"));
    auto *other = second.dock->findChild<QAction *>(QStringLiteral("NddMarkdownCodeWrap"));
    QVERIFY(!other->isChecked());
    other->setChecked(true);
    QVERIFY(!wrap->isChecked());
    first.refresh();
    QVERIFY(first.preview()->document()->begin().blockFormat().nonBreakableLines());
    QCOMPARE(tools(first)->records().first().text, QStringLiteral("first\n"));
    QCOMPARE(tools(second)->records().first().text, QStringLiteral("second\n"));
}


void CodeBlockTest::contextMenuAndReentrantClipboard()
{
    Fixture f(QStringLiteral("```cpp\nfirst\n```\n```js\nsecond\n```"));
    auto *code = tools(f);
    QString copied;
    code->setClipboardWriter([&](const QString &text) { copied = text; return true; });
    QTextCursor cursor(f.preview()->document());
    cursor.setPosition(code->records().at(1).position);
    f.preview()->setTextCursor(cursor);
    QContextMenuEvent context(QContextMenuEvent::Keyboard, QPoint(), QPoint());
    QCoreApplication::sendEvent(f.preview(), &context);
    QPointer<QMenu> menu = code->findChild<QMenu *>();
    QVERIFY(menu);
    QPointer<QAction> action = menu->actions().last();
    QVERIFY(action->text().contains(QStringLiteral("js")));
    action->trigger();
    QCOMPARE(copied, QStringLiteral("second\n"));
    copied.clear();
    f.controller->setRefreshMode(RefreshMode::Manual);
    f.editor->setPlainText(QStringLiteral("```\nnew\n```"));
    f.refresh();
    if (action)
        action->trigger();
    QCOMPARE(copied, QString());
    QVERIFY(!menu || !menu->isVisible());
    code->setClipboardWriter([&](const QString &text) {
        copied = text;
        f.preview()->setMarkdown(QStringLiteral("reentered"));
        return true;
    });
    QVERIFY(code->copyBlock(0));
    QCOMPARE(copied, QStringLiteral("new\n"));
    QVERIFY(code->records().isEmpty());
    QVERIFY(!code->copyBlock(0));
    f.adapter.failRefresh = true;
    f.refresh();
    QVERIFY(code->records().isEmpty());
}

void CodeBlockTest::attachedButtonsFollowLayout()
{
    Fixture f(QStringLiteral("```cpp\nfirst\n```\n\nparagraph\n\n```python\nsecond\n```"));
    auto *code = tools(f);
    auto *first = copyButton(f, 0);
    auto *second = copyButton(f, 1);
    QVERIFY(first && second);
    QTRY_VERIFY(first->isVisible() && second->isVisible());
    QString copied;
    code->setClipboardWriter([&](const QString &text) { copied = text; return true; });
    QTest::mouseClick(second, Qt::LeftButton);
    QCOMPARE(copied, QStringLiteral("second\n"));
    QTest::mouseClick(first, Qt::LeftButton);
    QCOMPARE(copied, QStringLiteral("first\n"));
    QVERIFY(first->parentWidget()->parentWidget() == f.preview()->viewport());
    const auto record = code->records().first();
    QTextCursor cursor(f.preview()->document());
    cursor.setPosition(record.position);
    const QRect textRect = f.preview()->cursorRect(cursor);
    QVERIFY(first->parentWidget()->geometry().bottom() < textRect.top());
    const QString exported = CodeBlockTools::htmlForExport(f.preview()->document());
    QVERIFY(!exported.contains(QStringLiteral("margin-top:34px")));
    QVERIFY(!exported.contains(QStringLiteral("margin-top:28px")));
    const int oldRight = first->parentWidget()->geometry().right();
    f.window.resize(1400, 650);
    f.window.resizeDocks({f.dock}, {720}, Qt::Horizontal);
    QTRY_VERIFY(first->parentWidget()->geometry().right() > oldRight);
    const qreal margin = cursor.blockFormat().topMargin();
    f.dock->setReadingFont(QFont(QStringLiteral("monospace"), 14), 1.25);
    QTest::qWait(50);
    QCOMPARE(cursor.blockFormat().topMargin(), margin);
    QCOMPARE(CodeBlockTools::htmlForExport(f.preview()->document()).count(QStringLiteral("first")), 1);
    QPointer<QToolButton> old = first;
    f.refresh();
    if (old)
        QVERIFY(!old->isVisible() && !old->isEnabled());
    QTRY_VERIFY(copyButton(f)->isVisible());
    f.editor->setPlainText(QStringLiteral("ordinary paragraph\n\nanother paragraph"));
    f.refresh();
    QVERIFY(tools(f)->records().isEmpty());
    QCOMPARE(f.preview()->document()->rootFrame()->frameFormat().topMargin(),
             f.preview()->document()->documentMargin());
}

void CodeBlockTest::attachedButtonsVirtualized()
{
    QString source;
    for (int i = 0; i < 100; ++i)
        source += QStringLiteral("```cpp\nline %1\n```\n\n").arg(i);
    Fixture f(source);
    QTest::qWait(80);
    QVERIFY(copyButton(f, 0));
    QVERIFY(f.preview()->findChildren<QToolButton *>(QStringLiteral("NddMarkdownCopyCode")).size() < 12);
    QPointer<QToolButton> initial = copyButton(f, 0);
    f.preview()->verticalScrollBar()->setValue(f.preview()->verticalScrollBar()->maximum());
    QVERIFY(!initial || !initial->isVisible());
    QTRY_VERIFY(copyButton(f, 99));
    QVERIFY(!copyButton(f, 0));
    QString copied;
    tools(f)->setClipboardWriter([&](const QString &text) { copied = text; return true; });
    QTest::mouseClick(copyButton(f, 99), Qt::LeftButton);
    QCOMPARE(copied, QStringLiteral("line 99\n"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(f.preview()->findChildren<QToolButton *>(QStringLiteral("NddMarkdownCopyCode")).size() < 12);
}

void CodeBlockTest::screenshotsAndMeasurements()
{
    QString source;
    for (int i = 0; i < 100; ++i)
        source += QStringLiteral("```cpp\n    item_%1\t= value;\n\n```\n\n").arg(i);
    Fixture f(source);
    QVector<qint64> samples;
    for (int i = 0; i < 21; ++i) {
        QElapsedTimer timer;
        timer.start();
        const auto records = indexCodeBlocks(f.preview()->document(), f.editor, 1, &source);
        QCOMPARE(records.size(), 100);
        QVERIFY(records.last().reliable);
        if (i)
            samples.append(timer.nsecsElapsed());
    }
    std::sort(samples.begin(), samples.end());
    qInfo("100 code blocks, n=20, median_ms=%.3f p95_ms=%.3f", samples[10] / 1e6, samples[18] / 1e6);
    const QString directory = qEnvironmentVariable("MARKDOWNVIEW_SCREENSHOT_DIR");
    if (!directory.isEmpty()) {
        Fixture visual(QStringLiteral("```python\ndef greet(name):\n    return f\"Hello, {name}!\"\n\nprint(greet(\"Markdown\"))\n```\n\n"
                                      "```json\n{\n  \"theme\": \"dark\",\n  \"copy\": true\n}\n```\n\n"
                                      "```bash\ngit status --short\n````\n"));
        visual.dock->findChild<QAction *>(QStringLiteral("NddMarkdownOutlineVisible"))->setChecked(false);
        visual.window.resizeDocks({visual.dock}, {540}, Qt::Horizontal);
        QTest::qWait(80);
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(visual.window.grab().save(directory + QStringLiteral("/req007-code.png")));
        QVERIFY(visual.dock->grab().save(directory + QStringLiteral("/req007-code-inline.png")));
        QVERIFY(tools(visual)->copyBlock(0));
        QTest::qWait(20);
        QVERIFY(visual.window.grab().save(directory + QStringLiteral("/req007-code-copied.png")));
        visual.controller->setRefreshMode(RefreshMode::Manual);
        visual.editor->setPlainText(QStringLiteral("changed"));
        QVERIFY(tools(visual)->copyBlock(0));
        QCoreApplication::processEvents();
        QVERIFY(visual.window.grab().save(directory + QStringLiteral("/req007-code-stale.png")));
        QPalette dark = visual.dock->palette();
        dark.setColor(QPalette::Base, QColor(QStringLiteral("#171a21")));
        dark.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#232832")));
        dark.setColor(QPalette::Text, QColor(QStringLiteral("#e2e6ef")));
        visual.dock->setPalette(dark);
        QTest::qWait(80);
        QVERIFY(visual.window.grab().save(directory + QStringLiteral("/req007-code-dark.png")));
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir config;
    if (!config.isValid())
        return 2;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
    CodeBlockTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "code_block_test.moc"
