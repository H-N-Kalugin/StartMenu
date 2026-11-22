#include "Workspace.h"
#include "Lang.h"
#include "IconCache.h"
#include "Settings.h"
#include "Skin.h"
#include "Shell.h"
#include "Theme.h"

#include <QApplication>
#include <QColorDialog>
#include <QGuiApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileDialog>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainterPath>
#include <QtMath>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHelpEvent>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#include <windows.h>
#include <climits>

namespace
{
    int cellsPx(int n) { return n * Theme::Cell + (n - 1) * Theme::CellGap; }
    int mediumPx() { return cellsPx(2); }

    QSize tileCells(TileSize s, int groupWidth)
    {
        QSize c;
        switch (s)
        {
        case TileSize::Small: c = {1, 1}; break;
        case TileSize::Wide:  c = {4, 2}; break;
        case TileSize::Large: c = {4, 4}; break;
        default:              c = {2, 2}; break;
        }
        if (c.width() > groupWidth) // в узкую группу широкая плитка не влезает — рисуем средней
            c = {2, 2};
        return c;
    }

    // Раскладка плиток внутри группы: каждая плитка встаёт в первое свободное место (слева направо, сверху вниз).
    QList<QRect> packTiles(const QList<Tile>& tiles, int width, int& rowsOut)
    {
        QList<QList<bool>> occ;
        auto ensureRows = [&](int rows) {
            while (occ.size() < rows)
                occ.append(QList<bool>(width, false));
        };
        auto isFree = [&](int x, int y, int w, int h) {
            ensureRows(y + h);
            for (int yy = y; yy < y + h; ++yy)
                for (int xx = x; xx < x + w; ++xx)
                    if (occ[yy][xx])
                        return false;
            return true;
        };

        QList<QRect> out;
        for (const Tile& t : tiles)
        {
            const QSize c = tileCells(t.size, width);
            for (int y = 0;; ++y)
            {
                int found = -1;
                for (int x = 0; x + c.width() <= width; ++x)
                    if (isFree(x, y, c.width(), c.height())) { found = x; break; }
                if (found < 0)
                    continue;
                for (int yy = y; yy < y + c.height(); ++yy)
                    for (int xx = found; xx < found + c.width(); ++xx)
                        occ[yy][xx] = true;
                out.append(QRect(found, y, c.width(), c.height()));
                break;
            }
        }
        // Хвостовые пустые строки не считаем.
        int rows = int(occ.size());
        while (rows > 0 && !occ[rows - 1].contains(true))
            --rows;
        rowsOut = rows;
        return out;
    }

    // Пока открыто меню или диалог, окно не должно закрываться по потере фокуса.
    struct BusyGuard
    {
        Workspace* w;
        explicit BusyGuard(Workspace* ws) : w(ws) { emit w->busyChanged(true); }
        ~BusyGuard() { emit w->busyChanged(false); }
    };

    const char* kSizeNames[] = {"Маленький", "Средний", "Широкий", "Большой"};

    // Группу g убирают с места: если она начинала ряд, а справа к ней «присоединена» следующая —
    // та становится началом ряда (иначе прилипнет к чужой группе выше).
    void detachRow(QList<Group>& groups, int g)
    {
        if (g < 0 || g >= groups.size() || groups[g].joinPrev)
            return;
        for (int j = g + 1; j < groups.size(); ++j)
            if (groups[j].column == groups[g].column)
            {
                groups[j].joinPrev = false;
                return;
            }
    }
}

Workspace::Workspace(IconCache* icons, QWidget* parent)
    : QWidget(parent), m_icons(icons)
{
    setMouseTracking(true);
    setAcceptDrops(true);
    connect(m_icons, &IconCache::changed, this, qOverload<>(&QWidget::update));
    m_anim.setInterval(16); // ~60 кадров в секунду, работает только пока что-то движется
    connect(&m_anim, &QTimer::timeout, this, &Workspace::animate);
    applyTheme();
}

const char* Workspace::itemMime()
{
    return "application/x-startmenu-item";
}

// ---------- плавность ----------

void Workspace::startAnimation()
{
    if (!m_anim.isActive())
        m_anim.start();
}

void Workspace::animate()
{
    bool moving = false;

    QRegion dirty;             // что перерисовать (а не всё окно)
    bool fullUpdate = false;
    const auto tileRegion = [this](quint64 key) {
        const int g = int(key >> 32), t = int(key & 0xffffffff);
        // С запасом: свечение (Bento) и уголки (Studio) выходят за плитку.
        return g < m_geo.size() && t < m_geo[g].tiles.size() ? toView(m_geo[g].tiles[t]).adjusted(-14, -14, 14, 14) : QRect();
    };

    // Подсветка наведения: быстро загорается (~100 мс), чуть медленнее гаснет (~160 мс).
    const bool hoverTile = m_hover.kind == Hit::Tile && m_mode != Mode::DragTile && m_mode != Mode::DragGroup;
    const quint64 hk = hoverTile ? tileKey(m_hover.g, m_hover.t) : ~0ull;
    if (hoverTile && !m_fade.contains(hk))
        m_fade.insert(hk, 0.0);
    for (auto it = m_fade.begin(); it != m_fade.end();)
    {
        const qreal target = (it.key() == hk) ? 1.0 : 0.0;
        const qreal v = std::clamp(it.value() + (target > it.value() ? 0.17 : -0.10), 0.0, 1.0);
        if (v <= 0.0 && target == 0.0)
        {
            dirty += tileRegion(it.key());
            it = m_fade.erase(it);
            continue;
        }
        if (it.value() != v)
            dirty += tileRegion(it.key());
        it.value() = v;
        moving |= (v != target);
        ++it;
    }

    // Плавная прокрутка: каждый кадр проходим треть оставшегося пути.
    if (m_targetX != m_scrollX || m_targetY != m_scrollY)
    {
        auto step = [](int cur, int target) {
            const int d = target - cur;
            if (d == 0)
                return cur;
            const int s = d / 3;
            return cur + (s != 0 ? s : (d > 0 ? 1 : -1));
        };
        m_scrollX = step(m_scrollX, m_targetX);
        m_scrollY = step(m_scrollY, m_targetY);
        moving = true;
        fullUpdate = true; // прокрутка сдвигает всё
    }

    if (!moving)
        m_anim.stop();
    if (fullUpdate)
        update();
    else if (!dirty.isEmpty())
        update(dirty);
}

void Workspace::scrollAnimated(int x, int y)
{
    m_targetX = std::clamp(x, 0, qMax(0, m_content.width() - width()));
    m_targetY = std::clamp(y, 0, qMax(0, m_content.height() - height()));
    startAnimation();
}

void Workspace::ensureVisible(const QRect& r)
{
    int x = m_targetX, y = m_targetY;
    if (r.left() - 8 < x)
        x = r.left() - 8;
    else if (r.right() + 8 > x + width())
        x = r.right() + 8 - width();
    if (r.top() - 8 < y)
        y = r.top() - 8;
    else if (r.bottom() + 8 > y + height())
        y = r.bottom() + 8 - height();
    scrollAnimated(x, y);
}

// ---------- клавиатура ----------

void Workspace::moveFocus(int dx, int dy)
{
    if (folderOpen())
        return;
    const auto& groups = Config::instance().groups();
    const bool valid = m_focusG >= 0 && m_focusG < groups.size() && m_focusT >= 0 && m_focusT < m_geo[m_focusG].tiles.size();

    int bestG = -1, bestT = -1;
    long long bestScore = LLONG_MAX;
    const QPoint c = valid ? m_geo[m_focusG].tiles[m_focusT].center() : QPoint(-100000, -100000);
    for (int g = 0; g < groups.size(); ++g)
        for (int t = 0; t < m_geo[g].tiles.size(); ++t)
        {
            const QPoint o = m_geo[g].tiles[t].center();
            long long score;
            if (!valid)
                score = 100000LL * o.y() + o.x(); // первый раз — самая левая верхняя плитка
            else
            {
                if (g == m_focusG && t == m_focusT)
                    continue;
                const int along = dx ? (o.x() - c.x()) * dx : (o.y() - c.y()) * dy;
                if (along <= 4)
                    continue;
                const int across = dx ? qAbs(o.y() - c.y()) : qAbs(o.x() - c.x());
                score = along + 2LL * across;
            }
            if (score < bestScore)
            {
                bestScore = score;
                bestG = g;
                bestT = t;
            }
        }
    if (bestG < 0)
        return;
    m_focusG = bestG;
    m_focusT = bestT;
    ensureVisible(m_geo[bestG].tiles[bestT]);
    update();
}

bool Workspace::activateFocus()
{
    const auto& groups = Config::instance().groups();
    if (folderOpen() || m_focusG < 0 || m_focusG >= groups.size() || m_focusT >= groups[m_focusG].tiles().size())
        return false;
    const Tile& tile = groups[m_focusG].tiles()[m_focusT];
    if (tile.isFolder)
    {
        m_folderG = m_focusG;
        m_folderT = m_focusT;
        update();
    }
    else
        launchItem(tile.item);
    return true;
}

void Workspace::applyTheme()
{
    m_titleFont = Skin::current().font(Skin::Font::GroupTitle);
    m_tileFont = font();
    m_tileFont.setPixelSize(Theme::TileFontPx);
    relayout();
    update();
}

void Workspace::reload()
{
    // Группы могли смениться целиком (другое рабочее пространство) — старые индексы недействительны.
    m_folderG = m_folderT = -1;
    m_focusG = m_focusT = -1;
    m_mode = Mode::Idle;
    m_drop = {};
    m_dragSrc = {};
    m_hover = {};
    m_press = {};
    m_fade.clear();
    m_frozenCell = 0;
    {
        // Иконки плиток — заранее и в фоне: к моменту отрисовки они уже лежат в кэше на диске
        QStringList sources;
        for (const Group& g : Config::instance().groups())
            for (const Tab& tab : g.tabs)
                for (const Tile& t : tab.tiles)
                    for (const Item& it : t.isFolder ? t.children : QList<Item>{t.item})
                        sources << (it.icon.isEmpty() ? it.path : it.icon);
        sources.removeDuplicates();
        IconCache::prefetch(sources);
    }
    setScroll(0, 0);
    relayout();
    update();
}

// ---------- раскладка ----------

int Workspace::savedColumnFor(int visualCol) const
{
    const auto& groups = Config::instance().groups();
    int maxSaved = -1;
    for (int g = 0; g < groups.size(); ++g)
    {
        maxSaved = qMax(maxSaved, groups[g].column);
        if (m_visualCol.value(g, -1) == visualCol)
            return groups[g].column; // в эту экранную колонку уже попала сохранённая — кладём туда же
    }
    return maxSaved + 1;             // пустая/новая колонка справа
}

