#include "Skin.h"
#include "Lang.h"
#include "Settings.h"
#include "Theme.h"

#include <QFontMetrics>
#include <cmath>
#include <functional>
#include <algorithm>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <memory>

// Стили из SkinStyles.cpp
Skin* makeStudioSkin();
Skin* makeBentoSkin();
Skin* makeFluentSkin();

namespace
{
    std::unique_ptr<Skin>& holder()
    {
        static std::unique_ptr<Skin> s(new Skin);
        return s;
    }
}

Skin& Skin::current()
{
    return *holder();
}

void Skin::select(int kind)
{
    if (holder()->kind() == kind)
        return;
    switch (kind)
    {
    case Studio: holder().reset(makeStudioSkin()); break;
    case Bento:  holder().reset(makeBentoSkin()); break;
    case Fluent: holder().reset(makeFluentSkin()); break;
    default:     holder().reset(new Skin); break;
    }
}

QStringList Skin::names()
{
    return {TXT("Классический"), QStringLiteral("Studio"), QStringLiteral("Bento"), QStringLiteral("Fluent")};
}

// ---------- помощники ----------

QFont Skin::makeFont(const QStringList& families, int px, int weight)
{
    QFont f;
    f.setFamilies(families);
    f.setPixelSize(qMax(6, px));
    f.setWeight(QFont::Weight(weight));
    f.setHintingPreference(QFont::PreferNoHinting); // ровнее на мелких размерах
    return f;
}

QColor Skin::mix(const QColor& a, const QColor& b, qreal k)
{
    k = qBound(0.0, k, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * k, a.greenF() + (b.greenF() - a.greenF()) * k,
                            a.blueF() + (b.blueF() - a.blueF()) * k, a.alphaF() + (b.alphaF() - a.alphaF()) * k);
}

// Подпись до maxLines строк с переносом по словам; не влезло — последняя строка с «…».
void Skin::drawLabel(QPainter& p, const QRect& r, const QString& text, const QFont& f, const QColor& c, int maxLines,
                     Qt::Alignment align)
{
    const QFontMetrics fm(f);
    QStringList lines;
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString line;
    int i = 0;
    for (; i < words.size(); ++i)
    {
        const QString candidate = line.isEmpty() ? words[i] : line + QLatin1Char(' ') + words[i];
        if (fm.horizontalAdvance(candidate) <= r.width() || line.isEmpty())
        {
            line = candidate;
            continue;
        }
        lines.append(line);
        line = words[i];
        if (lines.size() == maxLines - 1)
        {
            ++i;
            break;
        }
    }
    // Хвост (всё, что не поместилось) — в последнюю строку, с обрезкой.
    for (; i < words.size(); ++i)
        line += QLatin1Char(' ') + words[i];
    if (!line.isEmpty())
        lines.append(line);
    while (lines.size() > maxLines)
        lines.removeLast();
    for (QString& l : lines)
        l = fm.elidedText(l, Qt::ElideRight, r.width());

    const int lh = fm.height();
    const int total = lh * int(lines.size());
    int y = r.y();
    if (align & Qt::AlignBottom)
        y = r.bottom() + 1 - total;
    else if (align & Qt::AlignVCenter)
        y = r.y() + (r.height() - total) / 2;
    p.setFont(f);
    p.setPen(c);
    for (const QString& l : lines)
    {
        p.drawText(QRect(r.x(), y, r.width(), lh), (align & Qt::AlignHorizontal_Mask) | Qt::AlignVCenter, l);
        y += lh;
    }
}

// Название с подсветкой совпавшей части (поиск).
void Skin::drawHighlighted(QPainter& p, const QRect& r, const QString& text, const QStringList& needles,
                           const QFont& f, const QColor& c, const QColor& hi)
{
    const QFontMetrics fm(f);
    p.setFont(f);
    int pos = -1, len = 0;
    for (const QString& n : needles)
        if (!n.isEmpty() && (pos = int(text.indexOf(n, 0, Qt::CaseInsensitive))) >= 0)
        {
            len = int(n.size());
            break;
        }
    if (pos < 0 || fm.horizontalAdvance(text) > r.width())
    {
        p.setPen(c);
        p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(text, Qt::ElideRight, r.width()));
        return;
    }
    int x = r.x();
    const QString parts[3] = {text.left(pos), text.mid(pos, len), text.mid(pos + len)};
    for (int i = 0; i < 3; ++i)
    {
        p.setPen(i == 1 ? hi : c);
        p.drawText(QRect(x, r.y(), r.right() - x, r.height()), Qt::AlignLeft | Qt::AlignVCenter, parts[i]);
        x += fm.horizontalAdvance(parts[i]);
    }
}

void Skin::paintFolderIcons(QPainter& p, const Tile& tile, const QRect& area, int iconPx, const IconPainter& icon)
{
    // Папка: до четырёх мини-иконок квадратом 2×2.
    const int gap = qMax(2, iconPx / 14);
    const int mini = (iconPx - gap) / 2;
    QRect block(0, 0, 2 * mini + gap, 2 * mini + gap);
    block.moveCenter(area.center());
    for (int i = 0; i < qMin(4, int(tile.children.size())); ++i)
        icon(p, tile.children[i], QRect(block.x() + (i % 2) * (mini + gap), block.y() + (i / 2) * (mini + gap), mini, mini));
}

