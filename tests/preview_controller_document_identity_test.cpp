#include "markdown_preview_dock.h"
#include "host_adapter.h"
#include "preview_controller.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QScrollBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
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

    void renameTo(const QString &filePath)
    {
        setProperty("filePath", filePath);
    }

    void setSimulateDelayedLayout(bool enabled)
    {
        m_simulateDelayedLayout = enabled;
    }

    void setRenderDelayMs(int delayMs)
    {
        m_renderDelayMs = delayMs;
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
        if (m_renderDelayMs > 0) {
            QTest::qWait(m_renderDelayMs);
        }
        if (m_textEdit) {
            m_textEdit->setMarkdown(m_markdown);
            m_textEdit->verticalScrollBar()->setRange(0, 100);
            m_textEdit->verticalScrollBar()->setValue(0);
            if (m_simulateDelayedLayout) {
                QPointer<QTextEdit> textEdit = m_textEdit;
                QTimer::singleShot(10, m_textEdit, [textEdit]() {
                    if (textEdit) {
                        textEdit->verticalScrollBar()->setRange(0, 160);
                    }
                });
                QTimer::singleShot(45, m_textEdit, [textEdit]() {
                    if (textEdit) {
                        textEdit->verticalScrollBar()->setRange(0, 240);
                    }
                });
            }
        }
    }

private:
    QString m_markdown;
    QPointer<QWidget> m_preview;
    QPointer<QTextEdit> m_textEdit;
    int m_renderCount = 0;
    bool m_simulateDelayedLayout = false;
    int m_renderDelayMs = 0;
};

class PreviewControllerDocumentIdentityTest final : public QObject
{
    Q_OBJECT

private slots:
    void previewScrollCannotMoveNewActiveEditor();
    void manualRefreshUsesCurrentTab();
    void rapidSwitchAndTypingKeepLatestDocument();
    void hiddenDockExportRefreshesLatestDocument();
    void rapidSwitchExportUsesCurrentDocument();
    void messageAndMismatchedPreviewCannotBeExported();
    void snapshotWriterHandlesCancelAndAtomicReplacement();
    void filePathChangeRefreshesTypeMetadataAndResources();
    void refreshPreservesReadingPositionOnlyWhenSyncIsDisabled();
    void unchangedScheduledRefreshDoesNotRenderAgain();
    void switchingBackReusesUnchangedNativePreview();
    void largeFileDefersAutomaticRefreshUntilManualRequest();
    void slowRenderEnablesManualRefreshPolicy();
    void injectedHostAdapterAvoidsHostObjectNameAssumptions();
    void nativePreviewCacheEvictsAndRecreatesLeastRecentlyUsedDocument();
    void tabChangeSynchronizesWithoutPollingDelay();
    void editorRangeChangeUpdatesPreviewRatio();
    void hiddenDockStopsFallbackPolling();
};

class TestHostAdapter final : public HostAdapter
{
public:
    explicit TestHostAdapter(QWidget *editor)
        : m_editor(editor)
    {
    }

    QWidget *currentEditor() const override { return m_editor; }
    QMetaObject::Connection connectActiveEditorChanged(
        QObject *, std::function<void()>) override
    {
        return QMetaObject::Connection();
    }
    ScrollConnections connectEditorScrollChanged(
        QWidget *, QObject *, std::function<void()>) override
    {
        return ScrollConnections();
    }
    QString filePath(QWidget *) const override
    {
        return QStringLiteral("/virtual/injected.md");
    }
    bool isFilePathChangeEvent(QEvent *) const override { return false; }
    bool isEditorContextMenu(QMenu *, QWidget *) const override { return false; }
    bool isMarkdownContextAction(QAction *) const override { return false; }
    bool bridgeMarkdownContextAction(QAction *, QWidget *) override { return false; }
    PreviewResult ensurePreview(QWidget *editor) override
    {
        PreviewResult result;
        if (editor != m_editor) {
            result.error = QStringLiteral("unexpected editor");
            return result;
        }
        if (!m_preview) {
            m_preview = new QWidget(editor);
            auto *layout = new QVBoxLayout(m_preview);
            m_textEdit = new QTextEdit(m_preview);
            layout->addWidget(m_textEdit);
            m_textEdit->setMarkdown(QStringLiteral("# Injected adapter"));
            result.created = true;
        }
        result.window = m_preview;
        result.textEdit = m_textEdit;
        return result;
    }
    bool refreshPreview(QWidget *, qint64 *durationMs, QString *) override
    {
        if (durationMs) {
            *durationMs = 0;
        }
        ++m_refreshCount;
        return true;
    }
    bool disconnectImmediateRefresh(QWidget *, bool) override { return false; }
    bool previewIsCurrent(QWidget *) const override { return m_current; }
    void setPreviewCurrent(QWidget *, bool current) override { m_current = current; }
    bool releasePreview(QWidget *, QString *) override
    {
        if (m_preview) {
            m_preview->deleteLater();
            m_preview = nullptr;
            m_textEdit = nullptr;
        }
        m_current = false;
        return true;
    }