void Workspace::relayout()
{
    const auto& groups = Config::instance().groups();
    const int n = int(groups.size());
    m_geo = QList<GroupGeo>(n);
    m_columns.clear();

    // 1. Раскладка плиток внутри групп от размера ячейки не зависит — считаем один раз.
    QList<QList<QRect>> packed(n);
    QList<int> rows(n);
    for (int g = 0; g < n; ++g)
    {
        int r = 0;
        if (groups[g].listMode)
        {
            // Список: каждая плитка — строка высотой в одну ячейку на всю ширину группы
            for (int t = 0; t < groups[g].tiles().size(); ++t)
                packed[g].append(QRect(0, t, groups[g].width, 1));
            r = int(groups[g].tiles().size());
        }
        else
            packed[g] = packTiles(groups[g].tiles(), groups[g].width, r);
        rows[g] = qMax(r, groups[g].listMode ? 1 : 2); // пустая группа всё равно занимает место под одну среднюю плитку
    }

    // Ширина группы в ячейках. У обычной — как задана; у блока свободных ярлыков — ровно по его плиткам плюс отступы слева и
    // справа (раньше блок занимал всю заданную ширину, и плитки «плавали» внутри пустого места).
    QList<int> effW(n);
    for (int g = 0; g < n; ++g)
    {
        if (!groups[g].loose)
        {
            effW[g] = groups[g].width;
            continue;
        }
        int used = 0;
        for (const QRect& cr : packed[g])
            used = qMax(used, cr.x() + cr.width());
        effW[g] = qMax(1, used) + groups[g].indentL + groups[g].indentR;
    }

    // 2. Колонки — как сохранено.
    const bool wasReflowed = m_reflowed;
    m_visualCol = QList<int>(n);
    for (int g = 0; g < n; ++g)
        m_visualCol[g] = groups[g].column;
    m_reflowed = false;

    // 3. Адаптивность: подбираем ячейку так, чтобы всё влезло в окно (и по ширине, и по высоте).
    //    Ширина всего содержимого линейно зависит от ячейки: W(C) = A·C + B — поэтому C считается сразу.
    const int gap = Theme::CellGap;
    // Колонка = ряды групп сверху вниз; в ряду группы стоят слева направо (joinPrev).
    using Rows = QList<QList<int>>;
    auto rowsOf = [&](const QList<int>& col) {
        QMap<int, Rows> out;
        for (int g = 0; g < n; ++g)
        {
            Rows& r = out[col[g]];
            if (groups[g].joinPrev && !r.isEmpty())
                r.last().append(g);
            else
                r.append(QList<int>{g});
        }
        return out;
    };
    // Ширина и высота линейно зависят от ячейки C: W = a·C + b. Для ряда считаем (a, b),
    // для колонки — самый широкий ряд, для всего — сумма колонок.
    auto cellForWidth = [&](const QList<int>& col) {
        const auto cols = rowsOf(col);
        double a = 0, b = 0;
        for (const Rows& rs : cols)
        {
            double ba = 0, bb = -1e9;
            for (const QList<int>& row : rs)
            {
                double ra = 0, rb = (row.size() - 1) * double(Theme::GroupGapX);
                for (int g : row)
                {
                    ra += effW[g];
                    rb += effW[g] * gap - gap + 2 * padOf(groups[g]);
                }
                if (ra * 46 + rb > ba * 46 + bb)
                {
                    ba = ra;
                    bb = rb;
                }
            }
            a += ba;
            b += bb;
        }
        b += (cols.size() - 1) * double(Theme::GroupGapX) + Theme::ContentInsetX;
        return a > 0 ? (width() - b) / a : 1e9;
    };
    auto cellForHeight = [&](const QList<int>& col) {
        double best = 1e9;
        for (const Rows& rs : rowsOf(col))
        {
            double a = 0, b = (rs.size() - 1) * double(Theme::GroupGapY);
            for (const QList<int>& row : rs)
            {
                // Длинный список не должен сжимать все плитки — его досматривают прокруткой
                int maxRows = 0, hdr = 0, pad = 0;
                for (int g : row)
                {
                    maxRows = qMax(maxRows, groups[g].listMode ? qMin(rows[g], 2) : rows[g]);
                    hdr = qMax(hdr, headerOf(groups[g]));
                    pad = qMax(pad, padBottomOf(groups[g]));
                }
                a += maxRows;
                b += hdr + maxRows * gap - gap + pad;
            }
            if (a > 0)
                best = qMin(best, (height() - b) / a);
        }
        return best;
    };

    if (m_frozenCell > 0 && m_frozenVisual.size() == n)
    {
        // Идёт изменение ширины группы мышью — раскладку колонок и ячейку держим как было.
        m_visualCol = m_frozenVisual;
        m_reflowed = m_frozenReflowed;
        Theme::Cell = m_frozenCell;
    }
    else if (Theme::Adaptive && n > 0 && width() > 0 && height() > 0)
    {
        double cell = qMin(cellForWidth(m_visualCol), cellForHeight(m_visualCol));
        // Порог перестройки 40 (а не 32): плитки не мельчают до нечитаемого. Гистерезис +2,
        // чтобы на границе раскладка не мигала туда-сюда.
        const double threshold = Theme::ReflowCell + (wasReflowed ? 2.0 : 0.0);
        if (cellForWidth(m_visualCol) < threshold)
        {
            // Переносим сохранённые колонки ЦЕЛИКОМ в самую короткую экранную колонку —
            // группы, стоявшие друг под другом (Adobe и 3D), остаются вместе.
            QList<int> saved;
            for (int g = 0; g < n; ++g)
                if (!saved.contains(groups[g].column))
                    saved.append(groups[g].column);
            std::sort(saved.begin(), saved.end());
            QHash<int, double> blockH; // высота сохранённой колонки при минимальной ячейке
            QList<int> savedCol(n);
            for (int g = 0; g < n; ++g)
                savedCol[g] = groups[g].column;
            const auto savedRows = rowsOf(savedCol);
            for (auto it = savedRows.cbegin(); it != savedRows.cend(); ++it)
                for (const QList<int>& row : it.value())
                {
                    int maxRows = 0, hdr = 0;
                    for (int g : row)
                    {
                        maxRows = qMax(maxRows, rows[g]);
                        hdr = qMax(hdr, headerOf(groups[g]));
                    }
                    blockH[it.key()] += hdr + maxRows * (Theme::ReflowCell + gap) + Theme::GroupGapY;
                }

            for (int k = int(saved.size()) - 1; k >= 1; --k)
            {
                QList<double> colH(k, 0.0);
                QHash<int, int> visualOf;
                for (int sc : saved)
                {
                    const int c = int(std::min_element(colH.begin(), colH.end()) - colH.begin());
                    visualOf[sc] = c;
                    colH[c] += blockH[sc];
                }
                QList<int> assign(n);
                for (int g = 0; g < n; ++g)
                    assign[g] = visualOf[groups[g].column];
                if (cellForWidth(assign) >= Theme::ReflowCell || k == 1)
                {
                    m_visualCol = assign;
                    m_reflowed = true;
                    break;
                }
            }
            cell = qMin(cellForWidth(m_visualCol), qMax(cellForHeight(m_visualCol), double(Theme::ReflowCell)));
        }
        Theme::Cell = std::clamp(int(cell), int(Theme::MinCell), int(Theme::MaxCell));
    }
    else
        Theme::Cell = std::clamp(Settings::get().cell, int(Theme::MinCell), int(Theme::MaxCell));

    // Иконки и шрифты — пропорционально ячейке (46 px — «родной» размер).
    Theme::IconSmall = Theme::Cell * 60 / 100;
    Theme::IconMedium = Theme::Cell * 104 / 100;
    Theme::IconLarge = Theme::Cell * 174 / 100;
    Theme::TileFontScaled = std::clamp(qRound(Settings::get().fontPx * Theme::Cell / 46.0), 11, 18);
    m_tileFont.setPixelSize(Theme::TileFontScaled);

    m_rowStart = QList<bool>(n, true);
    int x = Theme::ContentInsetX;
    int maxBottom = 0;
    const auto colRows = rowsOf(m_visualCol);
    for (const Rows& rs : colRows)
    {
        int y = 0;
        int colW = 0;
        for (const QList<int>& row : rs)
        {
            int rx = x;
            int rowH = 0;
            for (int i = 0; i < row.size(); ++i)
            {
                const int gi = row[i];
                m_rowStart[gi] = (i == 0);
                const Group& g = groups[gi];
                const int bodyW = cellsPx(effW[gi]);
                const int bodyH = cellsPx(rows[gi]);
                GroupGeo& geo = m_geo[gi];
                const int hdr = headerOf(g), pad = padOf(g);
                geo.frame = QRect(rx, y, bodyW + 2 * pad, hdr + bodyH + padBottomOf(g));
                geo.header = QRect(rx, y, geo.frame.width(), hdr);
                geo.body = QRect(rx + pad, y + hdr, bodyW, bodyH);
                geo.tiles.clear();
                // Свободные ярлыки: отступ слева сдвигает плитки (в ячейках сетки — ровно как плитки в группах)
                const int shiftX = g.loose ? g.indentL * (Theme::Cell + Theme::CellGap) : 0;
                for (const QRect& cr : packed[gi])
                    geo.tiles.append(QRect(geo.body.x() + shiftX + cr.x() * (Theme::Cell + Theme::CellGap),
                                           geo.body.y() + cr.y() * (Theme::Cell + Theme::CellGap),
                                           cellsPx(cr.width()), cellsPx(cr.height())));
                rx += geo.frame.width() + Theme::GroupGapX;
                rowH = qMax(rowH, geo.frame.height());
            }
            colW = qMax(colW, rx - Theme::GroupGapX - x);
            y += rowH + Theme::GroupGapY;
        }
        if (colW <= 0)
            continue; // в перестроенном виде колонка могла остаться пустой
        m_columns.append({x, colW, y - Theme::GroupGapY});
        maxBottom = qMax(maxBottom, y - Theme::GroupGapY);
        x += colW + Theme::GroupGapX;
    }
    {
        // Столбец из одних свободных ярлыков встаёт по высоте на одну линию с ПЛИТКАМИ соседних групп (слева и справа):
        // верх к верху, по центру или низ к низу — как будто ярлык лежит в такой же группе, только без неё.
        QSet<int> hasGroup;
        for (int g = 0; g < n; ++g)
            if (!groups[g].loose)
                hasGroup.insert(m_visualCol[g]);
        QSet<int> done;
        for (int g = 0; g < n; ++g)
        {
            const int col = m_visualCol[g];
            if (!groups[g].loose || hasGroup.contains(col) || done.contains(col))
                continue;
            done.insert(col);
            // Плитки этого столбца
            int ct = 1 << 30, cb = 0, align = groups[g].alignV;
            for (int k = 0; k < n; ++k)
                if (groups[k].loose && m_visualCol[k] == col)
                    for (const QRect& r : m_geo[k].tiles)
                    {
                        ct = qMin(ct, r.top());
                        cb = qMax(cb, r.bottom() + 1);
                    }
            if (cb == 0)
                continue;
            // Плитки соседних столбцов; нет соседей с группами — все группы
            auto tilesSpan = [&](bool onlyNeighbours, int& top, int& bottom) {
                top = 1 << 30;
                bottom = 0;
                for (int k = 0; k < n; ++k)
                {
                    if (groups[k].loose)
                        continue;
                    if (onlyNeighbours && qAbs(m_visualCol[k] - col) != 1)
                        continue;
                    for (const QRect& r : m_geo[k].tiles)
                    {
                        top = qMin(top, r.top());
                        bottom = qMax(bottom, r.bottom() + 1);
                    }
                }
            };
            int refTop, refBottom;
            tilesSpan(true, refTop, refBottom);
            if (refBottom == 0)
                tilesSpan(false, refTop, refBottom);
            if (refBottom == 0)
                continue;
            const int free = (refBottom - refTop) - (cb - ct);
            int dy = refTop - ct + (free > 0 ? int(free * (align / 2.0)) : 0);
            // Выше верха окна не уезжаем
            int topMost = 1 << 30;
            for (int k = 0; k < n; ++k)
                if (groups[k].loose && m_visualCol[k] == col)
                    topMost = qMin(topMost, m_geo[k].frame.top());
            dy = qMax(dy, -topMost);
            if (dy == 0)
                continue;
            for (int k = 0; k < n; ++k)
                if (groups[k].loose && m_visualCol[k] == col)
                {
                    GroupGeo& geo = m_geo[k];
                    geo.frame.translate(0, dy);
                    geo.header.translate(0, dy);
                    geo.body.translate(0, dy);
                    for (QRect& r : geo.tiles)
                        r.translate(0, dy);
                }
        }
        for (int g = 0; g < n; ++g)
            maxBottom = qMax(maxBottom, m_geo[g].frame.bottom() + 1);
    }
    m_content = QSize(qMax(0, x - Theme::GroupGapX), qMax(0, maxBottom));
    // Номера групп: по колонкам слева направо, внутри — сверху вниз и слева направо.
    QList<int> order(n);
    for (int g = 0; g < n; ++g)
        order[g] = g;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (m_visualCol[a] != m_visualCol[b])
            return m_visualCol[a] < m_visualCol[b];
        if (m_geo[a].frame.top() != m_geo[b].frame.top())
            return m_geo[a].frame.top() < m_geo[b].frame.top();
        return m_geo[a].frame.left() < m_geo[b].frame.left();
    });
    m_number = QList<int>(n);
    int numbered = 0;
    for (int i = 0; i < n; ++i)
        m_number[order[i]] = groups[order[i]].loose ? 0 : ++numbered;
    setScroll(m_scrollX, m_scrollY);
}

