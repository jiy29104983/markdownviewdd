#include "markdown_preview_dock.h"
#include "preview_controller.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QScrollBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtTest>

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
        verticalScrollBar()->setRange(0, 100);
    }

    int renderCount() const
    {
        return m_renderCount;
    }

    void edit(const QString &markdown)
    {
        m_markdown = markdown;
        emit textChanged();
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
            m_textEdit->verticalScrollBar()->setRange(0, 100);
        }
    }

private:
    QString m_markdown;
    QWidget *m_preview = nullptr;
    QTextEdit *m_textEdit = nullptr;
    int m_renderCount = 0;
};

class PreviewControllerDocumentIdentityTest final : public QObject
{
    Q_OBJECT

private slots:
    void previewScrollCannotMoveNewActiveEditor();
    void manualRefreshUsesCurrentTab();
    void rapidSwitchAndTypingKeepLatestDocument();
};

namespace {
struct Fixture
{
    QMainWindow window;
    QTabWidget tabs;
    QMenu menu;
    PreviewController controller;

    Fixture()
        : menu(&window), controller(&window)
    {
        tabs.setObjectName(QStringLiteral("editTabWidget"));
        window.setCentralWidget(&tabs);
        controller.installMenu(&menu);
        window.resize(900, 600);
        window.show();
        QCoreApplication::processEvents();
    }

    MarkdownPreviewDock *dock()
    {
        return window.findChild<MarkdownPreviewDock *>();
    }

    void renderNow()
    {
        MarkdownPreviewDock *previewDock = dock();
        QVERIFY(previewDock);
        previewDock->show();
        QCoreApplication::processEvents();
        QVERIFY(QMetaObject::invokeMethod(&controller, "renderNow",
                                         Qt::DirectConnection));
    }

    void pollEditor()
    {
        QVERIFY(QMetaObject::invokeMethod(&controller, "pollEditor",
                                         Qt::DirectConnection));
    }
};
}

void PreviewControllerDocumentIdentityTest::previewScrollCannotMoveNewActiveEditor()
{
    Fixture fixture;
    auto *editorA = new QsciScintilla(QStringLiteral("a.md"),
                                     QStringLiteral("# A"));
    auto *editorB = new QsciScintilla(QStringLiteral("b.md"),
                                     QStringLiteral("# B"));
    fixture.tabs.addTab(editorA, QStringLiteral("A"));
    fixture.tabs.addTab(editorB, QStringLiteral("B"));
    fixture.tabs.setCurrentWidget(editorA);
    fixture.renderNow();

    QTextEdit *previewTextEdit = fixture.dock()->findChild<QTextEdit *>(
        QStringLiteral("textEdit"));
    QVERIFY(previewTextEdit);
    previewTextEdit->verticalScrollBar()->setRange(0, 100);
    previewTextEdit->verticalScrollBar()->setValue(23);

    editorB->verticalScrollBar()->setValue(17);
    fixture.tabs.setCurrentWidget(editorB);
    QVERIFY(QMetaObject::invokeMethod(fixture.dock(),
                                     "previewScrollRatioChanged",
                                     Qt::DirectConnection,
                                     Q_ARG(double, 0.9)));

    QCOMPARE(editorB->verticalScrollBar()->value(), 17);
    fixture.pollEditor();
    editorB->verticalScrollBar()->setValue(81);
    fixture.pollEditor();
    QCOMPARE(previewTextEdit->verticalScrollBar()->value(), 23);
}

void PreviewControllerDocumentIdentityTest::manualRefreshUsesCurrentTab()
{
    Fixture fixture;
    auto *editorA = new QsciScintilla(QStringLiteral("a.md"),
                                     QStringLiteral("# A"));
    auto *editorB = new QsciScintilla(QStringLiteral("b.md"),
                                     QStringLiteral("# B"));
    fixture.tabs.addTab(editorA, QStringLiteral("A"));
    fixture.tabs.addTab(editorB, QStringLiteral("B"));
    fixture.tabs.setCurrentWidget(editorA);
    fixture.renderNow();

    fixture.tabs.setCurrentWidget(editorB);
    QAction *refreshAction = nullptr;
    for (QAction *action : fixture.menu.actions()) {
        if (action->text() == QStringLiteral("立即刷新")) {
            refreshAction = action;
            break;
        }
    }
    QVERIFY(refreshAction);
    refreshAction->trigger();

    QCOMPARE(editorA->renderCount(), 1);
    QCOMPARE(editorB->renderCount(), 1);
}

void PreviewControllerDocumentIdentityTest::rapidSwitchAndTypingKeepLatestDocument()
{
    Fixture fixture;
    auto *editorA = new QsciScintilla(QStringLiteral("a.md"),
                                     QStringLiteral("# A"));
    auto *editorB = new QsciScintilla(QStringLiteral("b.md"),
                                     QStringLiteral("# B"));
    fixture.tabs.addTab(editorA, QStringLiteral("A"));
    fixture.tabs.addTab(editorB, QStringLiteral("B"));
    fixture.tabs.setCurrentWidget(editorA);
    fixture.renderNow();

    fixture.tabs.setCurrentWidget(editorB);
    fixture.pollEditor();
    fixture.tabs.setCurrentWidget(editorA);
    fixture.pollEditor();
    editorA->edit(QStringLiteral("# A2"));
    editorA->edit(QStringLiteral("# A3"));
    editorA->edit(QStringLiteral("# A4"));

    QTest::qWait(250);
    QCOMPARE(editorA->renderCount(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(editorA->renderCount(), 2, 1000);
    QCOMPARE(editorB->renderCount(), 0);
}

QTEST_MAIN(PreviewControllerDocumentIdentityTest)

#include "preview_controller_document_identity_test.moc"
