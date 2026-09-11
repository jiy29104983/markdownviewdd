#include "markdown_preview_dock.h"
#include "host_adapter.h"
#include "preview_controller.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QComboBox>
#include <QClipboard>
#include <QTextBrowser>
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
#include <QTextCursor>
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
            m_textEdit->setProperty("layoutFinished", false);
            m_textEdit->setMarkdown(m_markdown);
            if (!m_simulateDelayedLayout) {
                m_textEdit->verticalScrollBar()->setRange(0, 100);
            }
            m_textEdit->verticalScrollBar()->setValue(0);
            if (m_simulateDelayedLayout) {
                QPointer<QTextEdit> textEdit = m_textEdit;
                QTimer::singleShot(10, m_textEdit, [textEdit]() {
                    if (textEdit) {
                        QTextCursor cursor(textEdit->document());
                        cursor.movePosition(QTextCursor::End);
                        cursor.insertText(QStringLiteral("\nDelayed layout paragraph\n").repeated(60));
                    }
                });
                QTimer::singleShot(45, m_textEdit, [textEdit]() {
                    if (textEdit) {
                        QTextCursor cursor(textEdit->document());
                        cursor.movePosition(QTextCursor::End);
                        cursor.insertText(QStringLiteral("\nFinal layout paragraph\n").repeated(60));
                        textEdit->setProperty("layoutFinished", true);
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
    void backgroundEditInvalidatesCachedPreview();
    void backgroundPathChangeInvalidatesCachedPreview();
    void reverseScrollReachesHostReceiver();
    void slowDocumentPolicySurvivesTabSwitch();
    void manualModeDoesNotRenderOnShowEditOrReopen();
    void manualTabSwitchUsesOnlyMatchingSnapshot();
    void modeChangesCancelQueuedWorkAndResumeDebounce();
    void protectedDocumentKeepsModeAndFreshnessSeparate();
    void manualHiddenExportUsesCurrentTab();
    void staleSnapshotDisablesBothScrollDirections();
    void failedRefreshRetainsOnlyUntouchedSnapshot_data();
    void failedRefreshRetainsOnlyUntouchedSnapshot();
    void failedInitialRenderProvidesRetry();
    void renderReentrancyCannotPublishChangedSource();
    void manualCacheEvictionNeverRendersImplicitly();
    void manualPathChangeAndUnsupportedState();
    void stateScreenshots();
    void destroyedSnapshotClearsStatusWithoutManualRender();
    void renderTabSwitchAndModeChangeDiscardObsoleteResult();
    void automaticFirstOpenRetainsExistingLargeFilePolicy();
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
        if (failEnsure) {
            result.error = QStringLiteral("模拟宿主创建失败");
            return result;
        }
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
    bool refreshPreview(QWidget *, qint64 *durationMs, QString *error) override
    {
        if (durationMs) {
            *durationMs = 0;
        }
        ++m_refreshCount;
        if (failRefresh) {
            if (mutateOnFailure) {
                m_textEdit->setPlainText(QStringLiteral("Partially overwritten document"));
            }
            if (error) {
                *error = QStringLiteral("模拟宿主更新失败");
            }
            return false;
        }
        m_textEdit->setMarkdown(QStringLiteral("# Recovered snapshot"));
        if (onRefresh) {
            onRefresh();
        }
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
    bool failEnsure = false;
    bool failRefresh = false;
    bool mutateOnFailure = false;
    std::function<void()> onRefresh;

private:
    QWidget *m_editor = nullptr;
    QWidget *m_preview = nullptr;
    QTextEdit *m_textEdit = nullptr;
    int m_refreshCount = 0;
    bool m_current = false;
};

namespace {
QString longMarkdown(const QString &heading)
{
    return heading + QStringLiteral("\n\nA paragraph for scrolling.\n").repeated(200);
}

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
                                     longMarkdown(QStringLiteral("# A")));
    auto *editorB = new QsciScintilla(QStringLiteral("b.md"),
                                     longMarkdown(QStringLiteral("# B")));
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
    QTRY_VERIFY_WITH_TIMEOUT(previewTextEdit->property("layoutFinished").toBool(), 1000);
    QVERIFY(previewBar->maximum() > 0);

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

    previewBar->setValue(qRound(previewBar->maximum() * 0.75));
    editorA->edit(longMarkdown(QStringLiteral("# A updated")));
    QTRY_COMPARE_WITH_TIMEOUT(editorA->renderCount(), 2, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(previewTextEdit->property("layoutFinished").toBool(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(fixture.dock()->scrollRatio() - 0.75) < 0.01, 1000);

    QAction *refreshAction = fixture.action(QStringLiteral("立即刷新"));
    QVERIFY(refreshAction);
    previewBar->setValue(qRound(previewBar->maximum() * 0.5));
    refreshAction->trigger();
    QCOMPARE(editorA->renderCount(), 3);
    QTRY_VERIFY_WITH_TIMEOUT(previewTextEdit->property("layoutFinished").toBool(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(fixture.dock()->scrollRatio() - 0.5) < 0.01, 1000);

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
    editorB->edit(longMarkdown(QStringLiteral("# B updated")));
    QTRY_COMPARE_WITH_TIMEOUT(editorB->renderCount(), 2, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(fixture.dock()->scrollRatio() - 0.8) < 0.01, 1000);
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
    QCOMPARE(documentLabel->text(), QStringLiteral("large.md"));
    QLabel *statusLabel = fixture.dock()->findChild<QLabel *>(
        QStringLiteral("NddMarkdownPreviewStatusLabel"));
    QVERIFY(statusLabel);
    QVERIFY(statusLabel->text().contains(QStringLiteral("待手工刷新")));

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
            longMarkdown(QStringLiteral("# Document %1").arg(i)));
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
            QTest::qWait(100);
            QVERIFY(textEdit->verticalScrollBar()->maximum() > 0);
            textEdit->verticalScrollBar()->setValue(
                qRound(textEdit->verticalScrollBar()->maximum() * 0.65));
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
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(fixture.dock()->scrollRatio() - 0.65) < 0.01, 1000);

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

void PreviewControllerDocumentIdentityTest::backgroundEditInvalidatesCachedPreview()
{
    Fixture fixture;
    auto *a = new QsciScintilla(QStringLiteral("a.md"), QStringLiteral("# Old A"));
    auto *b = new QsciScintilla(QStringLiteral("b.md"), QStringLiteral("# B"));
    fixture.tabs.addTab(a, QStringLiteral("A"));
    fixture.tabs.addTab(b, QStringLiteral("B"));
    fixture.tabs.setCurrentWidget(a);
    fixture.renderNow();
    fixture.tabs.setCurrentWidget(b);
    fixture.renderNow();

    a->edit(QStringLiteral("# New A after replace all"));
    QCOMPARE(a->renderCount(), 1);
    QCOMPARE(b->renderCount(), 1);
    fixture.tabs.setCurrentWidget(a);
    QByteArray html;
    QString source;
    QVERIFY(fixture.controller.currentHtmlSnapshot(&html, &source));
    QVERIFY(html.contains("New A after replace all"));
    QVERIFY(!html.contains("Old A"));
    QCOMPARE(a->renderCount(), 2);

    a->edit(QStringLiteral("# Latest active A"));
    QTRY_COMPARE_WITH_TIMEOUT(a->renderCount(), 3, 1000);
    QVERIFY(fixture.controller.currentHtmlSnapshot(&html, &source));
    QVERIFY(html.contains("Latest active A"));
}

void PreviewControllerDocumentIdentityTest::backgroundPathChangeInvalidatesCachedPreview()
{
    Fixture fixture;
    auto *a = new QsciScintilla(QStringLiteral("old/a.md"), QStringLiteral("# A"));
    auto *b = new QsciScintilla(QStringLiteral("b.md"), QStringLiteral("# B"));
    fixture.tabs.addTab(a, QStringLiteral("A"));
    fixture.tabs.addTab(b, QStringLiteral("B"));
    fixture.tabs.setCurrentWidget(a);
    fixture.renderNow();
    fixture.tabs.setCurrentWidget(b);
    fixture.renderNow();
    a->renameTo(QStringLiteral("new/a.md"));
    QCOMPARE(a->renderCount(), 1);
    fixture.tabs.setCurrentWidget(a);
    QByteArray html;
    QString source;
    QVERIFY(fixture.controller.currentHtmlSnapshot(&html, &source));
    QCOMPARE(source, QStringLiteral("new/a.md"));
    QCOMPARE(a->renderCount(), 2);
}

void PreviewControllerDocumentIdentityTest::reverseScrollReachesHostReceiver()
{
    Fixture fixture;
    auto *editor = new QsciScintilla(QStringLiteral("a.md"), QStringLiteral("# A"));
    fixture.tabs.addTab(editor, QStringLiteral("A"));
    fixture.renderNow();
    QScrollBar *bar = editor->verticalScrollBar();
    bar->setRange(0, 100);
    bar->setValue(0);
    int hostFirstVisibleLine = 0;
    // The real QScintilla handleVSb slot consumes this same signal to scroll
    // its content. Checking only the scrollbar value misses a broken bridge.
    QObject::connect(bar, &QScrollBar::valueChanged, &fixture.window,
                     [&hostFirstVisibleLine](int value) { hostFirstVisibleLine = value; });
    QVERIFY(QMetaObject::invokeMethod(fixture.dock(), "previewScrollRatioChanged",
                                     Qt::DirectConnection, Q_ARG(double, 0.8)));
    QCOMPARE(bar->value(), 80);
    QCOMPARE(hostFirstVisibleLine, 80);
}

void PreviewControllerDocumentIdentityTest::slowDocumentPolicySurvivesTabSwitch()
{
    Fixture fixture;
    auto *a = new QsciScintilla(QStringLiteral("slow.md"), QStringLiteral("# Slow"));
    auto *b = new QsciScintilla(QStringLiteral("b.md"), QStringLiteral("# B"));
    a->setRenderDelayMs(800);
    fixture.tabs.addTab(a, QStringLiteral("Slow"));
    fixture.tabs.addTab(b, QStringLiteral("B"));
    fixture.tabs.setCurrentWidget(a);
    fixture.renderNow();
    fixture.tabs.setCurrentWidget(b);
    fixture.renderNow();
    b->edit(QStringLiteral("# Normal automatic refresh"));
    QTRY_COMPARE_WITH_TIMEOUT(b->renderCount(), 2, 1000);
    fixture.tabs.setCurrentWidget(a);
    QByteArray html;
    QString source;
    QVERIFY(fixture.controller.currentHtmlSnapshot(&html, &source));
    QCOMPARE(a->renderCount(), 1);
    a->edit(QStringLiteral("# Slow edit after switching back"));
    QTest::qWait(2200);
    QCOMPARE(a->renderCount(), 1);

    // Only a genuine fast full render can re-enable automatic refresh.
    a->setRenderDelayMs(0);
    QAction *refresh = fixture.action(QStringLiteral("立即刷新"));
    QVERIFY(refresh);
    refresh->trigger();
    QCOMPARE(a->renderCount(), 2);
    a->edit(QStringLiteral("# Fast again"));
    QTRY_COMPARE_WITH_TIMEOUT(a->renderCount(), 3, 1000);
}


void PreviewControllerDocumentIdentityTest::manualModeDoesNotRenderOnShowEditOrReopen()
{
    Fixture fixture;
    auto *editor = new QsciScintilla(QStringLiteral("manual.md"), QStringLiteral("# Initial"));
    fixture.tabs.addTab(editor, QStringLiteral("Manual"));
    fixture.action(QStringLiteral("手动刷新"))->trigger();
    fixture.dock()->show();
    QSignalSpy changed(&fixture.controller, &PreviewController::previewStatusChanged);
    for (int i = 0; i < 20; ++i) {
        editor->edit(QStringLiteral("# Edit %1").arg(i));
        QTest::qWait(10);
    }
    fixture.dock()->hide();
    fixture.dock()->show();
    QTest::qWait(2100);
    QCOMPARE(editor->renderCount(), 0);
    const PreviewStatus status = fixture.controller.previewStatus();
    QCOMPARE(status.mode, RefreshMode::Manual);
    QCOMPARE(status.state, PreviewState::Pending);
    QCOMPARE(status.activeEditor.data(), editor);
    QVERIFY(!status.displayedEditor);
    QCOMPARE(status.displayedVersion, quint64(0));
    QVERIFY(changed.count() >= 20);
    QCOMPARE(fixture.dock()->findChild<QLabel *>(
        QStringLiteral("NddMarkdownPreviewDocumentLabel"))->text(), QStringLiteral("manual.md"));
    QCOMPARE(fixture.dock()->findChild<QComboBox *>()->currentIndex(), 1);
    QVERIFY(fixture.action(QStringLiteral("手动刷新"))->isChecked());
    QVERIFY(!fixture.action(QStringLiteral("自动刷新"))->isChecked());
}

void PreviewControllerDocumentIdentityTest::manualTabSwitchUsesOnlyMatchingSnapshot()
{
    Fixture fixture;
    auto *a = new QsciScintilla(QStringLiteral("a.md"), QStringLiteral("# Cached A"));
    auto *b = new QsciScintilla(QStringLiteral("b.md"), QStringLiteral("# Unseen B"));
    fixture.tabs.addTab(a, QStringLiteral("A"));
    fixture.tabs.addTab(b, QStringLiteral("B"));
    fixture.renderNow();
    const quint64 originalVersion = fixture.controller.previewStatus().displayedVersion;
    fixture.controller.setRefreshMode(RefreshMode::Manual);
    a->edit(QStringLiteral("# New A"));
    fixture.tabs.setCurrentWidget(b);
    fixture.pollEditor();
    auto *browser = fixture.dock()->findChild<QTextBrowser *>();
    QVERIFY(browser->isVisible());
    QVERIFY(browser->toPlainText().contains(QStringLiteral("点击")));
    QVERIFY(!browser->toPlainText().contains(QStringLiteral("Cached A")));
    QVERIFY(!fixture.controller.previewStatus().displayedEditor);
    a->edit(QStringLiteral("# Background A"));
    fixture.tabs.setCurrentWidget(a);
    fixture.pollEditor();
    const PreviewStatus status = fixture.controller.previewStatus();
    QCOMPARE(status.displayedEditor.data(), a);
    QCOMPARE(status.displayedVersion, originalVersion);
    QVERIFY(status.contentVersion > status.displayedVersion);
    QCOMPARE(status.mode, RefreshMode::Manual);
    QCOMPARE(status.state, PreviewState::Pending);
    QVERIFY(!browser->isVisible());
    QTextEdit *preview = fixture.dock()->findChild<QTextEdit *>(QStringLiteral("textEdit"));
    QVERIFY(preview->toPlainText().contains(QStringLiteral("Cached A")));
    QVERIFY(fixture.dock()->htmlSnapshotFor(a, originalVersion).isEmpty());
    QCOMPARE(a->renderCount(), 1);
    QCOMPARE(b->renderCount(), 0);
}

void PreviewControllerDocumentIdentityTest::modeChangesCancelQueuedWorkAndResumeDebounce()
{
    Fixture fixture;
    auto *a = new QsciScintilla(QStringLiteral("a.md"), QStringLiteral("# A"));
    auto *b = new QsciScintilla(QStringLiteral("b.md"), QStringLiteral("# B"));
    fixture.tabs.addTab(a, QStringLiteral("A"));
    fixture.tabs.addTab(b, QStringLiteral("B"));
    fixture.renderNow();
    a->edit(QStringLiteral("# Pending A"));
    fixture.action(QStringLiteral("手动刷新"))->trigger();
    QVERIFY(QMetaObject::invokeMethod(&fixture.controller, "renderScheduled", Qt::DirectConnection));
    QCOMPARE(a->renderCount(), 1);
    fixture.tabs.setCurrentWidget(b);
    QVERIFY(QMetaObject::invokeMethod(&fixture.controller, "renderScheduled", Qt::DirectConnection));
    QCOMPARE(b->renderCount(), 0);
    auto *combo = fixture.dock()->findChild<QComboBox *>();
    combo->setCurrentIndex(0);
    QVERIFY(fixture.action(QStringLiteral("自动刷新"))->isChecked());
    for (int i = 0; i < 4; ++i) {
        b->edit(QStringLiteral("# Final B %1").arg(i));
        QTest::qWait(100);
        QCOMPARE(b->renderCount(), 0);
    }
    QTRY_COMPARE_WITH_TIMEOUT(b->renderCount(), 1, 1200);
    QByteArray html;
    QString path;
    QVERIFY(fixture.controller.currentHtmlSnapshot(&html, &path));
    QVERIFY(html.contains("Final B 3"));
    QCOMPARE(a->renderCount(), 1);

    // A queued timeout from A must only schedule B's work after synchronizing.
    fixture.tabs.setCurrentWidget(a);
    fixture.pollEditor();
    fixture.tabs.setCurrentWidget(b);
    QVERIFY(QMetaObject::invokeMethod(&fixture.controller, "renderScheduled", Qt::DirectConnection));
    QCOMPARE(a->renderCount(), 1);
    QCOMPARE(b->renderCount(), 1);
}

void PreviewControllerDocumentIdentityTest::protectedDocumentKeepsModeAndFreshnessSeparate()
{
    QTemporaryDir directory;
    QFile large(directory.filePath(QStringLiteral("protected.md")));
    QVERIFY(large.open(QIODevice::WriteOnly));
    QVERIFY(large.resize(1024 * 1024));
    large.close();
    Fixture fixture;
    auto *a = new QsciScintilla(large.fileName(), QStringLiteral("# Large"));
    auto *b = new QsciScintilla(QStringLiteral("normal.md"), QStringLiteral("# Normal"));
    fixture.tabs.addTab(a, QStringLiteral("Large"));
    fixture.tabs.addTab(b, QStringLiteral("Normal"));
    fixture.renderNow();
    QVERIFY(fixture.controller.previewStatus().performanceProtected);
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::Ready);
    fixture.controller.setRefreshMode(RefreshMode::Manual);
    a->edit(QStringLiteral("# Large pending"));
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::Pending);
    fixture.controller.setRefreshMode(RefreshMode::Automatic);
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::Paused);
    QVERIFY(QMetaObject::invokeMethod(&fixture.controller, "renderScheduled", Qt::DirectConnection));
    fixture.dock()->hide();
    fixture.dock()->show();
    QTest::qWait(450);
    QCOMPARE(a->renderCount(), 1);
    fixture.tabs.setCurrentWidget(b);
    QTRY_COMPARE_WITH_TIMEOUT(b->renderCount(), 1, 1200);
    QVERIFY(!fixture.controller.previewStatus().performanceProtected);
    fixture.tabs.setCurrentWidget(a);
    fixture.pollEditor();
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::Paused);
    QTest::qWait(450);
    QCOMPARE(a->renderCount(), 1);
    fixture.action(QStringLiteral("立即刷新"))->trigger();
    QCOMPARE(a->renderCount(), 2);
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::Ready);
    QVERIFY(fixture.controller.previewStatus().performanceProtected);
    QCOMPARE(fixture.controller.previewStatus().mode, RefreshMode::Automatic);
}