    int refreshCount() const { return m_refreshCount; }

private:
    QWidget *m_editor = nullptr;
    QWidget *m_preview = nullptr;
    QTextEdit *m_textEdit = nullptr;
    int m_refreshCount = 0;
    bool m_current = false;
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

    QAction *action(const QString &text)
    {
        for (QAction *candidate : menu.actions()) {
            if (candidate && candidate->text() == text) {
                return candidate;
            }
        }
        return nullptr;
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

void PreviewControllerDocumentIdentityTest::hiddenDockExportRefreshesLatestDocument()
{
    Fixture fixture;
    auto *editor = new QsciScintilla(QStringLiteral("notes/current.md"),
                                     QStringLiteral("# Initial"));
    fixture.tabs.addTab(editor, QStringLiteral("Current"));
    fixture.tabs.setCurrentWidget(editor);
    fixture.renderNow();
    QCOMPARE(editor->renderCount(), 1);

    fixture.dock()->hide();
    QCoreApplication::processEvents();
    editor->edit(QStringLiteral("# Latest export"));

    QByteArray html;
    QString sourceFilePath;
    QVERIFY(fixture.controller.currentHtmlSnapshot(&html, &sourceFilePath));
    QVERIFY(fixture.dock()->isHidden());
    QCOMPARE(editor->renderCount(), 2);
    QCOMPARE(sourceFilePath, QStringLiteral("notes/current.md"));
    QVERIFY(html.contains("Latest export"));
    QVERIFY(!html.contains("Initial"));
}

void PreviewControllerDocumentIdentityTest::rapidSwitchExportUsesCurrentDocument()
{
    Fixture fixture;
    auto *editorA = new QsciScintilla(QStringLiteral("a.md"),
                                     QStringLiteral("# Document A"));
    auto *editorB = new QsciScintilla(QStringLiteral("b.md"),
                                     QStringLiteral("# Document B"));
    fixture.tabs.addTab(editorA, QStringLiteral("A"));
    fixture.tabs.addTab(editorB, QStringLiteral("B"));
    fixture.tabs.setCurrentWidget(editorA);
    fixture.renderNow();

    fixture.tabs.setCurrentWidget(editorB);
    QByteArray html;
    QString sourceFilePath;
    QVERIFY(fixture.controller.currentHtmlSnapshot(&html, &sourceFilePath));
    QCOMPARE(sourceFilePath, QStringLiteral("b.md"));
    QVERIFY(html.contains("Document B"));
    QVERIFY(!html.contains("Document A"));
    QCOMPARE(editorA->renderCount(), 1);
    QCOMPARE(editorB->renderCount(), 1);
}

void PreviewControllerDocumentIdentityTest::messageAndMismatchedPreviewCannotBeExported()
{
    Fixture fixture;
    auto *editor = new QsciScintilla(QStringLiteral("document.md"),
                                     QStringLiteral("# Document"));
    fixture.tabs.addTab(editor, QStringLiteral("Document"));
    fixture.tabs.setCurrentWidget(editor);
    fixture.renderNow();

    fixture.dock()->showMessage(QStringLiteral("Error"),
                                QStringLiteral("This is not document data"));
    QVERIFY(fixture.dock()->htmlSnapshotFor(editor, 1).isEmpty());

    auto *unsupported = new QsciScintilla(QStringLiteral("notes.txt"),
                                          QStringLiteral("plain text"));
    fixture.tabs.addTab(unsupported, QStringLiteral("Text"));
    fixture.tabs.setCurrentWidget(unsupported);
    fixture.pollEditor();

    QAction *exportAction = fixture.action(QStringLiteral("导出 HTML…"));
    QVERIFY(exportAction);
    QVERIFY(!exportAction->isEnabled());

    QByteArray html;
    QString sourceFilePath;
    QVERIFY(!fixture.controller.currentHtmlSnapshot(&html, &sourceFilePath));
    QVERIFY(html.isEmpty());
    QVERIFY(sourceFilePath.isEmpty());
}

void PreviewControllerDocumentIdentityTest::snapshotWriterHandlesCancelAndAtomicReplacement()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString targetPath = directory.filePath(QStringLiteral("preview.html"));

    QFile existing(targetPath);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QCOMPARE(existing.write("old"), qint64(3));
    existing.close();

    QString errorMessage;
    QVERIFY(!MarkdownPreviewDock::writeHtmlSnapshot(
        QByteArray(), targetPath, &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArray("old"));
    existing.close();

    errorMessage.clear();
    QVERIFY(!MarkdownPreviewDock::writeHtmlSnapshot(
        QByteArray("new"), QString(), &errorMessage));
    QVERIFY(!errorMessage.isEmpty());

    const QString blockingPath = directory.filePath(QStringLiteral("not-a-directory"));
    QFile blockingFile(blockingPath);
    QVERIFY(blockingFile.open(QIODevice::WriteOnly));
    QCOMPARE(blockingFile.write("sentinel"), qint64(8));
    blockingFile.close();
    errorMessage.clear();
    QVERIFY(!MarkdownPreviewDock::writeHtmlSnapshot(
        QByteArray("new"),
        blockingPath + QStringLiteral("/preview.html"), &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
    QVERIFY(blockingFile.open(QIODevice::ReadOnly));
    QCOMPARE(blockingFile.readAll(), QByteArray("sentinel"));
    blockingFile.close();

    errorMessage.clear();
    QVERIFY(MarkdownPreviewDock::writeHtmlSnapshot(
        QByteArray("new"), targetPath, &errorMessage));
    QVERIFY(errorMessage.isEmpty());
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArray("new"));
}

void PreviewControllerDocumentIdentityTest::filePathChangeRefreshesTypeMetadataAndResources()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("旧目录")));
    QVERIFY(root.mkpath(QStringLiteral("新 目录")));

