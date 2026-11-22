// Стили Studio, Bento и Fluent. Каждый переопределяет только то, чем отличается от «Классического» (Skin.cpp).
// Цвета фона, текста и акцента берутся из Theme (их задаёт пресет стиля, а дальше — настройки пользователя).
#include "Glyphs.h"
#include "Lang.h"
#include "Settings.h"
#include "Skin.h"
#include "Theme.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

namespace
{
    QColor withAlpha(const QColor& c, qreal a)
    {
        QColor r = c;
        r.setAlphaF(qBound(0.0, a, 1.0));
        return r;
    }
    QColor text(qreal a = 1.0) { return withAlpha(Theme::TextColor.color(), a); }
    QColor white(qreal a) { return QColor(255, 255, 255, int(255 * a)); }
    // Фоны из настроек «Цвет групп и плиток», «Фон групп», «Фон плиток».
    QColor groupBg() { return Theme::GroupBg.color(); }
    QColor tileBg() { return Theme::TileBg.color(); }
    qreal tileAlpha() { return Theme::TileBg.a / 255.0; }
    // Подсвеченная плитка: тот же цвет, но плотнее.
    QColor tileHot(qreal extra) { return withAlpha(Theme::TileBg.color(), qMin(1.0, tileAlpha() + extra)); }
    qreal k() { return Theme::Cell / 46.0; } // масштаб от «родного» размера ячейки

    enum class Size { S, M, W, L };
    Size sizeOf(const QRect& r)
    {
        const bool tall = r.height() > Theme::Cell * 3;
        const bool wide = r.width() > Theme::Cell * 3;
        if (r.height() <= Theme::Cell)
            return Size::S;
        if (tall)
            return Size::L;
        return wide ? Size::W : Size::M;
    }

    void pressTransform(QPainter& p, const QRect& r, bool pressed, qreal scale = 0.96)
    {
        if (!pressed)
            return;
        p.translate(r.center());
        p.scale(scale, scale);
        p.translate(-r.center());
    }