void Workspace::finish()
{
    Config::instance().normalize();
    Config::instance().save();
    if (m_folderG >= 0)
    {
        const auto& groups = Config::instance().groups();
        if (m_folderG >= groups.size() || m_folderT >= groups[m_folderG].tiles().size()
            || !groups[m_folderG].tiles()[m_folderT].isFolder)
            m_folderG = m_folderT = -1;
    }
    m_hover = {};
    relayout();
    update();
}

void Workspace::setScroll(int x, int y)
{
    m_scrollX = m_targetX = std::clamp(x, 0, qMax(0, m_content.width() - width()));
    m_scrollY = m_targetY = std::clamp(y, 0, qMax(0, m_content.height() - height()));
    update();
}

void Workspace::resizeEvent(QResizeEvent*)
{
    relayout(); // адаптивная раскладка: плитки и колонки подстраиваются под новый размер
}

// ---------- геометрия ----------

QRect Workspace::titleRect(int g) const
{
    const QRect h = m_geo[g].header;
    const int off = Skin::current().titleOffset();
    return QRect(h.x() + Theme::GroupPad + off, h.y(), h.width() - 2 * Theme::GroupPad - off, h.height());
}

QStringList Workspace::statsLine() const
{
    int tiles = 0, groups = 0;
    for (const Group& g : Config::instance().groups())
    {
        groups += g.loose ? 0 : 1;
        for (const Tab& t : g.tabs)
            tiles += int(t.tiles.size());
    }
    // Склонение: 1 группа, 3 группы, 5 групп.
    auto plural = [](int n, const QString& one, const QString& few, const QString& many) {
        const int k = Lang::pluralIndex(n);
        return k == 0 ? one : k == 1 ? few : many;
    };
    return {QStringLiteral("%1 ").arg(groups) + plural(groups, TXT("ГРУППА"), TXT("ГРУППЫ"), TXT("ГРУПП")),
            QStringLiteral("%1 ").arg(tiles) + plural(tiles, TXT("ПЛИТКА"), TXT("ПЛИТКИ"), TXT("ПЛИТОК"))};
}

void Workspace::emitHoverInfo()
{
    const auto& groups = Config::instance().groups();
    if (m_hover.kind != Hit::Tile || m_hover.g < 0 || m_hover.g >= groups.size() || m_hover.t >= groups[m_hover.g].tiles().size())
    {
        emit hoverInfo({}, {}, {});
        return;
    }
    const Group& g = groups[m_hover.g];
    const Tile& t = g.tiles()[m_hover.t];
    const QRect r = m_geo[m_hover.g].tiles.value(m_hover.t);
    const int step = Theme::Cell + Theme::CellGap;
    const QString size = QStringLiteral("%1×%2").arg((r.width() + Theme::CellGap) / step).arg((r.height() + Theme::CellGap) / step);
    // Где лежит: для папки — сколько в ней ярлыков, для остального — цель ярлыка или путь.
    QString location;
    if (t.isFolder)
        location = TXT("Папка · %1 шт.").arg(t.children.size());
    else
        location = WinShell::locationOf(t.item);
    emit hoverInfo(t.item.name,
                   g.loose ? QStringLiteral("%1 · %2").arg(m_hover.t + 1, 2, 10, QLatin1Char('0')).arg(size)
                           : QStringLiteral("%1 / %2 · %3").arg(g.title().toUpper()).arg(m_hover.t + 1, 2, 10, QLatin1Char('0')).arg(size),
                   location);
}

QString Workspace::tabLabel(int g, int tab) const
{
    const QString& t = Config::instance().groups()[g].tabs[tab].title;
    return t.isEmpty() ? TXT("Без названия") : t;
}

// Вкладки в заголовке: «пилюли» подряд. Первая начинается чуть левее, чтобы её текст
// стоял на одной линии с заголовками обычных групп.
QRect Workspace::tabRect(int g, int tab) const
{
    const QFontMetrics fm(m_titleFont);
    const QRect tr = titleRect(g);
    int x = tr.x() - 8;
    for (int i = 0; i < tab; ++i)
        x += fm.horizontalAdvance(tabLabel(g, i)) + 16 + 4;
    return QRect(x, tr.center().y() - 11, fm.horizontalAdvance(tabLabel(g, tab)) + 16, 22);
}

QRect Workspace::gripRect(int g) const
{
    const QRect f = m_geo[g].frame;
    const int hdr = m_geo[g].header.height();
    return QRect(f.right() - 4, f.top() + hdr, 9, f.height() - hdr);
}

int Workspace::columnAt(int contentX) const
{
    for (int c = 0; c < m_columns.size(); ++c)
        if (contentX < m_columns[c].x + m_columns[c].width + Theme::GroupGapX / 2)
            return c;
    return int(m_columns.size()); // правее всех — новая колонка
}

Workspace::HitInfo Workspace::hitTest(const QPoint& view) const
{
    if (folderOpen())
    {
        const QRect panel = folderPanelRect();
        if (!panel.contains(view))
            return {};
        for (int i = 0; i < openFolder().children.size(); ++i)
            if (folderItemRect(i).contains(view))
                return {Hit::FolderItem, m_folderG, i};
        if (view.y() < panel.top() + 40)
            return {Hit::FolderTitle, m_folderG, m_folderT};
        return {Hit::FolderPanel, m_folderG, m_folderT};
    }

    const QPoint cp = toContent(view);
    const auto& groups = Config::instance().groups();
    for (int g = 0; g < groups.size(); ++g)
    {
        const GroupGeo& geo = m_geo[g];
        if (!groups[g].loose && gripRect(g).contains(cp))
            return {Hit::Grip, g, -1};
        if (!geo.frame.contains(cp))
            continue;
        for (int t = 0; t < geo.tiles.size(); ++t)
            if (geo.tiles[t].contains(cp))
                return {Hit::Tile, g, t};
        if (groups[g].tabs.size() > 1)
            for (int i = 0; i < groups[g].tabs.size(); ++i)
                if (tabRect(g, i).contains(cp))
                    return {Hit::Tab, g, i};
        if (geo.header.contains(cp))
            return {Hit::Header, g, -1};
        return {Hit::Group, g, -1};
    }
    return {};
}

// ---------- папка ----------

bool Workspace::folderOpen() const
{
    return m_folderG >= 0;
}

const Tile& Workspace::openFolder() const
{
    return Config::instance().groups()[m_folderG].tiles()[m_folderT];
}

bool Workspace::closeFolder()
{
    if (!folderOpen())
        return false;
    m_folderG = m_folderT = -1;
    m_hover = {};
    update();
    return true;
}

int Workspace::folderColumns() const
{
    return std::clamp(int(openFolder().children.size()), 1, 5);
}

QRect Workspace::folderPanelRect() const
{
    const int n = int(openFolder().children.size());
    const int cols = folderColumns();
    const int rows = (n + cols - 1) / cols;
    const int pad = 14;
    const int w = cols * mediumPx() + (cols - 1) * Theme::CellGap + 2 * pad;
    const int h = 40 + rows * mediumPx() + (rows - 1) * Theme::CellGap + pad;

    const QRect tile = toView(m_geo[m_folderG].tiles[m_folderT]);
    QRect r(0, 0, w, h);
    r.moveLeft(tile.center().x() - w / 2);
    r.moveTop(tile.bottom() + 8);
    if (r.bottom() > height() - 4)
        r.moveBottom(tile.top() - 8);
    r.moveLeft(std::clamp(r.left(), 4, qMax(4, width() - w - 4)));
    r.moveTop(std::clamp(r.top(), 4, qMax(4, height() - h - 4)));
    return r;
}

QRect Workspace::folderItemRect(int i) const
{
    const QRect panel = folderPanelRect();
    const int cols = folderColumns();
    return QRect(panel.x() + 14 + (i % cols) * (mediumPx() + Theme::CellGap),
                 panel.y() + 40 + (i / cols) * (mediumPx() + Theme::CellGap), mediumPx(), mediumPx());
}

// ---------- рисование ----------

void Workspace::paintIcon(QPainter& p, const Item& item, const QRect& r)
{
    const QPixmap pm = m_icons->get(item, r.width(), devicePixelRatioF());
    if (!pm.isNull())
        p.drawPixmap(r, pm);
}

bool Workspace::labelVisible(const Group& g, const Tile& t)
{
    if (t.label >= 0)
        return t.label == 1;
    if (g.labels >= 0)
        return g.labels == 1;
    return Theme::ShowLabels;
}

