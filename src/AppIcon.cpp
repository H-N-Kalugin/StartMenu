#include "AppIcon.h"

#include <QBuffer>
#include <QFile>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QtEndian>

namespace AppIcon
{
QImage render(int px)
{
    QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = px;

    // Подложка: тёмная, со скруглением и тонкой светлой кромкой
    const QRectF plate(s * 0.03, s * 0.03, s * 0.94, s * 0.94);
    QLinearGradient bg(plate.topLeft(), plate.bottomRight());
    bg.setColorAt(0, QColor(0x2A, 0x2D, 0x38));
    bg.setColorAt(1, QColor(0x14, 0x15, 0x1B));
    p.setPen(QPen(QColor(255, 255, 255, px <= 20 ? 0 : 30), qMax(1.0, s / 64)));
    p.setBrush(bg);
    p.drawRoundedRect(plate, s * 0.22, s * 0.22);

    // Четыре плитки 2×2: акцентный градиент, правая нижняя — «активная», светлее
    const qreal area = s * (px <= 20 ? 0.66 : 0.56);
    const qreal gap = area * 0.12;
    const qreal cell = (area - gap) / 2;
    const qreal x0 = (s - area) / 2, y0 = (s - area) / 2;
    const QColor c1(0x4C, 0x8D, 0xFF), c2(0x8A, 0x6B, 0xFF);
    for (int i = 0; i < 4; ++i)
    {
        const QRectF r(x0 + (i % 2) * (cell + gap), y0 + (i / 2) * (cell + gap), cell, cell);
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, i == 3 ? c1.lighter(125) : c1);
        g.setColorAt(1, i == 3 ? c2.lighter(125) : c2);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRoundedRect(r, cell * 0.24, cell * 0.24);
    }
    return img;
}

QIcon icon()
{
    QIcon ic;
    for (int s : {16, 20, 24, 32, 40, 48, 64, 128, 256})
        ic.addPixmap(QPixmap::fromImage(render(s)));
    return ic;
}

QIcon iconWithDot()
{
    QIcon ic;
    for (int s : {16, 20, 24, 32, 40, 48, 64, 128, 256})
    {
        QImage img = render(s);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal d = s * 0.38; // диаметр точки
        const QRectF dot(s - d - s * 0.02, s * 0.02, d, d);
        p.setPen(QPen(QColor(20, 20, 22), qMax(1.0, s * 0.05)));
        p.setBrush(QColor(0x4C, 0x8D, 0xFF));
        p.drawEllipse(dot);
        ic.addPixmap(QPixmap::fromImage(img));
    }
    return ic;
}

// Формат .ico: заголовок + оглавление + картинки (PNG внутри .ico Windows понимает с Vista).
bool writeIco(const QString& path)
{
    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};
    const int n = int(std::size(sizes));
    QList<QByteArray> pngs;
    for (int s : sizes)
    {
        QByteArray data;
        QBuffer buf(&data);
        buf.open(QIODevice::WriteOnly);
        render(s).save(&buf, "PNG");
        pngs.append(data);
    }
    QByteArray out;
    auto u16 = [&](quint16 v) { out.append(char(v & 0xFF)).append(char(v >> 8)); };
    auto u32 = [&](quint32 v) { u16(quint16(v & 0xFFFF)); u16(quint16(v >> 16)); };
    u16(0); u16(1); u16(quint16(n));
    quint32 offset = 6 + 16 * quint32(n);
    for (int i = 0; i < n; ++i)
    {
        out.append(char(sizes[i] >= 256 ? 0 : sizes[i]));
        out.append(char(sizes[i] >= 256 ? 0 : sizes[i]));
        out.append(char(0)).append(char(0));
        u16(1); u16(32);
        u32(quint32(pngs[i].size()));
        u32(offset);
        offset += quint32(pngs[i].size());
    }
    for (const QByteArray& d : pngs)
        out.append(d);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(out) == out.size();
}
}