// ---------- «Классический» стиль ----------

void Skin::applyMetrics() const
{
    Theme::ContentMargin = 16;
    Theme::CellGap = 4;
    Theme::TileRadius = 4;
    Theme::GroupPad = 10;
    Theme::GroupHeader = 32;
    Theme::GroupRadius = 8;
    Theme::GroupGapX = 16;
    Theme::GroupGapY = 14;
    Theme::TitleFontPx = 13;
    Theme::ListRow = 36;
    Theme::ListHeader = 30;
    Theme::ListIcon = 24;
    Theme::ListColumnWidth = 270;
}

void Skin::preset(Settings& s) const
{
    const Settings d;
    s.background = d.background;
    s.opacity = d.opacity;
    s.effect = d.effect;
    s.radius = d.radius;
    s.accent = d.accent;
    s.textColor = d.textColor;
    s.groupColor = d.groupColor;
    s.groupOpacity = d.groupOpacity;
    s.tileOpacity = d.tileOpacity;
    s.border = d.border;
}

QFont Skin::font(Font role) const
{
    const QStringList ui{QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")};
    switch (role)
    {
    case Font::Tile:        return makeFont(ui, Theme::TileFontScaled);
    case Font::TileBig:     return makeFont(ui, Theme::TileFontScaled + 1, QFont::DemiBold);
    case Font::GroupTitle:  return makeFont(ui, Theme::TitleFontPx, QFont::Bold);
    case Font::ListRow:     return makeFont(ui, Theme::ListFontPx);
    case Font::ListSection: return makeFont(ui, Theme::ListFontPx, QFont::Bold);
    case Font::ListTitle:   return makeFont(ui, Theme::ListFontPx + 1, QFont::Bold);
    case Font::Search:      return makeFont(ui, 14);
    case Font::Meta:        return makeFont({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")}, 11);
    }
    return QFont();
}

QString Skin::humanize(const QString& s)
{
    QStringList words = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString& w : words)
    {
        bool num = false;
        const int n = w.toInt(&num);
        if (num)
            w = QString::number(n); // «05» → «5»
        else
            w = w.toLower();
    }
    QString out = words.join(QLatin1Char(' '));
    if (!out.isEmpty())
        out[0] = out[0].toUpper();
    return out;
}

int Skin::paintHints(QPainter& p, const QRect& r, const QStringList& hints, const QFont& f, const QColor& label,
                     bool human, int keyH, const KeycapPainter& keycap)
{
    const QFontMetrics fm(f);
    p.setFont(f);
    const int cy = r.center().y();
    int rx = r.right();
    for (int i = int(hints.size()) - 1; i >= 0; --i)
    {
        QString key = hints[i].section(QLatin1Char('|'), 0, 0);
        QString what = hints[i].section(QLatin1Char('|'), 1);
        if (human)
        {
            what = humanize(what);
            if (key.size() > 1 && key[0].isLetter())
                key = humanize(key);
        }
        const int ww = fm.horizontalAdvance(what);
        const int kw = qMax(keyH + 2, fm.horizontalAdvance(key) + 12);
        if (rx - ww - 6 - kw < r.center().x())
            break; // не наезжаем на название слева
        p.setFont(f);
        p.setPen(label);
        p.drawText(QRect(rx - ww, r.y(), ww + 1, r.height()), Qt::AlignLeft | Qt::AlignVCenter, what);
        rx -= ww + 6;
        const QRectF kr(rx - kw, cy - keyH / 2.0, kw, keyH);
        p.save();
        keycap(p, kr, key);
        p.restore();
        rx -= kw + 16;
    }
    return rx;
}

void Skin::paintHoverName(QPainter& p, const QRect& r, int x, int right, const QString& name, const QString& path,
                          const QString& location, const QFont& nf, const QColor& nc, const QFont& pf, const QColor& pc)
{
    if (name.isEmpty() || right <= x)
        return;
    const QFontMetrics nfm(nf);
    const QString n = nfm.elidedText(name, Qt::ElideRight, right - x);
    const int w = nfm.horizontalAdvance(n);
    p.setFont(nf);
    p.setPen(nc);
    p.drawText(QRect(x, r.y(), w + 2, r.height()), Qt::AlignLeft | Qt::AlignVCenter, n);
    x += w + 10;
    const QFontMetrics pfm(pf);
    p.setFont(pf);
    if (!path.isEmpty() && right - x >= 40)
    {
        const QString s = pfm.elidedText(path, Qt::ElideRight, right - x);
        p.setPen(pc);
        p.drawText(QRect(x, r.y(), right - x, r.height()), Qt::AlignLeft | Qt::AlignVCenter, s);
        x += pfm.horizontalAdvance(s) + 14;
    }
    if (location.isEmpty() || right - x < 60)
        return;
    // Путь — тусклее и с многоточием в середине, чтобы были видны и диск, и имя файла.
    QColor lc = pc;
    lc.setAlphaF(pc.alphaF() * 0.8);
    p.setPen(lc);
    p.drawText(QRect(x, r.y(), right - x, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
               pfm.elidedText(location, Qt::ElideMiddle, right - x));
}

// Классический: спокойный текст через «·», клавиши — простые рамки.
void Skin::paintToolbarInfo(QPainter& p, const QRect& r, const QStringList& parts) const
{
    QStringList h;
    for (const QString& s : parts)
        h.append(humanize(s));
    const QFont f = makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 12);
    const QFontMetrics fm(f);
    const QString line = fm.elidedText(h.join(QStringLiteral("   ·   ")), Qt::ElideLeft, r.width());
    p.setFont(f);
    p.setPen(Theme::SubText.color());
    p.drawText(r, Qt::AlignRight | Qt::AlignVCenter, line);
}

void Skin::paintStatusBar(QPainter& p, const QRect& r, const QString& name, const QString& path,
                          const QString& location, const QStringList& hints) const
{
    const QColor txt = Theme::TextColor.color();
    auto a = [&](qreal k) { QColor c = txt; c.setAlphaF(k); return c; };
    p.fillRect(QRect(r.left() + 10, r.top(), r.width() - 10 - Theme::ContentMargin, 1), a(0.07));
    p.setRenderHint(QPainter::Antialiasing);
    const QFont f = makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 11);
    const int left = paintHints(p, r.adjusted(0, 1, -Theme::ContentMargin - 4, 0), hints, f, a(0.5), true, 17,
                                [&](QPainter& pp, const QRectF& kr, const QString& key) {
                                    pp.setPen(QPen(a(0.18), 1));
                                    pp.setBrush(Qt::NoBrush);
                                    pp.drawRoundedRect(kr.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
                                    pp.setPen(a(0.75));
                                    pp.drawText(kr, Qt::AlignCenter, key);
                                });
    paintHoverName(p, r.adjusted(0, 1, 0, 0), r.left() + 14 + Theme::ContentInsetX, left - 16, name, path, location,
                   makeFont({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 12), a(0.85), f, a(0.45));
}

void Skin::paintPanel(QPainter& p, const QRect& rect, const QImage& backdrop, int effect) const
{
    const QRectF panel = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
    if (effect == 1)
    {
        // Стекло рисует Windows. Почти невидимая заливка нужна, иначе клики проваливаются сквозь окно.
        p.fillRect(rect, QColor(0, 0, 0, 1));
        // Цвет фона с «Непрозрачностью» — поверх стекла, нашей заливкой
        QPainterPath glass;
        glass.addRoundedRect(panel, glassRadius(), glassRadius());
        p.fillPath(glass, Theme::PanelBg.color());
        if (Theme::PanelBorder.a > 0)
        {
            p.setPen(QPen(Theme::PanelBorder.color(), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(panel, glassRadius(), glassRadius());
        }
        return;
    }
    QPainterPath path;
    path.addRoundedRect(panel, Theme::Radius, Theme::Radius);
    if (effect == 2)
    {
        p.save();
        p.setClipPath(path);
        if (!backdrop.isNull())
            p.drawImage(rect, backdrop);
        p.fillRect(rect, Theme::PanelBg.color());
        p.restore();
    }
    else
        p.fillPath(path, Theme::PanelBg.color());
    p.setPen(QPen(Theme::PanelBorder.color(), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void Skin::paintSidebarBack(QPainter&, const QRect&) const {}

void Skin::paintSidebarButton(QPainter& p, const SidebarButtonPaint& b) const
{
    if (b.hover || b.active)
    {
        p.setPen(Qt::NoPen);
        p.setBrush((b.pressed ? Theme::TilePressed : b.hover ? Theme::TileHover : Theme::TileBg).color());
        p.drawRoundedRect(b.r, 6, 6);
    }
    if (b.active)
    {
        p.setBrush(Theme::TextColor.color());
        p.drawRoundedRect(QRectF(1, b.r.center().y() - 8, 3, 16), 1.5, 1.5);
    }
}

void Skin::paintSearchField(QPainter& p, const QRect& r, const QString& text, bool caret, bool focused) const
{
    const bool active = !text.isEmpty() || focused;
    p.setPen(QPen(active ? Theme::Accent.color() : Theme::PanelBorder.color(), 1));
    p.setBrush(Theme::Rgba::from(Theme::TextColor.color(), 14).color());
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    const QFont f = font(Font::Search);
    const QFontMetrics fm(f);
    p.setFont(f);
    const QRect tr = r.adjusted(14, 0, -14, 0);
    p.setPen((!text.isEmpty() ? Theme::TextColor : Theme::SubText).color());
    const QString shown = !text.isEmpty() ? text : TXT("Поиск приложений, параметров и файлов");
    if (!(focused && text.isEmpty()))
        p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(shown, Qt::ElideRight, tr.width()));
    if (active && caret)
    {
        const int x = tr.x() + qMin(fm.horizontalAdvance(text), tr.width()) + 1;
        p.fillRect(QRect(x, r.center().y() - 8, 1, 16), Theme::Accent.color());
    }
}

void Skin::paintGroup(QPainter& p, const GroupPaint& g) const
{
    const Group& gr = *g.group;
    if (!gr.clean)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(gr.color.isValid() ? gr.color : Theme::GroupBg.color());
        p.drawRoundedRect(g.frame, Theme::GroupRadius, Theme::GroupRadius);
    }
    else if (g.hover)
    {
        // У «чистой» группы при наведении — тонкая рамка, чтобы было видно её край.
        p.setPen(QPen(Theme::PanelBorder.color(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(g.frame.adjusted(0, 0, -1, -1), Theme::GroupRadius, Theme::GroupRadius);
    }

    const QFont f = font(Font::GroupTitle);
    const QFontMetrics fm(f);
    p.setFont(f);
    const QRect tr(g.header.x() + Theme::GroupPad, g.header.y(), g.header.width() - 2 * Theme::GroupPad, g.header.height());
    auto label = [&](int i) {
        return gr.tabs[i].title.isEmpty() ? TXT("Без названия") : gr.tabs[i].title;
    };
    if (gr.tabs.size() == 1)
    {
        p.setPen((gr.title().isEmpty() ? Theme::SubText : Theme::TextColor).color());
        p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(label(0), Qt::ElideRight, tr.width()));
        return;
    }
    p.save();
    p.setClipRect(g.header);
    for (int i = 0; i < gr.tabs.size(); ++i)
    {
        const QRect r = g.tabRect(i);
        const bool active = i == gr.activeTab;
        if (active || g.hoverTab == i)
        {
            p.setPen(Qt::NoPen);
            p.setBrush((active ? Theme::ChipBg : Theme::TileHover).color());
            p.drawRoundedRect(r, 5, 5);
        }
        QFont tf = f;
        tf.setBold(active);
        p.setFont(tf);
        p.setPen(active ? Theme::TextColor.color() : Theme::SubText.color());
        p.drawText(r, Qt::AlignCenter, label(i));
    }
    p.restore();
}

void Skin::paintTile(QPainter& p, const TilePaint& t) const
{
    const QRect& r = t.r;
    p.save();
    if (t.pressed)
    {
        // Нажатая плитка чуть «проседает».
        p.translate(r.center());
        p.scale(0.96, 0.96);
        p.translate(-r.center());
    }
    if (t.clean || t.hover > 0.0)
    {
        // Фон плавно перетекает из обычного в подсвеченный.
        const QColor base = t.clean ? Theme::TileBg.color() : Theme::Rgba::from(Theme::TileHover.color(), 0).color();
        const QColor top = (t.pressed ? Theme::TilePressed : Theme::TileHover).color();
        p.setPen(Qt::NoPen);
        p.setBrush(mix(base, top, t.hover));
        p.drawRoundedRect(r, Theme::TileRadius, Theme::TileRadius);
    }

    const bool small = r.height() <= Theme::Cell;
    const bool withLabel = !small && t.showLabel && Theme::Cell >= Theme::LabelMinCell;
    const QFont f = font(Font::Tile);
    const QFontMetrics fm(f);
    QRect iconArea = r;
    QRect label;
    if (withLabel)
    {
        label = QRect(r.x() + 8, r.bottom() - 7 - fm.height(), r.width() - 16, fm.height());
        iconArea = QRect(r.x(), r.y(), r.width(), label.top() - r.y());
    }
    const int iconPx = small ? Theme::IconSmall : (r.height() >= 4 * Theme::Cell ? Theme::IconLarge : Theme::IconMedium);
    if (t.tile->isFolder)
        paintFolderIcons(p, *t.tile, iconArea, small ? Theme::Cell - 10 : iconPx, t.icon);
    else
    {
        QRect ir(0, 0, iconPx, iconPx);
        ir.moveCenter(iconArea.center());
        t.icon(p, t.tile->item, ir);
    }
    if (withLabel)
        drawLabel(p, label, t.tile->item.name, f, Theme::TextColor.color(), 1, Qt::AlignLeft | Qt::AlignVCenter);
    p.restore();
}

void Skin::paintFocus(QPainter& p, const QRect& r) const
{
    p.setPen(QPen(Theme::Accent.color(), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(r).adjusted(1, 1, -1, -1), Theme::TileRadius + 1, Theme::TileRadius + 1);
}

void Skin::paintInsert(QPainter& p, const QRect& r) const
{
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::Accent.color());
    p.drawRoundedRect(r, 1.5, 1.5);
}

void Skin::paintFolderTarget(QPainter& p, const QRect& r) const
{
    p.setPen(QPen(Theme::Accent.color(), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(1, 1, -1, -1), Theme::TileRadius, Theme::TileRadius);
}

void Skin::paintNewGroup(QPainter& p, const QRect& r) const
{
    p.setPen(QPen(Theme::Accent.color(), 1.5, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, Theme::GroupRadius, Theme::GroupRadius);
    p.setFont(font(Font::Tile));
    p.drawText(r, Qt::AlignCenter, TXT("Новая группа"));
}

void Skin::paintListTitle(QPainter& p, const QRect& r, const QString& title) const
{
    p.setFont(font(Font::ListTitle));
    p.setPen(Theme::TextColor.color());
    p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, title);
}

void Skin::paintListSection(QPainter& p, const QRect& r, const QString& title, bool letter) const
{
    p.setFont(font(Font::ListSection));
    p.setPen((letter ? Theme::Accent : Theme::SubText).color());
    p.drawText(r.adjusted(8, 0, 0, -4), Qt::AlignLeft | Qt::AlignBottom, title);
}

void Skin::paintListRow(QPainter& p, const ListRowPaint& row) const
{
    if (row.hover || row.selected)
    {
        p.setPen(Qt::NoPen);
        p.setBrush((row.pressed ? Theme::TilePressed : row.hover ? Theme::TileHover : Theme::TileBg).color());
        p.drawRoundedRect(row.r, 6, 6);
    }
    const QRect ir(row.r.x() + 8, row.r.center().y() - Theme::ListIcon / 2, Theme::ListIcon, Theme::ListIcon);
    row.icon(p, *row.item, ir);
    const QRect tr(ir.right() + 10, row.r.y(), row.r.right() - ir.right() - 14, row.r.height());
    drawHighlighted(p, tr, row.item->name, row.highlight, font(Font::ListRow), Theme::TextColor.color(),
                    Theme::Accent.color());
}

// ---------- мини-плеер ----------

QRect Skin::mediaPartRect(const QRect& r, int part)
{
    const int pad = 4;
    const int s = r.height() - 2 * pad;
    const int y = r.y() + pad;
    const QRect close(r.right() - pad - 18 + 1, y, 18, s);
    const QRect next(close.left() - 2 - s, y, s, s);
    const QRect play(next.left() - 2 - s, y, s, s);
    const QRect prev(play.left() - 2 - s, y, s, s);
    const QRect art(r.x() + pad, y, s, s);
    switch (part)
    {
    case MediaPrev:  return prev;
    case MediaPlay:  return play;
    case MediaNext:  return next;
    case MediaClose: return close;
    case MediaArt:   return art;
    case MediaText:  return QRect(art.right() + 9, r.y(), prev.left() - 6 - (art.right() + 9), r.height());
    default:         return {};
    }
}

// Панель: обложка слева (или фоном), сверху трек и кнопки «свернуть / закрыть»,
// посередине «назад / играть / вперёд», внизу ползунок со временем.
QRect Skin::mediaPanelRect(const QRect& r, int part, bool artBackground)
{
    const int pad = 16;
    const int s = r.height() - 2 * pad;
    const QRect art(r.x() + pad, r.y() + pad, s, s);
    const int x0 = artBackground ? r.x() + pad + 6 : art.right() + 1 + 22;
    const int x1 = r.right() - pad;
    const QRect close(x1 - 30 + 1, r.y() + pad - 4, 30, 30);
    const QRect collapse(close.left() - 4 - 30, close.y(), 30, 30);
    const int cx = r.center().x(); // кнопки — ровно по центру панели (и меню под ней)
    const int cy = r.y() + pad + 66;
    const QRect play(cx - 23, cy - 23, 46, 46);
    const QRect prev(play.left() - 16 - 40, cy - 20, 40, 40);
    const QRect next(play.right() + 1 + 16, cy - 20, 40, 40);
    const int sy = r.bottom() - pad - 7;
    const QRect seek(x0 + 52, sy - 9, x1 - 52 - (x0 + 52), 18);
    switch (part)
    {
    case MediaArt:       return artBackground ? QRect() : art;
    case MediaText:      return QRect(x0, r.y() + pad - 2, collapse.left() - 14 - x0, 48);
    case MediaClose:     return close;
    case MediaCollapse:  return collapse;
    case MediaPlay:      return play;
    case MediaPrev:      return prev;
    case MediaNext:      return next;
    case MediaSeek:      return seek;
    case MediaTimeLeft:  return QRect(x0, sy - 9, 46, 18);
    case MediaTimeRight: return QRect(x1 - 46 + 1, sy - 9, 46, 18);
    default:             return {};
    }
}

MediaStyle Skin::mediaStyle(const QRect&) const
{
    const QColor t = Theme::TextColor.color();
    auto a = [&](qreal k) { QColor c = t; c.setAlphaF(k); return c; };
    MediaStyle st;
    st.radius = 6;
    st.bg = a(0.06);
    st.border = QColor(0, 0, 0, 0);
    st.hoverBg = a(0.10);
    st.playBg = a(0.12);
    st.playGlyph = t;
    st.glyph = a(0.8);
    const QStringList ui{QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")};
    st.title = makeFont(ui, 12, QFont::DemiBold);
    st.sub = makeFont(ui, 11);
    st.titleColor = a(0.95);
    st.subColor = a(0.55);
    st.progress = Theme::Accent.color();
    return st;
}

namespace
{
    // Сглаженное масштабирование дорогое, а плеер перерисовывается до 30 раз в секунду: держим последние результаты.
    QImage scaledCached(const QImage& img, const QSize& size, Qt::AspectRatioMode mode)
    {
        struct Key
        {
            qint64 image;
            int w, h, mode;
            bool operator==(const Key& o) const { return image == o.image && w == o.w && h == o.h && mode == o.mode; }
        };
        static QList<QPair<Key, QImage>> cache; // несколько последних
        const Key k{img.cacheKey(), size.width(), size.height(), int(mode)};
        for (const auto& c : cache)
            if (c.first == k)
                return c.second;
        QImage out = img.scaled(size, mode, Qt::SmoothTransformation);
        cache.prepend({k, out});
        if (cache.size() > 6)
            cache.removeLast();
        return out;
    }

    // Значки кнопок плеера — векторные, чтобы одинаково чётко на любом масштабе.
    void mediaGlyph(QPainter& p, int part, const QRectF& r, const QColor& c, bool playing)
    {
        const QPointF o = r.center();
        const qreal g = qMin(r.width(), r.height());
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        auto tri = [&](qreal cx, qreal h, bool right) {
            const qreal w = h * 0.86;
            const qreal x0 = right ? cx - w / 2 : cx + w / 2, x1 = right ? cx + w / 2 : cx - w / 2;
            const QPointF pts[3] = {{x0, o.y() - h / 2}, {x1, o.y()}, {x0, o.y() + h / 2}};
            p.drawPolygon(pts, 3);
        };
        if (part == Skin::MediaPlay)
        {
            const qreal h = g * 0.40;
            if (playing)
            {
                const qreal bw = h * 0.3;
                p.drawRoundedRect(QRectF(o.x() - h * 0.4, o.y() - h / 2, bw, h), 1, 1);
                p.drawRoundedRect(QRectF(o.x() + h * 0.4 - bw, o.y() - h / 2, bw, h), 1, 1);
            }
            else
                tri(o.x() + h * 0.06, h, true);
        }
        else if (part == Skin::MediaPrev || part == Skin::MediaNext)
        {
            const bool right = part == Skin::MediaNext;
            const qreal h = g * 0.36;
            tri(o.x() + (right ? -h * 0.12 : h * 0.12), h, right);
            const qreal bx = right ? o.x() + h * 0.43 : o.x() - h * 0.43 - h * 0.16;
            p.drawRoundedRect(QRectF(bx, o.y() - h / 2, h * 0.16, h), 0.8, 0.8);
        }
        else if (part == Skin::MediaClose)
        {
            const qreal h = g * 0.16;
            p.setPen(QPen(c, 1.4, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(o.x() - h, o.y() - h), QPointF(o.x() + h, o.y() + h));
            p.drawLine(QPointF(o.x() + h, o.y() - h), QPointF(o.x() - h, o.y() + h));
        }
        else if (part == Skin::MediaCollapse)
        {
            // «Свернуть»: галочка вниз
            const qreal h = g * 0.14;
            p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            const QPointF pts[3] = {{o.x() - h * 1.4, o.y() - h * 0.6}, {o.x(), o.y() + h * 0.8}, {o.x() + h * 1.4, o.y() - h * 0.6}};
            p.drawPolyline(pts, 3);
        }
    }

    void paintArt(QPainter& p, const QRect& ar, const QImage& art, qreal radius, const MediaStyle& st, const MediaPaint& m)
    {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(ar), radius, radius);
        if (!art.isNull())
        {
            p.save();
            p.setClipPath(clip);
            const QImage img = scaledCached(art, ar.size() * 2, Qt::KeepAspectRatioByExpanding);
            const QRect src((img.width() - ar.width() * 2) / 2, (img.height() - ar.height() * 2) / 2, ar.width() * 2, ar.height() * 2);
            p.drawImage(QRectF(ar), img, src);
            p.restore();
            return;
        }
        // Нет обложки: значок приложения-источника (у Spotify — его логотип) на размытом фоне из него же.
        // Нашли значок не всегда — тогда рисуем нашу иконку (её передаёт плеер), так что пустого квадрата не бывает.
        p.save();
        p.setClipPath(clip);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(ar, m.appPlate.isValid() ? m.appPlate : st.hoverBg);
        if (!m.appSoft.isNull())
            p.drawImage(QRectF(ar), m.appSoft);
        if (!m.appIcon.isNull())
        {
            const int s = int(ar.height() * 0.66);
            const QImage icon = scaledCached(m.appIcon, QSize(s * 2, s * 2), Qt::KeepAspectRatio);
            const QRectF dst(ar.center().x() - s / 2.0, ar.center().y() - s / 2.0, s, s);
            p.drawImage(dst, icon);
        }
        p.restore();
    }

    void paintButtons(QPainter& p, const MediaPaint& m, const MediaStyle& st, const std::function<QRect(int)>& rectOf,
                      std::initializer_list<int> parts)
    {
        for (int part : parts)
        {
            QRectF br = QRectF(rectOf(part));
            if (br.isEmpty())
                continue;
            if (part == Skin::MediaClose && br.height() > br.width())
                br = br.adjusted(0, (br.height() - br.width()) / 2, 0, -(br.height() - br.width()) / 2);
            else if (part != Skin::MediaClose)
                br = br.adjusted(1, 1, -1, -1);
            const bool enabled = part == Skin::MediaPrev ? m.canPrev : part == Skin::MediaNext ? m.canNext : true;
            const bool hot = enabled && m.hover == part;
            const qreal rad = st.roundButtons ? br.height() / 2 : 3;
            p.save();
            if (part == Skin::MediaPlay)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(st.playBg);
                p.drawRoundedRect(br, rad, rad);
                if (hot)
                {
                    p.setBrush(Theme::Light ? QColor(0, 0, 0, m.pressed == part ? 18 : 28) : QColor(255, 255, 255, m.pressed == part ? 20 : 34));
                    p.drawRoundedRect(br, rad, rad);
                }
            }
            else if (hot)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(st.hoverBg);
                p.drawRoundedRect(br, rad, rad);
            }
            QColor c = part == Skin::MediaPlay ? st.playGlyph : st.glyph;
            if (!enabled)
                c.setAlphaF(c.alphaF() * 0.3);
            if ((part == Skin::MediaClose || part == Skin::MediaCollapse) && !hot)
                c.setAlphaF(c.alphaF() * 0.65);
            mediaGlyph(p, part, br, c, m.playing);
            p.restore();
        }
    }

    QString fmtTime(qint64 ms)
    {
        const qint64 sec = ms / 1000;
        return sec >= 3600 ? QStringLiteral("%1:%2:%3").arg(sec / 3600).arg(sec / 60 % 60, 2, 10, QLatin1Char('0')).arg(sec % 60, 2, 10, QLatin1Char('0'))
                           : QStringLiteral("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QLatin1Char('0'));
    }
}

// Цвет плеера из настроек (акцент или обложка) поверх стиля: полоса, волна и кнопка «играть».
static void applyTint(MediaStyle& st, const MediaPaint& m)
{
    if (!m.tintA.isValid())
        return;
    QLinearGradient g(0, 0, 1, 1);
    g.setCoordinateMode(QGradient::ObjectBoundingMode);
    g.setColorAt(0, m.tintA);
    g.setColorAt(1, m.tintB.isValid() ? m.tintB : m.tintA);
    // Полоса тонкая — ей градиент слева направо, иначе виден только один цвет
    QLinearGradient h(0, 0, 1, 0);
    h.setCoordinateMode(QGradient::ObjectBoundingMode);
    h.setColorAt(0, m.tintA);
    h.setColorAt(1, m.tintB.isValid() ? m.tintB : m.tintA);
    st.progress = QBrush(h);
    const bool glyphOnly = st.playBg.style() == Qt::SolidPattern && st.playBg.color().alpha() == 0;
    if (glyphOnly)
    {
        st.playGlyph = m.tintA; // Studio: у кнопки нет подложки — красим сам значок
        return;
    }
    st.playBg = QBrush(g);
    // Значок на кнопке — тёмный на светлой заливке, белый на тёмной.
    const qreal l = 0.2126 * m.tintA.redF() + 0.7152 * m.tintA.greenF() + 0.0722 * m.tintA.blueF();
    st.playGlyph = l > 0.62 ? QColor(0x10, 0x12, 0x18) : QColor(255, 255, 255);
}

void Skin::paintMediaPlayer(QPainter& p, const MediaPaint& m) const
{
    MediaStyle st = mediaStyle(m.r);
    applyTint(st, m);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF rf = QRectF(m.r).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(st.border.alpha() ? QPen(st.border, 1) : QPen(Qt::NoPen));
    p.setBrush(st.bg);
    p.drawRoundedRect(rf, st.radius, st.radius);
    if (m.hover == MediaBody || m.hover == MediaArt || m.hover == MediaText)
    {
        // Наведение на трек — подсказка, что по клику плеер раскроется
        p.setBrush(st.hoverBg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(rf, st.radius, st.radius);
    }

    paintArt(p, mediaPartRect(m.r, MediaArt), m.art, st.artRadius, st, m);

    const QRect tr = mediaPartRect(m.r, MediaText);
    if (tr.width() > 20)
    {
        const QFontMetrics tfm(st.title), sfm(st.sub);
        const QString artist = st.upperSub ? m.artist.toUpper() : m.artist;
        const int total = tfm.height() + (artist.isEmpty() ? 0 : sfm.height() - 1);
        int y = tr.center().y() - total / 2;
        p.setFont(st.title);
        p.setPen(st.titleColor);
        p.drawText(QRect(tr.x(), y, tr.width(), tfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   tfm.elidedText(m.title, Qt::ElideRight, tr.width()));
        if (!artist.isEmpty())
        {
            y += tfm.height() - 1;
            p.setFont(st.sub);
            p.setPen(st.subColor);
            p.drawText(QRect(tr.x(), y, tr.width(), sfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       sfm.elidedText(artist, Qt::ElideRight, tr.width()));
        }
    }
    paintButtons(p, m, st, [&](int part) { return mediaPartRect(m.r, part); },
                 {int(MediaPrev), int(MediaPlay), int(MediaNext), int(MediaClose)});
    p.restore();
}

void Skin::paintMediaPanel(QPainter& p, const MediaPaint& m) const
{
    MediaStyle st = mediaStyle(m.r);
    applyTint(st, m);
    st.title.setPixelSize(st.title.pixelSize() + 6);
    st.sub.setPixelSize(st.sub.pixelSize() + 2);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    auto rectOf = [&](int part) { return mediaPanelRect(m.r, part, m.artBackground); };

    // Обложка на весь фон: размытая, с затемнением слева, чтобы читался текст
    if (m.artBackground && !m.artBlur.isNull())
    {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(m.r), m.panelRadius, m.panelRadius);
        p.save();
        p.setClipPath(clip);
        p.setOpacity(m.artOpacity);
        const QImage img = scaledCached(m.artBlur, m.r.size(), Qt::KeepAspectRatioByExpanding);
        p.drawImage(m.r.topLeft(), img, QRect((img.width() - m.r.width()) / 2, (img.height() - m.r.height()) / 2, m.r.width(), m.r.height()));
        p.setOpacity(1);
        QLinearGradient shade(m.r.topLeft(), m.r.topRight());
        QColor dark = Theme::PanelBg.color();
        dark.setAlpha(150);
        shade.setColorAt(0, dark);
        dark.setAlpha(40);
        shade.setColorAt(1, dark);
        p.fillRect(m.r, shade);
        p.restore();
    }

    // Подсветка снизу: два мягких пятна цветов обложки, яркость и размер — в такт звуку
    if (m.glow >= 0)
    {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(m.r), m.panelRadius, m.panelRadius);
        p.save();
        p.setClipPath(clip);
        const qreal k = m.glow;
        const qreal drift = std::sin(m.wavePhase * 0.15) * m.r.width() * 0.06; // пятна чуть «плывут»
        auto blob = [&](qreal cx, QColor c, qreal strength) {
            const QPointF center(cx, m.r.bottom() + m.r.height() * 0.35);
            const qreal rx = m.r.width() * (0.34 + 0.10 * k), ry = m.r.height() * (0.95 + 0.45 * k);
            QRadialGradient g(QPointF(0, 0), 1.0);
            c.setAlphaF(qBound(0.0, (0.10 + 0.42 * k) * strength, 1.0));
            g.setColorAt(0, c);
            c.setAlphaF(c.alphaF() * 0.45);
            g.setColorAt(0.5, c);
            c.setAlpha(0);
            g.setColorAt(1, c);
            p.save();
            p.translate(center);
            p.scale(rx, ry);
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawEllipse(QPointF(0, 0), 1.0, 1.0);
            p.restore();
        };
        p.setCompositionMode(QPainter::CompositionMode_Plus); // свет складывается, а не перекрывает
        blob(m.r.x() + m.r.width() * 0.32 + drift, m.glowA, 1.0);
        blob(m.r.x() + m.r.width() * 0.68 - drift, m.glowB, 0.9);
        p.restore();
    }

    if (!m.artBackground)
        paintArt(p, rectOf(MediaArt), m.art, st.artRadius * 2, st, m);

    // Трек
    const QRect tr = rectOf(MediaText);
    const QFontMetrics tfm(st.title), sfm(st.sub);
    p.setFont(st.title);
    p.setPen(st.titleColor);
    p.drawText(QRect(tr.x(), tr.y(), tr.width(), tfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
               tfm.elidedText(m.title, Qt::ElideRight, tr.width()));
    const QString artist = st.upperSub ? m.artist.toUpper() : m.artist;
    if (!artist.isEmpty())
    {
        p.setFont(st.sub);
        p.setPen(st.subColor);
        p.drawText(QRect(tr.x(), tr.y() + tfm.height() + 1, tr.width(), sfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   sfm.elidedText(artist, Qt::ElideRight, tr.width()));
    }

    paintButtons(p, m, st, rectOf, {int(MediaPrev), int(MediaPlay), int(MediaNext), int(MediaCollapse), int(MediaClose)});

    // Ползунок: пройденная часть — волна (пока играет), остаток — тонкая линия
    if (m.durationMs > 0)
    {
        const QRect sr = rectOf(MediaSeek);
        const qreal frac = m.seekFrac >= 0 ? m.seekFrac : qBound(0.0, double(m.positionMs) / double(m.durationMs), 1.0);
        const bool hot = m.canSeek && (m.hover == MediaSeek || m.seekFrac >= 0);
        const qreal cy = sr.center().y() + 0.5;
        const qreal xEnd = sr.left() + sr.width() * frac;
        QColor rest = st.titleColor;
        rest.setAlphaF(0.18f);
        p.setPen(QPen(rest, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(xEnd + 6, cy), QPointF(sr.right(), cy));
        // Градиент — по пройденной части полосы в настоящих координатах (у линии нет высоты,
        // и градиент «по фигуре» на ней вырождается в один цвет).
        QBrush prog = st.progress;
        if (const QGradient* gr = st.progress.gradient())
        {
            QLinearGradient lg(sr.left(), 0, qMax(xEnd, sr.left() + 40.0), 0);
            lg.setStops(gr->stops());
            prog = QBrush(lg);
        }
        QPen played(prog, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(played);
        p.setBrush(Qt::NoBrush);
        const qreal amp = 3.6 * m.waveAmp;
        if (amp > 0.05 && xEnd - sr.left() > 4)
        {
            QPainterPath wave;
            const qreal len = 26.0; // длина одной волны, px
            for (qreal x = sr.left(); x <= xEnd; x += 1.5)
            {
                // у краёв волна сходит на нет, чтобы линия плавно входила в ползунок
                const qreal edge = qMin(1.0, qMin(x - sr.left(), xEnd - x) / 14.0);
                const qreal y = cy + amp * edge * std::sin((x - sr.left()) / len * 6.2831853 - m.wavePhase);
                if (x == sr.left())
                    wave.moveTo(x, y);
                else
                    wave.lineTo(x, y);
            }
            p.drawPath(wave);
        }
        else
            p.drawLine(QPointF(sr.left(), cy), QPointF(qMax(qreal(sr.left()), xEnd), cy));
        // Ползунок-«таблетка», при наведении — крупнее
        p.setPen(Qt::NoPen);
        p.setBrush(hot ? QBrush(QColor(255, 255, 255)) : prog);
        const qreal kh = hot ? 18 : 14, kw = hot ? 6 : 4;
        p.drawRoundedRect(QRectF(xEnd - kw / 2, cy - kh / 2, kw, kh), kw / 2, kw / 2);

        p.setFont(st.sub);
        p.setPen(st.subColor);
        p.drawText(rectOf(MediaTimeLeft), Qt::AlignLeft | Qt::AlignVCenter, fmtTime(qint64(frac * m.durationMs)));
        p.drawText(rectOf(MediaTimeRight), Qt::AlignRight | Qt::AlignVCenter, fmtTime(m.durationMs));
    }
    p.restore();
}
