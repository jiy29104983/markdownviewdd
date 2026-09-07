#include "markdown_preview_dock.h"
#include "ndd_plugin_api.h"
#include "preview_controller.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QByteArray>
#include <QCoreApplication>
#include <QEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QtTest>

extern "C" {
bool NDD_PROC_IDENTIFY(NDD_PROC_DATA *data);
int NDD_PROC_MAIN(QWidget *notepad,
                  const QString &pluginFilePath,
                  NddGetCurrentEditor getCurrentEditor,
                  NddHostCallback hostCallback,
                  NDD_PROC_DATA *data);
}

class QsciScintilla final : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit QsciScintilla(const QString &filePath,
                           const QString &markdown,
                           QWidget *parent = nullptr)
        : QAbstractScrollArea(parent), m_markdown(markdown)
    {
        setProperty("filePath", filePath);
    }

    int renderCount() const
    {
        return m_renderCount;
    }

signals:
    void textChanged();

public slots:
    void on_viewMarkdown()
    {
        if (!m_preview) {
            m_preview = new QWidget(this);
            m_preview->setObjectName(QStringLiteral("MarkdownViewClass"));
            auto *layout = new QVBoxLayout(m_preview);
            m_textEdit = new QTextEdit(m_preview);
            m_textEdit->setObjectName(QStringLiteral("textEdit"));
            layout->addWidget(m_textEdit);
        }
        on_updataMarkdown();
    }

    void on_updataMarkdown()
    {
        ++m_renderCount;
        if (m_textEdit) {
            m_textEdit->setMarkdown(m_markdown);
        }
    }

private:
    QString m_markdown;
    QWidget *m_preview = nullptr;
    QTextEdit *m_textEdit = nullptr;
    int m_renderCount = 0;
};

namespace {
struct HostFixture
{
    QMainWindow window;
    QTabWidget tabs;
    QMenu *rootMenu = nullptr;
    QsciScintilla *editor = nullptr;

    HostFixture(const QString &title,
                const QString &filePath,
                const QString &markdown)
    {
        tabs.setObjectName(QStringLiteral("editTabWidget"));
        window.setCentralWidget(&tabs);
        rootMenu = window.menuBar()->addMenu(title);
        editor = new QsciScintilla(filePath, markdown);
        tabs.addTab(editor, title);
        tabs.setCurrentWidget(editor);
        window.resize(720, 480);
        window.show();
        QCoreApplication::processEvents();
    }

    int initialize()
    {
        NDD_PROC_DATA data;
        data.rootMenu = rootMenu;
        return NDD_PROC_MAIN(
            &window, QStringLiteral("markdownviewdd.dll"),
            [](QWidget *) -> QsciScintilla * { return nullptr; },
            [](QWidget *, int, void *) { return false; }, &data);
    }

    PreviewController *controller() const
    {
        return window.findChild<PreviewController *>(
            QString(), Qt::FindDirectChildrenOnly);
    }

    MarkdownPreviewDock *dock() const
    {
        return window.findChild<MarkdownPreviewDock *>(
            QString(), Qt::FindDirectChildrenOnly);
    }

    QAction *action(const QString &text) const
    {
        for (QAction *candidate : rootMenu->actions()) {
            if (candidate && candidate->text() == text) {
                return candidate;
            }
        }
        return nullptr;
    }