void PreviewControllerDocumentIdentityTest::manualHiddenExportUsesCurrentTab()
{
    Fixture fixture;
    auto *a = new QsciScintilla(QStringLiteral("a.md"), QStringLiteral("# A"));
    auto *b = new QsciScintilla(QStringLiteral("b.md"), QStringLiteral("# B"));
    fixture.tabs.addTab(a, QStringLiteral("A"));
    fixture.tabs.addTab(b, QStringLiteral("B"));
    fixture.renderNow();
    fixture.controller.setRefreshMode(RefreshMode::Manual);
    fixture.dock()->hide();
    fixture.tabs.setCurrentWidget(b);
    b->edit(QStringLiteral("# Latest hidden B"));
    QByteArray html;
    QString path;
    QVERIFY(fixture.controller.currentHtmlSnapshot(&html, &path));
    QVERIFY(fixture.dock()->isHidden());
    QVERIFY(html.contains("Latest hidden B"));
    QCOMPARE(path, QStringLiteral("b.md"));
    QCOMPARE(a->renderCount(), 1);
    QCOMPARE(b->renderCount(), 1);
    QCOMPARE(fixture.controller.previewStatus().mode, RefreshMode::Manual);
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::Ready);
    b->edit(QStringLiteral("# Newer hidden B"));
    fixture.action(QStringLiteral("立即刷新"))->trigger();
    QCOMPARE(b->renderCount(), 2);
    QVERIFY(fixture.dock()->isHidden());
    QCOMPARE(fixture.controller.previewStatus().mode, RefreshMode::Manual);
}

