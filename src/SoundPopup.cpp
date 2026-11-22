#include "SoundPopup.h"
#include "Lang.h"
#include "Glyphs.h"
#include "Shell.h"
#include "Skin.h"
#include "Theme.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>

namespace
{
    constexpr int kWidth = 300;
    constexpr int kPad = 12;
    constexpr int kTop = 34;       // заголовок «Звук»
    constexpr int kSliderRow = 40;
    constexpr int kDevRow = 32;
}

SoundPopup::SoundPopup(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    reload();
}

void SoundPopup::reload()
{
    m_devices = Audio::outputs();
    m_volume = Audio::volume();
    m_muted = Audio::muted();
    const int devices = m_devices.size() > 1 ? int(m_devices.size()) : 0; // одно устройство — выбирать нечего
    setFixedSize(kWidth, kTop + kSliderRow + (devices ? 26 + devices * kDevRow : 0) + kPad);
    update();
}

void SoundPopup::popupAt(const QPoint& globalTopLeft)
{
    QPoint pos = globalTopLeft;
    if (QScreen* s = QGuiApplication::screenAt(globalTopLeft))
    {
        const QRect a = s->availableGeometry();
        pos.setY(qMin(pos.y(), a.bottom() - height() - 8));
        pos.setX(qMin(pos.x(), a.right() - width() - 8));
    }
    move(pos);
    show();
    setFocus();
}

void SoundPopup::showEvent(QShowEvent* e)
{
    WinShell::setAcrylic(this, true, Theme::Rgba::from(Theme::MenuBg.color(), 170).color());
    WinShell::setRoundCorners(this, true);
    QWidget::showEvent(e);
}

void SoundPopup::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    emit closed();
}

QRect SoundPopup::muteRect() const { return QRect(kPad - 4, kTop + (kSliderRow - 30) / 2, 30, 30); }
QRect SoundPopup::sliderRect() const { return QRect(kPad + 36, kTop + kSliderRow / 2 - 8, kWidth - kPad * 2 - 36 - 44, 16); }
QRect SoundPopup::deviceRect(int i) const { return QRect(6, kTop + kSliderRow + 26 + i * kDevRow, kWidth - 12, kDevRow - 2); }

void SoundPopup::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor text = Theme::TextColor.color(), sub = Theme::SubText.color(), acc = Theme::Accent.color();
    const Skin& sk = Skin::current();
    QFont f = sk.font(Skin::Font::ListRow);
    p.setFont(f);
    p.setPen(sub);
    p.drawText(QRect(kPad, 0, kWidth - 2 * kPad, kTop), Qt::AlignLeft | Qt::AlignVCenter, TXT("Звук"));

    // «Без звука» и громкость
    const QRect mr = muteRect();
    if (m_hover == -2)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::TileHover.color());
        p.drawRoundedRect(QRectF(mr), 6, 6);
    }
    Glyphs::draw(p, m_muted ? Glyphs::Kind::VolumeMute : Glyphs::Kind::Volume, QRectF(mr).adjusted(5, 5, -5, -5), text,
                 Theme::PanelBg.color());
    const QRect sr = sliderRect();
    const qreal cy = sr.center().y() + 0.5;
    const qreal x = sr.left() + sr.width() * m_volume;
    QColor track = text;
    track.setAlphaF(0.18f);
    p.setPen(QPen(track, 4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(sr.left(), cy), QPointF(sr.right(), cy));
    p.setPen(QPen(m_muted ? sub : acc, 4, Qt::SolidLine, Qt::RoundCap));
    if (x > sr.left() + 1)
        p.drawLine(QPointF(sr.left(), cy), QPointF(x, cy));
    p.setPen(Qt::NoPen);
    p.setBrush(m_muted ? sub : text);
    p.drawEllipse(QPointF(x, cy), m_dragging ? 8 : 7, m_dragging ? 8 : 7);
    p.setPen(text);
    p.drawText(QRect(sr.right() + 10, sr.top() - 4, 40, sr.height() + 8), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(qRound(m_volume * 100)));

    if (m_devices.size() < 2)
        return;
    QColor line = text;
    line.setAlphaF(0.08f);
    p.fillRect(QRect(kPad, kTop + kSliderRow + 6, kWidth - 2 * kPad, 1), line);
    QFont metaFont = sk.font(Skin::Font::Meta);
    p.setFont(metaFont);
    p.setPen(sub);
    p.drawText(QRect(kPad, kTop + kSliderRow + 8, kWidth, 18), Qt::AlignLeft | Qt::AlignVCenter, TXT("Вывод"));
    const QFontMetrics fm(f);
    for (int i = 0; i < m_devices.size(); ++i)
    {
        const QRect r = deviceRect(i);
        if (i == m_hover)
        {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::TileHover.color());
            p.drawRoundedRect(QRectF(r), 6, 6);
        }
        // Кружок-переключатель: у выбранного — акцентная точка
        const QPointF c(r.left() + 16, r.center().y() + 0.5);
        p.setPen(QPen(m_devices[i].isDefault ? acc : sub, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 6, 6);
        if (m_devices[i].isDefault)
        {
            p.setPen(Qt::NoPen);
            p.setBrush(acc);
            p.drawEllipse(c, 3, 3);
        }
        p.setFont(f);
        p.setPen(m_devices[i].isDefault ? text : sub);
        const QRect tr(r.left() + 32, r.top(), r.width() - 40, r.height());
        p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(m_devices[i].name, Qt::ElideRight, tr.width()));
    }
}

