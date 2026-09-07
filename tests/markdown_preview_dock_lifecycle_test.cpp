#include "markdown_preview_dock.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QPointer>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QTemporaryDir>
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
    void nativePreviewActivatesAllowedLinks();
    void nativePreviewScrollsToAnchors();
    void nativePreviewRejectsUnsupportedSchemes();
    void nativePreviewSelectionDoesNotOpenLink();
    void htmlSnapshotEmbedsLocalImagesWithoutChangingPreview();
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

void MarkdownPreviewDockLifecycleTest::nativePreviewActivatesAllowedLinks()
{
    MarkdownPreviewDock dock;
    QWidget *preview = createNativePreview();
    QWidget editor;
    QTextEdit *textEdit = preview->findChild<QTextEdit *>(QStringLiteral("textEdit"));
    QVERIFY(textEdit);
    textEdit->setHtml(QStringLiteral(
        "<p><a href=\"https://example.com/path\">External</a></p>"
        "<p><a href=\"guide/next.md\">Relative</a></p>"));
    QVERIFY(dock.adoptNativePreview(
        preview, QStringLiteral("/tmp/docs/current.md"), &editor, 1));
    dock.resize(600, 400);
    dock.show();
    QTest::qWait(1);

    QList<QUrl> openedUrls;
    dock.setUrlOpener([&openedUrls](const QUrl &url) {
        openedUrls.append(url);
        return true;
    });

    QTextCursor external = textEdit->document()->find(QStringLiteral("External"));
    QVERIFY(!external.isNull());
    QTest::mouseClick(textEdit->viewport(), Qt::LeftButton, Qt::NoModifier,
                      textEdit->cursorRect(external).center());
    QCOMPARE(openedUrls.size(), 1);
    QCOMPARE(openedUrls.constFirst(), QUrl(QStringLiteral("https://example.com/path")));

    QVERIFY(QMetaObject::invokeMethod(
        &dock, "openLink", Qt::DirectConnection,
        Q_ARG(QUrl, QUrl(QStringLiteral("guide/next.md")))));
    QCOMPARE(openedUrls.size(), 2);
    QCOMPARE(openedUrls.constLast(),
             QUrl::fromLocalFile(QStringLiteral("/tmp/docs/guide/next.md")));
}

void MarkdownPreviewDockLifecycleTest::nativePreviewScrollsToAnchors()
{
    MarkdownPreviewDock dock;
    QWidget *preview = createNativePreview();
    QWidget editor;
    QTextEdit *textEdit = preview->findChild<QTextEdit *>(QStringLiteral("textEdit"));
    QVERIFY(textEdit);
    QString html = QStringLiteral("<p><a href=\"#target\">Jump</a></p>");
    for (int i = 0; i < 80; ++i) {
        html += QStringLiteral("<p>line %1</p>").arg(i);
    }
    html += QStringLiteral("<a name=\"target\"></a><h2>Target</h2>");
    textEdit->setHtml(html);
    QVERIFY(dock.adoptNativePreview(
        preview, QStringLiteral("/tmp/current.md"), &editor, 1));
    dock.resize(500, 250);
    dock.show();
    QTest::qWait(1);
    textEdit->verticalScrollBar()->setValue(0);

    QVERIFY(QMetaObject::invokeMethod(
        &dock, "openLink", Qt::DirectConnection,
        Q_ARG(QUrl, QUrl(QStringLiteral("#target")))));
    QVERIFY(textEdit->verticalScrollBar()->value() > 0);
}

void MarkdownPreviewDockLifecycleTest::nativePreviewRejectsUnsupportedSchemes()
{
    MarkdownPreviewDock dock;
    int openCount = 0;
    dock.setUrlOpener([&openCount](const QUrl &) {
        ++openCount;
        return false;
    });

    QVERIFY(QMetaObject::invokeMethod(
        &dock, "openLink", Qt::DirectConnection,
        Q_ARG(QUrl, QUrl(QStringLiteral("https://example.com/failure")))));
    QCOMPARE(openCount, 1);
    QLabel *label = dock.findChild<QLabel *>();
    QVERIFY(label);
    QVERIFY(label->text().contains(QStringLiteral("系统未能打开")));

    QVERIFY(QMetaObject::invokeMethod(
        &dock, "openLink", Qt::DirectConnection,
        Q_ARG(QUrl, QUrl(QStringLiteral("javascript:alert(1)")))));
    QCOMPARE(openCount, 1);
    QVERIFY(label->text().contains(QStringLiteral("不支持协议")));
}

