#pragma once

#include <QApplication>
#include <QColor>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QToolButton>
#include <QPointer>
#include <QtMath>

namespace PreviewUi {
enum class Symbol {
    Refresh,
    Sync,
    Search,
    Settings,
    Float,
    Dock,
    Close,
    Previous,
    Next,
    Copy,
    Success,
    Error,
    Outline,
    CaseSensitive,
    ChevronDown,
    ExpandAll,
    CollapseAll
};

inline QColor mix(const QColor &first, const QColor &second, qreal secondRatio)
{
    const qreal clamped = qBound(0.0, secondRatio, 1.0);
    const qreal firstRatio = 1.0 - clamped;
    return QColor(
        qRound(first.red() * firstRatio + second.red() * clamped),
        qRound(first.green() * firstRatio + second.green() * clamped),
        qRound(first.blue() * firstRatio + second.blue() * clamped),
        qRound(first.alpha() * firstRatio + second.alpha() * clamped));
}

inline bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128;
}

inline QColor subtleBorder(const QPalette &palette)
{
    return mix(palette.color(QPalette::Base),
               palette.color(QPalette::Text),
               isDark(palette) ? 0.16 : 0.12);
}

// Vector icons are painted at the requested device scale using the current palette.
class IconEngine final : public QIconEngine
{
public:
    explicit IconEngine(Symbol symbol, QWidget *widget = nullptr) : m_symbol(symbol), m_widget(widget) {}
    QIconEngine *clone() const override { return new IconEngine(m_symbol, m_widget); }
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->translate(rect.topLeft());
        p->scale(rect.width() / 16.0, rect.height() / 16.0);
        const QPalette pal = m_widget ? m_widget->palette() : QApplication::palette();
        QColor stroke = pal.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active,
                                  QPalette::WindowText);
        if (m_symbol == Symbol::Success) {
            stroke = mix(stroke, QColor(46, 160, 67), 0.72);
        } else if (m_symbol == Symbol::Error) {
            stroke = mix(stroke, QColor(218, 54, 51), 0.72);
        }
        p->setPen(QPen(stroke, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        QPainterPath path;
        switch (m_symbol) {
        case Symbol::Refresh:
            p->drawArc(QRectF(3, 3, 10, 10), 40 * 16, 290 * 16);
            path.moveTo(10, 2); path.lineTo(13, 3); path.lineTo(13, 6); break;
        case Symbol::Sync:
            path.moveTo(2, 5); path.lineTo(13, 5); path.lineTo(10, 2);
            path.moveTo(14, 11); path.lineTo(3, 11); path.lineTo(6, 14); break;
        case Symbol::Search:
            p->drawEllipse(QRectF(2.2, 2.2, 7.6, 7.6));
            p->drawLine(QPointF(8.8, 8.8), QPointF(13.8, 13.8)); break;
        case Symbol::Settings:
            for (int y : {4, 8, 12}) p->drawLine(2, y, 14, y);
            p->drawEllipse(QRectF(4, 2, 4, 4)); p->drawEllipse(QRectF(9, 6, 4, 4));
            p->drawEllipse(QRectF(3, 10, 4, 4)); break;
        case Symbol::Float:
            path.moveTo(7, 3); path.lineTo(2, 3); path.lineTo(2, 14); path.lineTo(13, 14); path.lineTo(13, 9);
            path.moveTo(7, 9); path.lineTo(14, 2); path.lineTo(9, 2);
            path.moveTo(14, 2); path.lineTo(14, 7); break;
        case Symbol::Dock:
            p->drawRoundedRect(QRectF(2, 2, 12, 12), 1.5, 1.5); p->drawLine(10, 2, 10, 14);
            path.moveTo(4, 8); path.lineTo(8, 8); path.lineTo(6, 6);
            path.moveTo(8, 8); path.lineTo(6, 10); break;
        case Symbol::Close:
            p->drawLine(4, 4, 12, 12); p->drawLine(12, 4, 4, 12); break;
        case Symbol::Previous:
        case Symbol::Next: {
            const int sign = m_symbol == Symbol::Previous ? -1 : 1;
            path.moveTo(4, 8 - sign * 2); path.lineTo(8, 8 + sign * 2); path.lineTo(12, 8 - sign * 2); break;
        }
        case Symbol::Copy:
            p->drawRoundedRect(QRectF(5.8, 4.8, 7.8, 8.8), 1.3, 1.3);
            path.moveTo(4, 11); path.lineTo(2.4, 11); path.lineTo(2.4, 2.4); path.lineTo(10, 2.4); break;
        case Symbol::Success:
            path.moveTo(3, 8.2); path.lineTo(6.8, 12); path.lineTo(13.8, 3.6); break;
        case Symbol::Error:
            p->drawEllipse(QRectF(2, 2, 12, 12)); p->drawLine(8, 5, 8, 9); p->drawPoint(8, 12); break;
        case Symbol::Outline:
            p->drawRoundedRect(QRectF(2, 2.5, 12, 11), 1.8, 1.8);
            p->drawLine(QPointF(6.2, 2.5), QPointF(6.2, 13.5));
            p->drawLine(QPointF(8.4, 5.5), QPointF(12.0, 5.5));
            p->drawLine(QPointF(8.4, 8.0), QPointF(11.4, 8.0));
            p->drawLine(QPointF(8.4, 10.5), QPointF(12.0, 10.5));
            break;
        case Symbol::CaseSensitive:
            // 'A' and 'a'
            path.moveTo(2.0, 12.2); path.lineTo(4.6, 4.0); path.lineTo(7.2, 12.2);
            path.moveTo(2.8, 9.8); path.lineTo(6.4, 9.8);
            p->drawEllipse(QRectF(9.2, 7.2, 4.0, 4.8));
            path.moveTo(13.2, 7.2); path.lineTo(13.2, 12.2);
            break;
        case Symbol::ChevronDown:
            path.moveTo(4.5, 6.2); path.lineTo(8.0, 9.8); path.lineTo(11.5, 6.2);
            break;
        case Symbol::ExpandAll:
            path.moveTo(5.0, 6.0); path.lineTo(8.0, 3.0); path.lineTo(11.0, 6.0);
            path.moveTo(5.0, 10.0); path.lineTo(8.0, 13.0); path.lineTo(11.0, 10.0);
            break;
        case Symbol::CollapseAll:
            path.moveTo(5.0, 3.5); path.lineTo(8.0, 6.5); path.lineTo(11.0, 3.5);
            path.moveTo(5.0, 12.5); path.lineTo(8.0, 9.5); path.lineTo(11.0, 12.5);
            break;
        }
        p->drawPath(path);
        p->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }
    void virtual_hook(int id, void *data) override
    {
        if (id == QIconEngine::ScaledPixmapHook) {
            auto *request = static_cast<QIconEngine::ScaledPixmapArgument *>(data);
            request->pixmap = pixmap(request->size * request->scale, request->mode, request->state);
            request->pixmap.setDevicePixelRatio(request->scale);
            return;
        }
        QIconEngine::virtual_hook(id, data);
    }
private:
    Symbol m_symbol;
    QPointer<QWidget> m_widget;
};

inline QIcon icon(Symbol symbol, QWidget *widget = nullptr) { return QIcon(new IconEngine(symbol, widget)); }
inline void setup(QToolButton *button, Symbol symbol, const QString &name)
{
    button->setIcon(icon(symbol, button));
    button->setIconSize(QSize(16, 16));
    button->setMinimumSize(28, 28);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setAccessibleName(name);
    button->setToolTip(name);
}
}