void Workspace::paintTile(QPainter& p, const Tile& tile, const QRect& r, bool withBg, qreal hover, bool pressed,
                          bool showLabel)
{
    const qreal radius = Theme::TileRadius;

    // Своё изображение: заполняет плитку целиком вместо фона и иконки.
    if (!tile.image.isEmpty())
    {
        const QPixmap img = tileImage(tile.image, r.size());
        if (!img.isNull())
        {
            p.save();
            p.setRenderHint(QPainter::Antialiasing);
            if (pressed)
            {
                p.translate(r.center());
                p.scale(0.96, 0.96);
                p.translate(-r.center());
            }
            QPainterPath clip;
            clip.addRoundedRect(QRectF(r), radius, radius);
            p.setClipPath(clip);
            p.drawPixmap(r, img);
            if (hover > 0)
                p.fillRect(r, QColor(255, 255, 255, int(28 * hover)));
            if (showLabel && r.height() > Theme::Cell && Theme::Cell >= Theme::LabelMinCell)
            {
                // Подпись поверх картинки — на затемнении снизу, чтобы читалась на любом фото.
                QLinearGradient shade(0, r.bottom() - r.height() * 0.45, 0, r.bottom());
                shade.setColorAt(0, QColor(0, 0, 0, 0));
                shade.setColorAt(1, QColor(0, 0, 0, 150));
                p.fillRect(r, shade);
                const QFont f = Skin::current().font(Skin::Font::Tile);
                const int lh = QFontMetrics(f).height();
                Skin::drawLabel(p, QRect(r.x() + 8, r.bottom() - 7 - 2 * lh, r.width() - 16, 2 * lh), tile.item.name, f,
                                Qt::white, 2, Qt::AlignLeft | Qt::AlignBottom);
            }
            p.setClipping(false);
            p.setPen(QPen(QColor(255, 255, 255, 26), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
            p.restore();
            return;
        }
    }

    TilePaint t;
    t.showLabel = showLabel;
    t.tile = &tile;
    t.r = r;
    t.clean = withBg;
    t.hover = hover;
    t.pressed = pressed;
    const Item& face = tile.isFolder && !tile.children.isEmpty() ? tile.children.first() : tile.item;
    if (Skin::current().kind() == Skin::Bento) // цвет иконки нужен стилю Bento
        t.tint = m_icons->dominantColor(face);

    // Фон плитки из иконки: цвет её подложки или мягкое «размытое» пятно её цветов.
    bool darkLabel = false;
    if (Theme::TileFill > 0)
    {
        const IconCache::Backdrop bd = m_icons->backdrop(face);
        if (bd.plate.isValid())
        {
            const qreal op = Theme::TileFillOpacity / 100.0;
            p.save();
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            if (pressed)
            {
                p.translate(r.center());
                p.scale(0.96, 0.96);
                p.translate(-r.center());
            }
            QPainterPath clip;
            clip.addRoundedRect(QRectF(r), radius, radius);
            p.setClipPath(clip);
            p.setOpacity(op);
            QColor avg = bd.plate; // цвет под подписью — по нему выбираем светлую или тёмную подпись
            if (Theme::TileFill == 2 && !bd.soft.isNull())
            {
                p.drawImage(r, bd.soft);
                if (bd.softBottom.isValid())
                    avg = bd.softBottom;
            }
            else
            {
                QLinearGradient g(r.topLeft(), r.bottomRight());
                g.setColorAt(0, bd.plate.lighter(108));
                g.setColorAt(1, bd.plate.darker(118));
                p.fillRect(r, g);
            }
            p.restore();
            // Светлая заливка (белая подложка, как у Asana) — подпись тёмная, чтобы читалась.
            const QColor panel = Theme::PanelBg.color();
            auto lin = [](int c) { const qreal v = c / 255.0; return v <= 0.04045 ? v / 12.92 : qPow((v + 0.055) / 1.055, 2.4); };
            auto lum = [&](const QColor& c) { return 0.2126 * lin(c.red()) + 0.7152 * lin(c.green()) + 0.0722 * lin(c.blue()); };
            const QColor eff = Skin::mix(panel, avg, op);
            // Какая подпись контрастнее на этом фоне — светлая или тёмная.
            const qreal L = lum(eff);
            const qreal contrastLight = (lum(Theme::TextColor.color()) + 0.05) / (L + 0.05);
            const qreal contrastDark = (L + 0.05) / (lum(QColor(26, 28, 34)) + 0.05);
            darkLabel = contrastDark > contrastLight;
        }
    }
    t.icon = [this](QPainter& pp, const Item& it, const QRect& ir) { paintIcon(pp, it, ir); };
    if (darkLabel)
    {
        const Theme::Rgba saved = Theme::TextColor;
        Theme::TextColor = {26, 28, 34};
        Skin::current().paintTile(p, t);
        Theme::TextColor = saved;
    }
    else
        Skin::current().paintTile(p, t);
}

// Своё изображение плитки: читаем сразу в нужном размере (большое фото не держим в памяти целиком)
// и обрезаем по центру под пропорции плитки.
QPixmap Workspace::tileImage(const QString& path, const QSize& size)
{
    const qreal dpr = devicePixelRatioF();
    const QSize px(qRound(size.width() * dpr), qRound(size.height() * dpr));
    const QString key = path + QLatin1Char('|') + QString::number(px.width()) + QLatin1Char('x') + QString::number(px.height());
    const auto it = m_images.constFind(key);
    if (it != m_images.constEnd())
        return *it;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize src = reader.size();
    QPixmap pm;
    if (src.isValid() && !src.isEmpty())
    {
        const qreal scale = qMax(qreal(px.width()) / src.width(), qreal(px.height()) / src.height());
        const QSize scaled(qMax(1, qCeil(src.width() * scale)), qMax(1, qCeil(src.height() * scale)));
        reader.setScaledSize(scaled);
        const QImage img = reader.read();
        if (!img.isNull())
        {
            pm = QPixmap::fromImage(img.copy((img.width() - px.width()) / 2, (img.height() - px.height()) / 2, px.width(), px.height()));
            pm.setDevicePixelRatio(dpr);
        }
    }
    if (m_images.size() > 64)
        m_images.clear();
    m_images.insert(key, pm);
    return pm;
}

void Workspace::paintEvent(QPaintEvent* e)
{
    static bool first = true;
    if (first)
        WinShell::perfMark("ws_paint");
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto& groups = Config::instance().groups();

    if (groups.isEmpty())
    {
        // Пустое меню (например, после мастера «начать с пустого») — карточка-подсказка по центру.
        const QRect card(rect().center().x() - 290, rect().center().y() - 105, 580, 210);
        QColor border = Theme::TextColor.color();
        border.setAlphaF(0.22f);
        p.setPen(QPen(border, 1.2, Qt::DashLine));
        p.setBrush(Theme::GroupBg.color());
        p.drawRoundedRect(QRectF(card).adjusted(0.5, 0.5, -0.5, -0.5), Theme::GroupRadius + 2, Theme::GroupRadius + 2);
        // «+» в кружке
        const QPointF c(card.center().x(), card.y() + 38);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::Accent.color());
        p.drawEllipse(c, 17, 17);
        p.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(c.x() - 7, c.y()), QPointF(c.x() + 7, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - 7), QPointF(c.x(), c.y() + 7));
        QFont f = Skin::current().font(Skin::Font::GroupTitle);
        f.setPixelSize(16);
        p.setFont(f);
        p.setPen(Theme::TextColor.color());
        p.drawText(QRect(card.x(), card.y() + 62, card.width(), 24), Qt::AlignCenter, TXT("Здесь пока пусто"));
        QFont t = Skin::current().font(Skin::Font::Tile);
        t.setPixelSize(12);
        p.setFont(t);
        p.setPen(Theme::SubText.color());
        p.drawText(QRect(card.x() + 20, card.y() + 90, card.width() - 40, 54), Qt::AlignCenter | Qt::TextWordWrap,
                   TXT("Перетащите сюда приложение или папку из Проводника — появится группа.\n"
                                  "ПКМ — новая группа, начните печатать — поиск."));
        const QRect b = emptyButtonRect();
        p.setPen(Qt::NoPen);
        p.setBrush(m_emptyHover ? Theme::Accent.color() : Theme::ChipBg.color());
        p.drawRoundedRect(QRectF(b), b.height() / 2.0, b.height() / 2.0);
        p.setPen(m_emptyHover ? QColor(Qt::white) : Theme::TextColor.color());
        p.drawText(b, Qt::AlignCenter, TXT("Открыть все приложения"));
    }

    p.save();
    p.translate(-m_scrollX, -m_scrollY);
    const bool dragging = (m_mode == Mode::DragTile || m_mode == Mode::DragGroup);
    {
        QList<QRect> frames;
        for (const GroupGeo& geo : m_geo)
            frames.append(geo.frame);
        Skin::current().paintCanvas(p, QRect(m_scrollX, m_scrollY, width(), height()), QPoint(Theme::ContentInsetX, 0),
                                    Theme::Cell + Theme::CellGap, frames);
    }

    // Что реально перерисовывается (в координатах содержимого) — остальное пропускаем
    const QRect visible = e->rect().translated(m_scrollX, m_scrollY).adjusted(-20, -20, 20, 20);
    for (int g = 0; g < groups.size(); ++g)
    {
        const Group& gr = groups[g];
        const GroupGeo& geo = m_geo[g];
        if (g < m_geo.size() && !geo.frame.adjusted(-16, -16, 16, 16).intersects(visible))
            continue;
        const bool groupHover = (m_hover.g == g && !dragging && !folderOpen() && m_hover.kind != Hit::None
                                 && m_hover.kind != Hit::FolderItem);

        GroupPaint gp;
        gp.group = &gr;
        gp.number = m_number.value(g);
        gp.count = int(gr.tiles().size());
        gp.frame = geo.frame;
        gp.header = geo.header;
        gp.body = geo.body;
        gp.hover = groupHover;
        gp.hoverTab = (m_hover.kind == Hit::Tab && m_hover.g == g) ? m_hover.t : -1;
        gp.tabRect = [this, g](int i) { return tabRect(g, i); };
        if (Skin::current().kind() == Skin::Bento)
        {
            // Цвета «острова» — два самых заметных цвета иконок группы.
            for (const Tile& t : gr.tiles())
            {
                const QColor c = m_icons->dominantColor(t.isFolder && !t.children.isEmpty() ? t.children.first() : t.item);
                if (!c.isValid())
                    continue;
                if (!gp.auto1.isValid())
                    gp.auto1 = c;
                else if (!gp.auto2.isValid() && qAbs(c.hsvHue() - gp.auto1.hsvHue()) > 25)
                    gp.auto2 = c;
            }
        }
        if (!gr.loose)
            Skin::current().paintGroup(p, gp);
        if ((m_hover.kind == Hit::Grip && m_hover.g == g) || (m_mode == Mode::Resize && m_press.g == g))
        {
            const QRect gr2 = gripRect(g);
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::Accent.color());
            p.drawRoundedRect(QRect(gr2.center().x() - 1, gr2.top() + 6, 3, gr2.height() - 12), 1, 1);
        }

        // Плитки
        for (int t = 0; t < geo.tiles.size(); ++t)
        {
            if (!geo.tiles[t].adjusted(-14, -14, 14, 14).intersects(visible))
                continue;
            const bool isDragged = (m_mode == Mode::DragTile && m_dragSrc.child < 0 && m_dragSrc.g == g && m_dragSrc.t == t);
            const bool isGroupDragged = (m_mode == Mode::DragGroup && m_press.g == g);
            p.setOpacity(isDragged || isGroupDragged ? 0.3 : 1.0);
            const bool hover = !dragging && !folderOpen() && m_hover.kind == Hit::Tile && m_hover.g == g && m_hover.t == t;
            const bool pressed = hover && m_mode == Mode::Pressed && m_press == m_hover;
            const bool target = (m_drop.kind == DropKind::IntoFolder && m_drop.g == g && m_drop.index == t);
            const qreal level = target ? 1.0 : m_fade.value(tileKey(g, t), 0.0);
            if (gr.listMode)
            {
                // Строка списка — тем же рисованием, что и «Все приложения»
                const Tile& tile = gr.tiles()[t];
                const Item& face = tile.isFolder && !tile.children.isEmpty() ? tile.children.first() : tile.item;
                ListRowPaint row;
                row.r = geo.tiles[t];
                Item shown = face;
                shown.name = tile.item.name;
                row.item = &shown;
                row.hover = level > 0.01;
                row.pressed = pressed;
                row.icon = [this](QPainter& pp, const Item& it, const QRect& ir) { paintIcon(pp, it, ir); };
                Skin::current().paintListRow(p, row);
            }
            else
                paintTile(p, gr.tiles()[t], geo.tiles[t], gr.clean, level, pressed, labelVisible(gr, gr.tiles()[t]));
            p.setOpacity(1.0);
            if (g == m_focusG && t == m_focusT)
                Skin::current().paintFocus(p, geo.tiles[t]); // выбор с клавиатуры
        }
    }

    // Куда упадёт перетаскиваемое
    if (m_drop.kind != DropKind::None)
    {
        switch (m_drop.kind)
        {
        case DropKind::Insert:
        case DropKind::MoveGroup:
        case DropKind::JoinRow:
            Skin::current().paintInsert(p, m_drop.indicator);
            break;
        case DropKind::IntoFolder:
            Skin::current().paintFolderTarget(p, m_drop.indicator);
            break;
        case DropKind::NewGroup:
            Skin::current().paintNewGroup(p, m_drop.indicator);
            break;
        default:
            break;
        }
    }
    p.restore();

    // Полосы прокрутки
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::ScrollHandle.color());
    if (m_content.height() > height())
    {
        const int h = qMax(30, height() * height() / m_content.height());
        const int y = m_scrollY * (height() - h) / (m_content.height() - height());
        p.drawRoundedRect(QRect(width() - 4, y, 4, h), 2, 2);
    }
    if (m_content.width() > width())
    {
        const int w = qMax(30, width() * width() / m_content.width());
        const int x = m_scrollX * (width() - w) / (m_content.width() - width());
        p.drawRoundedRect(QRect(x, height() - 4, w, 4), 2, 2);
    }

    // Плитка, которую тащим, — под курсором
    if (m_mode == Mode::DragTile)
    {
        p.setOpacity(0.85);
        paintTile(p, sourceTile(m_dragSrc), QRect(m_dragPos - m_dragOffset, m_dragSize), true, 1.0, false);
        p.setOpacity(1.0);
    }

    if (first)
    {
        first = false;
        WinShell::perfMark("ws_done");
    }
    // Открытая папка поверх всего
    if (folderOpen())
    {
        p.fillRect(rect(), Theme::Overlay.color());
        const QRect panel = folderPanelRect();
        p.setPen(QPen(Theme::PanelBorder.color(), 1));
        p.setBrush(Theme::FolderBg.color());
        p.drawRoundedRect(panel, 10, 10);
        const Tile& f = openFolder();
        p.setFont(m_titleFont);
        p.setPen(Theme::TextColor.color());
        p.drawText(QRect(panel.x() + 14, panel.y(), panel.width() - 28, 40), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(m_titleFont).elidedText(f.item.name, Qt::ElideRight, panel.width() - 28));
        for (int i = 0; i < f.children.size(); ++i)
        {
            const bool hover = m_hover.kind == Hit::FolderItem && m_hover.t == i;
            paintTile(p, Tile{f.children[i], TileSize::Medium, false, {}}, folderItemRect(i), false, hover ? 1.0 : 0.0,
                      hover && m_mode == Mode::Pressed);
        }
    }
}

