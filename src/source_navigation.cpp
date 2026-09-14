#include "source_navigation.h"

#include <QAbstractScrollArea>
#include <QAccessible>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QScrollBar>
#include <QThread>
#include <QWidget>

namespace {
QString message(const char *text)
{
    return QCoreApplication::translate("SourceNavigation", text);
}

QAccessibleTextInterface *textInterface(QWidget *editor)
{
    if (!editor || editor->thread() != QThread::currentThread() ||
        !editor->inherits("QsciScintilla")) {
        return nullptr;
    }
    QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(editor);
    return accessible && accessible->isValid() ? accessible->textInterface() : nullptr;
}
}

SourceReadResult readAccessibleSource(QWidget *editor)
{
    const QPointer<QWidget> guard(editor);
    QAccessibleTextInterface *text = textInterface(editor);
    if (!guard || !text) {
        return {QString(), message("宿主未提供源码读取能力。"), false};
    }
    const int count = text->characterCount();
    if (count < 0) {
        return {QString(), message("宿主返回了无效的源码长度。"), false};
    }
    const QString source = count == 0 ? QString() : text->text(0, count);
    if (!guard || source.toUcs4().size() != count) {
        return {QString(), message("源码读取期间内容失效或字符计数不一致。"), false};
    }
    return {source, QString(), true};
}

SourceNavigationResult navigateAccessibleSource(
    QWidget *editor, int line, int scalarOffset,
    const std::function<bool()> &stillCurrent)
{
    const QPointer<QWidget> guard(editor);
    auto valid = [&]() { return guard && stillCurrent && stillCurrent(); };
    if (!valid() || line < 0 || scalarOffset < 0) {
        return {false, message("文档或标题位置已失效，请刷新后重试。")};
    }
    const QPointer<QAbstractScrollArea> area = qobject_cast<QAbstractScrollArea *>(editor);
    QAccessibleTextInterface *text = textInterface(editor);
    if (!valid() || !area || !text ||
        editor->metaObject()->indexOfMethod("ensureLineVisible(int)") < 0) {
        return {false, message("宿主未提供标题源行定位能力。")};
    }
    const int count = text->characterCount();
    if (!valid() || scalarOffset >= count) {
        return {false, message("标题源码位置超出当前文档。")};
    }
    const QPointer<QScrollBar> bar = area->verticalScrollBar();
    const QPointer<QWidget> viewport = area->viewport();
    if (!bar || !viewport || !viewport->isVisible()) {
        return {false, message("源码视口当前不可用。")};
    }
    auto alive = [&]() { return valid() && area && bar && viewport; };
    if (!QMetaObject::invokeMethod(editor, "ensureLineVisible", Qt::DirectConnection,
                                   Q_ARG(int, line)) || !alive()) {
        return {false, message("源码定位期间文档已失效。")};
    }
    text->scrollToSubstring(scalarOffset, scalarOffset + 1);
    if (!alive()) {
        return {false, message("源码定位期间文档已失效。")};
    }
    // The baseline's characterRect() passes a wrong Scintilla argument. Use
    // source hit testing instead; preserve the host's valueChanged receivers.
    auto topOffset = [&]() {
        return text->offsetAtPoint(viewport->mapToGlobal(QPoint(0, 0)));
    };
    int low = bar->minimum();
    int high = bar->maximum();
    int best = low;
    for (int steps = 0; low <= high && steps < 32; ++steps) {
        const int mid = low + (high - low) / 2;
        bar->setValue(mid);
        if (!alive()) {
            return {false, message("源码定位期间文档已失效。")};
        }
        const int offset = topOffset();
        if (!alive() || offset < 0 || offset > count) {
            return {false, message("宿主无法核对源码阅读位置。")};
        }
        if (offset <= scalarOffset) {
            best = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    bar->setValue(best);
    if (!alive()) {
        return {false, message("源码定位期间文档已失效。")};
    }
    const int first = topOffset();
    if (!alive()) {
        return {false, message("源码定位期间文档已失效。")};
    }
    const int last = text->offsetAtPoint(viewport->mapToGlobal(
        QPoint(viewport->width() - 1, viewport->height() - 1)));
    if (!alive() || first < 0 || first > scalarOffset || last < scalarOffset) {
        return {false, message("未能确认标题在源码视口中可见。")};
    }
    return {true, QString()};
}
