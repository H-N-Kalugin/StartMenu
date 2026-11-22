#include "PlayerWindow.h"
#include "Settings.h"
#include "Shell.h"
#include "Skin.h"
#include "StartWindow.h"
#include "Theme.h"

#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

PlayerWindow::PlayerWindow(QWidget* owner)
    : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    hide();
}

QRect PlayerWindow::geometryAbove(const QRect& menu)
{
    return QRect(menu.x(), menu.y() - Gap - PanelHeight, menu.width(), PanelHeight);
}

bool PlayerWindow::fitsAbove(const QRect& menu)
{
    QScreen* screen = QGuiApplication::screenAt(menu.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return geometryAbove(menu).top() >= screen->availableGeometry().top() + 4;
}

void PlayerWindow::prepareBackdrop(const QRect& geo)
{
    m_backdrop = {};
    if (Settings::get().effect != Settings::Effect::Blur)
        return;
    QScreen* screen = QGuiApplication::screenAt(geo.center());
    if (!screen)
        return;
    const QRect sg = screen->geometry();
    const QPixmap shot = screen->grabWindow(0, geo.x() - sg.x(), geo.y() - sg.y(), geo.width(), geo.height());
    m_backdrop = frostedGlass(shot.toImage(), qRound(Settings::get().blurRadius * shot.devicePixelRatio()));
}

void PlayerWindow::applyEffect()
{
    const auto effect = Settings::get().effect;
    // Оттенок стекла рисуем сами (paintPanel) — Windows почти не учитывает его прозрачность
    WinShell::setAcrylic(this, effect == Settings::Effect::Acrylic, Theme::Rgba::from(Theme::PanelBg.color(), 1).color());
    if (effect == Settings::Effect::Acrylic)
        WinShell::setCornerRadius(this, Theme::Radius);
    else
        WinShell::setRoundCorners(this, false);
}

void PlayerWindow::showEvent(QShowEvent* e)
{
    applyEffect();
    QWidget::showEvent(e);
}

void PlayerWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Skin::current().paintPanel(p, rect(), m_backdrop, int(Settings::get().effect));
}
