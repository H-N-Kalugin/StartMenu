#include "AppList.h"
#include "Lang.h"
#include "IconCache.h"
#include "Shell.h"
#include "Skin.h"
#include "Theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#include <climits>

namespace
{
    QString letterOf(const QString& name)
    {
        const QChar c = name.isEmpty() ? QChar() : name.at(0).toUpper();
        return c.isLetter() ? QString(c) : QStringLiteral("#");
    }
}

AppList::AppList(IconCache* icons, QWidget* parent)
    : QWidget(parent), m_icons(icons)
{
    setMouseTracking(true);
    m_font = font();
    m_font.setPixelSize(Theme::ListFontPx);
    m_headerFont = m_font;
    m_headerFont.setBold(true);
    connect(m_icons, &IconCache::changed, this, qOverload<>(&QWidget::update));
    m_anim.setInterval(16);
    connect(&m_anim, &QTimer::timeout, this, [this] {
        const int d = m_target - m_scroll;
        if (d == 0)
        {
            m_anim.stop();
            return;
        }
        m_scroll += d / 3 != 0 ? d / 3 : (d > 0 ? 1 : -1);
        update();
    });
}

void AppList::setItems(QList<Item> items, bool letterHeaders, const QString& title,
                       const QList<QPair<int, QString>>& sections, const QStringList& highlight)
{
    m_items = std::move(items);
    m_letters = letterHeaders;
    m_title = title;
    m_sections = sections;
    m_highlight = highlight;
    m_scroll = 0;
    m_hover = m_pressed = -1;
    m_selected = m_items.isEmpty() ? -1 : 0;
    relayout();
    update();
}

void AppList::relayout()
{
    m_rows.clear();
    const int top = m_title.isEmpty() ? 0 : Theme::ListTitle;
    const int areaH = qMax(Theme::ListRow, height() - top);
    int x = 0, y = 0;
    QString lastLetter;

    auto place = [&](Row row, int h) {
        if (y + h > areaH && y > 0)
        {
            x += Theme::ListColumnWidth;
            y = 0;
        }
        row.rect = QRect(x, top + y, Theme::ListColumnWidth - 12, h);
        m_rows.append(row);
        y += h;
    };

    int nextSection = 0;
    for (int i = 0; i < m_items.size(); ++i)
    {
        while (nextSection < m_sections.size() && m_sections[nextSection].first == i)
        {
            if (y + Theme::ListHeader + Theme::ListRow > areaH && y > 0)
            {
                x += Theme::ListColumnWidth;
                y = 0;
            }
            place({-2, m_sections[nextSection].second, {}}, Theme::ListHeader); // -2 — заголовок раздела
            ++nextSection;
        }
        if (m_letters)
        {
            const QString l = letterOf(m_items[i].name);
            if (l != lastLetter)
            {
                lastLetter = l;
                // Буква не должна остаться последней строкой колонки без элементов под ней.
                if (y + Theme::ListHeader + Theme::ListRow > areaH && y > 0)
                {
                    x += Theme::ListColumnWidth;
                    y = 0;
                }
                place({-1, l, {}}, Theme::ListHeader);
            }
        }
        place({i, {}, {}}, Theme::ListRow);
    }
    m_contentW = m_rows.isEmpty() ? 0 : x + Theme::ListColumnWidth;
    setScroll(m_scroll);
}

void AppList::resizeEvent(QResizeEvent*)
{
    relayout();
}

void AppList::setScroll(int x)
{
    m_scroll = m_target = std::clamp(x, 0, qMax(0, m_contentW - width()));
    update();
}

void AppList::scrollAnimated(int x)
{
    m_target = std::clamp(x, 0, qMax(0, m_contentW - width()));
    if (!m_anim.isActive())
        m_anim.start();
}

int AppList::rowAt(const QPoint& view) const
{
    const QPoint cp = view + QPoint(m_scroll, 0);
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].item >= 0 && m_rows[i].rect.contains(cp))
            return i;
    return -1;
}

