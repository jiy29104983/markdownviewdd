#include "preview_search.h"
#include "host_adapter.h"
#include "markdown_preview_dock.h"
#include "preview_controller.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtTest>
#include <algorithm>
#include <memory>

namespace {
class SearchAdapter final : public HostAdapter
{
public:
    explicit SearchAdapter(QTabWidget *tabs) : tabs(tabs) {}
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
    SearchAdapter adapter{tabs};
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
    void activate(int index) { dock->headingActivated(dock->headings().at(index)); }
    QTextEdit *preview() { return adapter.cache.value(tabs->currentWidget()).preview; }
    QTreeWidget *tree() { return dock->findChild<QTreeWidget *>(); }
};
PreviewSearch *search(Fixture &f)
{
    auto *result = f.dock->findChild<PreviewSearch *>();
    result->openSearch();
    return result;
}
QLineEdit *query(PreviewSearch *s) { return s->findChild<QLineEdit *>(); }
void setQuery(PreviewSearch *s, const QString &text) { query(s)->setText(text); }
void next(PreviewSearch *s, bool previous = false)
{
    s->findChild<QToolButton *>(previous ? QStringLiteral("NddMarkdownSearchPrevious")
                                        : QStringLiteral("NddMarkdownSearchNext"))->click();
}
QString feedback(PreviewSearch *s)
{
    return s->findChild<QLabel *>(QStringLiteral("NddMarkdownSearchMessage"))->text();
}
QString longText(int count = 80)
{
    QString text;
    for (int i = 0; i < count; ++i)
        text += QStringLiteral("段落 %1 needle，中文和标点。正文内容。\n\n").arg(i);
    return text;
}
}

class PreviewSearchTest final : public QObject
{
    Q_OBJECT
private slots:
    void matching_data();
    void matching();
    void blockAndFormatBoundaries();
    void wrapsAndDoesNotMoveSelection();
    void typingKeepsReadingPosition();
    void emptyAndMultilinePaste();
    void keyboardScopesAndIme();
    void snapshotRefreshPreservesContext();
    void removedTargetFallsBack();
    void manualSnapshotNeverRenders();
    void tabsAndMissingSnapshots();
    void windowIsolationAndClose();
    void hiddenDockPausesAndResumes();
    void exportAndFormattingUnaffected();
    void contentMutationAndDestruction();
    void staleJobsCannotWin();
    void navigationArbitration();
    void queryAndDocumentChangeBetweenBatches();
    void formattingDuringSearch();
    void failureRejectsPartialDocument();
    void themeAndZoom();
    void longDocument_data();
    void longDocument();
    void measurementsAndScreenshots();
};

void PreviewSearchTest::matching_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("needle");
    QTest::addColumn<bool>("sensitive");
    QTest::addColumn<int>("count");
    QTest::newRow("Chinese") << QStringLiteral("中文，中文。中文！") << QStringLiteral("中文") << false << 3;
    QTest::newRow("punctuation") << QStringLiteral("中文，中文。中文！") << QStringLiteral("，") << false << 1;
    QTest::newRow("case-fold") << QStringLiteral("Needle NEEDLE needle") << QStringLiteral("needle") << false << 3;
    QTest::newRow("case-sensitive") << QStringLiteral("Needle NEEDLE needle") << QStringLiteral("needle") << true << 1;
    QTest::newRow("non-overlap") << QStringLiteral("aaaaa") << QStringLiteral("aa") << false << 2;
    QTest::newRow("emoji") << QString::fromUtf8("😀a😀") << QString::fromUtf8("😀") << false << 2;
    QTest::newRow("spaces-literal") << QStringLiteral("a b a  b") << QStringLiteral("a  b") << false << 1;
    QTest::newRow("no-result") << QStringLiteral("正文") << QStringLiteral("missing") << false << 0;
    QTest::newRow("KMP-prefix") << QStringLiteral("ababababac") << QStringLiteral("ababac") << false << 1;
    QTest::newRow("chunk-boundary") << (QString(4094, QLatin1Char('x')) + QStringLiteral("中文needle尾"))
        << QStringLiteral("中文needle") << false << 1;
}