    void render()
    {
        QAction *toggleAction = action(QStringLiteral("显示/隐藏预览"));
        QAction *refreshAction = action(QStringLiteral("立即刷新"));
        QVERIFY(toggleAction);
        QVERIFY(refreshAction);
        toggleAction->setChecked(true);
        QCoreApplication::processEvents();
        refreshAction->trigger();
        QCoreApplication::processEvents();
    }
};

QAction *findAction(QMenu *menu, const QString &text)
{
    if (!menu) {
        return nullptr;
    }
    for (QAction *action : menu->actions()) {
        if (action && action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

int initializeWindow(QMainWindow *window, QMenu *rootMenu)
{
    NDD_PROC_DATA data;
    data.rootMenu = rootMenu;
    return NDD_PROC_MAIN(
        window, QStringLiteral("markdownviewdd.dll"),
        [](QWidget *) -> QsciScintilla * { return nullptr; },
        [](QWidget *, int, void *) { return false; }, &data);
}
}

class PluginMultiWindowTest final : public QObject
{
    Q_OBJECT

private slots:
    void initializesAndOperatesEachHostWindowIndependently();
    void repeatedInitializationIsIdempotent();
    void closingOneWindowKeepsTheOtherControllerAlive();
    void contextMenuBridgeOnlyHandlesItsOwnWindow();
    void filePathChangeOnlyUpdatesOwningWindow();
};

void PluginMultiWindowTest::initializesAndOperatesEachHostWindowIndependently()
{
    HostFixture first(QStringLiteral("First"), QStringLiteral("first.md"),
                      QStringLiteral("# First window"));
    HostFixture second(QStringLiteral("Second"), QStringLiteral("second.md"),
                       QStringLiteral("# Second window"));

    QCOMPARE(first.initialize(), 0);
    QCOMPARE(second.initialize(), 0);
    QVERIFY(first.controller());
    QVERIFY(second.controller());
    QVERIFY(first.controller() != second.controller());
    QCOMPARE(first.window.findChildren<PreviewController *>(
                 QString(), Qt::FindDirectChildrenOnly).size(), 1);
    QCOMPARE(second.window.findChildren<PreviewController *>(
                 QString(), Qt::FindDirectChildrenOnly).size(), 1);

    QAction *firstToggle = first.action(QStringLiteral("显示/隐藏预览"));
    QAction *secondToggle = second.action(QStringLiteral("显示/隐藏预览"));
    QVERIFY(firstToggle);
    QVERIFY(secondToggle);
    QCOMPARE(firstToggle->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    QCOMPARE(secondToggle->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    QCOMPARE(firstToggle->shortcutContext(), Qt::WindowShortcut);
    QCOMPARE(secondToggle->shortcutContext(), Qt::WindowShortcut);

    first.render();
    QVERIFY(first.dock()->isVisible());
    QVERIFY(second.dock()->isHidden());
    QCOMPARE(first.editor->renderCount(), 1);
    QCOMPARE(second.editor->renderCount(), 0);

    QByteArray firstHtml;
    QByteArray secondHtml;
    QString firstPath;
    QString secondPath;
    QVERIFY(first.controller()->currentHtmlSnapshot(&firstHtml, &firstPath));
    QVERIFY(firstHtml.contains("First window"));
    QCOMPARE(firstPath, QStringLiteral("first.md"));

    second.render();
    QVERIFY(second.controller()->currentHtmlSnapshot(&secondHtml, &secondPath));
    QVERIFY(secondHtml.contains("Second window"));
    QVERIFY(!secondHtml.contains("First window"));
    QCOMPARE(secondPath, QStringLiteral("second.md"));
    QCOMPARE(first.editor->renderCount(), 1);
    QCOMPARE(second.editor->renderCount(), 1);
}

void PluginMultiWindowTest::repeatedInitializationIsIdempotent()
{
    HostFixture fixture(QStringLiteral("Repeated"), QStringLiteral("repeat.md"),
                        QStringLiteral("# Repeat"));

    QCOMPARE(fixture.initialize(), 0);
    PreviewController *controller = fixture.controller();
    QVERIFY(controller);
    const int actionCount = fixture.rootMenu->actions().size();
    QCOMPARE(controller->findChildren<QTimer *>(
                 QString(), Qt::FindDirectChildrenOnly).size(), 2);

    QCOMPARE(fixture.initialize(), 0);
    QCOMPARE(fixture.controller(), controller);
    QCOMPARE(fixture.rootMenu->actions().size(), actionCount);
    QCOMPARE(fixture.window.findChildren<MarkdownPreviewDock *>(
                 QString(), Qt::FindDirectChildrenOnly).size(), 1);
    QCOMPARE(controller->findChildren<QTimer *>(
                 QString(), Qt::FindDirectChildrenOnly).size(), 2);
}

void PluginMultiWindowTest::closingOneWindowKeepsTheOtherControllerAlive()
{
    auto *firstWindow = new QMainWindow;
    auto *firstTabs = new QTabWidget(firstWindow);
    firstTabs->setObjectName(QStringLiteral("editTabWidget"));
    firstWindow->setCentralWidget(firstTabs);
    auto *firstEditor = new QsciScintilla(
        QStringLiteral("first.md"), QStringLiteral("# First"));
    firstTabs->addTab(firstEditor, QStringLiteral("First"));
    QMenu *firstMenu = firstWindow->menuBar()->addMenu(QStringLiteral("First"));

    auto *secondWindow = new QMainWindow;
    auto *secondTabs = new QTabWidget(secondWindow);
    secondTabs->setObjectName(QStringLiteral("editTabWidget"));
    secondWindow->setCentralWidget(secondTabs);
    auto *secondEditor = new QsciScintilla(
        QStringLiteral("second.md"), QStringLiteral("# Second survives"));
    secondTabs->addTab(secondEditor, QStringLiteral("Second"));
    QMenu *secondMenu = secondWindow->menuBar()->addMenu(QStringLiteral("Second"));

    firstWindow->show();
    secondWindow->show();
    QCOMPARE(initializeWindow(firstWindow, firstMenu), 0);
    QCOMPARE(initializeWindow(secondWindow, secondMenu), 0);
    QPointer<PreviewController> firstController =
        firstWindow->findChild<PreviewController *>(
            QString(), Qt::FindDirectChildrenOnly);
    QPointer<PreviewController> secondController =
        secondWindow->findChild<PreviewController *>(
            QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(firstController);
    QVERIFY(secondController);

    delete firstWindow;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(firstController.isNull());
    QVERIFY(secondController);

    QAction *secondToggle = findAction(secondMenu, QStringLiteral("显示/隐藏预览"));
    QAction *secondRefresh = findAction(secondMenu, QStringLiteral("立即刷新"));
    QVERIFY(secondToggle);
    QVERIFY(secondRefresh);
    secondToggle->setChecked(true);
    secondRefresh->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(secondEditor->renderCount(), 1);

    QByteArray html;
    QString path;
    QVERIFY(secondController->currentHtmlSnapshot(&html, &path));
    QVERIFY(html.contains("Second survives"));
    QCOMPARE(path, QStringLiteral("second.md"));

    delete secondWindow;
}

void PluginMultiWindowTest::contextMenuBridgeOnlyHandlesItsOwnWindow()
{
    HostFixture first(QStringLiteral("First"), QStringLiteral("first.md"),
                      QStringLiteral("# First context"));
    HostFixture second(QStringLiteral("Second"), QStringLiteral("second.md"),
                       QStringLiteral("# Second context"));
    QCOMPARE(first.initialize(), 0);
    QCOMPARE(second.initialize(), 0);

    QMenu firstContext(first.editor);
    QAction *firstNativeAction = firstContext.addAction(QStringLiteral("Markdown Preview"));
    connect(firstNativeAction, &QAction::triggered,
            first.editor, &QsciScintilla::on_viewMarkdown);
    QEvent firstShow(QEvent::Show);
    QCoreApplication::sendEvent(&firstContext, &firstShow);
    QCOMPARE(firstNativeAction->text(), QStringLiteral("在侧边栏预览 Markdown"));
    firstNativeAction->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(first.dock()->isVisible(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(first.editor->renderCount(), 1, 1500);
    QVERIFY(second.dock()->isHidden());
    QCOMPARE(second.editor->renderCount(), 0);

    QMenu secondContext(second.editor);
    QAction *secondNativeAction = secondContext.addAction(QStringLiteral("Markdown Preview"));
    connect(secondNativeAction, &QAction::triggered,
            second.editor, &QsciScintilla::on_viewMarkdown);
    QEvent secondShow(QEvent::Show);
    QCoreApplication::sendEvent(&secondContext, &secondShow);
    QCOMPARE(secondNativeAction->text(), QStringLiteral("在侧边栏预览 Markdown"));
    secondNativeAction->trigger();

    QTRY_COMPARE_WITH_TIMEOUT(second.editor->renderCount(), 1, 1500);
    QCOMPARE(first.editor->renderCount(), 1);
}

void PluginMultiWindowTest::filePathChangeOnlyUpdatesOwningWindow()
{
    HostFixture first(QStringLiteral("First"), QStringLiteral("first.md"),
                      QStringLiteral("# First path"));
    HostFixture second(QStringLiteral("Second"), QStringLiteral("second.md"),
                       QStringLiteral("# Second path"));
    QCOMPARE(first.initialize(), 0);
    QCOMPARE(second.initialize(), 0);
    first.render();
    second.render();

    QAction *firstExport = first.action(QStringLiteral("导出 HTML…"));
    QAction *secondExport = second.action(QStringLiteral("导出 HTML…"));
    QVERIFY(firstExport);
    QVERIFY(secondExport);
    QVERIFY(firstExport->isEnabled());
    QVERIFY(secondExport->isEnabled());

    first.editor->setProperty("filePath", QStringLiteral("renamed.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(!firstExport->isEnabled(), 1000);
    QVERIFY(secondExport->isEnabled());
    QCOMPARE(second.editor->renderCount(), 1);

    QByteArray html;
    QString path;
    QVERIFY(second.controller()->currentHtmlSnapshot(&html, &path));
    QCOMPARE(path, QStringLiteral("second.md"));
    QVERIFY(html.contains("Second path"));
}

QTEST_MAIN(PluginMultiWindowTest)

#include "plugin_multi_window_test.moc"