int AppList::rowOfItem(int item) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].item == item)
            return i;
    return -1;
}

void AppList::ensureVisible(int item)
{
    const int r = rowOfItem(item);
    if (r < 0)
        return;
    const QRect rr = m_rows[r].rect;
    if (rr.left() < m_target)
        scrollAnimated(rr.left());
    else if (rr.right() > m_target + width())
        scrollAnimated(rr.right() - width() + 12);
}

void AppList::moveSelection(int dx, int dy)
{
    if (m_items.isEmpty())
        return;
    if (m_selected < 0)
        m_selected = 0;
    else if (dy != 0)
        m_selected = std::clamp(m_selected + dy, 0, int(m_items.size()) - 1);
    else if (dx != 0)
    {
        // Влево/вправо — в соседнюю колонку, на ту же высоту.
        const int r = rowOfItem(m_selected);
        const QRect cur = m_rows[r].rect;
        int best = -1, bestDist = INT_MAX;
        for (const Row& row : m_rows)
        {
            if (row.item < 0 || row.rect.x() != cur.x() + dx * Theme::ListColumnWidth)
                continue;
            const int dist = qAbs(row.rect.center().y() - cur.center().y());
            if (dist < bestDist)
            {
                bestDist = dist;
                best = row.item;
            }
        }
        if (best >= 0)
            m_selected = best;
    }
    ensureVisible(m_selected);
    update();
}

void AppList::activateSelected()
{
    if (m_selected < 0 || m_selected >= m_items.size())
        return;
    WinShell::launch(m_items[m_selected]);
    emit launched();
}

void AppList::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const Skin& skin = Skin::current();
    const int top = m_title.isEmpty() ? 0 : Theme::ListTitle;
    // Заголовок не листается
    if (!m_title.isEmpty())
        skin.paintListTitle(p, QRect(4, 0, width() - 8, Theme::ListTitle - 6), m_title);

    if (m_items.isEmpty())
    {
        p.setFont(skin.font(Skin::Font::ListRow));
        p.setPen(Theme::SubText.color());
        p.drawText(QRect(8, top, width() - 8, Theme::ListRow), Qt::AlignLeft | Qt::AlignVCenter, TXT("Ничего не найдено"));
        return;
    }

    p.setClipRect(QRect(0, top, width(), height() - top));
    p.translate(-m_scroll, 0);
    const qreal dpr = devicePixelRatioF();
    const IconPainter icon = [this, dpr](QPainter& pp, const Item& it, const QRect& ir) {
        const QPixmap pm = m_icons->get(it, ir.width(), dpr);
        if (!pm.isNull())
            pp.drawPixmap(ir, pm);
    };

    for (int i = 0; i < m_rows.size(); ++i)
    {
        const Row& row = m_rows[i];
        if (row.rect.right() < m_scroll || row.rect.left() > m_scroll + width())
            continue;
        if (row.item < 0)
        {
            skin.paintListSection(p, row.rect, row.letter, row.item == -1); // -1 — буква, -2 — раздел поиска
            continue;
        }
        ListRowPaint lr;
        lr.r = row.rect;
        lr.item = &m_items[row.item];
        lr.highlight = m_highlight;
        lr.hover = (i == m_hover);
        lr.selected = (row.item == m_selected);
        lr.pressed = (i == m_pressed);
        lr.icon = icon;
        skin.paintListRow(p, lr);
    }

    p.resetTransform();
    p.setClipping(false);
    if (m_contentW > width())
    {
        const int w = qMax(30, width() * width() / m_contentW);
        const int x = m_scroll * (width() - w) / (m_contentW - width());
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::ScrollHandle.color());
        p.drawRoundedRect(QRect(x, height() - 4, w, 4), 2, 2);
    }
}

