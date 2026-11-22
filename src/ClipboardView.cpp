#include "ClipboardView.h"
#include "Lang.h"
#include "Glyphs.h"
#include "Demo.h"
#include "Skin.h"
#include "Theme.h"

#include <QContextMenuEvent>
#include <QFileInfo>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QWheelEvent>

namespace
{
    constexpr int kHeader = 40;
    constexpr int kTextRow = 54;
    constexpr int kImageRow = 84;
}

ClipboardView::ClipboardView(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

void ClipboardView::reload()
{
    m_items = ClipboardStore::load();
    // Миниатюры картинок, которых в истории больше нет, — вон (меню «наготове» живёт неделями)
    QSet<QString> images;
    for (const auto& e : m_items)
        if (!e.image.isEmpty())
            images.insert(e.image);
    m_thumbs.removeIf([&images](const auto& it) { return !images.contains(it.key()); });
    // Для отрисовки хватает начала текста: длинные (до 20 000 символов) строки не меряем при каждой перерисовке
    m_previews.clear();
    for (const auto& e : m_items)
        m_previews.append(e.text.left(400));
    m_selected = m_items.isEmpty() ? -1 : 0;
    m_scroll = 0;
    m_hover = -1;
    update();
}

int ClipboardView::rowHeight(int i) const
{
    return m_items[i].type == ClipboardStore::Type::Image ? kImageRow : kTextRow;
}

QRect ClipboardView::rowRect(int i) const
{
    int y = kHeader - m_scroll;
    for (int k = 0; k < i; ++k)
        y += rowHeight(k) + 4;
    return QRect(Theme::ContentInsetX, y, qMin(width() - Theme::ContentInsetX - 8, 900), rowHeight(i));
}

int ClipboardView::contentHeight() const
{
    int h = kHeader;
    for (int i = 0; i < m_items.size(); ++i)
        h += rowHeight(i) + 4;
    return h;
}

QRect ClipboardView::clearRect() const
{
    const int w = qMin(width() - Theme::ContentInsetX - 8, 900);
    return QRect(Theme::ContentInsetX + w - 130, 6, 130, 26);
}

int ClipboardView::rowAt(const QPoint& pos) const
{
    if (pos.y() < kHeader)
        return -1;
    for (int i = 0; i < m_items.size(); ++i)
        if (rowRect(i).contains(pos))
            return i;
    return -1;
}

void ClipboardView::ensureVisible(int i)
{
    const QRect r = rowRect(i).translated(0, m_scroll);
    if (r.top() - kHeader < m_scroll)
        m_scroll = qMax(0, r.top() - kHeader);
    else if (r.bottom() > m_scroll + height() - 8)
        m_scroll = r.bottom() - height() + 8;
}

QPixmap ClipboardView::thumb(const ClipboardStore::Entry& e)
{
    if (const auto it = m_thumbs.constFind(e.image); it != m_thumbs.constEnd())
        return *it;
    const qreal dpr = devicePixelRatioF();
    const QImage img = Demo::active() ? Demo::clipboardImage(e.image) : QImage(ClipboardStore::dir() + QLatin1Char('/') + e.image);
    QPixmap pm;
    if (!img.isNull())
    {
        pm = QPixmap::fromImage(img.scaled(QSize(140, kImageRow - 16) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        pm.setDevicePixelRatio(dpr);
    }
    m_thumbs.insert(e.image, pm);
    return pm;
}

void ClipboardView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const Skin& sk = Skin::current();
    sk.paintListTitle(p, QRect(Theme::ContentInsetX, 0, width() - Theme::ContentInsetX, kHeader - 6), TXT("Буфер обмена"));

    const QColor text = Theme::TextColor.color();
    const QColor sub = Theme::SubText.color();
    if (!m_items.isEmpty())
    {
        const QRect cr = clearRect();
        p.setFont(sk.font(Skin::Font::Meta));
        p.setPen(m_clearHover ? text : sub);
        p.drawText(cr, Qt::AlignRight | Qt::AlignVCenter, TXT("Очистить историю"));
    }
    else
    {
        p.setFont(sk.font(Skin::Font::ListRow));
        p.setPen(sub);
        p.drawText(rect().adjusted(0, kHeader, 0, 0), Qt::AlignCenter,
                   TXT("Пока пусто.\nСкопируйте текст, картинку или файлы — они появятся здесь."));
        return;
    }

    p.setClipRect(QRect(0, kHeader, width(), height() - kHeader));
    const QFont rowFont = sk.font(Skin::Font::ListRow);
    const QFont meta = sk.font(Skin::Font::Meta);
    const QFontMetrics fm(rowFont);
    for (int i = 0; i < m_items.size(); ++i)
    {
        const QRect r = rowRect(i);
        if (r.bottom() < kHeader || r.top() > height())
            continue;
        const ClipboardStore::Entry& e = m_items[i];
        const bool hot = i == m_hover, sel = i == m_selected;
        p.setPen(Qt::NoPen);
        p.setBrush(hot || sel ? Theme::TilePressed.color() : Theme::TileBg.color());
        p.drawRoundedRect(QRectF(r), Theme::TileRadius, Theme::TileRadius);
        if (sel)
        {
            // Выбранная строка — тонкая акцентная полоска слева
            p.setBrush(Theme::Accent.color());
            p.drawRoundedRect(QRectF(r.left(), r.top() + 8, 3, r.height() - 16), 1.5, 1.5);
        }

        // Справа: время и булавка
        const QRect metaR(r.right() - 90, r.top() + 8, 80, 16);
        p.setFont(meta);
        p.setPen(sub);
        p.drawText(metaR, Qt::AlignRight | Qt::AlignVCenter, ClipboardStore::describeAge(e.time));
        if (e.pinned)
        {
            p.setPen(Theme::Accent.color());
            p.drawText(QRect(r.right() - 90, r.bottom() - 22, 80, 16), Qt::AlignRight | Qt::AlignVCenter, TXT("закреплено"));
        }

        const QRect body(r.left() + 14, r.top() + 6, r.width() - 14 - 100, r.height() - 12);
        p.setFont(rowFont);
        p.setPen(text);
        switch (e.type)
        {
        case ClipboardStore::Type::Text:
        {
            // Две строки текста, переносы и табы — пробелами
            QString t = m_previews.value(i);
            t.replace(QLatin1Char('\t'), QLatin1Char(' '));
            const QStringList lines = t.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            const QString l1 = lines.value(0).trimmed();
            QString l2 = lines.size() > 1 ? lines.mid(1).join(QLatin1Char(' ')).simplified() : QString();
            if (l2.isEmpty() && fm.horizontalAdvance(l1) > body.width())
            {
                // Одна длинная строка — переносим на вторую
                Skin::drawLabel(p, body, l1, rowFont, text, 2, Qt::AlignLeft | Qt::AlignVCenter);
                break;
            }
            // Одна строка — по центру записи, две — друг под другом
            const QRect firstLine = l2.isEmpty() ? body : QRect(body.x(), body.y() + 2, body.width(), body.height() / 2);
            p.drawText(firstLine, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(l1, Qt::ElideRight, body.width()));
            if (!l2.isEmpty())
            {
                p.setPen(sub);
                p.drawText(QRect(body.x(), body.center().y(), body.width(), body.height() / 2), Qt::AlignLeft | Qt::AlignVCenter,
                           fm.elidedText(l2, Qt::ElideRight, body.width()));
            }
            break;
        }
        case ClipboardStore::Type::Image:
        {
            const QPixmap pm = thumb(e);
            QRect tr(body.x(), r.top() + 8, 1, r.height() - 16);
            if (!pm.isNull())
            {
                tr.setWidth(int(pm.width() / pm.devicePixelRatio()));
                tr.setHeight(int(pm.height() / pm.devicePixelRatio()));
                tr.moveTop(r.center().y() - tr.height() / 2);
                QPainterPath clip;
                clip.addRoundedRect(QRectF(tr), 4, 4);
                p.save();
                p.setClipPath(clip, Qt::IntersectClip);
                p.drawPixmap(tr, pm);
                p.restore();
            }
            p.setFont(rowFont);
            p.setPen(text);
            p.drawText(QRect(tr.right() + 14, r.top(), body.right() - tr.right() - 14, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       TXT("Картинка  %1×%2").arg(e.size.width()).arg(e.size.height()));
            break;
        }
        case ClipboardStore::Type::Files:
        {
            const QRectF g(body.x(), r.center().y() - 10, 20, 20);
            Glyphs::draw(p, Glyphs::Kind::Folder, g, sub, Theme::PanelBg.color());
            QString names;
            for (const QString& f : e.files)
                names += (names.isEmpty() ? QString() : QStringLiteral(", ")) + QFileInfo(f).fileName();
            const QRect tr(body.x() + 30, body.y(), body.width() - 30, body.height());
            p.setFont(rowFont);
            p.setPen(text);
            p.drawText(QRect(tr.x(), tr.y() + 2, tr.width(), tr.height() / 2), Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(e.files.size() == 1 ? names : TXT("%1 файлов: %2").arg(e.files.size()).arg(names),
                                     Qt::ElideRight, tr.width()));
            p.setPen(sub);
            p.drawText(QRect(tr.x(), tr.center().y(), tr.width(), tr.height() / 2), Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(QFileInfo(e.files.value(0)).absolutePath(), Qt::ElideMiddle, tr.width()));
            break;
        }
        }
    }
}

void ClipboardView::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint pos = e->position().toPoint();
    const int h = rowAt(pos);
    const bool clearHot = !m_items.isEmpty() && clearRect().contains(pos);
    if (h != m_hover || clearHot != m_clearHover)
    {
        m_hover = h;
        m_clearHover = clearHot;
        setCursor(h >= 0 || clearHot ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void ClipboardView::leaveEvent(QEvent*)
{
    m_hover = -1;
    m_clearHover = false;
    update();
}

void ClipboardView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QPoint pos = e->position().toPoint();
    if (!m_items.isEmpty() && clearRect().contains(pos))
    {
        ClipboardStore::clearUnpinned();
        reload();
        return;
    }
    const int i = rowAt(pos);
    if (i >= 0)
        emit pasteRequested(m_items[i]);
}

void ClipboardView::wheelEvent(QWheelEvent* e)
{
    const int maxScroll = qMax(0, contentHeight() - height() + 8);
    m_scroll = std::clamp(m_scroll - e->angleDelta().y() / 2, 0, maxScroll);
    update();
}

void ClipboardView::moveSelection(int dy)
{
    if (m_items.isEmpty())
        return;
    m_selected = std::clamp(m_selected + dy, 0, int(m_items.size()) - 1);
    ensureVisible(m_selected);
    update();
}

void ClipboardView::activateSelected()
{
    if (m_selected >= 0 && m_selected < m_items.size())
        emit pasteRequested(m_items[m_selected]);
}

void ClipboardView::contextMenuEvent(QContextMenuEvent* e)
{
    const int i = rowAt(e->pos());
    if (i < 0)
        return;
    QMenu menu(this);
    QAction* paste = menu.addAction(TXT("Вставить"));
    QAction* copy = menu.addAction(TXT("Только скопировать"));
    QAction* pin = menu.addAction(m_items[i].pinned ? TXT("Открепить") : TXT("Закрепить"));
    menu.addSeparator();
    QAction* del = menu.addAction(TXT("Удалить"));
    emit busyChanged(true);
    QAction* chosen = menu.exec(e->globalPos());
    emit busyChanged(false);
    if (chosen == paste)
        emit pasteRequested(m_items[i]);
    else if (chosen == copy)
        ClipboardStore::putToClipboard(m_items[i]);
    else if (chosen == pin)
    {
        ClipboardStore::togglePin(m_items[i]);
        reload();
    }
    else if (chosen == del)
    {
        ClipboardStore::remove(m_items[i]);
        reload();
    }
}