void PreviewControllerDocumentIdentityTest::staleSnapshotDisablesBothScrollDirections()
{
    Fixture fixture;
    auto *editor = new QsciScintilla(QStringLiteral("a.md"), longMarkdown(QStringLiteral("# A")));
    fixture.tabs.addTab(editor, QStringLiteral("A"));
    fixture.renderNow();
    QCoreApplication::processEvents();
    auto *preview = fixture.dock()->findChild<QTextEdit *>(QStringLiteral("textEdit"));
    QVERIFY(preview);
    fixture.controller.setRefreshMode(RefreshMode::Manual);
    editor->edit(longMarkdown(QStringLiteral("# A changed")));
    const int originalPreviewValue = preview->verticalScrollBar()->value();
    editor->verticalScrollBar()->setValue(73);
    fixture.pollEditor();
    QCOMPARE(preview->verticalScrollBar()->value(), originalPreviewValue);
    QVERIFY(QMetaObject::invokeMethod(fixture.dock(), "previewScrollRatioChanged",
                                     Qt::DirectConnection, Q_ARG(double, 0.2)));
    QCOMPARE(editor->verticalScrollBar()->value(), 73);
    fixture.action(QStringLiteral("立即刷新"))->trigger();
    QVERIFY(QMetaObject::invokeMethod(fixture.dock(), "previewScrollRatioChanged",
                                     Qt::DirectConnection, Q_ARG(double, 0.2)));
    QCOMPARE(editor->verticalScrollBar()->value(), 20);
}

