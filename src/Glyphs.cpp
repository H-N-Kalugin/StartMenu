#include "Glyphs.h"

#include <QPainter>
#include <QPainterPath>

namespace Glyphs
{
void draw(QPainter& p, Kind kind, const QRectF& box, const QColor& color, const QColor& background)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const QRectF r = box.adjusted(2, 2, -2, -2);
    const qreal x = r.x(), y = r.y(), w = r.width(), h = r.height();

    switch (kind)
    {
    case Kind::Tiles:
    {
        const qreal s = (w - 3) / 2;
        for (int i = 0; i < 4; ++i)
            p.drawRoundedRect(QRectF(x + (i % 2) * (s + 3), y + (i / 2) * (s + 3), s, s), 1.5, 1.5);
        break;
    }
    case Kind::List:
        for (int i = 0; i < 3; ++i)
        {
            const qreal yy = y + h * (0.2 + 0.3 * i);
            p.drawRect(QRectF(x, yy - 1.5, 3, 3));
            p.drawLine(QPointF(x + 7, yy), QPointF(x + w, yy));
        }
        break;
    case Kind::Layers:
    {
        // Два «листа» ромбом друг над другом — набор раскладок.
        auto diamond = [&](qreal cy) {
            const QPointF pts[4] = {{x + w / 2, cy - h * 0.22}, {x + w, cy}, {x + w / 2, cy + h * 0.22}, {x, cy}};
            p.drawPolygon(pts, 4);
        };
        p.setBrush(background);
        diamond(y + h * 0.62);
        diamond(y + h * 0.38);
        break;
    }
    case Kind::Documents:
    {
        QPainterPath path;
        path.moveTo(x + w * 0.15, y);
        path.lineTo(x + w * 0.6, y);
        path.lineTo(x + w * 0.85, y + h * 0.25);
        path.lineTo(x + w * 0.85, y + h);
        path.lineTo(x + w * 0.15, y + h);
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(QPointF(x + w * 0.32, y + h * 0.5), QPointF(x + w * 0.68, y + h * 0.5));
        p.drawLine(QPointF(x + w * 0.32, y + h * 0.72), QPointF(x + w * 0.68, y + h * 0.72));
        break;
    }
    case Kind::Folder:
    {
        QPainterPath path;
        path.moveTo(x, y + h * 0.15);
        path.lineTo(x + w * 0.38, y + h * 0.15);
        path.lineTo(x + w * 0.5, y + h * 0.3);
        path.lineTo(x + w, y + h * 0.3);
        path.lineTo(x + w, y + h * 0.88);
        path.lineTo(x, y + h * 0.88);
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(QPointF(x, y + h * 0.45), QPointF(x + w, y + h * 0.45));
        break;
    }
    case Kind::Downloads:
    {
        const qreal cx = x + w / 2;
        p.drawLine(QPointF(cx, y), QPointF(cx, y + h * 0.7));
        const QPointF arrow[3] = {{cx - w * 0.3, y + h * 0.42}, {cx, y + h * 0.72}, {cx + w * 0.3, y + h * 0.42}};
        p.drawPolyline(arrow, 3);
        p.drawLine(QPointF(x + w * 0.1, y + h), QPointF(x + w * 0.9, y + h));
        break;
    }
    case Kind::Settings:
    {
        const qreal y1 = y + h * 0.3, y2 = y + h * 0.72;
        p.drawLine(QPointF(x, y1), QPointF(x + w, y1));
        p.drawLine(QPointF(x, y2), QPointF(x + w, y2));
        p.setBrush(background);
        p.drawEllipse(QPointF(x + w * 0.66, y1), 2.8, 2.8);
        p.drawEllipse(QPointF(x + w * 0.34, y2), 2.8, 2.8);
        break;
    }
    case Kind::Run:
    {
        p.drawRoundedRect(QRectF(x, y + h * 0.1, w, h * 0.8), 2, 2);
        p.drawLine(QPointF(x, y + h * 0.32), QPointF(x + w, y + h * 0.32));
        const QPointF chevron[3] = {{x + w * 0.32, y + h * 0.48}, {x + w * 0.5, y + h * 0.6}, {x + w * 0.32, y + h * 0.72}};
        p.drawPolyline(chevron, 3);
        break;
    }
    case Kind::Clipboard:
    {
        // Планшет с зажимом сверху и строчками
        p.drawRoundedRect(QRectF(x + w * 0.12, y + h * 0.1, w * 0.76, h * 0.9), 2, 2);
        p.setBrush(background);
        p.drawRoundedRect(QRectF(x + w * 0.32, y, w * 0.36, h * 0.22), 1.5, 1.5);
        p.drawLine(QPointF(x + w * 0.32, y + h * 0.48), QPointF(x + w * 0.68, y + h * 0.48));
        p.drawLine(QPointF(x + w * 0.32, y + h * 0.7), QPointF(x + w * 0.58, y + h * 0.7));
        break;
    }
    case Kind::Volume:
    case Kind::VolumeMute:
    {
        // Динамик; справа — две «волны» или крестик
        QPainterPath sp;
        sp.moveTo(x, y + h * 0.36);
        sp.lineTo(x + w * 0.22, y + h * 0.36);
        sp.lineTo(x + w * 0.48, y + h * 0.1);
        sp.lineTo(x + w * 0.48, y + h * 0.9);
        sp.lineTo(x + w * 0.22, y + h * 0.64);
        sp.lineTo(x, y + h * 0.64);
        sp.closeSubpath();
        p.drawPath(sp);
        if (kind == Kind::Volume)
        {
            p.drawArc(QRectF(x + w * 0.42, y + h * 0.3, w * 0.3, h * 0.4), -50 * 16, 100 * 16);
            p.drawArc(QRectF(x + w * 0.42, y + h * 0.1, w * 0.56, h * 0.8), -50 * 16, 100 * 16);
        }
        else
        {
            p.drawLine(QPointF(x + w * 0.66, y + h * 0.36), QPointF(x + w * 0.94, y + h * 0.64));
            p.drawLine(QPointF(x + w * 0.94, y + h * 0.36), QPointF(x + w * 0.66, y + h * 0.64));
        }
        break;
    }
    case Kind::Power:
    {
        const QRectF c = r.adjusted(w * 0.06, h * 0.1, -w * 0.06, -h * 0.02);
        // Дуга с разрывом сверху: углы в Qt — 1/16 градуса, против часовой от «трёх часов».
        p.drawArc(c, 125 * 16, 290 * 16);
        p.drawLine(QPointF(x + w / 2, y), QPointF(x + w / 2, y + h * 0.45));
        break;
    }
    }
    p.restore();
}
}