// ---------- мышь ----------

QRect Workspace::emptyButtonRect() const
{
    return QRect(rect().center().x() - 95, rect().center().y() + 52, 190, 32);
}

void Workspace::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QPoint pos = e->position().toPoint();
    if (Config::instance().groups().isEmpty() && emptyButtonRect().contains(pos))
    {
        emit allAppsRequested();
        return;
    }
    const HitInfo h = hitTest(pos);

    if (folderOpen())
    {
        if (h.kind == Hit::FolderItem)
        {
            m_mode = Mode::Pressed;
            m_press = h;
            m_pressPos = pos;
            update();
        }
        else if (h.kind == Hit::None)
            closeFolder();
        return;
    }

    switch (h.kind)
    {
    case Hit::Grip:
        m_mode = Mode::Resize;
        m_press = h;
        m_frozenCell = Theme::Cell;
        m_frozenVisual = m_visualCol;
        m_frozenReflowed = m_reflowed;
        break;
    case Hit::Tile:
    case Hit::Header:
    case Hit::Tab:
        m_mode = Mode::Pressed;
        m_press = h;
        m_pressPos = pos;
        update();
        break;
    default:
        break;
    }
}

void Workspace::mouseMoveEvent(QMouseEvent* e)
{
    if (Config::instance().groups().isEmpty())
    {
        const bool hot = emptyButtonRect().contains(e->position().toPoint());
        if (hot != m_emptyHover)
        {
            m_emptyHover = hot;
            setCursor(hot ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update(emptyButtonRect().adjusted(-2, -2, 2, 2));
        }
    }
    const QPoint pos = e->position().toPoint();
    switch (m_mode)
    {
    case Mode::Resize:
    {
        auto& gr = Config::instance().groups()[m_press.g];
        const QRect frame = m_geo[m_press.g].frame;
        const int cx = toContent(pos).x() - (frame.x() + padOf(gr));
        const int cells = std::clamp((cx + Theme::CellGap + Theme::Cell / 2) / (Theme::Cell + Theme::CellGap),
                                     Theme::GroupMinCells, Theme::GroupMaxCells);
        if (cells != gr.width)
        {
            gr.width = cells;
            relayout();
            update();
        }
        return;
    }
    case Mode::Pressed:
    {
        if ((pos - m_pressPos).manhattanLength() < QApplication::startDragDistance())
            return;
        if (m_press.kind == Hit::Tile)
        {
            m_dragSrc = {m_press.g, m_press.t, -1};
            const QRect r = toView(m_geo[m_press.g].tiles[m_press.t]);
            m_dragOffset = m_pressPos - r.topLeft();
            m_dragSize = r.size();
            m_mode = Mode::DragTile;
        }
        else if (m_press.kind == Hit::FolderItem)
        {
            // Вытаскиваем элемент из папки: папку закрываем, элемент едет за курсором.
            const QRect r = folderItemRect(m_press.t);
            m_dragSrc = {m_folderG, m_folderT, m_press.t};
            m_dragOffset = m_pressPos - r.topLeft();
            m_dragSize = r.size();
            m_folderG = m_folderT = -1;
            m_mode = Mode::DragTile;
        }
        else if (m_press.kind == Hit::Header || m_press.kind == Hit::Tab)
            m_mode = Mode::DragGroup;
        else
            return;
        [[fallthrough]];
    }
    case Mode::DragTile:
    case Mode::DragGroup:
        m_dragPos = pos;
        m_drop = (m_mode == Mode::DragTile) ? computeTileDrop(pos, m_dragSrc) : computeGroupDrop(pos, m_press.g);
        setCursor(Qt::ClosedHandCursor);
        update();
        return;
    case Mode::Idle:
    {
        const HitInfo h = hitTest(pos);
        if (!(h == m_hover))
        {
            // Перерисовываем только старую и новую группу (рамка/уголки/вкладки), плитки — анимацией по кусочкам
            QRegion dirty;
            for (int g : {m_hover.g, h.g})
                if (g >= 0 && g < m_geo.size())
                    dirty += toView(m_geo[g].frame).adjusted(-16, -16, 16, 16);
            m_hover = h;
            startAnimation();
            emitHoverInfo();
            if (dirty.isEmpty())
                update();
            else
                update(dirty);
        }
        switch (h.kind)
        {
        case Hit::Grip: setCursor(Qt::SizeHorCursor); break;
        case Hit::Tile:
        case Hit::Tab:
        case Hit::FolderItem: setCursor(Qt::PointingHandCursor); break;
        default: setCursor(Qt::ArrowCursor); break;
        }
        return;
    }
    }
}

void Workspace::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QPoint pos = e->position().toPoint();
    const Mode mode = m_mode;
    m_mode = Mode::Idle;
    setCursor(Qt::ArrowCursor);

    switch (mode)
    {
    case Mode::Resize:
        m_frozenCell = 0; // ширину выбрали — снова адаптивная ячейка
        finish();
        break;
    case Mode::Pressed:
    {
        const HitInfo h = hitTest(pos);
        if (h == m_press)
        {
            if (h.kind == Hit::Tile)
            {
                const Tile& tile = Config::instance().groups()[h.g].tiles()[h.t];
                if (tile.isFolder)
                {
                    m_folderG = h.g;
                    m_folderT = h.t;
                    m_hover = {};
                }
                else
                    launchItem(tile.item);
            }
            else if (h.kind == Hit::FolderItem)
                launchItem(openFolder().children[h.t]);
            else if (h.kind == Hit::Tab)
            {
                Config::instance().groups()[h.g].activeTab = h.t;
                finish();
            }
        }
        update();
        break;
    }
    case Mode::DragTile:
    {
        const Drop d = m_drop;
        m_drop = {};
        applyTileDrop(d, {sourceTile(m_dragSrc)}, m_dragSrc);
        m_dragSrc = {};
        break;
    }
    case Mode::DragGroup:
    {
        const Drop d = m_drop;
        m_drop = {};
        applyGroupDrop(d, m_press.g);
        break;
    }
    case Mode::Idle:
        break;
    }
}

void Workspace::mouseDoubleClickEvent(QMouseEvent* e)
{
    const HitInfo h = hitTest(e->position().toPoint());
    if (h.kind == Hit::Tab)
    {
        QString name = Config::instance().groups()[h.g].tabs[h.t].title;
        if (askText(TXT("Вкладка"), TXT("Название вкладки:"), name))
        {
            Config::instance().groups()[h.g].tabs[h.t].title = name;
            finish();
        }
    }
    else if (h.kind == Hit::Header)
    {
        auto& gr = Config::instance().groups()[h.g];
        QString name = gr.title();
        if (askText(TXT("Группа"), TXT("Название группы:"), name))
        {
            Config::instance().groups()[h.g].title() = name;
            finish();
        }
    }
    else if (h.kind == Hit::FolderTitle)
    {
        QString name = openFolder().item.name;
        if (askText(TXT("Папка"), TXT("Название папки:"), name) && folderOpen())
        {
            Config::instance().groups()[m_folderG].tiles()[m_folderT].item.name = name;
            finish();
        }
    }
}

void Workspace::leaveEvent(QEvent*)
{
    if (m_mode == Mode::Idle)
    {
        m_hover = {};
        startAnimation();
    }
}

void Workspace::wheelEvent(QWheelEvent* e)
{
    const QPoint px = e->pixelDelta();
    const QPoint ang = e->angleDelta();
    int dy = !px.isNull() ? px.y() : ang.y() * (Theme::Cell + Theme::CellGap) / 60;
    int dx = !px.isNull() ? px.x() : ang.x() * (Theme::Cell + Theme::CellGap) / 60;
    // Если по вертикали прокручивать нечего (или зажат Shift) — колесо листает вбок.
    if (e->modifiers() & Qt::ShiftModifier || m_content.height() <= height())
    {
        dx += dy;
        dy = 0;
    }
    scrollAnimated(m_targetX - dx, m_targetY - dy);
    e->accept();
}