void PreviewControllerDocumentIdentityTest::failedRefreshRetainsOnlyUntouchedSnapshot_data()
{
    QTest::addColumn<bool>("mutates");
    QTest::newRow("untouched") << false;
    QTest::newRow("partially-mutated") << true;
}

void PreviewControllerDocumentIdentityTest::failedRefreshRetainsOnlyUntouchedSnapshot()
{
    QFETCH(bool, mutates);
    QMainWindow window;
    auto *editor = new QsciScintilla(QStringLiteral("injected.md"), QStringLiteral("# Source"));
    window.setCentralWidget(editor);
    TestHostAdapter adapter(editor);
    PreviewController controller(&window, &adapter);
    auto *dock = window.findChild<MarkdownPreviewDock *>();
    window.resize(900, 600);
    window.show();
    dock->show();
    controller.setRefreshMode(RefreshMode::Manual);
    QVERIFY(QMetaObject::invokeMethod(&controller, "renderNow", Qt::DirectConnection));
    const quint64 version = controller.previewStatus().displayedVersion;
    adapter.failRefresh = true;
    adapter.mutateOnFailure = mutates;
    editor->edit(QStringLiteral("# New source"));
    QVERIFY(QMetaObject::invokeMethod(&controller, "renderNow", Qt::DirectConnection));
    QCOMPARE(controller.previewStatus().state, PreviewState::Failed);
    QCOMPARE(controller.previewStatus().mode, RefreshMode::Manual);
    QCOMPARE(controller.previewStatus().displayedVersion, mutates ? quint64(0) : version);
    QVERIFY(dock->htmlSnapshotFor(editor, version).isEmpty());
    auto *browser = dock->findChild<QTextBrowser *>();
    QCOMPARE(browser->isVisible(), mutates);
    auto *retry = dock->findChild<QToolButton *>(QStringLiteral("NddMarkdownRetryButton"));
    QVERIFY(retry->isVisible());
    const QString screenshotDir = qEnvironmentVariable("MARKDOWNVIEW_SCREENSHOT_DIR");
    if (!screenshotDir.isEmpty()) {
        QVERIFY(QDir().mkpath(screenshotDir));
        QCoreApplication::processEvents();
        QVERIFY(dock->grab().save(QDir(screenshotDir).filePath(mutates
            ? QStringLiteral("10-failure-no-snapshot.png")
            : QStringLiteral("09-failure-old-snapshot.png"))));
    }
    dock->findChild<QToolButton *>(QStringLiteral("NddMarkdownStatusDetails"))->click();
    QVERIFY(QApplication::clipboard()->text().contains(QStringLiteral("模拟宿主更新失败")));
    QByteArray html;
    QString path;
    QVERIFY(!controller.currentHtmlSnapshot(&html, &path));
    QVERIFY(html.isEmpty());
    adapter.failRefresh = false;
    retry->click();
    QCOMPARE(controller.previewStatus().state, PreviewState::Ready);
    QCOMPARE(controller.previewStatus().mode, RefreshMode::Manual);
    QVERIFY(controller.currentHtmlSnapshot(&html, &path));
    QVERIFY(html.contains("Recovered snapshot"));
    QVERIFY(!html.contains("Partially overwritten"));
}