    const QString oldPath = root.filePath(QStringLiteral("旧目录/文档.md"));
    const QString newPath = root.filePath(QStringLiteral("新 目录/文档.md"));

    Fixture fixture;
    auto *editor = new QsciScintilla(
        oldPath, QStringLiteral("![image](same-name.png)"));
    fixture.tabs.addTab(editor, QStringLiteral("Document"));
    fixture.tabs.setCurrentWidget(editor);
    fixture.renderNow();

    QTextEdit *previewTextEdit = fixture.dock()->findChild<QTextEdit *>(
        QStringLiteral("textEdit"));
    QVERIFY(previewTextEdit);
    QCOMPARE(previewTextEdit->document()->baseUrl(),
             QUrl::fromLocalFile(QFileInfo(oldPath).absolutePath() + QLatin1Char('/')));

    editor->renameTo(newPath);
    QTRY_COMPARE_WITH_TIMEOUT(editor->renderCount(), 2, 1000);
    QCOMPARE(previewTextEdit->document()->baseUrl(),
             QUrl::fromLocalFile(QFileInfo(newPath).absolutePath() + QLatin1Char('/')));

    QByteArray html;
    QString sourceFilePath;
    QVERIFY(fixture.controller.currentHtmlSnapshot(&html, &sourceFilePath));
    QCOMPARE(sourceFilePath, newPath);

    QAction *exportAction = fixture.action(QStringLiteral("导出 HTML…"));
    QVERIFY(exportAction);
    QVERIFY(exportAction->isEnabled());

