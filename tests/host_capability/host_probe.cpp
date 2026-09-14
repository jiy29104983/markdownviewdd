#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QScrollBar>
#include <QTextEdit>
#include <QTextStream>
#include <Qsci/qsciscintilla.h>
using Navigator = QJsonObject (*)(QWidget *, int);

static QJsonObject state(QsciScintilla &editor)
{
    QJsonArray selections;
    const int n = int(editor.SendScintilla(QsciScintilla::SCI_GETSELECTIONS));
    for (int i = 0; i < n; ++i) {
        selections.append(QJsonArray{int(editor.SendScintilla(QsciScintilla::SCI_GETSELECTIONNCARET, i)),
                                     int(editor.SendScintilla(QsciScintilla::SCI_GETSELECTIONNANCHOR, i))});
    }
    return {{"text", editor.text()}, {"modified", editor.isModified()},
            {"cursor", int(editor.SendScintilla(QsciScintilla::SCI_GETCURRENTPOS))},
            {"anchor", int(editor.SendScintilla(QsciScintilla::SCI_GETANCHOR))},
            {"selections", selections}, {"selectionMode", int(editor.SendScintilla(QsciScintilla::SCI_GETSELECTIONMODE))}};
}

static QString sample(QString eol, bool wrap)
{
    QStringList lines;
    for (int i = 0; i < 150; ++i)
        lines.append(i == 91 || i == 130 ? QStringLiteral("## 重复标题 😀") :
                     QStringLiteral("正文 %1 😀 中文 abc ").arg(i) + (wrap ? QString(140, QLatin1Char('w')) : QString()));
    return lines.join(eol);
}