void PreviewControllerDocumentIdentityTest::failedInitialRenderProvidesRetry()
{
    QMainWindow window;
    auto *editor = new QsciScintilla(QStringLiteral("injected.md"), QStringLiteral("# Source"));
    window.setCentralWidget(editor);
    TestHostAdapter adapter(editor);
    adapter.failEnsure = true;
    PreviewController controller(&window, &adapter);
    controller.setRefreshMode(RefreshMode::Manual);
    QByteArray html;
    QString path;
    QVERIFY(!controller.currentHtmlSnapshot(&html, &path));
    QCOMPARE(controller.previewStatus().state, PreviewState::Failed);
    QVERIFY(!controller.previewStatus().displayedEditor);
    adapter.failEnsure = false;
    QVERIFY(controller.currentHtmlSnapshot(&html, &path));
    QVERIFY(html.contains("Injected adapter"));
    QCOMPARE(controller.previewStatus().mode, RefreshMode::Manual);
}

void PreviewControllerDocumentIdentityTest::renderReentrancyCannotPublishChangedSource()
{
    QMainWindow window;
    auto *editor = new QsciScintilla(QStringLiteral("injected.md"), QStringLiteral("# Source"));
    window.setCentralWidget(editor);
    TestHostAdapter adapter(editor);
    PreviewController controller(&window, &adapter);
    controller.setRefreshMode(RefreshMode::Manual);
    QByteArray html;
    QString path;
    QVERIFY(controller.currentHtmlSnapshot(&html, &path));
    editor->edit(QStringLiteral("# Start refresh"));
    adapter.onRefresh = [&]() {
        editor->edit(QStringLiteral("# Changed during render"));
        QVERIFY(QMetaObject::invokeMethod(&controller, "renderNow", Qt::DirectConnection));
    };
    QVERIFY(!controller.currentHtmlSnapshot(&html, &path));
    QCOMPARE(adapter.refreshCount(), 1);
    QVERIFY(html.isEmpty());
    QVERIFY(!controller.previewStatus().displayedEditor);
    QCOMPARE(controller.previewStatus().state, PreviewState::Pending);
    QCoreApplication::processEvents();
    QCOMPARE(adapter.refreshCount(), 1);
    adapter.onRefresh = nullptr;
    QVERIFY(controller.currentHtmlSnapshot(&html, &path));
    QCOMPARE(adapter.refreshCount(), 2);
}