void SoundPopup::setFromX(int x)
{
    const QRect sr = sliderRect();
    m_volume = qBound(0.0f, float(x - sr.left()) / float(qMax(1, sr.width())), 1.0f);
    Audio::setVolume(m_volume);
    if (m_muted && m_volume > 0)
    {
        m_muted = false;
        Audio::setMuted(false);
    }
    update();
}

void SoundPopup::mousePressEvent(QMouseEvent* e)
{
    const QPoint pos = e->position().toPoint();
    if (!rect().contains(pos))
    {
        close();
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (sliderRect().adjusted(-8, -10, 8, 10).contains(pos))
    {
        m_dragging = true;
        setFromX(pos.x());
    }
}

void SoundPopup::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint pos = e->position().toPoint();
    if (m_dragging)
    {
        setFromX(pos.x());
        return;
    }
    int h = muteRect().contains(pos) ? -2 : -1;
    for (int i = 0; m_devices.size() > 1 && i < m_devices.size(); ++i)
        if (deviceRect(i).contains(pos))
            h = i;
    if (h != m_hover)
    {
        m_hover = h;
        setCursor(h != -1 || sliderRect().adjusted(-8, -10, 8, 10).contains(pos) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void SoundPopup::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_dragging)
    {
        m_dragging = false;
        update();
        return;
    }
    const QPoint pos = e->position().toPoint();
    if (muteRect().contains(pos))
    {
        m_muted = !m_muted;
        Audio::setMuted(m_muted);
        update();
        return;
    }
    for (int i = 0; m_devices.size() > 1 && i < m_devices.size(); ++i)
        if (deviceRect(i).contains(pos) && !m_devices[i].isDefault)
        {
            Audio::setDefault(m_devices[i].id);
            reload(); // громкость у нового устройства своя
            return;
        }
}

void SoundPopup::wheelEvent(QWheelEvent* e)
{
    m_volume = qBound(0.0f, m_volume + (e->angleDelta().y() > 0 ? 0.02f : -0.02f), 1.0f);
    Audio::setVolume(m_volume);
    update();
}

void SoundPopup::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape)
        close();
    else if (e->key() == Qt::Key_Right || e->key() == Qt::Key_Up)
    {
        m_volume = qMin(1.0f, m_volume + 0.02f);
        Audio::setVolume(m_volume);
        update();
    }
    else if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Down)
    {
        m_volume = qMax(0.0f, m_volume - 0.02f);
        Audio::setVolume(m_volume);
        update();
    }
}
