#include "markdown_preview_dock.h"

#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest>

namespace {
QWidget *createNativePreview()
{
    auto *preview = new QWidget;
    auto *layout = new QVBoxLayout(preview);
    auto *textEdit = new QTextEdit(preview);
    textEdit->setObjectName(QStringLiteral("textEdit"));
    layout->addWidget(textEdit);
    return preview;
}
}

class MarkdownPreviewDockLifecycleTest final : public QObject
{
    Q_OBJECT

private slots:
    void currentPreviewDestructionRestoresFallback();
    void hiddenPreviewDestructionKeepsCurrentPreview();
    void dockDestructionDisconnectsAllPreviewCallbacks();
    void editorDestructionDeleteLaterIsSafe();
};

void MarkdownPreviewDockLifecycleTest::currentPreviewDestructionRestoresFallback()
{
    MarkdownPreviewDock dock;
    QWidget *preview = createNativePreview();
    QWidget editor;
    QVERIFY(dock.adoptNativePreview(preview, QStringLiteral("document.md"),
                                    &editor, 1));

    QTextBrowser *browser = dock.findChild<QTextBrowser *>(
        QStringLiteral("NddMarkdownPreviewBrowser"));
    QVERIFY(browser);
    QVERIFY(browser->isHidden());

    delete preview;

    QVERIFY(!browser->isHidden());
}

void MarkdownPreviewDockLifecycleTest::hiddenPreviewDestructionKeepsCurrentPreview()
{
    MarkdownPreviewDock dock;
    QWidget *firstPreview = createNativePreview();
    QWidget *secondPreview = createNativePreview();
    QWidget firstEditor;
    QWidget secondEditor;
    QVERIFY(dock.adoptNativePreview(firstPreview, QStringLiteral("first.md"),
                                    &firstEditor, 1));
    QVERIFY(dock.adoptNativePreview(secondPreview, QStringLiteral("second.md"),
                                    &secondEditor, 2));

    QTextBrowser *browser = dock.findChild<QTextBrowser *>(
        QStringLiteral("NddMarkdownPreviewBrowser"));
    QVERIFY(browser);
    QVERIFY(firstPreview->isHidden());
    QVERIFY(!secondPreview->isHidden());

    delete firstPreview;

    QVERIFY(!secondPreview->isHidden());
    QVERIFY(browser->isHidden());
}

void MarkdownPreviewDockLifecycleTest::dockDestructionDisconnectsAllPreviewCallbacks()
{
    auto *dock = new MarkdownPreviewDock;
    QPointer<QWidget> firstPreview = createNativePreview();
    QPointer<QWidget> secondPreview = createNativePreview();
    QWidget firstEditor;
    QWidget secondEditor;
    QVERIFY(dock->adoptNativePreview(firstPreview, QStringLiteral("first.md"),
                                     &firstEditor, 1));
    QVERIFY(dock->adoptNativePreview(secondPreview, QStringLiteral("second.md"),
                                     &secondEditor, 2));

    delete dock;

    QVERIFY(firstPreview.isNull());
    QVERIFY(secondPreview.isNull());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void MarkdownPreviewDockLifecycleTest::editorDestructionDeleteLaterIsSafe()
{
    auto *editorOwner = new QObject;
    auto *dock = new MarkdownPreviewDock;
    QPointer<QWidget> preview = createNativePreview();
    QWidget editor;
    QVERIFY(dock->adoptNativePreview(preview, QStringLiteral("document.md"),
                                     &editor, 1));
    connect(editorOwner, &QObject::destroyed, preview.data(), &QObject::deleteLater);

    delete editorOwner;
    delete dock;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    QVERIFY(preview.isNull());
}

QTEST_MAIN(MarkdownPreviewDockLifecycleTest)

#include "markdown_preview_dock_lifecycle_test.moc"