bool Workspace::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip)
    {
        auto* he = static_cast<QHelpEvent*>(e);
        const HitInfo h = hitTest(he->pos());
        QString tip;
        if (h.kind == Hit::Tile)
        {
            const Tile& t = Config::instance().groups()[h.g].tiles()[h.t];
            if (t.isFolder)
            {
                QStringList names;
                for (const Item& c : t.children)
                    names << c.name;
                tip = t.item.name + QStringLiteral(": ") + names.join(QStringLiteral(", "));
            }
            else
                tip = t.item.name + QLatin1Char('\n') + QDir::toNativeSeparators(t.item.path);
        }
        else if (h.kind == Hit::FolderItem)
            tip = QDir::toNativeSeparators(openFolder().children[h.t].path);
        else if (h.kind == Hit::Tab)
            tip = TXT("Клик — открыть вкладку, двойной клик — переименовать");
        else if (h.kind == Hit::Grip)
            tip = TXT("Потяните, чтобы изменить ширину группы");
        else if (h.kind == Hit::Header)
            tip = TXT("Двойной клик — переименовать, перетащите — переместить группу");
        if (tip.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(he->globalPos(), tip, this);
        return true;
    }
    return QWidget::event(e);
}

// ---------- перетаскивание: куда упадёт ----------

Tile Workspace::sourceTile(const Source& src) const
{
    const auto& groups = Config::instance().groups();
    if (src.g < 0 || src.g >= groups.size() || src.t < 0 || src.t >= groups[src.g].tiles().size())
        return {};
    const Tile& t = groups[src.g].tiles()[src.t];
    if (src.child >= 0)
        return src.child < t.children.size() ? Tile{t.children[src.child], TileSize::Medium, false, {}} : Tile{};
    return t;
}

Workspace::Drop Workspace::computeTileDrop(const QPoint& view, const Source& src) const
{
    Drop d;
    const QPoint cp = toContent(view);
    const auto& groups = Config::instance().groups();
    const bool movingFolder = (src.g >= 0 && src.child < 0 && groups[src.g].tiles()[src.t].isFolder);

    for (int g = 0; g < groups.size(); ++g)
    {
        const GroupGeo& geo = m_geo[g];
        for (int t = 0; t < geo.tiles.size(); ++t)
        {
            const QRect r = geo.tiles[t];
            if (!r.contains(cp))
                continue;
            if (src.child < 0 && src.g == g && src.t == t)
                return {}; // над самой собой — ничего не делаем
            if (groups[g].listMode)
            {
                const bool above = cp.y() < r.center().y();
                d.kind = DropKind::Insert;
                d.g = g;
                d.index = above ? t : t + 1;
                d.indicator = QRect(r.left(), above ? r.top() - 3 : r.bottom() + 1, r.width(), 3);
                return d;
            }
            const QRect inner = r.adjusted(r.width() / 4, r.height() / 4, -r.width() / 4, -r.height() / 4);
            if (inner.contains(cp) && !movingFolder)
            {
                d.kind = DropKind::IntoFolder;
                d.g = g;
                d.index = t;
                d.indicator = r;
                return d;
            }
            const bool before = cp.x() < r.center().x();
            d.kind = DropKind::Insert;
            d.g = g;
            d.index = before ? t : t + 1;
            d.indicator = QRect(before ? r.left() - 3 : r.right() + 1, r.top(), 3, r.height());
            return d;
        }
        if (geo.frame.adjusted(-4, -4, 4, 4).contains(cp))
        {
            d.kind = DropKind::Insert;
            d.g = g;
            d.index = int(groups[g].tiles().size());
            if (geo.tiles.isEmpty())
                d.indicator = QRect(geo.body.left(), geo.body.top(), 3, mediumPx());
            else
            {
                const QRect last = geo.tiles.last();
                d.indicator = QRect(last.right() + 1, last.top(), 3, last.height());
            }
            return d;
        }
    }

    // Пустое место — новая группа в этой колонке (или новая колонка справа).
    d.kind = DropKind::NewGroup;
    d.column = columnAt(cp.x());
    int x = 0, y = 0;
    if (d.column < m_columns.size())
    {
        x = m_columns[d.column].x;
        y = m_columns[d.column].bottom + Theme::GroupGapY;
    }
    else if (!m_columns.isEmpty())
        x = m_columns.last().x + m_columns.last().width + Theme::GroupGapX;
    // Свободный ярлык: на пустом месте ляжет сам, рядом с другими свободными ярлыками колонки
    d.indicator = QRect(x, y, mediumPx(), mediumPx());
    return d;
}

Workspace::Drop Workspace::computeGroupDrop(const QPoint& view, int srcGroup) const
{
    Drop d;
    d.kind = DropKind::MoveGroup;
    const QPoint cp = toContent(view);
    const auto& groups = Config::instance().groups();
    d.column = columnAt(cp.x());

    // Правая половина чужой группы — поставить перетаскиваемую справа от неё, в один ряд.
    for (int g = 0; g < groups.size(); ++g)
    {
        if (g == srcGroup)
            continue;
        const QRect f = m_geo[g].frame;
        if (f.adjusted(0, 0, Theme::GroupGapX, 0).contains(cp) && cp.x() > f.center().x())
        {
            d.kind = DropKind::JoinRow;
            d.g = g;
            d.indicator = QRect(f.right() + Theme::GroupGapX / 2 - 1, f.top(), 3, f.height());
            return d;
        }
    }

    if (d.column >= m_columns.size())
    {
        const int x = m_columns.isEmpty() ? Theme::ContentInsetX
                                          : m_columns.last().x + m_columns.last().width + Theme::GroupGapX / 2;
        d.indicator = QRect(x - 1, 0, 3, qMax(m_content.height(), 100));
        return d;
    }

    const ColumnGeo& col = m_columns[d.column];
    int y = col.bottom + Theme::GroupGapY / 2;
    for (int g = 0; g < groups.size(); ++g)
    {
        // Вставка «над» — только перед началом ряда, ряды не разрываем.
        if (m_visualCol.value(g) != d.column || g == srcGroup || !m_rowStart.value(g, true))
            continue;
        if (cp.y() < m_geo[g].frame.center().y())
        {
            d.index = g;
            y = m_geo[g].frame.top() - Theme::GroupGapY / 2;
            break;
        }
    }
    d.indicator = QRect(col.x, y - 1, qMax(col.width, 60), 3);
    return d;
}

// ---------- перетаскивание: применить ----------

void Workspace::applyTileDrop(const Drop& d, QList<Tile> moving, const Source& src)
{
    auto& groups = Config::instance().groups();
    if (d.kind == DropKind::None || moving.isEmpty())
    {
        update();
        return;
    }

    int tg = d.g;
    int ti = d.index;

    // 1. Убираем из старого места
    bool srcGroupMayBeEmpty = false;
    if (src.g >= 0)
    {
        if (src.child >= 0)
            groups[src.g].tiles()[src.t].children.removeAt(src.child);
        else
        {
            groups[src.g].tiles().removeAt(src.t);
            if (d.kind != DropKind::NewGroup && tg == src.g && ti > src.t)
                --ti; // плитка ушла раньше цели — индекс цели сдвинулся
            srcGroupMayBeEmpty = true;
        }
    }

    // 2. Кладём в новое
    switch (d.kind)
    {
    case DropKind::Insert:
        for (const Tile& m : moving)
            groups[tg].tiles().insert(std::clamp(ti++, 0, int(groups[tg].tiles().size())), m);
        break;
    case DropKind::IntoFolder:
    {
        Tile& target = groups[tg].tiles()[ti];
        if (!target.isFolder)
        {
            Tile folder;
            folder.isFolder = true;
            folder.item.name = TXT("Папка");
            folder.size = target.size;
            folder.children = {target.item};
            target = folder;
        }
        for (const Tile& m : moving)
        {
            if (m.isFolder)
                target.children += m.children;
            else
                target.children.append(m.item);
        }
        break;
    }
    case DropKind::NewGroup:
        // Пустое место — свободный ярлык (см. addLooseTiles)
        addLooseTiles(moving, savedColumnFor(d.column));
        break;
    default:
        break;
    }

    // 3. Группа, из которой утащили последнюю плитку, исчезает (как в «Пуске» Windows 10).
    if (srcGroupMayBeEmpty && src.g < groups.size() && groups[src.g].tabs.size() == 1 && groups[src.g].tiles().isEmpty())
    {
        detachRow(groups, src.g);
        groups.removeAt(src.g);
    }

    finish();
}

// Свободные ярлыки ложатся в блок свободных ярлыков этой колонки и сами выстраиваются рядом с остальными;
// нет блока — создаётся новый. Не сохраняет и не перерисовывает — это делает вызывающий (finish()).
void Workspace::addLooseTiles(const QList<Tile>& tiles, int savedColumn)
{
    auto& groups = Config::instance().groups();
    for (Group& g : groups)
        if (g.loose && g.column == savedColumn)
        {
            g.tiles() += tiles;
            return;
        }
    Group ng;
    ng.loose = true;
    ng.clean = true;
    ng.column = savedColumn;
    ng.width = Theme::GroupDefaultCells;
    ng.tiles() = tiles;
    groups.append(ng);
}

void Workspace::applyGroupDrop(const Drop& d, int srcGroup)
{
    auto& groups = Config::instance().groups();
    if (srcGroup < 0 || srcGroup >= groups.size())
    {
        update();
        return;
    }
    if (d.kind == DropKind::JoinRow && d.g >= 0 && d.g < groups.size() && d.g != srcGroup)
    {
        detachRow(groups, srcGroup);
        Group g = groups.takeAt(srcGroup);
        const int target = d.g > srcGroup ? d.g - 1 : d.g;
        g.column = groups[target].column;
        g.joinPrev = true;
        groups.insert(target + 1, g);
        finish();
        return;
    }
    if (d.kind != DropKind::MoveGroup)
    {
        update();
        return;
    }
    const int savedCol = savedColumnFor(d.column);
    detachRow(groups, srcGroup);
    Group g = groups.takeAt(srcGroup);
    g.column = savedCol;
    g.joinPrev = false;
    int insertAt = int(groups.size());
    if (d.index >= 0)
        insertAt = d.index > srcGroup ? d.index - 1 : d.index;
    groups.insert(insertAt, g);
    finish();
}

// ---------- файлы из Проводника ----------

void Workspace::dragEnterEvent(QDragEnterEvent* e)
{
    if (!e->mimeData()->hasUrls() && !e->mimeData()->hasFormat(itemMime()))
        return;
    closeFolder();
    m_drop = computeTileDrop(e->position().toPoint(), {});
    e->acceptProposedAction();
    update();
}

void Workspace::dragMoveEvent(QDragMoveEvent* e)
{
    m_drop = computeTileDrop(e->position().toPoint(), {});
    e->acceptProposedAction();
    update();
}

void Workspace::dragLeaveEvent(QDragLeaveEvent*)
{
    m_drop = {};
    update();
}