void PreviewControllerDocumentIdentityTest::manualCacheEvictionNeverRendersImplicitly()
{
    Fixture fixture;
    fixture.controller.setRefreshMode(RefreshMode::Manual);
    QList<QsciScintilla *> editors;
    for (int i = 0; i < 4; ++i) {
        auto *editor = new QsciScintilla(QStringLiteral("%1.md").arg(i), QStringLiteral("# Cached"));
        editors.append(editor);
        fixture.tabs.addTab(editor, QString::number(i));
        fixture.tabs.setCurrentWidget(editor);
        fixture.renderNow();
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    fixture.tabs.setCurrentWidget(editors.first());
    fixture.pollEditor();
    QVERIFY(!fixture.controller.previewStatus().displayedEditor);
    QTest::qWait(450);
    QCOMPARE(editors.first()->renderCount(), 1);
    QCOMPARE(fixture.controller.previewStatus().mode, RefreshMode::Manual);
}

void PreviewControllerDocumentIdentityTest::manualPathChangeAndUnsupportedState()
{
    Fixture fixture;
    auto *editor = new QsciScintilla(QStringLiteral("old/a.md"), QStringLiteral("# A"));
    fixture.tabs.addTab(editor, QStringLiteral("A"));
    fixture.renderNow();
    fixture.controller.setRefreshMode(RefreshMode::Manual);
    const quint64 originalVersion = fixture.controller.previewStatus().displayedVersion;
    editor->renameTo(QStringLiteral("new/b.md"));
    QCOMPARE(fixture.controller.previewStatus().displayedVersion, originalVersion);
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::Pending);
    QCOMPARE(fixture.dock()->findChild<QLabel *>(QStringLiteral("NddMarkdownPreviewDocumentLabel"))->text(),
             QStringLiteral("b.md"));
    editor->renameTo(QStringLiteral("unsupported.txt"));
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::Unsupported);
    QVERIFY(!fixture.controller.previewStatus().displayedEditor);
    QCOMPARE(editor->renderCount(), 1);
    fixture.tabs.removeTab(0);
    delete editor;
    QCoreApplication::processEvents();
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::NoDocument);
}