static QJsonObject run(Navigator nav, const QString &name, QString eol, bool wrap, int target, bool folded, bool multi = false, int zoom = 0)
{
    QsciScintilla editor;
    editor.setUtf8(true);
    editor.resize(wrap ? 320 : 800, 380);
    editor.setFont(QFont(QStringLiteral("DejaVu Sans Mono"), 11));
    editor.setWrapMode(wrap ? QsciScintilla::WrapWord : QsciScintilla::WrapNone);
    editor.setText(sample(eol, wrap));
    editor.zoomTo(zoom);
    editor.show();
    QApplication::processEvents();
    if (folded) {
        editor.setFolding(QsciScintilla::PlainFoldStyle);
        for (int i = 0; i < 150; ++i) {
            int depth = (i > 0 && i < 100) ? 1 : 0;
            if (i > 20 && i < 30) depth = 2;
            if (i > 80 && i < 95) depth = 2;
            if (i > 85 && i < 94) depth = 3;
            if (i > 110 && i < 120) depth = 1;
            const bool header = i == 0 || i == 20 || i == 80 || i == 85 || i == 110;
            editor.SendScintilla(QsciScintilla::SCI_SETFOLDLEVEL, i,
                                QsciScintilla::SC_FOLDLEVELBASE + depth + (header ? QsciScintilla::SC_FOLDLEVELHEADERFLAG : 0));
        }
        for (int i : {20, 85, 80, 0, 110})
            editor.SendScintilla(QsciScintilla::SCI_FOLDLINE, i, QsciScintilla::SC_FOLDACTION_CONTRACT);
    }
    editor.SendScintilla(QsciScintilla::SCI_SETSEL, 5UL, 18L);
    if (multi) {
        editor.SendScintilla(QsciScintilla::SCI_SETMULTIPLESELECTION, 1);
        editor.SendScintilla(QsciScintilla::SCI_ADDSELECTION, 50UL, 40L);
    }
    editor.setModified(false);
    QApplication::processEvents();
    const auto before = state(editor);
    const int initialScroll = editor.verticalScrollBar()->value();
    QJsonObject read = nav(&editor, -1);
    const bool readPreserved = before == state(editor) && initialScroll == editor.verticalScrollBar()->value();
    const int lineVisibleBefore = int(editor.SendScintilla(QsciScintilla::SCI_GETLINEVISIBLE, target));
    QJsonObject result = nav(&editor, target);
    QApplication::processEvents();
    const int position = int(editor.SendScintilla(QsciScintilla::SCI_POSITIONFROMLINE, target));
    const int y = int(editor.SendScintilla(QsciScintilla::SCI_POINTYFROMPOSITION, 0UL, long(position)));
    const int lineHeight = int(editor.SendScintilla(QsciScintilla::SCI_TEXTHEIGHT, target));
    const int displayLine = int(editor.SendScintilla(QsciScintilla::SCI_VISIBLEFROMDOCLINE, target));
    const int first = int(editor.SendScintilla(QsciScintilla::SCI_GETFIRSTVISIBLELINE));
    result["name"] = name;
    result["sourceEqual"] = read["source"].toString() == editor.text();
    result.remove("source");
    result["readPreservedState"] = readPreserved;
    result["navigationPreservedState"] = before == state(editor);
    result["actualY"] = y;
    result["actualLineHeight"] = lineHeight;
    result["actualDisplayLine"] = displayLine;
    result["actualFirstDisplayLine"] = first;
    result["lineVisibleBefore"] = lineVisibleBefore;
    result["actualLineVisible"] = int(editor.SendScintilla(QsciScintilla::SCI_GETLINEVISIBLE, target));
    result["topOrEndClamped"] = first == displayLine || editor.verticalScrollBar()->value() == editor.verticalScrollBar()->maximum();
    result["actualTargetVisible"] = y >= 0 && y < editor.viewport()->height();
    if (folded) {
        QJsonObject folds;
        for (int i : {0, 20, 80, 85, 110})
            folds[QString::number(i)] = bool(editor.SendScintilla(QsciScintilla::SCI_GETFOLDEXPANDED, i));
        result["foldsAfter"] = folds;
    }
    return result;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc != 2) return 2;
    QLibrary lib(QString::fromLocal8Bit(argv[1]));
    Navigator nav = reinterpret_cast<Navigator>(lib.resolve("navigateSource"));
    if (!nav) { QTextStream(stderr) << lib.errorString(); return 2; }
    QJsonArray results;
    results.append(run(nav, "LF_chinese_emoji_duplicate_second", "\n", false, 130, false));
    results.append(run(nav, "CRLF_chinese_emoji", "\r\n", false, 91, false));
    results.append(run(nav, "CR_chinese_emoji", "\r", false, 91, false));
    results.append(run(nav, "wrap_before_target", "\n", true, 91, false));
    results.append(run(nav, "wrap_CRLF_zoom", "\r\n", true, 130, false, false, 5));
    results.append(run(nav, "document_end", "\n", false, 149, false));
    results.append(run(nav, "document_start", "\n", false, 0, false));
    results.append(run(nav, "nested_folds", "\n", false, 91, true));
    results.append(run(nav, "nested_folds_wrapped", "\r\n", true, 91, true));
    results.append(run(nav, "multiple_selections", "\n", true, 91, false, true));
    results.append(run(nav, "invalid_line", "\n", false, 500, false));
    results.append(QJsonObject{{"name", "null_editor"}, {"result", nav(nullptr, 0)}});
    QTextEdit unsupported;
    unsupported.setPlainText(QStringLiteral("# heading"));
    results.append(QJsonObject{{"name", "missing_slot"}, {"result", nav(&unsupported, 0)}});
    int failed = 0;
    for (const QJsonValue &value : results) {
        const QJsonObject row = value.toObject();
        const QString name = row[QStringLiteral("name")].toString();
        bool passed = true;
        if (name == QStringLiteral("null_editor") || name == QStringLiteral("missing_slot")) {
            passed = !row[QStringLiteral("result")].toObject()[QStringLiteral("ok")].toBool();
        } else if (name == QStringLiteral("invalid_line")) {
            passed = !row[QStringLiteral("ok")].toBool() && row[QStringLiteral("navigationPreservedState")].toBool();
        } else {
            for (const char *key : {"ok", "sourceEqual", "navigationPreservedState", "readPreservedState",
                                    "actualTargetVisible", "topOrEndClamped"}) {
                passed = passed && row[QLatin1String(key)].toBool();
            }
        }
        if (!passed) ++failed;
    }
    QTextStream(stdout) << QJsonDocument(QJsonObject{{"qtVersion", qVersion()},
        {"scenarios", results}, {"failed", failed}}).toJson();
    return failed ? 1 : 0;
}