    void drawSearchGlyph(QPainter& p, const QRectF& box, const QColor& c)
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        const qreal s = box.width();
        p.drawEllipse(QPointF(box.x() + s * 0.42, box.y() + s * 0.42), s * 0.30, s * 0.30);
        p.drawLine(QPointF(box.x() + s * 0.64, box.y() + s * 0.64), QPointF(box.x() + s * 0.92, box.y() + s * 0.92));
        p.restore();
    }

    // «Сетка 46·4» / «5 групп» → число и подпись отдельно (у чипов Bento число выделено).
    void splitNumber(const QString& s, QString& num, QString& label, bool& numFirst)
    {
        const int sp = int(s.indexOf(QLatin1Char(' ')));
        if (sp < 0)
        {
            num.clear();
            label = s;
            numFirst = false;
            return;
        }
        const QString a = s.left(sp), b = s.mid(sp + 1);
        numFirst = a[0].isDigit();
        num = numFirst ? a : (b[0].isDigit() ? b : QString());
        label = numFirst ? b : (num.isEmpty() ? s : a);
    }

    // Подсветка-«ромб» (Studio): квадрат, повёрнутый на 45°.
    void diamond(QPainter& p, const QPointF& c, qreal size, const QColor& col)
    {
        const qreal h = size / 2;
        const QPointF pts[4] = {{c.x(), c.y() - h}, {c.x() + h, c.y()}, {c.x(), c.y() + h}, {c.x() - h, c.y()}};
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawPolygon(pts, 4);
    }

    // =====================================================================================
    // Studio — панель профессионального редактора: графит, сетка, линии в 1 px, почти острые углы,
    // уголки-«кадрирование» при наведении, один акцент.
    // =====================================================================================
    class StudioSkin : public Skin
    {
    public:
        int kind() const override { return Studio; }

        void applyMetrics() const override
        {
            Theme::ContentMargin = 14;
            Theme::ContentInsetX = 14; // как в макете: всё в рабочей зоне выровнено по x = 80
            Theme::CellGap = 4;
            Theme::TileRadius = 3;
            Theme::GroupPad = 0;      // плитки стоят прямо на сетке; панель «с фоном» выступает на 8 px в желоб
            Theme::GroupHeader = 38;  // строка заголовка ≈ одна ячейка
            Theme::GroupRadius = 4;
            Theme::GroupGapX = 46;    // желоб между колонками
            Theme::GroupGapY = 16;
            Theme::TitleFontPx = 13;
            Theme::ListRow = 32;
            Theme::ListHeader = 32;
            Theme::ListIcon = 20;
            Theme::ListColumnWidth = 252;
        }

        void preset(Settings& s) const override
        {
            Skin::preset(s);
            s.background = QColor(0x14, 0x15, 0x18);
            s.opacity = 74;
            s.effect = Settings::Effect::Acrylic;
            s.radius = 8;
            s.accent = QColor(0xFF, 0x5A, 0x1F);
            s.textColor = QColor(0xE7, 0xE8, 0xEB);
            s.groupColor = QColor(255, 255, 255);
            s.groupOpacity = 4;
            s.tileOpacity = 5;
        }

        QFont font(Font role) const override
        {
            const QStringList ui{QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")};
            const QStringList mono{QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")};
            switch (role)
            {
            case Font::Tile:        return makeFont(ui, Theme::TileFontScaled);
            case Font::TileBig:     return makeFont(ui, Theme::TileFontScaled + 1, QFont::DemiBold);
            case Font::GroupTitle:  return makeFont(ui, 13, QFont::DemiBold);
            case Font::ListRow:     return makeFont(ui, 13);
            case Font::ListSection: return makeFont(mono, 11, QFont::Bold);
            case Font::ListTitle:   return makeFont(ui, 14, QFont::DemiBold);
            case Font::Search:      return makeFont(ui, 13);
            case Font::Meta:        return makeFont(mono, 11);
            }
            return Skin::font(role);
        }

        int searchFieldHeight() const override { return 32; }
        int titleOffset() const override { return QFontMetrics(font(Font::Meta)).horizontalAdvance(QStringLiteral("00")) + 8; }

        QColor t2() const { return text(0.68); }
        QColor t3() const { return text(0.55); } // ≈ #8B9099 на графите — контраст ≥ 4.5:1

        void paintSidebarBack(QPainter& p, const QRect& r) const override
        {
            p.fillRect(r, QColor(6, 7, 9, 100));
            p.fillRect(QRect(r.right(), r.top(), 1, r.height()), white(0.07));
        }

        void paintSidebarButton(QPainter& p, const SidebarButtonPaint& b) const override
        {
            const QRect r = b.r.adjusted(2, 2, -2, -2);
            if (b.hover || b.active)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(white(b.pressed ? 0.10 : b.active ? 0.07 : 0.045));
                p.drawRoundedRect(r, 4, 4);
            }
            if (b.active)
                p.fillRect(QRect(0, b.r.center().y() - 8, 2, 16), Theme::Accent.color());
        }

        void paintSearchField(QPainter& p, const QRect& r, const QString& txt, bool caret, bool focused) const override
        {
            const bool active = !txt.isEmpty() || focused;
            const bool hasText = !txt.isEmpty();
            const QColor acc = Theme::Accent.color();
            p.setRenderHint(QPainter::Antialiasing);
            if (active)
            {
                // Ореол 3 px вокруг активного поля
                p.setPen(QPen(withAlpha(acc, 0.12), 3));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(QRectF(r).adjusted(-1.5, -1.5, 1.5, 1.5), 5, 5);
            }
            p.setPen(QPen(active ? withAlpha(acc, 0.75) : white(0.13), 1));
            p.setBrush(QColor(0, 0, 0, 77));
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            drawSearchGlyph(p, QRectF(r.x() + 10, r.center().y() - 8, 16, 16), active ? acc : t3());

            const QFont f = font(Font::Search);
            const QFontMetrics fm(f);
            const QRect tr = r.adjusted(34, 0, -48, 0);
            p.setFont(f);
            p.setPen(hasText ? text() : t2());
            if (!(focused && !hasText))
                p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter,
                           fm.elidedText(hasText ? txt : TXT("Поиск приложений, параметров и файлов"),
                                         Qt::ElideRight, tr.width()));
            if (active && caret)
                p.fillRect(QRectF(tr.x() + qMin(fm.horizontalAdvance(txt), tr.width()) + 1, r.center().y() - 8, 1.5, 16), acc);
            // Клавиша справа: «Esc», пока поле активно (подсказка, как очистить)
            if (!active)
                return;
            const QFont mono = font(Font::Meta);
            const QString key = QStringLiteral("Esc");
            const int kw = QFontMetrics(mono).horizontalAdvance(key) + 10;
            const QRect kr(r.right() - 8 - kw, r.center().y() - 9, kw, 18);
            p.setPen(QPen(white(0.13), 1));
            p.setBrush(white(0.03));
            p.drawRoundedRect(QRectF(kr).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
            p.setFont(mono);
            p.setPen(t2());
            p.drawText(kr, Qt::AlignCenter, key);
        }

        void paintToolbarInfo(QPainter& p, const QRect& r, const QStringList& parts) const override
        {
            // Пункты справа налево, между ними черта 1×12.
            const QFont mono = font(Font::Meta);
            const QFontMetrics fm(mono);
            p.setFont(mono);
            int x = r.right();
            for (int i = int(parts.size()) - 1; i >= 0; --i)
            {
                const int w = fm.horizontalAdvance(parts[i]);
                if (x - w < r.left())
                    break;
                // Цифры светлее подписи: «07 ГРУПП» → «07» t2, «ГРУПП» t3.
                const QString& s = parts[i];
                const int sp = int(s.indexOf(QLatin1Char(' ')));
                const QString head = sp > 0 ? s.left(sp) : s, tail = sp > 0 ? s.mid(sp) : QString();
                const bool numberFirst = !head.isEmpty() && head[0].isDigit();
                p.setPen(numberFirst ? t2() : t3());
                p.drawText(QRect(x - w, r.y(), w, r.height()), Qt::AlignLeft | Qt::AlignVCenter, head);
                p.setPen(numberFirst ? t3() : t2());
                p.drawText(QRect(x - w + fm.horizontalAdvance(head), r.y(), w, r.height()), Qt::AlignLeft | Qt::AlignVCenter, tail);
                x -= w + 14;
                if (i > 0)
                {
                    p.fillRect(QRect(x + 6, r.center().y() - 6, 1, 12), white(0.13));
                    x -= 0;
                }
            }
        }

        int statusBarHeight() const override { return 28; }

        // Плеер как поле поиска: тёмная подложка, рамка 1 px, острые углы, значок «играть» акцентом.
        MediaStyle mediaStyle(const QRect& r) const override
        {
            MediaStyle st = Skin::mediaStyle(r);
            st.radius = 4;
            st.artRadius = 2;
            st.roundButtons = false;
            st.bg = QColor(0, 0, 0, 77);
            st.border = white(0.13);
            st.hoverBg = white(0.07);
            st.playBg = QColor(0, 0, 0, 0);
            st.playGlyph = Theme::Accent.color();
            st.glyph = t2();
            st.title = makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 12, QFont::DemiBold);
            st.sub = font(Font::Meta);
            st.sub.setPixelSize(10);
            st.titleColor = text(0.92);
            st.subColor = t3();
            st.upperSub = true;
            return st;
        }

        void paintStatusBar(QPainter& p, const QRect& r, const QString& name, const QString& path,
                            const QString& location, const QStringList& hints) const override
        {
            p.fillRect(QRect(r.left(), r.top(), r.width(), 1), white(0.07));
            p.setRenderHint(QPainter::Antialiasing);
            const QFont ui = makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 12);
            const QFont mono = font(Font::Meta);
            const int cy = r.center().y() + 1;
            // Подсказки: «клавиша  подпись», справа налево.
            const QFontMetrics mfm(mono);
            p.setFont(mono);
            int rx = r.right() - 24;
            for (int i = int(hints.size()) - 1; i >= 0; --i)
            {
                const QString key = hints[i].section(QLatin1Char('|'), 0, 0);
                const QString what = hints[i].section(QLatin1Char('|'), 1);
                const int ww = mfm.horizontalAdvance(what);
                const int kw = qMax(18, mfm.horizontalAdvance(key) + 8);
                if (rx - ww - 6 - kw < r.center().x())
                    break;
                p.setFont(mono);
                p.setPen(t3());
                p.drawText(QRect(rx - ww, r.y(), ww, r.height()), Qt::AlignLeft | Qt::AlignVCenter, what);
                rx -= ww + 6;
                const QRectF kr(rx - kw, cy - 8, kw, 16);
                p.setPen(QPen(white(0.13), 1));
                p.setBrush(white(0.03));
                p.drawRoundedRect(kr.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
                p.setPen(t2());
                p.drawText(kr, Qt::AlignCenter, key);
                rx -= kw + 16;
            }
            if (name.isEmpty())
                return;
            const int x = r.left() + Theme::ContentInsetX + 10;
            diamond(p, QPointF(x + 3, cy), 7, Theme::Accent.color());
            // Название, «ГРУППА / 01 · 2×2», а за ними — где лежит файл.
            paintHoverName(p, r.adjusted(0, 1, 0, 1), x + 16, rx - 8, name, path, location, ui, t2(), mono, t3());
        }

        void paintCanvas(QPainter& p, const QRect& area, const QPoint& origin, int pitch,
                         const QList<QRect>& occupied) const override
        {
            // Полупрозрачные крестики в узлах сетки — там, где нет групп.
            if (pitch < 8)
                return;
            const QColor c = white(0.09);
            const int gap = Theme::CellGap;
            const int x0 = origin.x() - gap / 2 + ((area.left() - origin.x()) / pitch) * pitch;
            const int y0 = origin.y() - gap / 2 + qMax(0, (area.top() - origin.y()) / pitch) * pitch;
            for (int y = y0; y <= area.bottom(); y += pitch)
                for (int x = x0; x <= area.right(); x += pitch)
                {
                    const QPoint pt(x, y);
                    bool busy = false;
                    for (const QRect& o : occupied)
                        if (o.adjusted(-10, -10, 10, 10).contains(pt))
                        {
                            busy = true;
                            break;
                        }
                    if (busy)
                        continue;
                    p.fillRect(QRect(x - 3, y, 7, 1), c);
                    p.fillRect(QRect(x, y - 3, 1, 7), c);
                }
        }

        void paintGroup(QPainter& p, const GroupPaint& g) const override
        {
            const Group& gr = *g.group;
            p.setRenderHint(QPainter::Antialiasing);
            const QColor base = gr.color.isValid() ? QColor(gr.color.red(), gr.color.green(), gr.color.blue()) : QColor();
            if (!gr.clean)
            {
                // Панель выступает на 8 px в желоб, плитки остаются на сетке. Тинт ≤ 0.40 — белый текст держит контраст.
                const QRect panel(g.frame.left() - 8, g.header.top() + 6, g.frame.width() + 16,
                                  g.body.bottom() + 8 - (g.header.top() + 6) + 1);
                p.setPen(QPen(base.isValid() ? withAlpha(Skin::mix(base, Qt::white, 0.55), 0.14) : white(0.06), 1));
                p.setBrush(base.isValid() ? withAlpha(Skin::mix(base, QColor(10, 12, 16), 0.45), 0.26) : groupBg());
                p.drawRoundedRect(QRectF(panel).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            }
            // Заголовок: название слева + линия 1 px по низу строки заголовка.
            const int lineY = g.header.bottom() - 3;
            p.fillRect(QRect(g.frame.left(), lineY, g.frame.width(), 1),
                       !gr.clean && base.isValid() ? withAlpha(Skin::mix(base, Qt::white, 0.6), 0.16) : white(0.13));
            const QFont f = font(Font::GroupTitle);
            const QFontMetrics fm(f);
            p.setFont(f);
            const QRect tr(g.frame.left(), g.header.top(), g.frame.width(), lineY - g.header.top() - 6);
            auto label = [&](int i) { return gr.tabs[i].title.isEmpty() ? TXT("Без названия") : gr.tabs[i].title; };
            // Номер группы «как слой» (акцентом — если в группе курсор) и счётчик плиток справа.
            const QFont mono = font(Font::Meta);
            const QFontMetrics mfm(mono);
            const QString num = QStringLiteral("%1").arg(g.number, 2, 10, QLatin1Char('0'));
            p.setFont(mono);
            p.setPen(g.hover ? Theme::Accent.color() : t3());
            p.drawText(tr, Qt::AlignLeft | Qt::AlignBottom, num);
            p.setPen(t3());
            p.drawText(tr, Qt::AlignRight | Qt::AlignBottom, QStringLiteral("%1").arg(g.count, 2, 10, QLatin1Char('0')));
            p.setFont(f);
            const int shift = mfm.horizontalAdvance(num) + 8;
            const QRect ttr = tr.adjusted(shift, 0, -mfm.horizontalAdvance(QStringLiteral("00")) - 8, 0);
            if (gr.tabs.size() == 1)
            {
                p.setPen(gr.title().isEmpty() ? t3() : text());
                p.drawText(ttr, Qt::AlignLeft | Qt::AlignBottom, fm.elidedText(label(0), Qt::ElideRight, ttr.width()));
                return;
            }
            for (int i = 0; i < gr.tabs.size(); ++i)
            {
                const QRect r = g.tabRect(i);
                const QRect txt(r.x(), tr.y(), r.width(), tr.height());
                const bool act = i == gr.activeTab;
                p.setPen(act ? text() : (g.hoverTab == i ? t2() : t3()));
                p.drawText(txt, Qt::AlignHCenter | Qt::AlignBottom, label(i));
                if (act)
                {
                    const int w = fm.horizontalAdvance(label(i));
                    p.fillRect(QRect(r.center().x() - w / 2, lineY - 1, w, 2), Theme::Accent.color());
                }
                if (i + 1 < gr.tabs.size())
                    p.fillRect(QRect(r.right() + 2, tr.bottom() - 12, 1, 12), white(0.13));
            }
        }

        // Уголки-«кадрирование», как рамка выделения во вьюпорте.
        static void brackets(QPainter& p, const QRectF& r0, const QColor& c, qreal arm)
        {
            const QRectF r = r0.adjusted(-1.5, -1.5, 1.5, 1.5);
            QPen pen(c, 1.5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            const QPointF cs[4] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
            const int dx[4] = {1, -1, -1, 1}, dy[4] = {1, 1, -1, -1};
            for (int i = 0; i < 4; ++i)
            {
                QPainterPath path;
                path.moveTo(cs[i].x() + dx[i] * arm, cs[i].y());
                path.lineTo(cs[i]);
                path.lineTo(cs[i].x(), cs[i].y() + dy[i] * arm);
                p.drawPath(path);
            }
        }

        void paintTile(QPainter& p, const TilePaint& t) const override
        {
            const QRect& r = t.r;
            p.save();
            p.setRenderHint(QPainter::Antialiasing);
            pressTransform(p, r, t.pressed);

            const QColor fill = Skin::mix(tileBg(), tileHot(0.06), t.hover);
            p.setPen(QPen(white(0.055 + 0.105 * t.hover), 1));
            p.setBrush(t.pressed ? QColor(20, 21, 24, 240) : fill);
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 2.5, 2.5);

            const qreal s = k();
            const Size sz = sizeOf(r);
            const bool labels = t.showLabel && Theme::Cell >= Theme::LabelMinCell;
            const Tile& tile = *t.tile;
            if (!labels && sz != Size::S)
            {
                // Подписи выключены — иконка ровно по центру плитки.
                const int ip = qRound(Theme::Cell * (sz == Size::L ? 1.56 : sz == Size::W ? 1.04 : 0.86));
                QRect ir(0, 0, ip, ip);
                ir.moveCenter(r.center());
                tile.isFolder ? paintFolderIcons(p, tile, ir, ip, t.icon) : t.icon(p, tile.item, ir);
            }
            else if (sz == Size::S)
            {
                const int ip = qRound(Theme::Cell * 0.61);
                QRect ir(0, 0, ip, ip);
                ir.moveCenter(r.center());
                if (tile.isFolder)
                    paintFolderIcons(p, tile, ir, ip, t.icon);
                else
                    t.icon(p, tile.item, ir);
            }
            else if (sz == Size::W)
            {
                const int ip = qRound(Theme::Cell * 1.04);
                QRect ir(0, 0, ip, ip);
                ir.moveCenter(QPoint(r.x() + r.height() / 2, r.center().y()));
                tile.isFolder ? paintFolderIcons(p, tile, ir, ip, t.icon) : t.icon(p, tile.item, ir);
                if (labels)
                    drawLabel(p, QRect(r.x() + r.height(), r.y() + 8, r.width() - r.height() - 12, r.height() - 16),
                              tile.item.name, font(Font::TileBig), text(), 2, Qt::AlignLeft | Qt::AlignVCenter);
            }
            else
            {
                // M и L: иконка в левом верхнем углу, подпись снизу слева, до двух строк.
                const bool large = sz == Size::L;
                const int pad = qRound((large ? 14 : 10) * s);
                const int ip = qRound(Theme::Cell * (large ? 1.56 : 0.78));
                const QRect ir(r.x() + pad, r.y() + pad, ip, ip);
                if (tile.isFolder)
                {
                    paintFolderIcons(p, tile, ir, ip, t.icon);
                    p.fillRect(QRect(r.x() + pad, r.y() - 1, 22, 2), t2()); // «язычок» папки
                }
                else
                    t.icon(p, tile.item, ir);
                if (labels)
                {
                    const QFont f = font(large ? Font::TileBig : Font::Tile);
                    const int lh = QFontMetrics(f).height();
                    const QRect lr(r.x() + pad, ir.bottom() + 4, r.width() - 2 * pad, r.bottom() - qRound(9 * s) - ir.bottom() - 4);
                    drawLabel(p, lr, tile.item.name, f, text(0.92), qMax(1, qMin(2, lr.height() / lh)), Qt::AlignLeft | Qt::AlignBottom);
                }
            }
            if (t.hover > 0.01 && !t.focused)
                brackets(p, r, white(0.85 * t.hover), 4 + 4 * t.hover);
            p.restore();
        }

        void paintFocus(QPainter& p, const QRect& r) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(withAlpha(Theme::Accent.color(), 0.55), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 2.5, 2.5);
            brackets(p, r, Theme::Accent.color(), 8);
        }

        void paintInsert(QPainter& p, const QRect& r) const override
        {
            // Линия 2 px с «ключевыми кадрами»-ромбами на концах.
            p.setRenderHint(QPainter::Antialiasing);
            const QColor acc = Theme::Accent.color();
            if (r.height() >= r.width())
            {
                const qreal x = r.center().x();
                p.fillRect(QRectF(x - 1, r.top(), 2, r.height()), acc);
                diamond(p, QPointF(x, r.top()), 8, acc);
                diamond(p, QPointF(x, r.bottom()), 8, acc);
            }
            else
            {
                const qreal y = r.center().y();
                p.fillRect(QRectF(r.left(), y - 1, r.width(), 2), acc);
                diamond(p, QPointF(r.left(), y), 8, acc);
                diamond(p, QPointF(r.right(), y), 8, acc);
            }
        }

        void paintFolderTarget(QPainter& p, const QRect& r) const override { paintFocus(p, r); }

        void paintNewGroup(QPainter& p, const QRect& r) const override
        {
            const QColor acc = Theme::Accent.color();
            QPen pen(acc, 1, Qt::CustomDashLine);
            pen.setDashPattern({3, 3});
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5));
            p.setFont(font(Font::Meta));
            p.drawText(r, Qt::AlignCenter, TXT("+  НОВАЯ ГРУППА"));
        }

        void paintListSection(QPainter& p, const QRect& r, const QString& title, bool letter) const override
        {
            const QFont f = letter ? makeFont({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")}, 12, QFont::Bold)
                                   : font(Font::ListSection);
            const QFontMetrics fm(f);
            p.setFont(f);
            p.setPen(letter ? Theme::Accent.color() : t3());
            const QString shown = letter ? title : title.toUpper();
            const QRect tr = r.adjusted(4, 0, 0, -6);
            p.drawText(tr, Qt::AlignLeft | Qt::AlignBottom, shown);
            const int x = tr.x() + fm.horizontalAdvance(shown) + 10;
            p.fillRect(QRect(x, tr.bottom() - fm.ascent() / 2, r.right() - x - 6, 1), white(0.13));
        }

        void paintListRow(QPainter& p, const ListRowPaint& row) const override
        {
            if (row.hover || row.selected)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(white(row.pressed ? 0.10 : 0.07));
                p.drawRoundedRect(row.r, 3, 3);
            }
            if (row.selected)
                p.fillRect(QRect(row.r.x(), row.r.center().y() - 8, 2, 16), Theme::Accent.color());
            const QRect ir(row.r.x() + 12, row.r.center().y() - Theme::ListIcon / 2, Theme::ListIcon, Theme::ListIcon);
            row.icon(p, *row.item, ir);
            const QRect tr(ir.right() + 10, row.r.y(), row.r.right() - ir.right() - 14, row.r.height());
            drawHighlighted(p, tr, row.item->name, row.highlight, font(Font::ListRow), text(), Theme::Accent.color());
        }
    };

    // =====================================================================================
    // Bento — группы-«острова» с мягким цветом из иконок, крупные скругления, жирные сжатые заголовки.
    // =====================================================================================
    class BentoSkin : public Skin
    {
    public:
        int kind() const override { return Bento; }

        void applyMetrics() const override
        {
            Theme::ContentMargin = 18;
            Theme::CellGap = 6;
            Theme::TileRadius = 14;
            Theme::GroupPad = 14;
            Theme::GroupHeader = 40;
            Theme::GroupRadius = 22;
            Theme::GroupGapX = 16;
            Theme::GroupGapY = 16;
            Theme::TitleFontPx = 16;
            Theme::ListRow = 34;
            Theme::ListHeader = 36;
            Theme::ListIcon = 22;
            Theme::ListColumnWidth = 262;
        }

        void preset(Settings& s) const override
        {
            Skin::preset(s);
            s.background = QColor(0x14, 0x16, 0x1F);
            s.opacity = 66;
            s.effect = Settings::Effect::Acrylic;
            s.radius = 20;
            s.accent = QColor(0x6E, 0x7B, 0xFF);
            s.textColor = QColor(0xF3, 0xF4, 0xF8);
            s.groupOpacity = 6;
            s.tileOpacity = 8;
        }

        QFont font(Font role) const override
        {
            const QStringList ui{QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")};
            const QStringList head{QStringLiteral("Bahnschrift SemiBold SemiCondensed"), QStringLiteral("Bahnschrift"),
                                   QStringLiteral("Segoe UI")};
            switch (role)
            {
            case Font::Tile:        return makeFont(ui, Theme::TileFontScaled, QFont::DemiBold);
            case Font::TileBig:     return makeFont(head, Theme::TileFontScaled + 5, QFont::DemiBold);
            case Font::GroupTitle:  return makeFont(head, 16, QFont::DemiBold);
            case Font::ListRow:     return makeFont(ui, 13);
            case Font::ListSection: return makeFont(head, 13, QFont::DemiBold);
            case Font::ListTitle:   return makeFont(head, 20, QFont::DemiBold);
            case Font::Search:      return makeFont(ui, 15);
            case Font::Meta:        return makeFont(ui, 11);
            }
            return Skin::font(role);
        }

        int searchFieldHeight() const override { return 38; }

        // Сведения справа от поиска — капсулы: число акцентом, подпись приглушённо.
        void paintToolbarInfo(QPainter& p, const QRect& r, const QStringList& parts) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            const QFont numF = makeFont({QStringLiteral("Bahnschrift SemiBold SemiCondensed"), QStringLiteral("Bahnschrift"),
                                         QStringLiteral("Segoe UI")}, 14, QFont::DemiBold);
            const QFont lblF = makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 12);
            const QFontMetrics nfm(numF), lfm(lblF);
            const int h = 28;
            int x = r.right();
            for (int i = int(parts.size()) - 1; i >= 0; --i)
            {
                QString num, lbl;
                bool numFirst = false;
                splitNumber(humanize(parts[i]), num, lbl, numFirst);
                const int nw = num.isEmpty() ? 0 : nfm.horizontalAdvance(num);
                const int lw = lfm.horizontalAdvance(lbl);
                const int w = 14 + nw + (nw ? 6 : 0) + lw + 14;
                if (x - w < r.left())
                    break;
                const QRectF chip(x - w, r.center().y() - h / 2.0, w, h);
                p.setPen(QPen(white(0.10), 1));
                p.setBrush(white(0.06));
                p.drawRoundedRect(chip.adjusted(0.5, 0.5, -0.5, -0.5), h / 2.0, h / 2.0);
                int tx = int(chip.x()) + 14;
                auto drawNum = [&] {
                    if (!nw)
                        return;
                    p.setFont(numF);
                    p.setPen(QPen(QBrush(accGrad(chip)), 1));
                    p.drawText(QRect(tx, int(chip.y()), nw + 2, h), Qt::AlignLeft | Qt::AlignVCenter, num);
                    tx += nw + 6;
                };
                auto drawLbl = [&] {
                    p.setFont(lblF);
                    p.setPen(text(0.62));
                    p.drawText(QRect(tx, int(chip.y()), lw + 2, h), Qt::AlignLeft | Qt::AlignVCenter, lbl);
                    tx += lw + 6;
                };
                if (numFirst)
                {
                    drawNum();
                    drawLbl();
                }
                else
                {
                    drawLbl();
                    drawNum();
                }
                x -= w + 8;
            }
        }

        int statusBarHeight() const override { return 34; }

        // Плеер-капсула: круглая обложка, кнопка «играть» — градиент акцента.
        MediaStyle mediaStyle(const QRect& r) const override
        {
            MediaStyle st = Skin::mediaStyle(r);
            st.radius = r.height() / 2.0;
            st.artRadius = (r.height() - 8) / 2.0;
            st.bg = white(0.07);
            st.border = white(0.12);
            st.hoverBg = white(0.10);
            // Градиент относительно самой фигуры — одинаково для компактного плеера и панели.
            QLinearGradient g(0, 0, 1, 1);
            g.setCoordinateMode(QGradient::ObjectBoundingMode);
            g.setColorAt(0, Theme::Accent.color());
            g.setColorAt(1, acc2());
            st.playBg = QBrush(g);
            st.progress = QBrush(g);
            st.playGlyph = QColor(255, 255, 255);
            st.glyph = text(0.85);
            st.title = makeFont({QStringLiteral("Bahnschrift SemiBold SemiCondensed"), QStringLiteral("Bahnschrift"),
                                 QStringLiteral("Segoe UI")}, 14, QFont::DemiBold);
            st.sub = makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 11);
            st.titleColor = text(0.95);
            st.subColor = text(0.55);
            return st;
        }

        void paintStatusBar(QPainter& p, const QRect& r, const QString& name, const QString& path,
                            const QString& location, const QStringList& hints) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            const QFont f = makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 12);
            const QFont keyF = makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 11, QFont::DemiBold);
            const int left = paintHints(p, r.adjusted(0, -2, -Theme::ContentMargin - 2, -2), hints, f, text(0.55), true, 22,
                                        [&](QPainter& pp, const QRectF& kr, const QString& key) {
                                            pp.setPen(Qt::NoPen);
                                            pp.setBrush(white(0.10));
                                            pp.drawRoundedRect(kr, kr.height() / 2, kr.height() / 2);
                                            pp.setFont(keyF);
                                            pp.setPen(text(0.9));
                                            pp.drawText(kr, Qt::AlignCenter, key);
                                        });
            if (name.isEmpty())
                return;
            // Точка-градиент перед названием — как акцент на боковой панели.
            const int x = r.left() + 10 + Theme::ContentMargin;
            const QRectF dot(x, r.center().y() - 2 - 4, 8, 8);
            p.setPen(Qt::NoPen);
            p.setBrush(accGrad(dot));
            p.drawEllipse(dot);
            paintHoverName(p, r.adjusted(0, -2, 0, -2), x + 16, left - 16, name, path, location,
                           makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 12, QFont::DemiBold),
                           text(0.9), f, text(0.45));
        }

        QColor acc2() const { return Skin::mix(Theme::Accent.color(), QColor(0xB2, 0x6B, 0xFF), 0.85); }
        QLinearGradient accGrad(const QRectF& r) const
        {
            QLinearGradient g(r.topLeft(), r.bottomRight());
            g.setColorAt(0, Theme::Accent.color());
            g.setColorAt(1, acc2());
            return g;
        }

        void paintPanel(QPainter& p, const QRect& rect, const QImage& backdrop, int effect) const override
        {
            Skin::paintPanel(p, rect, backdrop, effect);
            // Внутренний блик по верхней кромке
            p.fillRect(QRect(rect.x() + 18, rect.y() + 1, rect.width() - 36, 1), white(0.10));
        }

        void paintSidebarButton(QPainter& p, const SidebarButtonPaint& b) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            const QRectF r = QRectF(b.r).adjusted(3, 3, -3, -3);
            if (b.active)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(accGrad(r));
                p.drawRoundedRect(r, 12, 12);
            }
            else if (b.hover)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(white(b.pressed ? 0.14 : 0.08));
                p.drawRoundedRect(r, 12, 12);
            }
        }

        void paintSearchField(QPainter& p, const QRect& r, const QString& txt, bool caret, bool focused) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            const bool active = !txt.isEmpty() || focused;
            const bool hasText = !txt.isEmpty();
            const QRectF rf = QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);
            p.setBrush(white(active ? 0.10 : 0.07));
            if (active)
                p.setPen(QPen(QBrush(accGrad(rf)), 1.5));
            else
                p.setPen(QPen(white(0.12), 1));
            p.drawRoundedRect(rf, 14, 14);
            drawSearchGlyph(p, QRectF(r.x() + 14, r.center().y() - 8, 16, 16), text(active ? 0.9 : 0.5));
            const QFont f = font(Font::Search);
            const QFontMetrics fm(f);
            const QRect tr = r.adjusted(40, 0, -16, 0);
            p.setFont(f);
            p.setPen(hasText ? text() : text(0.45));
            if (!(focused && !hasText))
                p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter,
                           fm.elidedText(hasText ? txt : TXT("Поиск приложений, параметров и файлов"), Qt::ElideRight, tr.width()));
            if (active && caret)
                p.fillRect(QRectF(tr.x() + qMin(fm.horizontalAdvance(txt), tr.width()) + 1, r.center().y() - 9, 1.5, 18),
                           Theme::Accent.color());
        }

        void paintGroup(QPainter& p, const GroupPaint& g) const override
        {
            const Group& gr = *g.group;
            p.setRenderHint(QPainter::Antialiasing);
            if (!gr.clean)
            {
                // Остров: полупрозрачная карточка + два мягких свечения (цвета группы) в противоположных углах.
                QColor c1 = gr.color.isValid() ? QColor(gr.color.red(), gr.color.green(), gr.color.blue())
                                               : (g.auto1.isValid() ? g.auto1 : Theme::Accent.color());
                QColor c2 = g.auto2.isValid() ? g.auto2 : c1.toHsv();
                if (!g.auto2.isValid())
                    c2.setHsv((c1.hsvHue() + 30) % 360, c1.hsvSaturation(), c1.value());
                const QRectF fr = QRectF(g.frame).adjusted(0.5, 0.5, -0.5, -0.5);
                QPainterPath path;
                path.addRoundedRect(fr, Theme::GroupRadius, Theme::GroupRadius);
                p.fillPath(path, groupBg());
                const qreal rad = qMax(fr.width(), fr.height()) * 0.95;
                QRadialGradient a(fr.topLeft(), rad);
                a.setColorAt(0, withAlpha(c1, 0.34));
                a.setColorAt(1, withAlpha(c1, 0.0));
                p.fillPath(path, a);
                QRadialGradient b(fr.bottomRight(), rad * 0.9);
                b.setColorAt(0, withAlpha(c2, 0.26));
                b.setColorAt(1, withAlpha(c2, 0.0));
                p.fillPath(path, b);
                p.setPen(QPen(white(0.10 + (g.hover ? 0.06 : 0.0)), 1));
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
            }
            const QFont f = font(Font::GroupTitle);
            const QFontMetrics fm(f);
            p.setFont(f);
            const QRect tr(g.header.x() + Theme::GroupPad + 2, g.header.y() + 4, g.header.width() - 2 * Theme::GroupPad, g.header.height() - 4);
            auto label = [&](int i) { return gr.tabs[i].title.isEmpty() ? TXT("Без названия") : gr.tabs[i].title; };
            if (gr.tabs.size() == 1)
            {
                p.setPen(gr.title().isEmpty() ? text(0.45) : text());
                p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(label(0), Qt::ElideRight, tr.width()));
                return;
            }
            // Вкладки — сегментированная «пилюля». Подложка подбирается под яркость текста:
            // при тёмном тексте — светлая, иначе неактивная вкладка сливается с фоном.
            const bool darkText = Theme::TextColor.color().lightnessF() < 0.5;
            const QRect first = g.tabRect(0), last = g.tabRect(int(gr.tabs.size()) - 1);
            const QRectF pill = QRectF(first.left() - 3, first.top() - 3, last.right() - first.left() + 7, first.height() + 6);
            p.setPen(Qt::NoPen);
            p.setBrush(darkText ? white(0.55) : white(0.10));
            p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
            for (int i = 0; i < gr.tabs.size(); ++i)
            {
                const QRectF r = QRectF(g.tabRect(i)).adjusted(0, -1, 0, 1);
                if (i == gr.activeTab)
                {
                    p.setBrush(darkText ? white(0.92) : white(0.22));
                    p.drawRoundedRect(r, r.height() / 2.0, r.height() / 2.0);
                }
                // Неактивная — не тусклее 75 %: должна читаться, а не угадываться
                p.setPen(i == gr.activeTab ? text() : text(g.hoverTab == i ? 0.92 : 0.75));
                // Ширина вкладки считается шрифтом рабочей области; подпись Bento чуть шире — центрируем без обрезки
                const int tw = fm.horizontalAdvance(label(i));
                QRectF lr = r;
                if (tw > r.width() - 8)
                    lr.adjust(-(tw - r.width() + 8) / 2.0, 0, (tw - r.width() + 8) / 2.0, 0);
                p.drawText(lr, Qt::AlignCenter, label(i));
                p.setPen(Qt::NoPen);
            }
        }

        void paintTile(QPainter& p, const TilePaint& t) const override
        {
            const QRect& r = t.r;
            const Tile& tile = *t.tile;
            p.save();
            p.setRenderHint(QPainter::Antialiasing);
            pressTransform(p, r, t.pressed);
            const QRectF rf = QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);
            const QColor tint = t.tint.isValid() ? t.tint : Theme::Accent.color();
            if (t.clean)
            {
                // Плитка в «чистой» группе окрашена цветом своей иконки.
                QLinearGradient g(rf.topLeft(), rf.bottomRight());
                // Насыщенность окраски — от «Фона плиток» (8 % → как задумано в стиле).
                const qreal k = tileAlpha() / 0.08;
                g.setColorAt(0, withAlpha(tint, qMin(1.0, 0.30 * k + 0.10 * t.hover)));
                g.setColorAt(1, withAlpha(tint, qMin(1.0, 0.10 * k + 0.06 * t.hover)));
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 14));
                p.drawRoundedRect(rf, Theme::TileRadius, Theme::TileRadius);
                p.setBrush(g);
            }
            else
                p.setBrush(Skin::mix(tileBg(), tileHot(0.07), t.hover));
            p.setPen(QPen(white(0.10 + 0.14 * t.hover), 1));
            p.drawRoundedRect(rf, Theme::TileRadius, Theme::TileRadius);

            const qreal s = k();
            const Size sz = sizeOf(r);
            const bool labels = t.showLabel && Theme::Cell >= Theme::LabelMinCell;
            auto glowIcon = [&](const QRect& ir) {
                // Мягкий цветной блик за иконкой
                QRadialGradient gl(QRectF(ir).center(), ir.width() * 0.85);
                gl.setColorAt(0, withAlpha(tint, 0.30));
                gl.setColorAt(1, withAlpha(tint, 0.0));
                p.setPen(Qt::NoPen);
                p.setBrush(gl);
                p.drawEllipse(QRectF(ir).adjusted(-ir.width() * 0.6, -ir.height() * 0.6, ir.width() * 0.6, ir.height() * 0.6));
                tile.isFolder ? paintFolderIcons(p, tile, ir, ir.width(), t.icon) : t.icon(p, tile.item, ir);
            };
            if (!labels && sz != Size::S)
            {
                // Подписи выключены — иконка ровно по центру плитки.
                const int ip = qRound(Theme::Cell * (sz == Size::L ? 1.56 : sz == Size::W ? 1.04 : 0.86));
                QRect ir(0, 0, ip, ip);
                ir.moveCenter(r.center());
                glowIcon(ir);
            }
            else if (sz == Size::S)
            {
                QRect ir(0, 0, qRound(Theme::Cell * 0.62), qRound(Theme::Cell * 0.62));
                ir.moveCenter(r.center());
                glowIcon(ir);
            }
            else if (sz == Size::W)
            {
                const int ip = qRound(Theme::Cell * 1.0);
                QRect ir(0, 0, ip, ip);
                ir.moveCenter(QPoint(r.x() + qRound(18 * s) + ip / 2, r.center().y()));
                glowIcon(ir);
                if (labels)
                    drawLabel(p, QRect(ir.right() + qRound(14 * s), r.y() + 6, r.right() - ir.right() - qRound(26 * s), r.height() - 12),
                              tile.item.name, font(Font::TileBig), text(), 2, Qt::AlignLeft | Qt::AlignVCenter);
            }
            else
            {
                const bool large = sz == Size::L;
                const int pad = qRound((large ? 18 : 12) * s);
                const int ip = qRound(Theme::Cell * (large ? 1.5 : 0.80));
                glowIcon(QRect(r.x() + pad, r.y() + pad, ip, ip));
                if (labels)
                {
                    const QFont f = font(large ? Font::TileBig : Font::Tile);
                    const int lh = QFontMetrics(f).height();
                    const QRect lr(r.x() + pad, r.y() + pad + ip + 2, r.width() - 2 * pad, r.bottom() - qRound(10 * s) - (r.y() + pad + ip + 2));
                    drawLabel(p, lr, tile.item.name, f, text(), qMax(1, qMin(2, lr.height() / lh)), Qt::AlignLeft | Qt::AlignBottom);
                }
            }
            p.restore();
        }

        void paintFocus(QPainter& p, const QRect& r) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(0xA9, 0xB4, 0xFF), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(r).adjusted(-2, -2, 2, 2), Theme::TileRadius + 3, Theme::TileRadius + 3);
        }

        void paintInsert(QPainter& p, const QRect& r) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(accGrad(r));
            p.drawRoundedRect(QRectF(r).adjusted(0, -2, 0, 2), 2, 2);
        }

        void paintFolderTarget(QPainter& p, const QRect& r) const override { paintFocus(p, r); }

        void paintNewGroup(QPainter& p, const QRect& r) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            QPen pen(QBrush(accGrad(r)), 1.5, Qt::DashLine);
            p.setPen(pen);
            p.setBrush(white(0.03));
            p.drawRoundedRect(QRectF(r).adjusted(1, 1, -1, -1), Theme::GroupRadius, Theme::GroupRadius);
            p.setFont(font(Font::GroupTitle));
            p.setPen(text(0.7));
            p.drawText(r, Qt::AlignCenter, TXT("Новый остров"));
        }

        void paintListTitle(QPainter& p, const QRect& r, const QString& title) const override
        {
            p.setFont(font(Font::ListTitle));
            p.setPen(text());
            p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, title);
        }

        void paintListSection(QPainter& p, const QRect& r, const QString& title, bool letter) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            if (letter)
            {
                // Буква — в цветной «пилюле»
                const QRectF pill(r.x() + 4, r.bottom() - 26, 24, 22);
                p.setPen(Qt::NoPen);
                p.setBrush(accGrad(pill));
                p.drawRoundedRect(pill, 8, 8);
                p.setFont(font(Font::ListSection));
                p.setPen(Qt::white);
                p.drawText(pill, Qt::AlignCenter, title);
                return;
            }
            p.setFont(font(Font::ListSection));
            p.setPen(text(0.5));
            p.drawText(r.adjusted(6, 0, 0, -6), Qt::AlignLeft | Qt::AlignBottom, title.toUpper());
        }

        void paintListRow(QPainter& p, const ListRowPaint& row) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            if (row.hover || row.selected)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(white(row.pressed ? 0.14 : row.selected ? 0.11 : 0.08));
                p.drawRoundedRect(QRectF(row.r), 10, 10);
                if (row.selected)
                {
                    const QRectF bar(row.r.x() + 3, row.r.center().y() - 8, 3, 16);
                    p.setBrush(accGrad(bar));
                    p.drawRoundedRect(bar, 1.5, 1.5);
                }
            }
            const QRect ir(row.r.x() + 12, row.r.center().y() - Theme::ListIcon / 2, Theme::ListIcon, Theme::ListIcon);
            row.icon(p, *row.item, ir);
            const QRect tr(ir.right() + 10, row.r.y(), row.r.right() - ir.right() - 14, row.r.height());
            drawHighlighted(p, tr, row.item->name, row.highlight, font(Font::ListRow), text(), QColor(0xA9, 0xB4, 0xFF));
        }
    };

    // =====================================================================================
    // Fluent Glass — утончённый Windows 11: живое стекло, слоистые поверхности, мягкая подсветка, один сдержанный акцент.
    // =====================================================================================
    class FluentSkin : public Skin
    {
    public:
        int kind() const override { return Fluent; }

        void applyMetrics() const override
        {
            Theme::ContentMargin = 18;
            Theme::CellGap = 4;
            Theme::TileRadius = 8;
            Theme::GroupPad = 12;
            Theme::GroupHeader = 36;
            Theme::GroupRadius = 12;
            Theme::GroupGapX = 20;
            Theme::GroupGapY = 14;
            Theme::TitleFontPx = 14;
            Theme::ListRow = 36;
            Theme::ListHeader = 34;
            Theme::ListIcon = 24;
            Theme::ListColumnWidth = 270;
        }

        void preset(Settings& s) const override
        {
            Skin::preset(s);
            s.background = QColor(0x1A, 0x1C, 0x24);
            s.opacity = 70;
            s.effect = Settings::Effect::Acrylic;
            s.radius = 12;
            s.accent = QColor(0x8C, 0xB8, 0xFF);
            s.textColor = QColor(0xF5, 0xF6, 0xFA);
            s.groupOpacity = 6;
            s.tileOpacity = 6;
        }

        QFont font(Font role) const override
        {
            const QStringList textF{QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")};
            const QStringList disp{QStringLiteral("Segoe UI Variable Display"), QStringLiteral("Segoe UI")};
            switch (role)
            {
            case Font::Tile:        return makeFont(textF, Theme::TileFontScaled);
            case Font::TileBig:     return makeFont(disp, Theme::TileFontScaled + 2, QFont::DemiBold);
            case Font::GroupTitle:  return makeFont(disp, 14, QFont::DemiBold);
            case Font::ListRow:     return makeFont(textF, 13);
            case Font::ListSection: return makeFont(disp, 13, QFont::DemiBold);
            case Font::ListTitle:   return makeFont(disp, 16, QFont::DemiBold);
            case Font::Search:      return makeFont(textF, 14);
            case Font::Meta:        return makeFont(textF, 11);
            }
            return Skin::font(role);
        }

        int searchFieldHeight() const override { return 36; }

        // Сведения — обычным регистром, разделители-точки как в Windows 11.
        void paintToolbarInfo(QPainter& p, const QRect& r, const QStringList& parts) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            const QFont f = makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 12);
            const QFontMetrics fm(f);
            p.setFont(f);
            int x = r.right();
            for (int i = int(parts.size()) - 1; i >= 0; --i)
            {
                const QString s = humanize(parts[i]);
                const int w = fm.horizontalAdvance(s);
                if (x - w < r.left())
                    break;
                p.setPen(text(0.66));
                p.drawText(QRect(x - w, r.y(), w + 1, r.height()), Qt::AlignLeft | Qt::AlignVCenter, s);
                x -= w + 20;
                if (i > 0)
                {
                    p.setPen(Qt::NoPen);
                    p.setBrush(text(0.32));
                    p.drawEllipse(QPointF(x + 10, r.center().y() + 0.5), 1.6, 1.6);
                }
            }
        }

        int statusBarHeight() const override { return 32; }

        // Плеер как карточка Windows 11: мягкая подложка, круглая акцентная кнопка с тёмным значком.
        MediaStyle mediaStyle(const QRect& r) const override
        {
            MediaStyle st = Skin::mediaStyle(r);
            st.radius = 8;
            st.artRadius = 5;
            st.bg = white(0.06);
            st.border = white(0.08);
            st.hoverBg = white(0.08);
            st.playBg = Theme::Accent.color();
            st.playGlyph = QColor(0x10, 0x12, 0x18);
            st.glyph = text(0.85);
            st.title = makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 12, QFont::DemiBold);
            st.sub = makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 11);
            st.titleColor = text(0.95);
            st.subColor = text(0.6);
            return st;
        }

        void paintStatusBar(QPainter& p, const QRect& r, const QString& name, const QString& path,
                            const QString& location, const QStringList& hints) const override
        {
            // Нижняя полоса чуть светлее панели, с тонкой линией сверху — как подвал «Пуска» Windows 11.
            p.fillRect(r, white(0.025));
            p.fillRect(QRect(r.left(), r.top(), r.width(), 1), white(0.07));
            p.setRenderHint(QPainter::Antialiasing);
            const QFont f = makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 12);
            const QFont keyF = makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 11);
            const int left = paintHints(p, r.adjusted(0, 1, -Theme::ContentMargin - 2, 0), hints, f, text(0.6), true, 20,
                                        [&](QPainter& pp, const QRectF& kr, const QString& key) {
                                            // Клавиша Windows 11: заливка, рамка и чуть более тёмная нижняя кромка.
                                            const QRectF k = kr.adjusted(0.5, 0.5, -0.5, -0.5);
                                            pp.setPen(QPen(white(0.10), 1));
                                            pp.setBrush(white(0.06));
                                            pp.drawRoundedRect(k, 4, 4);
                                            pp.setPen(QPen(QColor(0, 0, 0, 70), 1));
                                            pp.drawLine(QPointF(k.left() + 3, k.bottom()), QPointF(k.right() - 3, k.bottom()));
                                            pp.setFont(keyF);
                                            pp.setPen(text(0.85));
                                            pp.drawText(kr.adjusted(0, -1, 0, -1), Qt::AlignCenter, key);
                                        });
            if (name.isEmpty())
                return;
            // Акцентная «пилюля» перед названием — как индикатор выбранного пункта.
            const int x = r.left() + 10 + Theme::ContentMargin;
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::Accent.color());
            p.drawRoundedRect(QRectF(x, r.center().y() - 7, 3, 14), 1.5, 1.5);
            paintHoverName(p, r.adjusted(0, 1, 0, 0), x + 12, left - 16, name, path, location, f, text(0.92), keyF, text(0.5));
        }

        void paintPanel(QPainter& p, const QRect& rect, const QImage& backdrop, int effect) const override
        {
            Skin::paintPanel(p, rect, backdrop, effect);
            // Верхний «блик» стекла: белый 4.5 % → 0 на 160 px.
            QPainterPath path;
            const qreal rad = effect == 1 ? Skin::glassRadius() : Theme::Radius;
            path.addRoundedRect(QRectF(rect).adjusted(1, 1, -1, -1), rad, rad);
            QLinearGradient g(0, rect.top(), 0, rect.top() + 160);
            g.setColorAt(0, white(0.045));
            g.setColorAt(1, white(0.0));
            p.fillPath(path, g);
        }

        void paintSidebarButton(QPainter& p, const SidebarButtonPaint& b) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            const QRectF r = QRectF(b.r).adjusted(3, 3, -3, -3);
            if (b.hover || b.active)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(white(b.pressed ? 0.05 : b.active ? 0.08 : 0.06));
                p.drawRoundedRect(r, 6, 6);
            }
            if (b.active)
            {
                p.setBrush(Theme::Accent.color());
                p.drawRoundedRect(QRectF(r.left() + 1, r.center().y() - 8, 3, 16), 1.5, 1.5);
            }
        }

        void paintSearchField(QPainter& p, const QRect& r, const QString& txt, bool caret, bool focused) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            const bool active = !txt.isEmpty() || focused;
            const bool hasText = !txt.isEmpty();
            const QRectF rf = QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);
            p.setPen(QPen(white(0.08), 1));
            p.setBrush(white(active ? 0.04 : 0.06));
            p.drawRoundedRect(rf, 8, 8);
            if (active)
            {
                // Фокус как у полей Windows 11: акцентная линия по нижней кромке.
                QPainterPath clip;
                clip.addRoundedRect(rf, 8, 8);
                p.save();
                p.setClipPath(clip);
                p.fillRect(QRectF(rf.left(), rf.bottom() - 2, rf.width(), 2), Theme::Accent.color());
                p.restore();
            }
            drawSearchGlyph(p, QRectF(r.x() + 12, r.center().y() - 8, 16, 16), text(active ? 0.85 : 0.55));
            const QFont f = font(Font::Search);
            const QFontMetrics fm(f);
            const QRect tr = r.adjusted(38, 0, -14, 0);
            p.setFont(f);
            p.setPen(hasText ? text() : text(0.55));
            if (!(focused && !hasText))
                p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter,
                           fm.elidedText(hasText ? txt : TXT("Поиск приложений, параметров и файлов"), Qt::ElideRight, tr.width()));
            if (active && caret)
                p.fillRect(QRectF(tr.x() + qMin(fm.horizontalAdvance(txt), tr.width()) + 1, r.center().y() - 8, 1, 16), text());
        }

        void paintGroup(QPainter& p, const GroupPaint& g) const override
        {
            const Group& gr = *g.group;
            p.setRenderHint(QPainter::Antialiasing);
            if (!gr.clean)
            {
                const QRectF fr = QRectF(g.frame).adjusted(0.5, 0.5, -0.5, -0.5);
                p.setPen(QPen(white(0.08), 1));
                p.setBrush(gr.color.isValid() ? withAlpha(gr.color, qMax(0.10, gr.color.alphaF() * 1.4)) : groupBg());
                p.drawRoundedRect(fr, Theme::GroupRadius, Theme::GroupRadius);
                // Светлая кромка сверху — поверхность «приподнята»
                QLinearGradient edge(0, fr.top(), 0, fr.top() + 24);
                edge.setColorAt(0, white(0.05));
                edge.setColorAt(1, white(0.0));
                QPainterPath path;
                path.addRoundedRect(fr, Theme::GroupRadius, Theme::GroupRadius);
                p.fillPath(path, edge);
            }
            const QFont f = font(Font::GroupTitle);
            const QFontMetrics fm(f);
            p.setFont(f);
            const QRect tr(g.header.x() + Theme::GroupPad, g.header.y() + 2, g.header.width() - 2 * Theme::GroupPad, g.header.height() - 2);
            auto label = [&](int i) { return gr.tabs[i].title.isEmpty() ? TXT("Без названия") : gr.tabs[i].title; };
            if (gr.tabs.size() == 1)
            {
                p.setPen(gr.title().isEmpty() ? text(0.45) : text(0.92));
                p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(label(0), Qt::ElideRight, tr.width()));
                return;
            }
            for (int i = 0; i < gr.tabs.size(); ++i)
            {
                const QRect r = g.tabRect(i);
                const bool act = i == gr.activeTab;
                if (g.hoverTab == i && !act)
                {
                    p.setPen(Qt::NoPen);
                    p.setBrush(white(0.06));
                    p.drawRoundedRect(QRectF(r), 6, 6);
                }
                p.setPen(act ? text() : text(g.hoverTab == i ? 0.78 : 0.55));
                p.drawText(r, Qt::AlignCenter, label(i));
                if (act)
                {
                    const int w = qMin(16, fm.horizontalAdvance(label(i)));
                    p.setPen(Qt::NoPen);
                    p.setBrush(Theme::Accent.color());
                    p.drawRoundedRect(QRectF(r.center().x() - w / 2.0, r.bottom() - 2, w, 3), 1.5, 1.5);
                }
            }
        }

        void paintTile(QPainter& p, const TilePaint& t) const override
        {
            const QRect& r = t.r;
            const Tile& tile = *t.tile;
            p.save();
            p.setRenderHint(QPainter::Antialiasing);
            pressTransform(p, r, t.pressed, 0.97);
            const QRectF rf = QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);
            if (t.hover > 0.01)
            {
                // Мягкая тень при наведении — несколько полупрозрачных слоёв вместо размытия.
                p.setPen(Qt::NoPen);
                for (int i = 3; i >= 1; --i)
                {
                    p.setBrush(QColor(0, 0, 0, int(18 * t.hover)));
                    p.drawRoundedRect(rf.adjusted(-i, -i + 2, i, i + 2), Theme::TileRadius + i, Theme::TileRadius + i);
                }
            }
            const qreal base = t.clean ? tileAlpha() : 0.0;
            if (base > 0 || t.hover > 0.01)
            {
                p.setPen(QPen(white(0.04 + 0.08 * t.hover), 1));
                p.setBrush(withAlpha(Theme::TileBg.color(), t.pressed ? 0.04 : base + 0.05 * t.hover + (t.clean ? 0 : 0.03 * t.hover)));
                p.drawRoundedRect(rf, Theme::TileRadius, Theme::TileRadius);
            }

            const qreal s = k();
            const Size sz = sizeOf(r);
            const bool labels = t.showLabel && Theme::Cell >= Theme::LabelMinCell;
            const QFont f = font(sz == Size::L ? Font::TileBig : Font::Tile);
            const QFontMetrics fm(f);
            if (!labels && sz != Size::S)
            {
                // Подписи выключены — иконка ровно по центру плитки.
                const int ip = qRound(Theme::Cell * (sz == Size::L ? 1.56 : sz == Size::W ? 1.04 : 0.86));
                QRect ir(0, 0, ip, ip);
                ir.moveCenter(r.center());
                tile.isFolder ? paintFolderIcons(p, tile, ir, ip, t.icon) : t.icon(p, tile.item, ir);
            }
            else if (sz == Size::S)
            {
                QRect ir(0, 0, qRound(Theme::Cell * 0.62), qRound(Theme::Cell * 0.62));
                ir.moveCenter(r.center());
                tile.isFolder ? paintFolderIcons(p, tile, ir, ir.width(), t.icon) : t.icon(p, tile.item, ir);
            }
            else if (sz == Size::W)
            {
                const int ip = qRound(Theme::Cell * 0.95);
                QRect ir(0, 0, ip, ip);
                ir.moveCenter(QPoint(r.x() + qRound(20 * s) + ip / 2, r.center().y()));
                tile.isFolder ? paintFolderIcons(p, tile, ir, ip, t.icon) : t.icon(p, tile.item, ir);
                if (labels)
                    drawLabel(p, QRect(ir.right() + qRound(14 * s), r.y() + 6, r.right() - ir.right() - qRound(24 * s), r.height() - 12),
                              tile.item.name, font(Font::TileBig), text(0.95), 2, Qt::AlignLeft | Qt::AlignVCenter);
            }
            else
            {
                // M и L: иконка по центру сверху, подпись по центру снизу (до двух строк).
                const bool large = sz == Size::L;
                const int lines = labels ? 2 : 0;
                const QRect lr(r.x() + 6, r.bottom() - qRound(8 * s) - lines * fm.height(), r.width() - 12, lines * fm.height());
                const int ip = qRound(Theme::Cell * (large ? 1.5 : 0.86));
                QRect ir(0, 0, ip, ip);
                ir.moveCenter(QPoint(r.center().x(), labels ? (r.y() + lr.y()) / 2 + 2 : r.center().y()));
                tile.isFolder ? paintFolderIcons(p, tile, ir, ip, t.icon) : t.icon(p, tile.item, ir);
                if (labels)
                    drawLabel(p, lr, tile.item.name, f, text(0.86), 2, Qt::AlignHCenter | Qt::AlignTop);
            }
            p.restore();
        }

        void paintFocus(QPainter& p, const QRect& r) const override
        {
            // Фокус как в Windows 11: внешнее белое кольцо 2 px и тонкое тёмное внутри.
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(white(0.95), 2));
            p.drawRoundedRect(QRectF(r).adjusted(-2, -2, 2, 2), Theme::TileRadius + 3, Theme::TileRadius + 3);
            p.setPen(QPen(QColor(0, 0, 0, 160), 1));
            p.drawRoundedRect(QRectF(r).adjusted(-0.5, -0.5, 0.5, 0.5), Theme::TileRadius + 1, Theme::TileRadius + 1);
        }

        void paintInsert(QPainter& p, const QRect& r) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::Accent.color());
            p.drawRoundedRect(QRectF(r).adjusted(0, 2, 0, -2), 1.5, 1.5);
        }

        void paintListSection(QPainter& p, const QRect& r, const QString& title, bool letter) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            if (letter)
            {
                const QRectF plate(r.x() + 4, r.bottom() - 28, 26, 24);
                p.setPen(QPen(white(0.06), 1));
                p.setBrush(white(0.06));
                p.drawRoundedRect(plate, 6, 6);
                p.setFont(font(Font::ListSection));
                p.setPen(text(0.9));
                p.drawText(plate, Qt::AlignCenter, title);
                return;
            }
            p.setFont(font(Font::ListSection));
            p.setPen(text(0.6));
            p.drawText(r.adjusted(8, 0, 0, -6), Qt::AlignLeft | Qt::AlignBottom, title);
        }

        void paintListRow(QPainter& p, const ListRowPaint& row) const override
        {
            p.setRenderHint(QPainter::Antialiasing);
            if (row.hover || row.selected)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(white(row.pressed ? 0.04 : row.selected ? 0.08 : 0.06));
                p.drawRoundedRect(QRectF(row.r), 6, 6);
                if (row.selected)
                {
                    p.setBrush(Theme::Accent.color());
                    p.drawRoundedRect(QRectF(row.r.x() + 2, row.r.center().y() - 8, 3, 16), 1.5, 1.5);
                }
            }
            const QRect ir(row.r.x() + 12, row.r.center().y() - Theme::ListIcon / 2, Theme::ListIcon, Theme::ListIcon);
            row.icon(p, *row.item, ir);
            const QRect tr(ir.right() + 12, row.r.y(), row.r.right() - ir.right() - 16, row.r.height());
            drawHighlighted(p, tr, row.item->name, row.highlight, font(Font::ListRow), text(0.92), Theme::Accent.color());
        }
    };
}

Skin* makeStudioSkin() { return new StudioSkin; }
Skin* makeBentoSkin() { return new BentoSkin; }
Skin* makeFluentSkin() { return new FluentSkin; }