void PreviewControllerDocumentIdentityTest::stateScreenshots()
{
    // Optional evidence output from real Qt widgets, never generated illustrations.
    const QString output = qEnvironmentVariable("MARKDOWNVIEW_SCREENSHOT_DIR");
    if (output.isEmpty()) {
        return;
    }
    QVERIFY(QDir().mkpath(output));
    Fixture fixture;
    fixture.dock()->show();
    auto capture = [&](const QString &name) {
        QCoreApplication::processEvents();
        QVERIFY(fixture.dock()->grab().save(QDir(output).filePath(name + QStringLiteral(".png"))));
    };
    capture(QStringLiteral("01-no-document"));
    auto *editor = new QsciScintilla(QStringLiteral("阅读示例.md"), longMarkdown(QStringLiteral("# 阅读示例")));
    fixture.tabs.addTab(editor, QStringLiteral("Markdown"));
    fixture.controller.setRefreshMode(RefreshMode::Manual);
    capture(QStringLiteral("02-manual-no-cache"));
    fixture.renderNow();
    capture(QStringLiteral("03-manual-synchronized"));
    editor->edit(QStringLiteral("# 待刷新"));
    capture(QStringLiteral("04-manual-pending"));
    fixture.controller.setRefreshMode(RefreshMode::Automatic);
    QTRY_COMPARE_WITH_TIMEOUT(editor->renderCount(), 2, 1200);
    capture(QStringLiteral("05-automatic-synchronized"));
    QTemporaryDir directory;
    QFile large(directory.filePath(QStringLiteral("large.md")));
    QVERIFY(large.open(QIODevice::WriteOnly));
    QVERIFY(large.resize(1024 * 1024));
    large.close();
    editor->renameTo(large.fileName());
    capture(QStringLiteral("06-protection-paused"));
    fixture.renderNow();
    capture(QStringLiteral("07-synchronized-protected"));
    editor->renameTo(QStringLiteral("unsupported.txt"));
    capture(QStringLiteral("08-unsupported"));
}