void PreviewSearchTest::matching()
{
    QFETCH(QString, text); QFETCH(QString, needle); QFETCH(bool, sensitive); QFETCH(int, count);
    Fixture f(text);
    // Use exact plain text for literal-space and very long paragraph fixtures.
    f.preview()->setPlainText(text);
    f.dock->setSearchStatus(f.controller->previewStatus());
    auto *s = search(f);
    s->findChild<QCheckBox *>()->setChecked(sensitive);
    setQuery(s, needle);
    QTRY_VERIFY(!s->isSearching());
    QCOMPARE(s->matchCount(), count);
    QCOMPARE(s->currentIndex(), count ? 0 : -1);
}

void PreviewSearchTest::blockAndFormatBoundaries()
{
    Fixture f(QStringLiteral("# needle\n\nnee**dl**e [needle](https://example.invalid)\n\n"
        "| col | col |\n|---|---|\n| needle | needle |\n\n```text\nneedle\n```\n\nnee\n\ndle"));
    auto *s = search(f);
    setQuery(s, QStringLiteral("needle"));
    QTRY_VERIFY(!s->isSearching());
    QCOMPARE(s->matchCount(), 6);
    setQuery(s, QStringLiteral("needleneedle"));
    QTRY_VERIFY(!s->isSearching());
    QCOMPARE(s->matchCount(), 0);
    setQuery(s, QStringLiteral("https://example.invalid"));
    QTRY_VERIFY(!s->isSearching());
    QCOMPARE(s->matchCount(), 0);
}

void PreviewSearchTest::wrapsAndDoesNotMoveSelection()
{
    Fixture f(longText(3));
    QMetaObject::invokeMethod(f.controller.get(), "setSyncScrolling", Q_ARG(bool, false));
    QTextCursor cursor = f.preview()->textCursor();
    cursor.setPosition(1); cursor.setPosition(4, QTextCursor::KeepAnchor);
    f.preview()->setTextCursor(cursor);
    auto *s = search(f);
    setQuery(s, QStringLiteral("needle"));
    QTRY_COMPARE(s->matchCount(), 3);
    QTRY_VERIFY(!s->isSearching());
    next(s, true);
    QCOMPARE(s->currentIndex(), 2);
    QVERIFY(feedback(s).contains(QStringLiteral("末尾")));
    next(s);
    QCOMPARE(s->currentIndex(), 0);
    QVERIFY(feedback(s).contains(QStringLiteral("开头")));
    QCOMPARE(f.preview()->textCursor().selectedText(), cursor.selectedText());
    QCOMPARE(f.adapter.navigations, 0);
}

void PreviewSearchTest::typingKeepsReadingPosition()
{
    Fixture f(longText());
    QMetaObject::invokeMethod(f.controller.get(), "setSyncScrolling", Q_ARG(bool, false));
    auto *s = search(f);
    QTest::qWait(100);
    auto *bar = f.preview()->verticalScrollBar();
    bar->setValue(bar->maximum() / 2);
    const int position = bar->value();
    setQuery(s, QStringLiteral("needle"));
    QTRY_VERIFY(!s->isSearching());
    QCOMPARE(s->matchCount(), 80);
    QCOMPARE(bar->value(), position);
    next(s);
    QTRY_VERIFY(bar->value() < position);
    QTextCursor target(f.preview()->document()); target.setPosition(s->currentPosition());
    QVERIFY(f.preview()->viewport()->rect().intersects(f.preview()->cursorRect(target)));
}