void MarkdownPreviewDockLifecycleTest::nativePreviewSelectionDoesNotOpenLink()
{
    MarkdownPreviewDock dock;
    QWidget *preview = createNativePreview();
    QWidget editor;
    QTextEdit *textEdit = preview->findChild<QTextEdit *>(QStringLiteral("textEdit"));
    QVERIFY(textEdit);
    textEdit->setHtml(QStringLiteral(
        "<p><a href=\"https://example.com\">Selectable link text</a></p>"));
    QVERIFY(dock.adoptNativePreview(
        preview, QStringLiteral("/tmp/current.md"), &editor, 1));
    dock.resize(600, 300);
    dock.show();
    QTest::qWait(1);

    int openCount = 0;
    dock.setUrlOpener([&openCount](const QUrl &) {
        ++openCount;
        return true;
    });
    QTextCursor link = textEdit->document()->find(QStringLiteral("Selectable"));
    const QPoint start = textEdit->cursorRect(link).center();
    const QPoint end = start + QPoint(80, 0);
    QTest::mousePress(textEdit->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(textEdit->viewport(), end, 10);
    QTest::mouseRelease(textEdit->viewport(), Qt::LeftButton, Qt::NoModifier, end);

    QCOMPARE(openCount, 0);
    QVERIFY(textEdit->textCursor().hasSelection());
}

void MarkdownPreviewDockLifecycleTest::htmlSnapshotEmbedsLocalImagesWithoutChangingPreview()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("图片 目录")));
    const QString markdownPath = root.filePath(QStringLiteral("文档.md"));
    const QString imagePath = root.filePath(QStringLiteral("图片 目录/示例.png"));
    QImage image(2, 2, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(imagePath, "PNG"));

    MarkdownPreviewDock dock;
    QWidget *preview = createNativePreview();
    QWidget editor;
    QTextEdit *textEdit = preview->findChild<QTextEdit *>(QStringLiteral("textEdit"));
    QVERIFY(textEdit);
    QVERIFY(dock.adoptNativePreview(preview, markdownPath, &editor, 1));
    textEdit->setHtml(QStringLiteral(
        "<p><img src=\"图片 目录/示例.png\"></p>"
        "<p><img src=\"missing.png\"></p>"
        "<p><img src=\"https://example.com/remote.png\"></p>"
        "<p><a href=\"附件/report.pdf\">attachment</a></p>"));
    const QString previewHtml = textEdit->document()->toHtml();

    const QByteArray snapshot = dock.htmlSnapshotFor(&editor, 1);
    QVERIFY(snapshot.contains("data:image/png;base64,"));
    QVERIFY(snapshot.contains("https://example.com/remote.png"));
    QVERIFY(snapshot.contains("missing.png"));
    QVERIFY(snapshot.contains("markdownview-export: local images not embedded"));
    QVERIFY(snapshot.contains("report.pdf"));
    QCOMPARE(textEdit->document()->toHtml(), previewHtml);

    QVERIFY(root.mkpath(QStringLiteral("其他目录")));
    const QString targetPath = root.filePath(QStringLiteral("其他目录/导出.html"));
    QString errorMessage;
    QVERIFY(MarkdownPreviewDock::writeHtmlSnapshot(
        snapshot, targetPath, &errorMessage));
    QVERIFY(errorMessage.isEmpty());
    QFile exported(targetPath);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QVERIFY(exported.readAll().contains("data:image/png;base64,"));
}

QTEST_MAIN(MarkdownPreviewDockLifecycleTest)

#include "markdown_preview_dock_lifecycle_test.moc"