void PreviewControllerDocumentIdentityTest::destroyedSnapshotClearsStatusWithoutManualRender()
{
    Fixture fixture;
    auto *editor = new QsciScintilla(QStringLiteral("a.md"), QStringLiteral("# A"));
    fixture.tabs.addTab(editor, QStringLiteral("A"));
    fixture.renderNow();
    fixture.controller.setRefreshMode(RefreshMode::Manual);
    QSignalSpy changed(&fixture.controller, &PreviewController::previewStatusChanged);
    delete fixture.dock()->findChild<QWidget *>(QStringLiteral("MarkdownViewClass"));
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::Pending);
    QVERIFY(!fixture.controller.previewStatus().displayedEditor);
    QCOMPARE(fixture.controller.previewStatus().displayedVersion, quint64(0));
    QVERIFY(changed.count() > 0);
    QCOMPARE(editor->renderCount(), 1);
    fixture.renderNow();
    QCOMPARE(editor->renderCount(), 2);
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::Ready);
}

void PreviewControllerDocumentIdentityTest::renderTabSwitchAndModeChangeDiscardObsoleteResult()
{
    Fixture fixture;
    auto *a = new QsciScintilla(QStringLiteral("a.md"), QStringLiteral("# A"));
    auto *b = new QsciScintilla(QStringLiteral("b.md"), QStringLiteral("# B"));
    fixture.tabs.addTab(a, QStringLiteral("A"));
    fixture.tabs.addTab(b, QStringLiteral("B"));
    fixture.renderNow();
    a->setRenderDelayMs(100);
    QTimer::singleShot(0, &fixture.window, [&]() {
        fixture.controller.setRefreshMode(RefreshMode::Manual);
        fixture.tabs.setCurrentWidget(b);
        fixture.pollEditor();
        fixture.action(QStringLiteral("立即刷新"))->trigger();
    });
    fixture.action(QStringLiteral("立即刷新"))->trigger();
    QCOMPARE(a->renderCount(), 2);
    QCOMPARE(b->renderCount(), 0);
    QCOMPARE(fixture.controller.previewStatus().activeEditor.data(), b);
    QVERIFY(!fixture.controller.previewStatus().displayedEditor);
    QCOMPARE(fixture.controller.previewStatus().mode, RefreshMode::Manual);
    fixture.tabs.setCurrentWidget(a);
    fixture.pollEditor();
    QVERIFY(!fixture.controller.previewStatus().displayedEditor);
    QCOMPARE(a->renderCount(), 2);
}

void PreviewControllerDocumentIdentityTest::automaticFirstOpenRetainsExistingLargeFilePolicy()
{
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("large.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(1024 * 1024));
    file.close();
    Fixture fixture;
    auto *editor = new QsciScintilla(file.fileName(), QStringLiteral("# First open"));
    fixture.tabs.addTab(editor, QStringLiteral("Large"));
    fixture.dock()->show();
    QTRY_COMPARE_WITH_TIMEOUT(editor->renderCount(), 1, 1200);
    QCOMPARE(fixture.controller.previewStatus().state, PreviewState::Ready);
    QVERIFY(fixture.controller.previewStatus().performanceProtected);
}

QTEST_MAIN(PreviewControllerDocumentIdentityTest)

#include "preview_controller_document_identity_test.moc"