void PreviewSearchTest::emptyAndMultilinePaste()
{
    Fixture f;
    auto *s = search(f);
    QApplication::clipboard()->setText(QStringLiteral("needle\r\nother"));
    QTest::keyClick(query(s), Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(query(s)->text(), QStringLiteral("needle"));
    QVERIFY(feedback(s).contains(QStringLiteral("第一行")));
    QTRY_COMPARE(s->matchCount(), 2);
    setQuery(s, QString());
    QCOMPARE(s->matchCount(), 0);
    QVERIFY(f.preview()->extraSelections().isEmpty());
    QCOMPARE(s->findChild<QLabel *>(QStringLiteral("NddMarkdownSearchCount"))->text(), QStringLiteral("0/0"));
    setQuery(s, QStringLiteral("\nneedle"));
    QCOMPARE(query(s)->text(), QString());
    QVERIFY(feedback(s).contains(QStringLiteral("第一行")));
}

void PreviewSearchTest::keyboardScopesAndIme()
{
    Fixture f;
    auto *s = f.dock->findChild<PreviewSearch *>();
    QShortcut hostFind(QKeySequence::Find, &f.window);
    int hostCalls = 0;
    connect(&hostFind, &QShortcut::activated, &f.window, [&]() { ++hostCalls; });
    f.window.activateWindow();
    f.editor->setFocus();
    QTest::qWait(30);
    QTest::keyClick(f.editor, Qt::Key_F, Qt::ControlModifier);
    QCOMPARE(hostCalls, 1);
    QVERIFY(!s->isOpen());
    f.preview()->setFocus();
    QTest::keyClick(f.preview(), Qt::Key_F, Qt::ControlModifier);
    QVERIFY(s->isOpen());
    QCOMPARE(QApplication::focusWidget(), query(s));
    QCOMPARE(hostCalls, 1);
    setQuery(s, QStringLiteral("needle"));
    QTRY_VERIFY(!s->isSearching());
    QTest::keyClick(query(s), Qt::Key_Return);
    QCOMPARE(s->currentIndex(), 1);
    f.preview()->setFocus();
    QTest::keyClick(f.preview(), Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(s->currentIndex(), 0);
    query(s)->setFocus();
    QInputMethodEvent preedit(QStringLiteral("ni"), {});
    QApplication::sendEvent(query(s), &preedit);
    QTest::keyClick(query(s), Qt::Key_Return);
    QCOMPARE(s->currentIndex(), 0);
    QInputMethodEvent commit;
    QApplication::sendEvent(query(s), &commit);
    QTest::keyClick(query(s), Qt::Key_Escape);
    QVERIFY(!s->isOpen());
    QCOMPARE(QApplication::focusWidget(), f.preview());
    QVERIFY(query(s)->text().isEmpty());
    QVERIFY(f.preview()->extraSelections().isEmpty());
}

void PreviewSearchTest::snapshotRefreshPreservesContext()
{
    Fixture f(QStringLiteral("alpha needle unique first\n\nbeta needle unique second"));
    f.controller->setRefreshMode(RefreshMode::Manual);
    auto *s = search(f);
    setQuery(s, QStringLiteral("needle"));
    QTRY_VERIFY(!s->isSearching()); next(s);
    const int oldPosition = s->currentPosition();
    f.editor->setPlainText(QStringLiteral("new needle inserted\n\nalpha needle unique first\n\nbeta needle unique second"));
    f.refresh();
    QTRY_VERIFY(!s->isSearching());
    QCOMPARE(s->matchCount(), 3);
    QCOMPARE(s->currentIndex(), 2);
    QVERIFY(s->currentPosition() > oldPosition);
}

void PreviewSearchTest::removedTargetFallsBack()
{
    Fixture f(QStringLiteral("alpha needle unique first\n\nbeta needle unique second"));
    f.controller->setRefreshMode(RefreshMode::Manual);
    auto *s = search(f); setQuery(s, QStringLiteral("needle"));
    QTRY_VERIFY(!s->isSearching()); next(s);
    f.editor->setPlainText(QStringLiteral("different needle\n\nother needle")); f.refresh();
    QTRY_VERIFY(!s->isSearching());
    QCOMPARE(s->matchCount(), 2); QCOMPARE(s->currentIndex(), 0);
}

void PreviewSearchTest::manualSnapshotNeverRenders()
{
    Fixture f;
    f.controller->setRefreshMode(RefreshMode::Manual);
    auto *s = search(f); setQuery(s, QStringLiteral("needle"));
    QTRY_VERIFY(!s->isSearching());
    const int renders = f.adapter.renders;
    f.editor->setPlainText(QStringLiteral("new source without old query"));
    QTest::qWait(100);
    QCOMPARE(s->matchCount(), 2);
    QVERIFY(feedback(s).contains(QStringLiteral("待刷新")));
    next(s);
    QCOMPARE(f.adapter.navigations, 0);
    QCOMPARE(f.adapter.renders, renders);
    f.refresh();
    QTRY_VERIFY(!s->isSearching());
    QCOMPARE(s->matchCount(), 0);
    QVERIFY(!feedback(s).contains(QStringLiteral("待刷新")));
}

void PreviewSearchTest::tabsAndMissingSnapshots()
{
    Fixture f;
    f.controller->setRefreshMode(RefreshMode::Manual);
    auto *s = search(f); setQuery(s, QStringLiteral("needle"));
    QTRY_VERIFY(!s->isSearching());
    auto *other = f.add(QStringLiteral("needle only once"));
    const int renders = f.adapter.renders;
    f.tabs->setCurrentWidget(other);
    QTRY_COMPARE(s->matchCount(), 0);
    QVERIFY(feedback(s).contains(QStringLiteral("没有可搜索")));
    QCOMPARE(query(s)->text(), QStringLiteral("needle"));
    QCOMPARE(f.adapter.renders, renders);
    f.refresh(); QTRY_VERIFY(!s->isSearching()); QCOMPARE(s->matchCount(), 1);
    f.tabs->setCurrentWidget(f.editor);
    QTRY_COMPARE(s->matchCount(), 2);
    QVERIFY(!s->isSearching());
    f.dock->showMessage(QStringLiteral("needle"), QStringLiteral("needle error"));
    QCOMPARE(s->matchCount(), 0);
}

void PreviewSearchTest::windowIsolationAndClose()
{
    Fixture first, second;
    auto *a = search(first); auto *b = search(second);
    setQuery(a, QStringLiteral("needle")); setQuery(b, QStringLiteral("middle"));
    QTRY_VERIFY(!a->isSearching() && !b->isSearching());
    QCOMPARE(a->matchCount(), 2); QCOMPARE(b->matchCount(), 1);
    a->findChild<QCheckBox *>()->setChecked(true);
    a->closeSearch();
    QVERIFY(!a->findChild<QCheckBox *>()->isChecked());
    QCOMPARE(query(b)->text(), QStringLiteral("middle"));
    QCOMPARE(b->matchCount(), 1);
    a->openSearch(); QCOMPARE(query(a)->text(), QString());
}

void PreviewSearchTest::hiddenDockPausesAndResumes()
{
    Fixture f;
    f.controller->setRefreshMode(RefreshMode::Manual);
    auto *s = search(f); setQuery(s, QStringLiteral("needle"));
    f.dock->hide();
    QVERIFY(!s->isSearching());
    QTest::qWait(100); QCOMPARE(s->matchCount(), 0);
    QCOMPARE(query(s)->text(), QStringLiteral("needle"));
    f.editor->setPlainText(QStringLiteral("new content"));
    f.dock->show();
    QTRY_COMPARE(s->matchCount(), 2);
    QVERIFY(feedback(s).contains(QStringLiteral("待刷新")));
    f.refresh(); QTRY_VERIFY(!s->isSearching()); QCOMPARE(s->matchCount(), 0);
}

void PreviewSearchTest::exportAndFormattingUnaffected()
{
    Fixture f(QStringLiteral("# needle\n\n**needle** and `needle`"));
    const QString html = f.preview()->document()->toHtml();
    const auto status = f.controller->previewStatus();
    const QByteArray exported = f.dock->htmlSnapshotFor(status.activeEditor, status.displayedVersion);
    QTextEdit::ExtraSelection external;
    external.cursor = QTextCursor(f.preview()->document());
    external.cursor.setPosition(0); external.cursor.setPosition(1, QTextCursor::KeepAnchor);
    external.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    f.preview()->setExtraSelections({external});
    auto *s = search(f); setQuery(s, QStringLiteral("needle"));
    QTRY_VERIFY(!s->isSearching());
    QCOMPARE(f.preview()->extraSelections().size(), 4);
    QCOMPARE(f.preview()->document()->toHtml(), html);
    QCOMPARE(f.dock->htmlSnapshotFor(status.activeEditor, status.displayedVersion), exported);
    s->closeSearch();
    QCOMPARE(f.preview()->extraSelections().size(), 1);
    QCOMPARE(f.preview()->extraSelections().first().format, external.format);
    QCOMPARE(f.preview()->document()->toHtml(), html);
}

void PreviewSearchTest::contentMutationAndDestruction()
{
    auto fixture = std::unique_ptr<Fixture>(new Fixture(longText()));
    auto *s = search(*fixture); setQuery(s, QStringLiteral("needle"));
    fixture->preview()->setPlainText(QStringLiteral("changed without accepted snapshot"));
    QTest::qWait(100);
    QCOMPARE(s->matchCount(), 0);
    fixture->refresh(); QTRY_VERIFY(!s->isSearching());
    QCOMPARE(s->matchCount(), 80);
    fixture->preview()->setDocument(new QTextDocument(fixture->preview()));
    QTest::qWait(100); QCOMPARE(s->matchCount(), 0);
    fixture->refresh();
    setQuery(s, QStringLiteral("new query"));
    QPointer<PreviewSearch> guarded = s;
    fixture.reset();
    QTest::qWait(100);
    QVERIFY(guarded.isNull());
}

void PreviewSearchTest::staleJobsCannotWin()
{
    Fixture f(longText(1000));
    auto *s = search(f);
    setQuery(s, QStringLiteral("needle"));
    setQuery(s, QStringLiteral("missing"));
    QTRY_VERIFY(!s->isSearching());
    QCOMPARE(s->matchCount(), 0);
    setQuery(s, QStringLiteral("needle"));
    QTest::qWait(80);
    s->closeSearch();
    QTest::qWait(150); QCOMPARE(s->matchCount(), 0);
    QVERIFY(f.preview()->extraSelections().isEmpty());
}

void PreviewSearchTest::queryAndDocumentChangeBetweenBatches()
{
    QWidget editor, secondEditor;
    QTextEdit view;
    view.setPlainText(QStringLiteral("needle ").repeated(200000));
    PreviewSearch s;
    s.setSnapshot(&view, &editor, 1);
    s.openSearch();
    setQuery(&s, QStringLiteral("needle"));
    QTRY_VERIFY(s.matchCount() > 0);
    QVERIFY(s.isSearching());
    setQuery(&s, QStringLiteral("absent"));
    s.setSnapshot(&view, &secondEditor, 2);
    QTRY_VERIFY_WITH_TIMEOUT(!s.isSearching(), 10000);
    QCOMPARE(s.matchCount(), 0);
    setQuery(&s, QStringLiteral("needle"));
    QTRY_VERIFY(s.matchCount() > 0);
    view.setPlainText(QStringLiteral("replacement"));
    s.setSnapshot(&view, &secondEditor, 3);
    QTRY_VERIFY(!s.isSearching());
    QCOMPARE(s.matchCount(), 0);
    QTest::qWait(100);
    QCOMPARE(s.matchCount(), 0);
}

void PreviewSearchTest::formattingDuringSearch()
{
    Fixture f(longText(500));
    auto *s = search(f);
    setQuery(s, QStringLiteral("needle"));
    QFont font; font.setPointSizeF(15);
    f.dock->setReadingFont(font, 1.2);
    QTRY_VERIFY(!s->isSearching());
    QCOMPARE(s->matchCount(), 500);
    QVERIFY(!f.preview()->extraSelections().isEmpty());
    QVERIFY(f.preview()->extraSelections().size() <= 256);
}

void PreviewSearchTest::failureRejectsPartialDocument()
{
    Fixture f;
    f.controller->setRefreshMode(RefreshMode::Manual);
    auto *s = search(f);
    setQuery(s, QStringLiteral("needle"));
    QTRY_COMPARE(s->matchCount(), 2);
    f.adapter.failRefresh = true;
    f.editor->setPlainText(QStringLiteral("needle partial update"));
    f.refresh();
    QTest::qWait(100);
    QCOMPARE(s->matchCount(), 0);
    QVERIFY(feedback(s).contains(QStringLiteral("没有可搜索")));
}

void PreviewSearchTest::navigationArbitration()
{
    Fixture f(QStringLiteral("# heading\n\n") + longText());
    auto *s = search(f); setQuery(s, QStringLiteral("needle"));
    QTRY_VERIFY(!s->isSearching());
    f.activate(0);
    const int sourceNavigations = f.adapter.navigations;
    next(s, true);
    const int position = f.preview()->verticalScrollBar()->value();
    QVERIFY(position > 0);
    f.window.resize(1000, 620);
    QTest::qWait(150);
    QCOMPARE(f.adapter.navigations, sourceNavigations);
    QVERIFY(f.dock->hasNavigationTarget());
    f.preview()->verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMinimum);
    QTRY_VERIFY(!f.dock->hasNavigationTarget());
}

void PreviewSearchTest::themeAndZoom()
{
    Fixture f;
    auto *s = search(f); setQuery(s, QStringLiteral("needle"));
    QTRY_VERIFY(!s->isSearching());
    QPalette dark = f.preview()->palette();
    dark.setColor(QPalette::Base, QColor(25, 25, 25));
    dark.setColor(QPalette::Text, Qt::white);
    dark.setColor(QPalette::Highlight, QColor(80, 140, 220));
    dark.setColor(QPalette::HighlightedText, Qt::black);
    f.preview()->setPalette(dark);
    s->updateHighlights();
    QCOMPARE(f.preview()->extraSelections().last().format.background().color(), dark.color(QPalette::Highlight));
    QFont font; font.setPointSizeF(14);
    f.dock->setReadingFont(font, 1.25);
    QTRY_VERIFY(!s->isSearching());
    QCOMPARE(s->matchCount(), 2);
}

void PreviewSearchTest::longDocument_data()
{
    QTest::addColumn<int>("bytes");
    QTest::newRow("100KiB") << 100 * 1024;
    QTest::newRow("1MiB") << 1024 * 1024;
    QTest::newRow("5MiB") << 5 * 1024 * 1024;
}

void PreviewSearchTest::longDocument()
{
    QFETCH(int, bytes);
    QWidget editor;
    QTextEdit view;
    view.resize(640, 480);
    const int repeats = bytes / 64;
    const QString line = QStringLiteral("needle ").repeated(8) + QStringLiteral("padding\n");
    QCOMPARE(line.size(), 64);
    view.setPlainText(line.repeated(repeats));
    view.show();
    PreviewSearch s;
    s.setSnapshot(&view, &editor, 1); s.openSearch();
    int ticks = 0;
    qint64 longestGap = 0;
    QElapsedTimer gap;
    gap.start();
    QTimer heartbeat;
    connect(&heartbeat, &QTimer::timeout, &s, [&]() {
        ++ticks;
        longestGap = qMax(longestGap, gap.nsecsElapsed());
        gap.restart();
    });
    heartbeat.start(2);
    setQuery(&s, QStringLiteral("needle"));
    QTRY_VERIFY_WITH_TIMEOUT(!s.isSearching(), 30000);
    QCOMPARE(s.matchCount(), repeats * 8);
    QVERIFY(ticks > 1);
    QVERIFY(view.extraSelections().size() <= 256);
    QVERIFY(feedback(&s).contains(QStringLiteral("完整计数")));
    QVERIFY2(s.maxBatchNanoseconds() < 250000000, "A search batch blocked input for >=250ms");
    qInfo().noquote() << QStringLiteral("search bytes=%1 matches=%2 elapsed_ms=%3 max_batch_ms=%4 ticks=%5 max_event_gap_ms=%6")
        .arg(bytes).arg(s.matchCount()).arg(s.lastSearchNanoseconds() / 1e6)
        .arg(s.maxBatchNanoseconds() / 1e6).arg(ticks).arg(longestGap / 1e6);
}

void PreviewSearchTest::measurementsAndScreenshots()
{
    Fixture f(longText(500));
    auto *s = search(f);
    QVector<double> elapsed, batches;
    for (int i = 0; i < 21; ++i) {
        setQuery(s, i % 2 ? QStringLiteral("needle") : QStringLiteral("段落"));
        QTRY_VERIFY(!s->isSearching());
        QCOMPARE(s->matchCount(), 500);
        if (i) { elapsed.append(s->lastSearchNanoseconds() / 1e6); batches.append(s->maxBatchNanoseconds() / 1e6); }
    }
    std::sort(elapsed.begin(), elapsed.end()); std::sort(batches.begin(), batches.end());
    qInfo().noquote() << QJsonDocument(QJsonObject{
        {QStringLiteral("samples"), 20}, {QStringLiteral("search_median_ms"), elapsed.at(10)},
        {QStringLiteral("search_p95_ms"), elapsed.at(18)}, {QStringLiteral("batch_p95_ms"), batches.at(18)}
    }).toJson(QJsonDocument::Compact);
    const QString directory = qEnvironmentVariable("MARKDOWNVIEW_SCREENSHOT_DIR");
    if (!directory.isEmpty()) {
        QDir().mkpath(directory);
        QTest::qWait(100);
        QVERIFY(f.window.grab().save(QDir(directory).filePath(QStringLiteral("req004-search.png"))));
        f.controller->setRefreshMode(RefreshMode::Manual);
        f.editor->append(QStringLiteral("new source"));
        QTest::qWait(100);
        QVERIFY(f.window.grab().save(QDir(directory).filePath(QStringLiteral("req004-search-stale.png"))));
    }
}

QTEST_MAIN(PreviewSearchTest)
#include "preview_search_test.moc"