void AppList::mouseMoveEvent(QMouseEvent* e)
{
    // Потянули нажатую строку — перетаскивание в группу плиток.
    if ((e->buttons() & Qt::LeftButton) && m_pressed >= 0
        && (e->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance())
    {
        const Item item = m_items[m_rows[m_pressed].item];
        m_pressed = -1;
        update();
        emit dragRequested(item);
        return;
    }
    const int r = rowAt(e->position().toPoint());
    if (r != m_hover)
    {
        m_hover = r;
        setCursor(r >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void AppList::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
    {
        m_pressed = rowAt(e->position().toPoint());
        m_pressPos = e->position().toPoint();
        update();
    }
}

void AppList::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const int r = rowAt(e->position().toPoint());
    const bool click = (r >= 0 && r == m_pressed);
    m_pressed = -1;
    update();
    if (click)
    {
        m_selected = m_rows[r].item;
        activateSelected();
    }
}

void AppList::leaveEvent(QEvent*)
{
    m_hover = m_pressed = -1;
    update();
}

void AppList::wheelEvent(QWheelEvent* e)
{
    const QPoint px = e->pixelDelta();
    const QPoint ang = e->angleDelta();
    const int d = !px.isNull() ? (px.y() + px.x()) : (ang.y() + ang.x()) * Theme::ListColumnWidth / 240;
    scrollAnimated(m_target - d);
    e->accept();
}

bool AppList::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip)
    {
        auto* he = static_cast<QHelpEvent*>(e);
        const int r = rowAt(he->pos());
        if (r >= 0)
        {
            const Item& it = m_items[m_rows[r].item];
            QString tip = WinShell::isLocalFile(it.path) ? it.name + QLatin1Char('\n') + QDir::toNativeSeparators(it.path)
                                                         : it.name;
            if (it.path.startsWith(QStringLiteral("calc:")))
                tip = TXT("Enter или клик — скопировать результат");
            QToolTip::showText(he->globalPos(), tip, this);
        }
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

void AppList::contextMenuEvent(QContextMenuEvent* e)
{
    const int r = rowAt(e->pos());
    if (r >= 0)
        itemMenu(m_rows[r].item, e->globalPos());
}

void AppList::itemMenu(int index, const QPoint& globalPos)
{
    emit busyChanged(true);
    const Item item = m_items[index];
    auto& groups = Config::instance().groups();

    QMenu menu(this);
    QAction* open = menu.addAction(TXT("Открыть"));
    QAction* admin = menu.addAction(TXT("Запуск от имени администратора"));
    QAction* where = WinShell::isLocalFile(item.path) ? menu.addAction(TXT("Показать в папке")) : nullptr;
    menu.addSeparator();
    QMenu* pin = menu.addMenu(TXT("Закрепить в группе"));
    for (int g = 0; g < groups.size(); ++g)
        pin->addAction(groups[g].title().isEmpty() ? TXT("Без названия") : groups[g].title())->setData(g);
    if (!groups.isEmpty())
        pin->addSeparator();
    pin->addAction(TXT("Новая группа"))->setData(-1);

    QAction* chosen = menu.exec(globalPos);
    emit busyChanged(false);
    if (!chosen)
        return;

    if (chosen == open || chosen == admin)
    {
        WinShell::launch(item, chosen == admin);
        emit launched();
    }
    else if (chosen == where)
    {
        WinShell::showInFolder(item.path);
        emit launched();
    }
    else if (chosen->data().isValid())
    {
        const int g = chosen->data().toInt();
        const Tile tile{item, TileSize::Medium, false, {}};
        if (g >= 0)
            groups[g].tiles().append(tile);
        else
        {
            Group ng;
            ng.title() = TXT("Новая группа");
            int maxCol = -1;
            for (const Group& gr : groups)
                maxCol = qMax(maxCol, gr.column);
            ng.column = maxCol + 1;
            ng.tiles() = {tile};
            groups.append(ng);
        }
        Config::instance().normalize();
        Config::instance().save();
    }
}