    editor->renameTo(root.filePath(QStringLiteral("新 目录/文档.txt")));
    QTRY_VERIFY_WITH_TIMEOUT(!exportAction->isEnabled(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.dock()->hasPreviewFor(editor, 2), 1000);

    editor->renameTo(QString());
    QTRY_VERIFY_WITH_TIMEOUT(exportAction->isEnabled(), 1000);
    editor->renameTo(root.filePath(QStringLiteral("新 目录/未命名.md")));
    QTRY_COMPARE_WITH_TIMEOUT(editor->renderCount(), 3, 1000);
    QVERIFY(fixture.controller.currentHtmlSnapshot(&html, &sourceFilePath));
    QCOMPARE(sourceFilePath,
             root.filePath(QStringLiteral("新 目录/未命名.md")));
}

void PreviewControllerDocumentIdentityTest::refreshPreservesReadingPositionOnlyWhenSyncIsDisabled()
{
    Fixture fixture;
    auto *editorA = new QsciScintilla(QStringLiteral("a.md"),
                                     QStringLiteral("# A"));
    auto *editorB = new QsciScintilla(QStringLiteral("b.md"),
                                     QStringLiteral("# B"));
    editorA->setSimulateDelayedLayout(true);
    fixture.tabs.addTab(editorA, QStringLiteral("A"));
    fixture.tabs.addTab(editorB, QStringLiteral("B"));
    fixture.tabs.setCurrentWidget(editorA);
    fixture.renderNow();

    QTextEdit *previewTextEdit = fixture.dock()->findChild<QTextEdit *>(
        QStringLiteral("textEdit"));
    QVERIFY(previewTextEdit);
    QScrollBar *previewBar = previewTextEdit->verticalScrollBar();
    QVERIFY(previewBar);
    QTRY_COMPARE_WITH_TIMEOUT(previewBar->maximum(), 240, 500);

    QToolButton *syncButton = nullptr;
    const QList<QToolButton *> buttons = fixture.dock()->findChildren<QToolButton *>();
    for (QToolButton *button : buttons) {
        if (button && button->text() == QStringLiteral("同步滚动")) {
            syncButton = button;
            break;
        }
    }
    QVERIFY(syncButton);
    QVERIFY(syncButton->isChecked());
    syncButton->click();
    QVERIFY(!syncButton->isChecked());

    previewBar->setValue(180);
    editorA->edit(QStringLiteral("# A updated"));
    QTRY_COMPARE_WITH_TIMEOUT(editorA->renderCount(), 2, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(previewBar->maximum(), 240, 500);
    QTRY_COMPARE_WITH_TIMEOUT(previewBar->value(), 180, 500);

    QAction *refreshAction = fixture.action(QStringLiteral("立即刷新"));
    QVERIFY(refreshAction);
    previewBar->setValue(120);
    refreshAction->trigger();
    QCOMPARE(editorA->renderCount(), 3);
    QTRY_COMPARE_WITH_TIMEOUT(previewBar->value(), 120, 500);

    fixture.tabs.setCurrentWidget(editorB);
    fixture.renderNow();
    QTextEdit *previewB = nullptr;
    const QList<QTextEdit *> previewEditors =
        fixture.dock()->findChildren<QTextEdit *>(QStringLiteral("textEdit"));
    for (QTextEdit *candidate : previewEditors) {
        if (candidate && candidate->isVisible()) {
            previewB = candidate;
            break;
        }
    }
    QVERIFY(previewB);
    QCOMPARE(previewB->verticalScrollBar()->value(), 0);

    syncButton->click();
    QVERIFY(syncButton->isChecked());
    editorB->verticalScrollBar()->setValue(80);
    editorB->edit(QStringLiteral("# B updated"));
    QTRY_COMPARE_WITH_TIMEOUT(editorB->renderCount(), 2, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(previewB->verticalScrollBar()->value(), 80, 500);
}

void PreviewControllerDocumentIdentityTest::unchangedScheduledRefreshDoesNotRenderAgain()
{
    Fixture fixture;
    auto *editor = new QsciScintilla(QStringLiteral("ordinary.md"),
                                     QStringLiteral("# Current"));
    fixture.tabs.addTab(editor, QStringLiteral("Ordinary"));
    fixture.tabs.setCurrentWidget(editor);
    fixture.renderNow();
    QCOMPARE(editor->renderCount(), 1);

    QVERIFY(QMetaObject::invokeMethod(&fixture.controller, "scheduleRender",
                                     Qt::DirectConnection));
    QTest::qWait(450);
    QCOMPARE(editor->renderCount(), 1);
}

void PreviewControllerDocumentIdentityTest::switchingBackReusesUnchangedNativePreview()
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
    fixture.renderNow();
    QCOMPARE(editorA->renderCount(), 1);
    QCOMPARE(editorB->renderCount(), 1);

    fixture.tabs.setCurrentWidget(editorA);
    fixture.pollEditor();
    QTest::qWait(450);
    QCOMPARE(editorA->renderCount(), 1);
    QTextEdit *previewTextEdit = fixture.dock()->findChild<QTextEdit *>(
        QStringLiteral("textEdit"));
    QVERIFY(previewTextEdit);
    QVERIFY(previewTextEdit->toPlainText().contains(QStringLiteral("A")));
}

void PreviewControllerDocumentIdentityTest::largeFileDefersAutomaticRefreshUntilManualRequest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filePath = directory.filePath(QStringLiteral("large.md"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray content(1024 * 1024, 'x');
    QCOMPARE(file.write(content), qint64(content.size()));
    file.close();

    Fixture fixture;
    auto *editor = new QsciScintilla(filePath, QStringLiteral("# Initial"));
    fixture.tabs.addTab(editor, QStringLiteral("Large"));
    fixture.tabs.setCurrentWidget(editor);
    fixture.renderNow();
    QCOMPARE(editor->renderCount(), 1);

    editor->edit(QStringLiteral("# Latest"));
    QTest::qWait(500);
    QCOMPARE(editor->renderCount(), 1);

    QLabel *documentLabel = fixture.dock()->findChild<QLabel *>(
        QStringLiteral("NddMarkdownPreviewDocumentLabel"));
    QVERIFY(documentLabel);
    QVERIFY(documentLabel->text().contains(QStringLiteral("待手工刷新")));

    QAction *refreshAction = fixture.action(QStringLiteral("立即刷新"));
    QVERIFY(refreshAction);
    refreshAction->trigger();
    QCOMPARE(editor->renderCount(), 2);
    QTextEdit *previewTextEdit = fixture.dock()->findChild<QTextEdit *>(
        QStringLiteral("textEdit"));
    QVERIFY(previewTextEdit);
    QVERIFY(previewTextEdit->toPlainText().contains(QStringLiteral("Latest")));
}

void PreviewControllerDocumentIdentityTest::slowRenderEnablesManualRefreshPolicy()
{
    Fixture fixture;
    auto *editor = new QsciScintilla(QStringLiteral("measured.md"),
                                     QStringLiteral("# Initial"));
    editor->setRenderDelayMs(800);
    fixture.tabs.addTab(editor, QStringLiteral("Measured"));
    fixture.tabs.setCurrentWidget(editor);
    fixture.renderNow();
    QCOMPARE(editor->renderCount(), 1);

    editor->edit(QStringLiteral("# Deferred"));
    QTest::qWait(2200);
    QCOMPARE(editor->renderCount(), 1);

    QAction *refreshAction = fixture.action(QStringLiteral("立即刷新"));
    QVERIFY(refreshAction);
    refreshAction->trigger();
    QCOMPARE(editor->renderCount(), 2);
}

void PreviewControllerDocumentIdentityTest::injectedHostAdapterAvoidsHostObjectNameAssumptions()
{
    QMainWindow window;
    auto *editor = new QWidget(&window);
    window.setCentralWidget(editor);
    TestHostAdapter adapter(editor);
    PreviewController controller(&window, &adapter);
    QMenu menu(&window);
    QVERIFY(controller.installMenu(&menu));
    MarkdownPreviewDock *dock = window.findChild<MarkdownPreviewDock *>();
    QVERIFY(dock);
    dock->show();
    QCoreApplication::processEvents();
    QVERIFY(QMetaObject::invokeMethod(&controller, "renderNow",
                                     Qt::DirectConnection));

    QByteArray html;
    QString sourcePath;
    QVERIFY(controller.currentHtmlSnapshot(&html, &sourcePath));
    QVERIFY(html.contains("Injected adapter"));
    QCOMPARE(sourcePath, QStringLiteral("/virtual/injected.md"));
    QCOMPARE(adapter.refreshCount(), 0);
}

void PreviewControllerDocumentIdentityTest::nativePreviewCacheEvictsAndRecreatesLeastRecentlyUsedDocument()
{
    Fixture fixture;
    QList<QsciScintilla *> editors;
    for (int i = 0; i < 4; ++i) {
        auto *editor = new QsciScintilla(
            QStringLiteral("document-%1.md").arg(i),
            QStringLiteral("# Document %1").arg(i));
        editors.append(editor);
    fixture.tabs.addTab(editor, QString::number(i));
        fixture.tabs.setCurrentWidget(editor);
        fixture.renderNow();
        if (i == 0) {
            QToolButton *syncButton = nullptr;
            const QList<QToolButton *> buttons =
                fixture.dock()->findChildren<QToolButton *>();
            for (QToolButton *button : buttons) {
                if (button && button->text() == QStringLiteral("同步滚动")) {
                    syncButton = button;
                    break;
                }
            }
            QVERIFY(syncButton);
            syncButton->click();
            QTextEdit *textEdit = fixture.dock()->findChild<QTextEdit *>(
                QStringLiteral("textEdit"));
            QVERIFY(textEdit);
            textEdit->verticalScrollBar()->setRange(0, 100);
            textEdit->verticalScrollBar()->setValue(65);
        }
    }

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QCOMPARE(fixture.dock()->findChildren<QWidget *>(
                 QStringLiteral("MarkdownViewClass")).size(), 3);
    QCOMPARE(editors.at(0)->renderCount(), 1);

    fixture.tabs.setCurrentWidget(editors.at(0));
    fixture.renderNow();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();

    QCOMPARE(editors.at(0)->renderCount(), 2);
    QCOMPARE(fixture.dock()->findChildren<QWidget *>(
                 QStringLiteral("MarkdownViewClass")).size(), 3);
    QTextEdit *recreatedTextEdit = nullptr;
    const QList<QTextEdit *> textEdits =
        fixture.dock()->findChildren<QTextEdit *>(QStringLiteral("textEdit"));
    for (QTextEdit *textEdit : textEdits) {
        if (textEdit && textEdit->isVisible()) {
            recreatedTextEdit = textEdit;
            break;
        }
    }
    QVERIFY(recreatedTextEdit);
    QTRY_COMPARE_WITH_TIMEOUT(recreatedTextEdit->verticalScrollBar()->value(), 65, 500);

    QPointer<QWidget> closingPreview = qobject_cast<QWidget *>(
        editors.at(3)->property("_markdownview_native_preview").value<QObject *>());
    QVERIFY(closingPreview);
    fixture.tabs.removeTab(fixture.tabs.indexOf(editors.at(3)));
    delete editors.at(3);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QVERIFY(closingPreview.isNull());
}

void PreviewControllerDocumentIdentityTest::tabChangeSynchronizesWithoutPollingDelay()
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
    QTRY_COMPARE_WITH_TIMEOUT(editorB->renderCount(), 1, 1000);
    QTextEdit *activePreview = nullptr;
    const QList<QTextEdit *> previews =
        fixture.dock()->findChildren<QTextEdit *>(QStringLiteral("textEdit"));
    for (QTextEdit *preview : previews) {
        if (preview && preview->isVisible()) {
            activePreview = preview;
            break;
        }
    }
    QVERIFY(activePreview);
    QVERIFY(activePreview->toPlainText().contains(QStringLiteral("B")));
}

void PreviewControllerDocumentIdentityTest::editorRangeChangeUpdatesPreviewRatio()
{
    Fixture fixture;
    auto *editor = new QsciScintilla(QStringLiteral("range.md"),
                                    QStringLiteral("# Range"));
    fixture.tabs.addTab(editor, QStringLiteral("Range"));
    fixture.tabs.setCurrentWidget(editor);
    fixture.renderNow();

    QTextEdit *preview = fixture.dock()->findChild<QTextEdit *>(
        QStringLiteral("textEdit"));
    QVERIFY(preview);
    preview->verticalScrollBar()->setRange(0, 100);

    editor->verticalScrollBar()->setRange(0, 100);
    editor->verticalScrollBar()->setValue(50);
    QTRY_COMPARE_WITH_TIMEOUT(preview->verticalScrollBar()->value(), 50, 250);

    // Keep value=50 but expand the editor range. The ratio changes from 0.5
    // to 0.25 and must be propagated even though valueChanged is not emitted.
    editor->verticalScrollBar()->setRange(0, 200);
    QTRY_COMPARE_WITH_TIMEOUT(preview->verticalScrollBar()->value(), 25, 250);
}

void PreviewControllerDocumentIdentityTest::hiddenDockStopsFallbackPolling()
{
    Fixture fixture;
    fixture.dock()->show();
    QCoreApplication::processEvents();

    QTimer *fallbackTimer = nullptr;
    const QList<QTimer *> timers = fixture.controller.findChildren<QTimer *>();
    for (QTimer *timer : timers) {
        if (timer && timer->interval() == 1500 && !timer->isSingleShot()) {
            fallbackTimer = timer;
            break;
        }
    }
    QVERIFY(fallbackTimer);
    QVERIFY(fallbackTimer->isActive());

    fixture.dock()->hide();
    QCoreApplication::processEvents();
    QVERIFY(!fallbackTimer->isActive());
}

QTEST_MAIN(PreviewControllerDocumentIdentityTest)

#include "preview_controller_document_identity_test.moc"
