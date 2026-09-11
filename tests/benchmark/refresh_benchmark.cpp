#include "preview_controller.h"
#include "markdown_preview_dock.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

class QsciScintilla final : public QAbstractScrollArea
{
    Q_OBJECT
public:
    QsciScintilla(const QString &path, const QString &text) : m_text(text)
    {
        setProperty("filePath", path);
    }
    void edit(const QString &text) { m_text = text; emit textChanged(); }
    int renders = 0;
signals:
    void textChanged();
public slots:
    void on_viewMarkdown()
    {
        if (!m_preview) {
            m_preview = new QWidget(this);
            m_preview->setObjectName(QStringLiteral("MarkdownViewClass"));
            auto *layout = new QVBoxLayout(m_preview);
            m_browser = new QTextEdit(m_preview);
            m_browser->setObjectName(QStringLiteral("textEdit"));
            layout->addWidget(m_browser);
        }
        on_updataMarkdown();
    }
    void on_updataMarkdown()
    {
        ++renders;
        m_browser->setMarkdown(m_text);
    }
private:
    QString m_text;
    QPointer<QWidget> m_preview;
    QPointer<QTextEdit> m_browser;
};

struct Fixture
{
    QMainWindow window;
    QTabWidget tabs;
    QMenu menu;
    PreviewController controller;
    QsciScintilla *editor;
    Fixture(const QString &path, const QString &text)
        : menu(&window), controller(&window), editor(new QsciScintilla(path, text))
    {
        tabs.setObjectName(QStringLiteral("editTabWidget"));
        window.setCentralWidget(&tabs);
        tabs.addTab(editor, QStringLiteral("Sample"));
        controller.installMenu(&menu);
        window.resize(1000, 700);
        window.show();
        window.findChild<MarkdownPreviewDock *>()->show();
        QCoreApplication::processEvents();
    }
    void refresh()
    {
        QMetaObject::invokeMethod(&controller, "renderNow", Qt::DirectConnection);
    }
};

static double elapsedMs(const std::function<void()> &operation)
{
    QElapsedTimer timer;
    timer.start();
    operation();
    return static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.arguments().size() < 2) {
        return 2;
    }
    const QString sample = app.arguments().at(1);
    QString text;
    if (sample == QStringLiteral("headings")) {
        for (int i = 0; i < 500; ++i) {
            text += QStringLiteral("## Heading %1\n\nParagraph.\n\n").arg(i);
        }
    } else if (sample == QStringLiteral("tables-images")) {
        text = QStringLiteral("| Key | Value |\n| --- | --- |\n| A | B |\n\n"
                              "![pixel](data:image/png;base64,"
                              "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aV6sAAAAASUVORK5CYII=)\n\n")
                   .repeated(100);
    } else {
        const int bytes = sample == QStringLiteral("5m") ? 5 * 1024 * 1024
            : sample == QStringLiteral("1m") ? 1024 * 1024 : 100 * 1024;
        const QString paragraph = QStringLiteral("A plain sentence for rendering and layout. ")
            .repeated(bytes >= 1024 * 1024 ? 1536 : 24) + QStringLiteral("\n\n");
        text = paragraph.repeated(bytes / paragraph.size() + 1);
        text.truncate(bytes);
    }
    QTemporaryDir directory;
    const QString path = sample == QStringLiteral("unsaved") ? QString()
        : directory.filePath(QStringLiteral("sample.md"));
    if (!path.isEmpty()) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0) {
            return 3;
        }
    }
    QMap<QString, QList<double>> measures;
    for (int iteration = -1; iteration < 20; ++iteration) {
        Fixture fixture(path, text);
        auto measure = [&](const QString &name, const std::function<void()> &operation) {
            const double duration = elapsedMs(operation);
            if (iteration >= 0) {
                measures[name].append(duration);
            }
        };
        measure(QStringLiteral("first-render-sync"), [&]() { fixture.refresh(); });
        measure(QStringLiteral("typing-burst-20"), [&]() {
            for (int key = 0; key < 20; ++key) {
                fixture.editor->edit(sample == QStringLiteral("unsaved")
                    ? text + QString(1024 * (key + 1), QLatin1Char('x')) : text);
            }
        });
        if (fixture.editor->renders != 1) {
            return 4; // Source edits must not synchronously render.
        }
        measure(QStringLiteral("explicit-refresh-sync"), [&]() { fixture.refresh(); });
        measure(QStringLiteral("latest-html-export"), [&]() {
            QByteArray html;
            QString source;
            if (!fixture.controller.currentHtmlSnapshot(&html, &source) || html.isEmpty()) {
                qFatal("snapshot unavailable");
            }
        });
        auto *other = new QsciScintilla(QStringLiteral("other.md"), QStringLiteral("# Other"));
        fixture.tabs.addTab(other, QStringLiteral("Other"));
        fixture.tabs.setCurrentWidget(other);
        fixture.refresh();
        measure(QStringLiteral("cached-tab-activation-sync"), [&]() {
            fixture.tabs.setCurrentWidget(fixture.editor);
            QMetaObject::invokeMethod(&fixture.controller, "pollEditor", Qt::DirectConnection);
            QByteArray html;
            QString source;
            // The same public snapshot entry activates cached previews on the
            // baseline, whose normal tab activation waits for a timer.
            if (!fixture.controller.currentHtmlSnapshot(&html, &source)) {
                qFatal("cached snapshot unavailable");
            }
        });
        if (sample == QStringLiteral("two-windows")) {
            measure(QStringLiteral("second-window-first-render"), [&]() {
                Fixture second(path, text);
                second.refresh();
            });
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    QJsonObject result;
    result.insert(QStringLiteral("sample"), sample);
    result.insert(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
    result.insert(QStringLiteral("source_bytes"), text.toUtf8().size());
    result.insert(QStringLiteral("source_lines"), text.count(QLatin1Char('\n')) + 1);
    for (auto it = measures.begin(); it != measures.end(); ++it) {
        QList<double> values = it.value();
        std::sort(values.begin(), values.end());
        QJsonObject metric;
        metric.insert(QStringLiteral("n"), values.size());
        metric.insert(QStringLiteral("median_ms"), (values.at(9) + values.at(10)) / 2.0);
        metric.insert(QStringLiteral("p95_ms"), values.at(18));
        result.insert(it.key(), metric);
    }
    // Linux process peak includes QApplication and the mock renderer. Other
    // platforms can record process peak externally; missing is never zero.
    QFile process(QStringLiteral("/proc/self/status"));
    if (process.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = process.readAll().split('\n');
        for (const QByteArray &line : lines) {
            if (line.startsWith("VmHWM:")) {
                result.insert(QStringLiteral("process_peak_rss"), QString::fromLatin1(line.mid(6).trimmed()));
            }
        }
    }
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
    return 0;
}

#include "refresh_benchmark.moc"