void Workspace::dropEvent(QDropEvent* e)
{
    QList<Tile> tiles;
    if (e->mimeData()->hasFormat(itemMime()))
    {
        // Перетащили из «Всех приложений» / поиска
        const QJsonObject o = QJsonDocument::fromJson(e->mimeData()->data(itemMime())).object();
        tiles.append({Item{o["name"].toString(), o["path"].toString(), o["args"].toString(), o["icon"].toString()},
                      TileSize::Medium, false, {}});
    }
    else
        for (const QUrl& url : e->mimeData()->urls())
        {
            if (url.isLocalFile() && QFileInfo::exists(url.toLocalFile()))
                tiles.append({Config::itemFromPath(url.toLocalFile()), TileSize::Medium, false, {}});
            else if (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))
                tiles.append({Config::itemFromUrl(url.toString()), TileSize::Medium, false, {}}); // ссылка из адресной строки браузера
        }

    Drop d = computeTileDrop(e->position().toPoint(), {});
    m_drop = {};
    applyTileDrop(d, tiles, {});
    e->acceptProposedAction();
    window()->activateWindow();
}

// ---------- контекстные меню ----------

void Workspace::contextMenuEvent(QContextMenuEvent* e)
{
    if (m_mode != Mode::Idle)
        return;
    const HitInfo h = hitTest(e->pos());
    if (folderOpen())
    {
        if (h.kind == Hit::FolderItem)
            folderItemMenu(h.t, e->globalPos());
        return;
    }
    switch (h.kind)
    {
    case Hit::Tile: tileMenu(h.g, h.t, e->globalPos()); break;
    case Hit::Tab:
        // ПКМ по вкладке сначала делает её активной — пункты меню относятся к ней.
        Config::instance().groups()[h.g].activeTab = h.t;
        finish();
        groupMenu(h.g, e->globalPos());
        break;
    case Hit::Header:
    case Hit::Grip:
    case Hit::Group:
        if (Config::instance().groups()[h.g].loose)
            looseMenu(h.g, e->globalPos());
        else
            groupMenu(h.g, e->globalPos());
        break;
    default: emptyMenu(e->pos(), e->globalPos()); break;
    }
}

bool Workspace::askLink(Item& out, const QString& current)
{
    // Адрес — из буфера, если там ссылка: обычно её только что скопировали из браузера
    QString url = current;
    if (url.isEmpty())
    {
        const QString clip = QGuiApplication::clipboard()->text().trimmed();
        if (Config::isWebLink(clip) && !clip.contains(QLatin1Char('\n')))
            url = clip;
    }
    if (!askText(TXT("Ссылка на сайт"), TXT("Адрес (например, youtube.com):"), url) || url.trimmed().isEmpty())
        return false;
    out = Config::itemFromUrl(url);
    QString name = out.name;
    if (!askText(TXT("Ссылка на сайт"), TXT("Название:"), name))
        return false;
    if (!name.trimmed().isEmpty())
        out.name = name.trimmed();
    return true;
}

bool Workspace::askText(const QString& title, const QString& label, QString& value)
{
    BusyGuard busy(this);
    bool ok = false;
    const QString v = QInputDialog::getText(this, title, label, QLineEdit::Normal, value, &ok).trimmed();
    if (!ok)
        return false;
    value = v;
    return true;
}

QAction* Workspace::addCheck(QMenu& menu, const QString& text, bool checked)
{
    QAction* a = menu.addAction(text);
    a->setCheckable(true);
    a->setChecked(checked);
    return a;
}

void Workspace::setAllLabels(bool on)
{
    // Общая настройка — в свежем файле настроек (их могло поменять окно настроек);
    // частные выборы групп и плиток сбрасываем, чтобы «у всех» действительно было у всех.
    Config::instance().resetLabelOverrides();
    Config::instance().save();
    auto& s = Settings::get();
    {
        const FileLock lock;
        s.load();
        s.showLabels = on;
        s.save();
    }
    s.apply();
    relayout();
    update();
    if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\StartMenu.App.SettingsReload"))
    {
        SetEvent(ev);
        CloseHandle(ev);
    }
}

void Workspace::launchItem(const Item& item, bool admin)
{
    WinShell::launch(item, admin);
    emit launched();
}

void Workspace::tileMenu(int g, int t, const QPoint& globalPos)
{
    BusyGuard busy(this);
    const Tile tile = Config::instance().groups()[g].tiles()[t]; // копия: меню модальное, но на всякий случай

    QMenu menu(this);
    QAction *open = nullptr, *admin = nullptr, *where = nullptr, *ungroup = nullptr;
    if (!tile.isFolder)
    {
        open = menu.addAction(TXT("Открыть"));
        if (!Config::isWebLink(tile.item.path))
            admin = menu.addAction(TXT("Запуск от имени администратора"));
        if (!WinShell::fileLocation(tile.item).isEmpty())
            where = menu.addAction(TXT("Открыть расположение файла"));
    }
    else
        open = menu.addAction(TXT("Открыть папку"));
    menu.addSeparator();

    QMenu* sizeMenu = menu.addMenu(TXT("Размер"));
    for (int s = 0; s < 4; ++s)
    {
        static const char* dims[] = {"1×1", "2×2", "4×2", "4×4"};
        QAction* a = sizeMenu->addAction(Lang::tr(QString::fromUtf8(kSizeNames[s])) + QStringLiteral("   ") + QString::fromUtf8(dims[s]));
        a->setCheckable(true);
        a->setChecked(int(tile.size) == s);
        a->setData(s);
    }
    const bool labelNow = labelVisible(Config::instance().groups()[g], tile);
    QAction* labelAct = addCheck(menu, TXT("Подпись"), labelNow);
    QAction* setImage = menu.addAction(TXT("Своё изображение…"));
    QAction* clearImage = tile.image.isEmpty() ? nullptr : menu.addAction(TXT("Убрать своё изображение"));
    QAction* rename = menu.addAction(tile.isFolder ? TXT("Переименовать папку…") : TXT("Переименовать…"));
    QAction* editLink = Config::isWebLink(tile.item.path) ? menu.addAction(TXT("Изменить адрес…")) : nullptr;
    if (tile.isFolder)
        ungroup = menu.addAction(TXT("Разгруппировать"));
    menu.addSeparator();
    QAction* remove = menu.addAction(tile.isFolder ? TXT("Удалить папку") : TXT("Открепить"));

    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    auto& tiles = Config::instance().groups()[g].tiles();
    if (chosen == open)
    {
        if (tile.isFolder)
        {
            m_folderG = g;
            m_folderT = t;
            update();
        }
        else
            launchItem(tile.item);
        return;
    }
    if (chosen == admin)
    {
        launchItem(tile.item, true);
        return;
    }
    if (chosen == where)
    {
        WinShell::showInFolder(WinShell::fileLocation(tile.item));
        emit launched();
        return;
    }
    if (chosen == labelAct)
        tiles[t].label = labelNow ? 0 : 1;
    else if (chosen == editLink)
    {
        Item link;
        if (!askLink(link, tile.item.path))
            return;
        tiles[t].item = link;
    }
    else if (chosen == setImage)
    {
        BusyGuard busy2(this);
        QString filter = TXT("Изображения (*.png *.jpg *.jpeg *.webp *.bmp *.gif)");
        // WebP читается только с отдельным модулем Qt — без него такой файл дал бы пустую плитку
        if (!QImageReader::supportedImageFormats().contains("webp"))
            filter.remove(QStringLiteral(" *.webp"));
        const QString f = QFileDialog::getOpenFileName(this, TXT("Изображение для плитки"), QString(), filter);
        if (f.isEmpty())
            return;
        tiles[t].image = QDir::fromNativeSeparators(f);
    }
    else if (chosen == clearImage)
        tiles[t].image.clear();
    else if (chosen->data().isValid())
        tiles[t].size = TileSize(chosen->data().toInt());
    else if (chosen == rename)
    {
        QString name = tile.item.name;
        if (!askText(TXT("Переименовать"), TXT("Название:"), name))
            return;
        tiles[t].item.name = name;
    }
    else if (chosen == ungroup)
    {
        const QList<Item> children = tile.children;
        tiles.removeAt(t);
        for (int i = 0; i < children.size(); ++i)
            tiles.insert(t + i, Tile{children[i], TileSize::Medium, false, {}});
    }
    else if (chosen == remove)
    {
        if (tile.isFolder && !tile.children.isEmpty())
        {
            BusyGuard busy2(this);
            if (QMessageBox::question(this, TXT("Удалить папку"),
                                      TXT("Открепить папку «%1» вместе с содержимым (%2)?")
                                          .arg(tile.item.name).arg(tile.children.size()))
                != QMessageBox::Yes)
                return;
        }
        tiles.removeAt(t);
    }
    finish();
}

void Workspace::looseMenu(int g, const QPoint& globalPos)
{
    BusyGuard busy(this);
    const Group group = Config::instance().groups()[g];
    QMenu menu(this);
    QAction* toGroup = menu.addAction(TXT("Сделать группой"));
    QMenu* addMenu = menu.addMenu(TXT("Добавить"));
    QAction* addFolder = addMenu->addAction(TXT("Папку…"));
    QAction* addLink = addMenu->addAction(TXT("Ссылку на сайт…"));
    QMenu* widthMenu = menu.addMenu(TXT("Ширина"));
    const QList<QPair<int, QString>> widths{{4, TXT("Узкая")}, {8, TXT("Средняя")}, {12, TXT("Широкая")}, {16, TXT("Очень широкая")}};
    for (const auto& w : widths)
    {
        QAction* a = widthMenu->addAction(w.second);
        a->setCheckable(true);
        a->setChecked(group.width == w.first);
        a->setData(w.first);
    }
    // Отступы слева и справа — в долях обычной плитки (2 ячейки сетки = одна плитка)
    static const int kIndents[] = {0, 1, 2, 3, 4, 6, 8};
    static const char* kIndentNames[] = {nullptr, "\u00bd", "1", "1\u00bd", "2", "3", "4"};
    auto addIndentMenu = [&](const QString& title, int base, int current) {
        QMenu* m = menu.addMenu(title);
        for (int k = 0; k < 7; ++k)
        {
            QAction* a = m->addAction(k == 0 ? TXT("Нет") : QString::fromUtf8(kIndentNames[k]));
            a->setCheckable(true);
            a->setChecked(current == kIndents[k]);
            a->setData(base + k);
        }
    };
    addIndentMenu(TXT("Отступ слева, в плитках"), 300, group.indentL);
    addIndentMenu(TXT("Отступ справа, в плитках"), 400, group.indentR);
    // По вертикали — относительно плиток соседних групп
    QMenu* alignMenu = menu.addMenu(TXT("Выравнивание"));
    const QStringList vNames{TXT("Сверху"), TXT("По центру"), TXT("Снизу")};
    for (int k = 0; k < 3; ++k)
    {
        QAction* a = alignMenu->addAction(vNames[k]);
        a->setCheckable(true);
        a->setChecked(group.alignV == k);
        a->setData(200 + k);
    }
    const bool groupLabels = group.labels >= 0 ? group.labels == 1 : Theme::ShowLabels;
    QAction* labels = addCheck(menu, TXT("Подписи плиток"), groupLabels);

    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    auto& groups = Config::instance().groups();
    if (chosen->data().isValid() && chosen->data().toInt() >= 200)
    {
        const int code = chosen->data().toInt();
        if (code < 300)
            groups[g].alignV = code - 200;
        else if (code < 400)
            groups[g].indentL = kIndents[code - 300];
        else
            groups[g].indentR = kIndents[code - 400];
    }
    else if (chosen == toGroup)
    {
        groups[g].loose = false;
        groups[g].clean = false;
        groups[g].title() = TXT("Новая группа");
    }
    else if (chosen == addFolder)
    {
        BusyGuard busy2(this);
        const QString dir = QFileDialog::getExistingDirectory(this, TXT("Папка для группы"));
        if (dir.isEmpty())
            return;
        groups[g].tiles().append({Config::itemFromPath(dir), TileSize::Medium, false, {}});
    }
    else if (chosen == addLink)
    {
        Item link;
        if (!askLink(link))
            return;
        groups[g].tiles().append({link, TileSize::Medium, false, {}});
    }
    else if (chosen == labels)
    {
        groups[g].labels = groupLabels ? 0 : 1;
        for (Tile& tl : groups[g].tiles())
            tl.label = -1;
    }
    else if (chosen->data().isValid())
        groups[g].width = chosen->data().toInt();
    finish();
}

void Workspace::groupMenu(int g, const QPoint& globalPos)
{
    BusyGuard busy(this);
    const Group group = Config::instance().groups()[g];

    QMenu menu(this);
    const bool hasTabs = group.tabs.size() > 1;
    QAction* rename = menu.addAction(hasTabs ? TXT("Переименовать вкладку…") : TXT("Переименовать группу…"));

    // Добавить ▸
    QMenu* addMenu = menu.addMenu(TXT("Добавить"));
    QAction* addFolder = addMenu->addAction(TXT("Папку…"));
    QAction* addLink = addMenu->addAction(TXT("Ссылку на сайт…"));
    addMenu->addSeparator();
    QAction* addTab = addMenu->addAction(TXT("Вкладку"));
    QAction* add = addMenu->addAction(TXT("Группу ниже"));

    // Вид ▸
    QMenu* viewMenu = menu.addMenu(TXT("Вид"));
    QAction* asTiles = addCheck(*viewMenu, TXT("Плитки"), !group.listMode);
    QAction* asList = addCheck(*viewMenu, TXT("Список"), group.listMode);
    viewMenu->addSeparator();
    QAction* filled = addCheck(*viewMenu, TXT("С фоном"), !group.clean);
    QAction* clean = addCheck(*viewMenu, TXT("Без фона"), group.clean);
    QAction* pickColor = viewMenu->addAction(TXT("Цвет фона…"));
    QAction* resetColor = group.color.isValid() ? viewMenu->addAction(TXT("Цвет фона — как в настройках")) : nullptr;
    viewMenu->addSeparator();
    QMenu* widthMenu = viewMenu->addMenu(TXT("Ширина"));
    const QList<QPair<int, QString>> widths{{4, TXT("Узкая")}, {8, TXT("Средняя")}, {12, TXT("Широкая")}, {16, TXT("Очень широкая")}};
    for (const auto& w : widths)
    {
        QAction* a = widthMenu->addAction(w.second);
        a->setCheckable(true);
        a->setChecked(group.width == w.first);
        a->setData(w.first);
    }
    const bool groupLabels = group.labels >= 0 ? group.labels == 1 : Theme::ShowLabels;
    QAction* labels = addCheck(*viewMenu, TXT("Подписи плиток"), groupLabels);

    // Удаление — отдельно, после разделителя
    menu.addSeparator();
    QAction* removeTab = hasTabs ? menu.addAction(TXT("Удалить вкладку «%1»").arg(group.title())) : nullptr;
    QAction* remove = menu.addAction(TXT("Удалить группу"));

    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    auto& groups = Config::instance().groups();
    if (chosen == asList)
        groups[g].listMode = true;
    else if (chosen == asTiles)
        groups[g].listMode = false;
    else if (chosen == addFolder)
    {
        BusyGuard busy2(this);
        const QString dir = QFileDialog::getExistingDirectory(this, TXT("Папка для группы"));
        if (dir.isEmpty())
            return;
        groups[g].tiles().append({Config::itemFromPath(dir), TileSize::Medium, false, {}});
    }
    else if (chosen == addLink)
    {
        Item link;
        if (!askLink(link))
            return;
        groups[g].tiles().append({link, TileSize::Medium, false, {}});
    }
    else if (chosen == labels)
    {
        // Только эта группа; частные выборы её плиток сбрасываем — у группы станет одинаково.
        groups[g].labels = groupLabels ? 0 : 1;
        for (Tab& tab : groups[g].tabs)
            for (Tile& tl : tab.tiles)
                tl.label = -1;
    }
    else if (chosen->data().isValid())
        groups[g].width = chosen->data().toInt();
    else if (chosen == rename)
    {
        QString name = group.title();
        if (!askText(TXT("Группа"), TXT("Название группы:"), name))
            return;
        groups[g].title() = name;
    }
    else if (chosen == addTab)
    {
        groups[g].tabs.append(Tab{TXT("Вкладка %1").arg(group.tabs.size() + 1), {}});
        groups[g].activeTab = int(groups[g].tabs.size()) - 1;
    }
    else if (chosen == removeTab)
    {
        if (!group.tiles().isEmpty())
        {
            BusyGuard busy2(this);
            if (QMessageBox::question(this, TXT("Удалить вкладку"),
                                      TXT("Удалить вкладку «%1» и открепить её плитки (%2)?")
                                          .arg(group.title()).arg(group.tiles().size()))
                != QMessageBox::Yes)
                return;
        }
        groups[g].tabs.removeAt(group.activeTab);
        groups[g].activeTab = qMax(0, group.activeTab - 1);
    }
    else if (chosen == pickColor)
    {
        BusyGuard busy2(this);
        const QColor start = group.color.isValid() ? group.color : Theme::GroupBg.color();
        const QColor c = QColorDialog::getColor(start, this, TXT("Цвет фона группы"), QColorDialog::ShowAlphaChannel);
        if (!c.isValid())
            return;
        groups[g].color = c;
        groups[g].clean = false;
    }
    else if (chosen == resetColor)
        groups[g].color = QColor();
    else if (chosen == filled)
        groups[g].clean = false;
    else if (chosen == clean)
        groups[g].clean = true;
    else if (chosen == add)
    {
        Group ng;
        ng.title() = TXT("Новая группа");
        ng.column = group.column;
        groups.insert(g + 1, ng);
    }
    else if (chosen == remove)
    {
        int count = 0;
        for (const Tab& tab : group.tabs)
            count += int(tab.tiles.size());
        if (count > 0)
        {
            BusyGuard busy2(this);
            if (QMessageBox::question(this, TXT("Удалить группу"),
                                      TXT("Удалить группу «%1» и открепить все её плитки (%2)?")
                                          .arg(group.title()).arg(count))
                != QMessageBox::Yes)
                return;
        }
        detachRow(groups, g);
        groups.removeAt(g);
    }
    finish();
}

void Workspace::emptyMenu(const QPoint& view, const QPoint& globalPos)
{
    BusyGuard busy(this);
    QMenu menu(this);
    QAction* add = menu.addAction(TXT("Новая группа"));
    QAction* addList = menu.addAction(TXT("Новая группа-список"));
    // Папку и ссылку на сайт можно добавить отдельным ярлыком (без группы) или в новую группу
    QMenu* looseMenuAdd = menu.addMenu(TXT("Добавить ярлык"));
    QAction* looseFolder = looseMenuAdd->addAction(TXT("Папку…"));
    QAction* looseLink = looseMenuAdd->addAction(TXT("Ссылку на сайт…"));
    QMenu* groupMenuAdd = menu.addMenu(TXT("Добавить в новую группу"));
    QAction* addFolder = groupMenuAdd->addAction(TXT("Папку…"));
    QAction* addLink = groupMenuAdd->addAction(TXT("Ссылку на сайт…"));
    menu.addSeparator();
    QAction* labels = addCheck(menu, TXT("Подписи во всех группах"), Theme::ShowLabels);
    QAction* settings = menu.addAction(TXT("Настройки меню…"));
    QAction* chosen = menu.exec(globalPos);
    if (chosen == labels)
    {
        setAllLabels(!Theme::ShowLabels);
        return;
    }
    if (chosen == settings)
    {
        WinShell::startSelf(QStringLiteral("--settings"));
        return;
    }
    if (!chosen)
        return;
    if (chosen == looseFolder || chosen == looseLink)
    {
        Tile tile{Item{}, TileSize::Medium, false, {}};
        if (chosen == looseLink)
        {
            if (!askLink(tile.item))
                return;
        }
        else
        {
            BusyGuard busy2(this);
            const QString dir = QFileDialog::getExistingDirectory(this, TXT("Папка для группы"));
            if (dir.isEmpty())
                return;
            tile.item = Config::itemFromPath(dir);
        }
        addLooseTiles({tile}, savedColumnFor(columnAt(toContent(view).x())));
        finish();
        return;
    }
    Group ng;
    ng.title() = TXT("Новая группа");
    if (chosen == addList)
    {
        ng.listMode = true;
        ng.width = 6;
    }
    else if (chosen == addLink)
    {
        Item link;
        if (!askLink(link))
            return;
        ng.title() = TXT("Ссылки");
        ng.tiles().append({link, TileSize::Medium, false, {}});
    }
    else if (chosen == addFolder)
    {
        BusyGuard busy2(this);
        const QString dir = QFileDialog::getExistingDirectory(this, TXT("Папка для группы"));
        if (dir.isEmpty())
            return;
        ng.title() = TXT("Папки");
        ng.tiles().append({Config::itemFromPath(dir), TileSize::Medium, false, {}});
    }
    else if (chosen != add)
        return;
    ng.column = savedColumnFor(columnAt(toContent(view).x()));
    Config::instance().groups().append(ng);
    finish();
}

void Workspace::folderItemMenu(int child, const QPoint& globalPos)
{
    BusyGuard busy(this);
    const int g = m_folderG, t = m_folderT;
    const Item item = openFolder().children[child];

    QMenu menu(this);
    QAction* open = menu.addAction(TXT("Открыть"));
    QAction* admin = menu.addAction(TXT("Запуск от имени администратора"));
    QAction* where = !WinShell::fileLocation(item).isEmpty() ? menu.addAction(TXT("Открыть расположение файла")) : nullptr;
    menu.addSeparator();
    QAction* rename = menu.addAction(TXT("Переименовать…"));
    QAction* takeOut = menu.addAction(TXT("Вынести из папки"));
    QAction* remove = menu.addAction(TXT("Открепить"));

    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    if (chosen == open)
        return launchItem(item);
    if (chosen == admin)
        return launchItem(item, true);
    if (chosen == where)
    {
        WinShell::showInFolder(WinShell::fileLocation(item));
        emit launched();
        return;
    }

    auto& tiles = Config::instance().groups()[g].tiles();
    if (chosen == rename)
    {
        QString name = item.name;
        if (!askText(TXT("Переименовать"), TXT("Название:"), name))
            return;
        tiles[t].children[child].name = name;
    }
    else if (chosen == takeOut)
    {
        tiles[t].children.removeAt(child);
        tiles.insert(t + 1, Tile{item, TileSize::Medium, false, {}});
    }
    else if (chosen == remove)
        tiles[t].children.removeAt(child);
    finish();
}
