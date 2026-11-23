#include "StartWindow.h"
#include "Lang.h"
#include "AppList.h"
#include "IconCache.h"
#include "ClipboardStore.h"
#include "ClipboardView.h"
#include "Demo.h"
#include "MediaPlayer.h"
#include "SoundPopup.h"
#include "SystemStats.h"
#include "PlayerWindow.h"
#include "SearchExtras.h"
#include "Settings.h"
#include <cmath>
#include "Skin.h"
#include "Shell.h"
#include "Sidebar.h"
#include "Theme.h"
#include "Updater.h"
#include "WhatsNew.h"
#include "Workspace.h"

#include <QApplication>
#include <QCursor>
#include <QFile>
#include <QGuiApplication>
#include <QDrag>
#include <QJsonDocument>
#include <QJsonObject>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QKeyEvent>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <thread>
#include <QMimeData>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QCloseEvent>
#include <QLineEdit>
#include <QContextMenuEvent>
#include <QWheelEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QMessageBox>
#include <QInputDialog>
#include <QScreen>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QLabel>
#include <QWidgetAction>
#include <QVariantAnimation>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#undef small // rpcndr.h объявляет макрос small — мешает обычным именам переменных
#include <powrprof.h>
#include <psapi.h>
#endif

namespace
{
    // Размытие по прямоугольнику в одну сторону (скользящая сумма) — быстро при любом радиусе.
    void boxBlurLine(QRgb* line, int count, int stride, int r, QList<QRgb>& tmp)
    {
        tmp.resize(count);
        for (int i = 0; i < count; ++i)
            tmp[i] = line[i * stride];
        int sa = 0, sr = 0, sg = 0, sb = 0;
        const int window = 2 * r + 1;
        auto at = [&](int i) { return tmp[std::clamp(i, 0, count - 1)]; };
        for (int i = -r; i <= r; ++i)
        {
            const QRgb c = at(i);
            sa += qAlpha(c); sr += qRed(c); sg += qGreen(c); sb += qBlue(c);
        }
        for (int i = 0; i < count; ++i)
        {
            line[i * stride] = qRgba(sr / window, sg / window, sb / window, sa / window);
            const QRgb out = at(i - r), in = at(i + r + 1);
            sa += qAlpha(in) - qAlpha(out); sr += qRed(in) - qRed(out);
            sg += qGreen(in) - qGreen(out); sb += qBlue(in) - qBlue(out);
        }
    }

    // Матовое стекло своими силами: уменьшаем снимок в 4 раза, три прохода box blur
    // (в сумме почти гаусс) и растягиваем обратно — мягко и быстро.
    QImage frostedGlassImpl(const QImage& src, int radius)
    {
        if (src.isNull() || radius <= 0)
            return src;
        const int factor = 4;
        QImage small = src.scaled(qMax(1, src.width() / factor), qMax(1, src.height() / factor), Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const int r = qMax(1, radius / factor);
        QList<QRgb> tmp;
        for (int pass = 0; pass < 3; ++pass)
        {
            for (int y = 0; y < small.height(); ++y)
                boxBlurLine(reinterpret_cast<QRgb*>(small.scanLine(y)), small.width(), 1, r, tmp);
            auto* bits = reinterpret_cast<QRgb*>(small.bits());
            const int stride = int(small.bytesPerLine() / 4);
            for (int x = 0; x < small.width(); ++x)
                boxBlurLine(bits + x, small.height(), stride, r, tmp);
        }
        return small.scaled(src.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
}

QImage frostedGlass(const QImage& src, int radius)
{
    return frostedGlassImpl(src, radius);
}

StartWindow::StartWindow(bool preview, bool persistent)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), m_preview(preview), m_persistent(persistent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);              // чтобы курсор менялся над краями окна
    qApp->installEventFilter(this);      // края перехватываем раньше дочерних виджетов

    m_icons = new IconCache(this);
    m_sidebar = new Sidebar(this);
    m_workspace = new Workspace(m_icons, this);
    m_list = new AppList(m_icons, this);
    m_list->hide();
    m_clip = new ClipboardView(this);
    m_clip->hide();
    connect(m_clip, &ClipboardView::busyChanged, this, &StartWindow::setBusy);
    connect(m_clip, &ClipboardView::pasteRequested, this, &StartWindow::pasteFromHistory);
    // Монитор системы: замер раз в секунду, только пока меню на экране
    if (Settings::get().systemMonitor)
        m_stats = std::make_unique<SystemStats>(); // счётчики подключаются в фоне, пока меню готовится
    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, &StartWindow::sampleStats);
    // Мини-плеер справа от поиска: виден, только когда в Windows что-то играет.
    m_playerWin = new PlayerWindow(this);
    m_media = new MediaPlayer(this);
    m_media->setWindowShown(false); // окно ещё не на экране (у «наготове» — до первого Win)
    connect(m_media, &MediaPlayer::availabilityChanged, this, [this] { layoutChildren(); });
    connect(m_media, &MediaPlayer::busyChanged, this, &StartWindow::setBusy);

    auto close = [this] {
        if (Settings::get().closeOnLaunch && !m_preview)
        {
            logClose(QStringLiteral("запуск"));
            this->close();
        }
    };
    connect(m_workspace, &Workspace::launched, this, close);
    connect(m_list, &AppList::launched, this, close);
    connect(m_sidebar, &Sidebar::launched, this, close);
    connect(m_workspace, &Workspace::busyChanged, this, &StartWindow::setBusy);
    connect(m_list, &AppList::busyChanged, this, &StartWindow::setBusy);
    connect(m_sidebar, &Sidebar::busyChanged, this, &StartWindow::setBusy);
    connect(m_sidebar, &Sidebar::tilesRequested, this, [this] {
        m_query.clear();
        setView(View::Tiles);
    });
    connect(m_workspace, &Workspace::allAppsRequested, this, [this] {
        m_query.clear();
        setView(View::AllApps);
    });
    connect(m_sidebar, &Sidebar::allAppsRequested, this, [this] {
        m_query.clear();
        setView(View::AllApps);
    });
    connect(m_sidebar, &Sidebar::powerRequested, this, &StartWindow::powerMenu);
    connect(m_sidebar, &Sidebar::clipboardRequested, this, [this] {
        m_query.clear();
        setView(m_view == View::Clipboard ? View::Tiles : View::Clipboard);
    });
    connect(m_sidebar, &Sidebar::soundRequested, this, [this](const QPoint& pos) {
        auto* popup = new SoundPopup(this);
        setBusy(true); // всплывающее окно забирает фокус — меню не должно от этого закрыться
        connect(popup, &SoundPopup::closed, this, [this] {
            setBusy(false);
            m_sidebar->relayout(); // значок звука (выключен/включён)
            m_sidebar->update();
        });
        popup->popupAt(pos);
    });
    connect(m_list, &AppList::dragRequested, this, &StartWindow::startDragToTiles);
    connect(m_workspace, &Workspace::hoverInfo, this, [this](const QString& name, const QString& path, const QString& location) {
        m_statusName = name;
        m_statusPath = path;
        m_statusLocation = location;
        if (Skin::current().statusBarHeight() > 0)
            update(0, height() - Skin::current().statusBarHeight(), width(), Skin::current().statusBarHeight());
    });


    applyStyle();

    // Мигающий курсор в поле поиска (только пока что-то набрано).
    m_caretTimer = new QTimer(this);
    m_caretTimer->setInterval(530);
    connect(m_caretTimer, &QTimer::timeout, this, [this] {
        m_caretOn = !m_caretOn;
        update(m_searchRect.adjusted(-4, -4, 4, 4));
    });

    // Подстраховка к WindowDeactivate: раз в 250 мс проверяем, у кого фокус.
    m_focusTimer = new QTimer(this);
    m_focusTimer->setInterval(250);
    connect(m_focusTimer, &QTimer::timeout, this, &StartWindow::checkFocus);
}

StartWindow::~StartWindow() = default; // здесь, где SystemStats уже известен целиком

void StartWindow::applyStyle()
{
    // Контекстные меню наследуют этот стиль. setStyleSheet заново оформляет все дочерние виджеты (заметная
    // пауза на каждом показе «наготове») — поэтому применяем только изменившийся.
    const QString css = QStringLiteral(R"(
        QMenu { background: %1; color: %2; border: 1px solid rgba(255,255,255,30); border-radius: 8px; padding: 5px; }
        QMenu::item { padding: 7px 22px; border-radius: 5px; }
        QMenu::item:selected { background: %3; }
        QMenu::separator { height: 1px; background: rgba(255,255,255,25); margin: 4px 8px; }
        QMenu::indicator { width: 12px; height: 12px; left: 6px; }
    )")
                      .arg(Theme::Rgba::from(Theme::MenuBg.color(), 150).css(), Theme::TextColor.css(), Theme::TilePressed.css());
    if (css != styleSheet())
        setStyleSheet(css);
}

QRect StartWindow::targetGeometry() const
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect r = screen->availableGeometry();
    const int w = qMin(Theme::WindowWidth, r.width() - 2 * Theme::BottomGap);
    const int h = qMin(Theme::WindowHeight, r.height() - 2 * Theme::BottomGap);
    const int x = Settings::get().position == Settings::Position::Left ? r.left() + Theme::BottomGap
                                                                      : r.center().x() - w / 2;
    return QRect(x, r.bottom() + 1 - h - Theme::BottomGap, w, h);
}

void StartWindow::grabBackdrop(const QRect& geo)
{
    m_backdrop = {};
    if (Settings::get().effect != Settings::Effect::Blur)
        return;
    QScreen* screen = QGuiApplication::screenAt(geo.center());
    if (!screen)
        return;
    // Снимок экрана под будущим окном (самого окна в этот момент на экране нет), потом размываем.
    const QRect sg = screen->geometry();
    const QPixmap shot = screen->grabWindow(0, geo.x() - sg.x(), geo.y() - sg.y(), geo.width(), geo.height());
    m_backdrop = frostedGlassImpl(shot.toImage(), qRound(Settings::get().blurRadius * shot.devicePixelRatio()));
}

void StartWindow::applyEffect()
{
    const auto effect = Settings::get().effect;
    // Системное стекло рисует Windows; углы тогда скругляет тоже Windows (Win11), а мы не рисуем подложку.
    // Оттенок стекла рисуем сами (paintPanel) — Windows почти не учитывает его прозрачность
    WinShell::setAcrylic(this, effect == Settings::Effect::Acrylic, Theme::Rgba::from(Theme::PanelBg.color(), 1).color());
    // Стекло рисует Windows — углы тоже её: 0 / малые / обычные по настройке «Скругление углов».
    if (effect == Settings::Effect::Acrylic)
        WinShell::setCornerRadius(this, Theme::Radius);
    else
        WinShell::setRoundCorners(this, false);
}

// Панель плеера живёт над меню: та же ширина, двигается и проявляется вместе с ним.
void StartWindow::syncPlayer()
{
    if (!m_playerWin)
        return;
    if (m_panelMode && isVisible())
    {
        m_playerWin->setGeometry(PlayerWindow::geometryAbove(geometry()));
        m_playerWin->setWindowOpacity(windowOpacity());
        if (!m_playerWin->isVisible())
            m_playerWin->show();
        m_playerWin->update();
    }
    else if (m_playerWin->isVisible())
        m_playerWin->hide();
}

void StartWindow::paintLayers(QPainter& p)
{
    const auto& layers = Config::instance().layouts();
    const int active = Config::instance().activeLayout();
    QFont f = Skin::current().font(Skin::Font::Meta);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(f);
    const qreal rad = qMin(Theme::TileRadius, 8) * 1.0;
    for (int i = 0; i < m_layerRects.size(); ++i)
    {
        const QRect r = m_layerRects[i];
        const bool on = i == active, hot = i == m_layerHover;
        p.setPen(Qt::NoPen);
        p.setBrush(on ? Theme::Accent.color() : hot ? Theme::TilePressed.color() : Theme::ChipBg.color());
        p.drawRoundedRect(QRectF(r), rad, rad);
        // Текст на акценте — белый или тёмный, что контрастнее
        const QColor a = Theme::Accent.color();
        const bool darkText = on && (0.2126 * a.redF() + 0.7152 * a.greenF() + 0.0722 * a.blueF()) > 0.62;
        p.setPen(on ? (darkText ? QColor(0x10, 0x12, 0x18) : QColor(Qt::white)) : Theme::TextColor.color());
        const QString label = layers[i].name.isEmpty() ? QString::number(i + 1)
                                                       : QFontMetrics(f).elidedText(layers[i].name, Qt::ElideRight, 90);
        p.drawText(r, Qt::AlignCenter, label);
    }
    // «+» — добавить слой
    const QRect a = m_layerAdd;
    if (m_layerHover == -2)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::TilePressed.color());
        p.drawRoundedRect(QRectF(a), rad, rad);
    }
    else
    {
        QColor b = Theme::TextColor.color();
        b.setAlphaF(0.18f);
        p.setPen(QPen(b, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(a).adjusted(0.5, 0.5, -0.5, -0.5), rad, rad);
    }
    p.setPen(QPen(Theme::SubText.color(), 1.5, Qt::SolidLine, Qt::RoundCap));
    const QPointF c(a.left() + a.height() / 2.0 + 1, a.center().y() + 0.5);
    p.drawLine(QPointF(c.x() - 4, c.y()), QPointF(c.x() + 4, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - 4), QPointF(c.x(), c.y() + 4));
    p.setPen(Theme::SubText.color());
    p.drawText(a.adjusted(a.height() - 2, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter, TXT("Слой"));
    p.restore();
}

int StartWindow::layerAt(const QPoint& pos) const
{
    for (int i = 0; i < m_layerRects.size(); ++i)
        if (m_layerRects[i].contains(pos))
            return i;
    return m_layerAdd.contains(pos) ? -2 : -1;
}

void StartWindow::switchLayer(int i)
{
    auto& cfg = Config::instance();
    if (i < 0 || i >= cfg.layouts().size() || i == cfg.activeLayout())
        return;
    cfg.setActiveLayout(i);
    cfg.save();
    m_query.clear();
    setView(View::Tiles);
    layoutChildren();
    update();
}

void StartWindow::addLayer()
{
    auto& cfg = Config::instance();
    // У первого слоя появляется номер, у нового — «Слой N»
    cfg.layouts().append({TXT("Слой %1").arg(cfg.layouts().size() + 1), {}});
    cfg.setActiveLayout(int(cfg.layouts().size()) - 1);
    cfg.save();
    m_query.clear();
    setView(View::Tiles);
    layoutChildren();
    update();
}

QRect StartWindow::updateRect() const
{
    QFont f = Skin::current().font(Skin::Font::Meta);
    f.setPixelSize(11);
    const QString text = m_updateBusy ? TXT("Скачиваю установщик…") : QStringLiteral("v") + Updater::currentVersion();
    const int w = QFontMetrics(f).horizontalAdvance(text) + 8 + (m_updateNew ? 22 : 0); // слева место под кружок
    return QRect(width() - Theme::ContentMargin - w, height() - 22, w, 18);
}

// После обновления программы при первом открытии меню — небольшая карточка с тем, что изменилось.
void StartWindow::maybeShowWhatsNew(bool force)
{
    const QString cur = Updater::currentVersion();
    if (!force && Settings::get().lastSeenVersion == cur)
        return;
    if (!force && !Settings::get().showWhatsNew)
    {
        // Человек отключил карточку — версию просто запоминаем
        Settings::get().lastSeenVersion = cur;
        Settings::get().save();
        return;
    }
    const QStringList bullets = WhatsNew::bulletsFor(cur);
    if (bullets.isEmpty())
    {
        // Для этой версии показывать нечего — просто запоминаем, чтобы не проверять каждый раз
        if (!force)
        {
            Settings::get().lastSeenVersion = cur;
            Settings::get().save();
        }
        return;
    }
    if (!m_whatsNew)
    {
        m_whatsNew = new WhatsNewCard(this);
        connect(m_whatsNew, &WhatsNewCard::dismissed, this, [this, force](bool dontShowAgain) {
            if (!force)
            {
                Settings::get().lastSeenVersion = Updater::currentVersion();
                if (dontShowAgain)
                    Settings::get().showWhatsNew = false; // обновления приходят, а «Что нового» больше не показываем
                Settings::get().save();
            }
        });
    }
    m_whatsNew->setContent(cur, bullets);
    m_whatsNew->placeIn(QRect(mapToGlobal(m_workspace->geometry().topLeft()), m_workspace->size()));
    m_whatsNew->show();
    m_whatsNew->raise();
}

void StartWindow::refreshUpdate()
{
    const auto upd = Updater::visible();
    m_updateNew = upd.has_value();
    m_updateVersion = upd ? upd->version : QString();
    m_sidebar->setUpdateDot(m_updateNew);
    if (!m_pulseTimer)
    {
        // Пульсация идёт только пока меню на экране и есть что показывать
        m_pulseTimer = new QTimer(this);
        m_pulseTimer->setInterval(16);
        m_pulseTimer->setTimerType(Qt::PreciseTimer);
        connect(m_pulseTimer, &QTimer::timeout, this, [this] {
            m_pulse = double(m_pulseClock.elapsed() % 2400) / 2400.0; // один оборот — 2,4 с
            update(updateRect().adjusted(-6, -8, 6, 8));
        });
    }
    if (m_updateNew && isVisible())
    {
        m_pulseClock.start();
        m_pulseTimer->start();
    }
    else
        m_pulseTimer->stop();
}

// Маленькое меню по номеру версии (всегда доступно): что нового, обновить, проверить и все переключатели про обновления.
void StartWindow::updateMenu()
{
    const auto info = Updater::available();
    setBusy(true);
    QMenu menu(this);
    // Заголовок — просто надпись (не пункт): какая версия стоит сейчас
    auto* titleLabel = new QLabel(TXT("Установлена версия StartMenu %1").arg(Updater::currentVersion()));
    QColor sub = Theme::SubText.color();
    titleLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; padding: 6px 14px 4px 14px; font-size: 12px; }").arg(sub.name()));
    auto* title = new QWidgetAction(&menu);
    title->setDefaultWidget(titleLabel);
    title->setEnabled(false);
    menu.addAction(title);
    menu.addSeparator();
    QAction* upd = info ? menu.addAction(TXT("Обновить до версии %1").arg(info->version)) : nullptr;
    QAction* hide = (info && m_updateNew) ? menu.addAction(TXT("Скрыть уведомление")) : nullptr;
    QAction* check = menu.addAction(TXT("Проверить сейчас"));
    menu.addSeparator();
    // Переключатели: каждый сразу сохраняется
    Settings& st = Settings::get();
    auto toggle = [&](const QString& text, bool on, bool enabled = true) {
        QAction* a = menu.addAction(text);
        a->setCheckable(true);
        a->setChecked(on);
        a->setEnabled(enabled);
        return a;
    };
    QAction* notify = toggle(TXT("Уведомлять о новых версиях"), st.checkUpdates);
    QAction* autoInst = toggle(TXT("Устанавливать автоматически"), st.autoUpdate, st.checkUpdates);
    QAction* whatsNew = toggle(TXT("Показывать «Что нового»"), st.showWhatsNew);
    const QRect r = updateRect();
    const QSize sz = menu.sizeHint();
    // Пока меню открыто — свежая проверка в фоне, чтобы надпись и кружок знали о самой новой версии
    {
        QPointer<StartWindow> self(this);
        Updater::check(true, [self](bool) {
            if (self)
            {
                self->refreshUpdate();
                self->update();
            }
        });
    }
    QAction* chosen = menu.exec(mapToGlobal(QPoint(r.right() - sz.width(), r.top() - sz.height() - 4)));
    setBusy(false);
    if (!chosen)
        return;
    auto saveSettings = [this] {
        Settings::get().save();
        refreshUpdate();
        // Трей перечитает настройки (значок, автоустановка)
        if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\StartMenu.App.HostReload"))
        {
            SetEvent(ev);
            CloseHandle(ev);
        }
        update();
    };
    if (chosen == upd && info)
    {
        m_updateBusy = true;
        update();
        Updater::install(*info, [this](bool) {
            m_updateBusy = false;
            update();
        });
    }
    else if (hide && chosen == hide)
    {
        st.hiddenUpdate = info->version;
        saveSettings();
    }
    else if (chosen == check)
    {
        QPointer<StartWindow> self(this);
        Updater::check(true, [self](bool) {
            if (self)
            {
                self->refreshUpdate();
                self->update();
            }
        });
    }
    else if (chosen == notify)
    {
        st.checkUpdates = notify->isChecked();
        saveSettings();
    }
    else if (chosen == autoInst)
    {
        st.autoUpdate = autoInst->isChecked();
        saveSettings();
    }
    else if (chosen == whatsNew)
    {
        st.showWhatsNew = whatsNew->isChecked();
        saveSettings();
    }
}

void StartWindow::mouseMoveEvent(QMouseEvent* e)
{
    {
        const bool over = updateRect().contains(e->position().toPoint()); // номер версии нажимается всегда
        if (over != m_updateHover)
        {
            m_updateHover = over;
            setCursor(over ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update(updateRect().adjusted(-4, -2, 4, 2));
        }
    }
    const int h = layerAt(e->position().toPoint());
    if (h != m_layerHover)
    {
        m_layerHover = h;
        setCursor(h != -1 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update(m_searchRect.right(), 0, m_layerAdd.right() - m_searchRect.right() + 4, m_searchRect.bottom() + 8);
    }
    QWidget::mouseMoveEvent(e);
}

void StartWindow::wheelEvent(QWheelEvent* e)
{
    // Колёсико над переключателем — следующий/предыдущий слой
    const QPoint pos = e->position().toPoint();
    if (layerAt(pos) == -1 && !(m_layerRects.isEmpty() ? false : QRect(m_layerRects.first().topLeft(), m_layerAdd.bottomRight()).contains(pos)))
        return;
    const int n = int(Config::instance().layouts().size());
    if (n > 1)
        switchLayer((Config::instance().activeLayout() + (e->angleDelta().y() < 0 ? 1 : n - 1)) % n);
}

void StartWindow::contextMenuEvent(QContextMenuEvent* e)
{
    const int i = layerAt(e->pos());
    if (i < 0)
        return;
    auto& cfg = Config::instance();
    setBusy(true);
    QMenu menu(this);
    QAction* rename = menu.addAction(TXT("Переименовать слой…"));
    QAction* remove = cfg.layouts().size() > 1 ? menu.addAction(TXT("Удалить слой")) : nullptr;
    QAction* chosen = menu.exec(e->globalPos());
    if (chosen == rename)
    {
        bool ok = false;
        const QString name = QInputDialog::getText(this, TXT("Слой"), TXT("Название:"), QLineEdit::Normal,
                                                   cfg.layouts()[i].name, &ok).trimmed();
        if (ok)
        {
            cfg.layouts()[i].name = name;
            cfg.save();
        }
    }
    else if (chosen && chosen == remove)
    {
        int tiles = 0;
        for (const Group& g : cfg.layouts()[i].groups)
            for (const Tab& t : g.tabs)
                tiles += int(t.tiles.size());
        if (tiles == 0 || QMessageBox::question(this, TXT("Удалить слой"),
                                                TXT("Удалить слой «%1» со всеми его плитками (%2)?")
                                                    .arg(cfg.layouts()[i].name).arg(tiles)) == QMessageBox::Yes)
        {
            const int active = cfg.activeLayout();
            cfg.layouts().removeAt(i);
            cfg.setActiveLayout(qMin(active > i ? active - 1 : active, int(cfg.layouts().size()) - 1));
            cfg.save();
            m_workspace->reload();
        }
    }
    setBusy(false);
    layoutChildren();
    update();
}

void StartWindow::logClose(const QString& reason)
{
    // Закрытие может прийти дважды (после запуска и затем от потери фокуса) — в журнал пишем один раз
    if (m_closeLogged)
        return;
    m_closeLogged = true;
    WinShell::log(QStringLiteral("закрыто: ") + reason);
}

void StartWindow::startStats()
{
    if (Settings::get().systemMonitor && isVisible())
    {
        if (!m_statsTimer->isActive())
        {
            m_statsTimer->start();
            QTimer::singleShot(250, this, &StartWindow::sampleStats); // первое значение — сразу
        }
    }
    else
        m_statsTimer->stop();
}

void StartWindow::prepareRender(bool clipboard)
{
    refreshUpdate();
    if (qEnvironmentVariableIsSet("STARTMENU_WHATSNEW"))
        maybeShowWhatsNew(true); // для снимков: показать карточку «Что нового»
    if (clipboard)
        setView(View::Clipboard);
    if (Settings::get().systemMonitor)
    {
        sampleStats();
        QThread::msleep(400);
        sampleStats();
    }
}

void StartWindow::sampleStats()
{
    if (!m_stats)
        m_stats = std::make_unique<SystemStats>(); // обычно уже создан в конструкторе — здесь на всякий случай
    if (Demo::active())
        m_stats->setFake(7, 9.8, 32, 12, 512); // снимки для страницы проекта — не настоящие цифры
    m_stats->sample();
    m_statsLine = m_stats->line();
    const int sh = Skin::current().statusBarHeight();
    if (sh > 0 && m_statusName.isEmpty())
        update(0, height() - sh, width(), sh);
}

void StartWindow::clipboardChanged()
{
    const bool on = Settings::get().clipboardHistory;
    if (m_view == View::Clipboard && isVisible())
    {
        m_clip->reload();
        ClipboardStore::markSeen();
    }
    else
        m_sidebar->setClipboardDot(on && ClipboardStore::hasUnseen());
}

void StartWindow::pasteFromHistory(const ClipboardStore::Entry& entry)
{
    // Кладём запись в буфер, прячем меню, возвращаем фокус окну, где был курсор, и нажимаем Ctrl+V.
    if (!ClipboardStore::putToClipboard(entry))
        return;
    logClose(QStringLiteral("вставка из истории буфера"));
    const HWND target = reinterpret_cast<HWND>(m_prevForeground);
    hide();
    if (target && IsWindow(target))
    {
        SetForegroundWindow(target);
        QThread::msleep(90);
        INPUT in[4] = {};
        for (INPUT& i : in)
            i.type = INPUT_KEYBOARD;
        in[0].ki.wVk = VK_CONTROL;
        in[1].ki.wVk = 'V';
        in[2].ki.wVk = 'V';
        in[2].ki.dwFlags = KEYEVENTF_KEYUP;
        in[3].ki.wVk = VK_CONTROL;
        in[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, in, sizeof(INPUT));
    }
    close();
}

void StartWindow::closeEvent(QCloseEvent* e)
{
    if (!m_persistent)
    {
        QWidget::closeEvent(e);
        return;
    }
    // «Наготове»: не закрываемся, а прячемся и возвращаемся к плиткам — следующее открытие мгновенное
    e->ignore();
    m_focusTimer->stop();      // спрятанное меню фокус не проверяет и не забирает
    if (m_showAnim)
        m_showAnim->stop();    // закрыли посреди появления — анимация не должна двигать спрятанное окно
    m_closePending = false;
    if (QWidget* popup = QApplication::activePopupWidget())
        popup->close();
    m_busy = 0;                // всплывающие меню закрылись вместе с окном
    hide();
    m_hiddenAt.start();
    m_query.clear();
    m_searchFocused = false;
    m_caretTimer->stop();
    m_workspace->closeFolder();
    setView(View::Tiles);
    m_icons->releaseSources();
#ifdef _WIN32
    // Разрослось (много иконок, длинные сессии) — перезапускаемся: трей сразу поднимет свежее меню
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
    if (pmc.PrivateUsage > 160ull * 1024 * 1024)
    {
        WinShell::log(QStringLiteral("меню наготове заняло %1 МБ — перезапуск").arg(pmc.PrivateUsage / 1048576));
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    }
#endif
}

void StartWindow::itemsChangedOutside()
{
    // Не дёргаем раскладку посреди перетаскивания или открытого меню — чуть позже
    if (m_busy > 0)
    {
        QTimer::singleShot(300, this, &StartWindow::itemsChangedOutside);
        return;
    }
    Config::instance().load();
    m_workspace->reload();
    layoutChildren();
    update();
}

void StartWindow::moveEvent(QMoveEvent* e)
{
    QWidget::moveEvent(e);
    syncPlayer();

    if (m_whatsNew && m_whatsNew->isVisible())
        m_whatsNew->placeIn(QRect(mapToGlobal(m_workspace->geometry().topLeft()), m_workspace->size()));
}

void StartWindow::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    m_media->setWindowShown(true);
    layoutChildren();
    m_sidebar->setClipboardDot(Settings::get().clipboardHistory && ClipboardStore::hasUnseen());
    refreshUpdate();
    maybeShowWhatsNew();
    startStats();
}

void StartWindow::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    syncPlayer();
    m_statsTimer->stop();
    if (m_pulseTimer)
        m_pulseTimer->stop();
    m_media->setWindowShown(false);
    if (m_view == View::Clipboard)
        setView(View::Tiles); // в следующий раз меню открывается с плиток
}

QImage StartWindow::renderWithPlayer()
{
    // Для --render: меню и (если есть) панель плеера над ним одной картинкой.
    const QPixmap menu = grab();
    if (!m_panelMode)
        return menu.toImage();
    m_playerWin->resize(width(), PlayerWindow::PanelHeight);
    const QPixmap panel = m_playerWin->grab();
    const qreal dpr = menu.devicePixelRatio();
    QImage out(QSize(width(), height() + PlayerWindow::PanelHeight + PlayerWindow::Gap) * dpr, QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.drawPixmap(0, 0, panel);
    p.drawPixmap(0, PlayerWindow::PanelHeight + PlayerWindow::Gap, menu);
    return out;
}

void StartWindow::popup()
{
    // Меню открыли — заодно тихо проверяем обновления (не чаще раза в 2 часа): кружок появится, пока меню на экране
    if (Settings::get().checkUpdates)
    {
        // С задержкой в пару секунд: ничего не делаем в момент открытия, пока идёт анимация появления
        QPointer<StartWindow> self(this);
        QTimer::singleShot(2500, this, [self] {
            if (!self || !self->isVisible())
                return;
            Updater::check(false, [self](bool) {
                if (self)
                {
                    self->refreshUpdate();
                    self->update();
                }
            });
        });
    }
    // Тема «как в Windows»: если в Windows переключили светлую/тёмную — перекрашиваемся
    if (Settings::get().theme == 2 && Settings::windowsLight() != Theme::Light)
        reloadSettings();
    m_media->refresh();
    const QRect geo = targetGeometry();
    setFixedSize(geo.size());
    grabBackdrop(geo);
    if (m_panelMode)
        m_playerWin->prepareBackdrop(PlayerWindow::geometryAbove(geo));

    const auto& s = Settings::get();
    const bool animate = s.animation != Settings::Animation::None && s.animationMs > 0;
    const int slide = (s.animation == Settings::Animation::Slide) ? 24 : 0;
    move(geo.topLeft() + QPoint(0, animate ? slide : 0));
    if (m_showAnim)
        m_showAnim->stop(); // прошлое появление ещё идёт (быстрые нажатия) — останавливаем
    setWindowOpacity(animate ? 0.0 : 1.0);

    m_prevForeground = WinShell::foregroundWindow();
    QRect screenRect;
    bool overFullscreen = false;
    if (QScreen* scr = QGuiApplication::screenAt(geo.center()))
    {
        screenRect = scr->geometry();
        overFullscreen = WinShell::raiseTaskbar(screenRect); // в играх панель задач появляется вместе с меню
    }
    show();
    applyEffect();
    raise();
    WinShell::forceForeground(this);
    activateWindow();
    setFocus();
    m_busy = 0; // при открытии никаких всплывающих меню ещё нет
    m_closePending = false;
    m_busyStale = 0;
    m_hiddenByFocus = false;
    m_wasActive = false; // с прошлого показа (режим «наготове») — не считается: фокус этого показа ещё не получен
    m_focusTimer->start();
    m_openedAt.start();
    m_regrabs = 0;
    if (overFullscreen)
    {
        // Проводник прячет панель, когда фокус уходит с неё на меню, — возвращаем её поверх игры несколько раз
        WinShell::keepTaskbarAbove(screenRect);
        for (int ms : {80, 200, 400, 800})
            QTimer::singleShot(ms, this, [this, screenRect] {
                if (isVisible())
                    WinShell::keepTaskbarAbove(screenRect);
            });
    }
    m_closeLogged = false;
    WinShell::log(QStringLiteral("открыто (до этого фокус: %1)").arg(WinShell::describeWindow(m_prevForeground)));

    if (animate)
    {
        auto* anim = new QVariantAnimation(this);
        m_showAnim = anim;
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setDuration(s.animationMs);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QVariantAnimation::valueChanged, this, [this, geo, slide](const QVariant& v) {
            const double k = v.toDouble();
            setWindowOpacity(k);
            move(geo.topLeft() + QPoint(0, qRound((1.0 - k) * slide)));
            syncPlayer();
            // У прозрачного окна Windows применяет новую прозрачность только при перерисовке — без неё
            // повторно показанное меню оставалось невидимым, пока что-нибудь не обновится
            update();
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
    // Страховка: что бы ни случилось с анимацией, через её длительность меню полностью видно и на месте
    {
        const int settle = (animate ? s.animationMs : 0) + 40;
        QTimer::singleShot(settle, this, [this, geo] {
            if (!isVisible())
                return;
            if (windowOpacity() < 1.0)
                setWindowOpacity(1.0);
            if (pos() != geo.topLeft())
                move(geo.topLeft());
            syncPlayer();
            update();
        });
    }
}

void StartWindow::refreshBackdrop()
{
    if (!isVisible())
        return;
    if (Settings::get().effect != Settings::Effect::Blur)
    {
        m_backdrop = {};
        return;
    }
    // Чтобы снять фон без самого меню, на мгновение делаем окно невидимым.
    setWindowOpacity(0.0);
    repaint();
    QApplication::processEvents();
    QThread::msleep(30);
    grabBackdrop(geometry());
    setWindowOpacity(1.0);
    update();
}

void StartWindow::reloadSettings()
{
    const bool labelsBefore = Settings::get().showLabels;
    const QString langBefore = Lang::current();
    const auto effectBefore = Settings::get().effect;
    const int blurBefore = Settings::get().blurRadius;
    const QRect geoBefore = geometry();
    Settings::get().load();
    Lang::load(Settings::get().language); // подписи рисуются при отрисовке — новый язык виден сразу
    if (Settings::get().showLabels != labelsBefore || Lang::current() != langBefore)
    {
        // Окно настроек сбросило частные подписи в items.json — берём свежие плитки.
        Config::instance().load();
        m_workspace->reload();
    }
    applyStyle();
    const QRect geo = targetGeometry();
    setFixedSize(geo.size());
    move(geo.topLeft());
    // Новый снимок фона нужен только при смене размытия или размера — иначе окно мигало бы на каждый шаг ползунка
    if (Settings::get().effect != effectBefore || Settings::get().blurRadius != blurBefore || geo != geoBefore)
        refreshBackdrop();
    applyEffect();
    layoutChildren();          // у стиля свои отступы и высота поля поиска
    m_workspace->applyTheme();
    m_list->update();
    m_sidebar->relayout(); // кнопки могли включить/выключить
    m_sidebar->setClipboardDot(Settings::get().clipboardHistory && ClipboardStore::hasUnseen());
    update();
}

void StartWindow::closeFromHost()
{
    m_hiddenByFocus = false; // закрыли клавишей Win / кнопкой «Пуск» — следующее нажатие сразу открывает
    logClose(QStringLiteral("Win / кнопка «Пуск» / трей"));
    m_focusTimer->stop(); // окно уже закрывается — проверка фокуса не нужна
    // Если открыто контекстное меню или диалог — сначала закрываем их, само окно — следующим тактом.
    if (m_busy > 0)
    {
        QWidget* popup = QApplication::activePopupWidget();
        QWidget* modal = QApplication::activeModalWidget();
        if (popup || modal || QApplication::mouseButtons() != Qt::NoButton)
        {
            // Открыто меню/диалог/идёт перетаскивание: закрываем их, а само окно — когда они завершатся (setBusy).
            m_closePending = true;
            if (popup)
                popup->close();
            if (modal)
                modal->close();
            // Страховка: если метку «занято» так и не сняли, через 1,5 с закрываемся всё равно —
            // иначе меню осталось бы на экране и не закрывалось ни Win, ни кликом (сторож фокуса уже выключен).
            QTimer::singleShot(1500, this, [this] {
                if (!m_closePending || !isVisible())
                    return;
                WinShell::log(QStringLiteral("метка «занято» не снялась — закрываю принудительно"));
                m_busy = 0;
                m_closePending = false;
                close();
            });
            return;
        }
        m_busy = 0; // ни меню, ни диалога, ни перетаскивания — метка «занято» зависла
    }
    QTimer::singleShot(0, this, &QWidget::close);
}

void StartWindow::resizeEvent(QResizeEvent*)
{
    layoutChildren();
}

void StartWindow::layoutChildren()
{
    if (m_whatsNew && m_whatsNew->isVisible())
        m_whatsNew->placeIn(QRect(mapToGlobal(m_workspace->geometry().topLeft()), m_workspace->size()));
    m_sidebar->setGeometry(0, 0, Theme::SidebarWidth, height());
    const int left = Theme::SidebarWidth + 10;
    const int contentW = width() - left - Theme::ContentMargin;
    const int fieldH = Skin::current().searchFieldHeight();
    m_searchRect = QRect(left + Theme::ContentInsetX, Theme::ContentMargin, qMin(440, contentW - Theme::ContentInsetX), fieldH);
    const int top = m_searchRect.bottom() + 1 + 14;
    const int status = Skin::current().statusBarHeight();
    int bottom = status > 0 ? status + 6 : Theme::ContentMargin;

    // Слои: «1 2 3 +» (если слой один — только «+»)
    m_layerRects.clear();
    {
        const auto& layers = Config::instance().layouts();
        QFont f = Skin::current().font(Skin::Font::Meta);
        const QFontMetrics fm(f);
        const int h = qMin(26, m_searchRect.height() - 6);
        const int y = m_searchRect.center().y() - h / 2 + 1;
        int x = m_searchRect.right() + 1 + 12;
        if (layers.size() > 1)
            for (int i = 0; i < layers.size(); ++i)
            {
                const QString label = layers[i].name.isEmpty() ? QString::number(i + 1) : fm.elidedText(layers[i].name, Qt::ElideRight, 90);
                const int w = qMax(h, fm.horizontalAdvance(label) + 18);
                m_layerRects.append(QRect(x, y, w, h));
                x += w + 4;
            }
        // «+ Слой» подписан, чтобы его не приняли за «добавить приложение»
        m_layerAdd = QRect(x, y, fm.horizontalAdvance(TXT("Слой")) + h + 6, h);
    }
    const int afterLayers = m_layerAdd.right() + 1;

    // Плеер: панелью над меню (если выбран и помещается на экране) или компактно в строке поиска.
    m_mediaRect = {};
    m_panelMode = m_media->hasMedia() && m_media->wantsPanel() &&
                  PlayerWindow::fitsAbove(isVisible() ? geometry() : targetGeometry());
    m_media->setPanel(m_panelMode);
    if (m_panelMode)
    {
        if (m_media->parentWidget() != m_playerWin)
            m_media->setParent(m_playerWin);
        m_playerWin->resize(width(), PlayerWindow::PanelHeight);
        m_media->setGeometry(m_playerWin->rect());
        m_media->show();
    }
    else
    {
        if (m_media->parentWidget() != this)
            m_media->setParent(this);
        if (m_media->hasMedia())
        {
            // Компактный — сразу за полем поиска; справа от него остаётся место для строки сведений.
            const int mx = afterLayers + 12;
            const int avail = width() - Theme::ContentMargin - 10 - mx;
            int mw = qMin(340, avail - 200);
            if (mw < 220)
                mw = qMin(300, avail);
            if (mw >= 200)
                m_mediaRect = QRect(mx, m_searchRect.y(), mw, m_searchRect.height());
        }
        if (m_mediaRect.isValid())
        {
            m_media->setGeometry(m_mediaRect);
            m_media->show();
            m_media->raise();
        }
        else
            m_media->hide();
    }
    syncPlayer();

    const QRect content(left, top, contentW, height() - top - bottom);
    m_workspace->setGeometry(content);
    m_list->setGeometry(content);
    m_clip->setGeometry(content);
    update();
}

void StartWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!m_firstPaintLogged)
        WinShell::perfMark("win_paint");
    const Skin& skin = Skin::current();
    skin.paintPanel(p, rect(), m_backdrop, int(Settings::get().effect));
    skin.paintSearchField(p, m_searchRect, m_query, m_caretOn, m_searchFocused);
    // Служебная строка справа от поиска и строка состояния внизу (есть не у всех стилей).
    QStringList info;
    const bool hintsOn = Settings::get().showHints;
    if (m_view == View::Tiles)
    {
        if (hintsOn)
            info = m_workspace->statsLine();
    }
    else if (m_view == View::Search)
        info = {TXT("НАЙДЕНО: %1").arg(m_list->count())};
    else if (m_view == View::Clipboard)
        info = {TXT("ЗАПИСЕЙ: %1").arg(m_clip->count())};
    else
        info = {TXT("ПРИЛОЖЕНИЙ: %1").arg(m_apps.size())};
    paintLayers(p);
    const int infoLeft = (m_mediaRect.isValid() ? m_mediaRect.right() : m_layerAdd.right()) + 24;
    skin.paintToolbarInfo(p, QRect(infoLeft, m_searchRect.y(), width() - Theme::ContentMargin - 10 - infoLeft, m_searchRect.height()),
                          info);
    if (const int sh = skin.statusBarHeight())
    {
        const QStringList hints = !hintsOn ? QStringList{}
            : m_view == View::Tiles     ? QStringList{TXT("↵|ЗАПУСК"), TXT("←→↑↓|НАВИГАЦИЯ"), TXT("ESC|ЗАКРЫТЬ")}
            : m_view == View::Clipboard ? QStringList{TXT("↵|ВСТАВИТЬ"), TXT("↑↓|ВЫБОР"), TXT("ESC|НАЗАД")}
                                        : QStringList{TXT("↵|ЗАПУСК"), TXT("↑↓|ВЫБОР"), TXT("ESC|ОЧИСТИТЬ")};
        skin.paintStatusBar(p, QRect(Theme::SidebarWidth, height() - sh, width() - Theme::SidebarWidth, sh),
                            m_statusName, m_statusPath, m_statusLocation, hints);
        if (m_statusName.isEmpty() && Settings::get().systemMonitor && !m_statsLine.isEmpty())
        {
            // Монитор системы — тихо, приглушённым цветом, пока курсор не на плитке
            QFont f = skin.font(Skin::Font::Meta);
            p.setFont(f);
            p.setPen(Theme::SubText.color());
            const QRect r(Theme::SidebarWidth + Theme::ContentInsetX + 14, height() - sh, width() / 2, sh);
            p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter,
                       skin.kind() == Skin::Studio ? m_statsLine.toUpper() : m_statsLine);
        }
    }

    {
        // Номер версии — мелко и приглушённо. Есть обновление — рядом пульсирует кружок с расходящимися волнами.
        const QRect ur = updateRect();
        QFont f = skin.font(Skin::Font::Meta);
        f.setPixelSize(11);
        f.setUnderline(m_updateHover && !m_updateBusy);
        p.setFont(f);
        QColor c = m_updateHover ? Theme::TextColor.color() : Theme::SubText.color();
        if (!m_updateHover)
            c.setAlphaF(0.7f);
        p.setPen(c);
        p.drawText(ur, Qt::AlignRight | Qt::AlignVCenter,
                   m_updateBusy ? TXT("Скачиваю установщик…") : QStringLiteral("v") + Updater::currentVersion());
        if (m_updateNew && !m_updateBusy)
        {
            // Кружок слева от номера: две расходящиеся волны и мерцание
            const QPointF ctr(ur.left() + 8, ur.center().y() + 0.5);
            const QColor acc = Theme::Accent.color();
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(Qt::NoBrush);
            for (int k = 0; k < 2; ++k)
            {
                const qreal ph = std::fmod(m_pulse + k * 0.5, 1.0);
                QColor wc = acc;
                wc.setAlphaF(float(0.55 * (1.0 - ph)));
                p.setPen(QPen(wc, 1.2));
                const qreal rad = 3.0 + 7.0 * ph;
                p.drawEllipse(ctr, rad, rad);
            }
            QColor dc = acc;
            dc.setAlphaF(float(0.65 + 0.35 * std::sin(m_pulse * 6.2831853)));
            p.setPen(Qt::NoPen);
            p.setBrush(dc);
            p.drawEllipse(ctr, 3.2, 3.2);
        }
    }

    if (!m_firstPaintLogged)
    {
        // Замер (STARTMENU_PERF): время первой отрисовки и память — после того, как кадр отрисован.
        m_firstPaintLogged = true;
        QTimer::singleShot(0, this, [] {
            WinShell::perfMark("painted");
#ifdef _WIN32
            PROCESS_MEMORY_COUNTERS_EX pmc = {};
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
            WinShell::perfFlush(QStringLiteral("private_mb=%1").arg(pmc.PrivateUsage / 1048576.0, 0, 'f', 1));
#endif
        });
    }
}

namespace
{
    QString appsCacheFile()
    {
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/apps.json");
    }

    QList<Item> readAppsCache()
    {
        QList<Item> out;
        for (const auto& v : readJsonFile(appsCacheFile())["apps"].toArray())
        {
            const QJsonObject o = v.toObject();
            out.append({o["name"].toString(), o["path"].toString(), {}, {}});
        }
        return out;
    }

    void writeAppsCache(const QList<Item>& apps)
    {
        QJsonArray arr;
        for (const Item& it : apps)
            arr.append(QJsonObject{{"name", it.name}, {"path", it.path}});
        QDir().mkpath(QFileInfo(appsCacheFile()).absolutePath());
        QSaveFile f(appsCacheFile());
        if (f.open(QIODevice::WriteOnly))
        {
            f.write(QJsonDocument(QJsonObject{{"apps", arr}}).toJson(QJsonDocument::Compact));
            f.commit();
        }
    }
}

// Список всех приложений: сразу из кэша на диске (≈1 мс), а свежий — в фоне (Windows отдаёт его ≈120 мс).
void StartWindow::ensureApps()
{
    if (m_appsLoaded)
        return;
    m_appsLoaded = true;
    m_apps = readAppsCache();
    if (m_apps.isEmpty())
    {
        // Кэша ещё нет (самое первое открытие) — один раз ждём Windows.
        m_apps = WinShell::allApps();
        writeAppsCache(m_apps);
        return;
    }
    prefetchApps();
}

void StartWindow::prefetchApps()
{
    if (!m_appsLoaded)
    {
        ensureApps();
        return;
    }
    if (m_appsRefreshing)
        return;
    // Меню «наготове» живёт долго: перечитывать все приложения на каждом открытии незачем
    static QElapsedTimer lastRefresh;
    if (lastRefresh.isValid() && lastRefresh.elapsed() < 10 * 60 * 1000)
        return;
    lastRefresh.start();
    m_appsRefreshing = true;
    QPointer<StartWindow> self(this);
    std::thread([self] {
        // Shell-объекты требуют COM; у потока своя «квартира», с окном он не пересекается.
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        QList<Item> fresh = WinShell::allApps();
        CoUninitialize();
        QMetaObject::invokeMethod(qApp, [self, fresh] {
            if (self)
                self->applyFreshApps(fresh);
        }, Qt::QueuedConnection);
    }).detach();
}

void StartWindow::applyFreshApps(const QList<Item>& apps)
{
    m_appsRefreshing = false;
    if (apps.isEmpty())
        return;
    bool same = apps.size() == m_apps.size();
    for (int i = 0; same && i < apps.size(); ++i)
        same = apps[i].path == m_apps[i].path && apps[i].name == m_apps[i].name;
    if (same)
        return;
    m_apps = apps;
    writeAppsCache(apps);
    // Если список сейчас на экране — обновим его.
    if (m_view == View::AllApps)
        m_list->setItems(m_apps, true, TXT("Все приложения"));
    else if (m_view == View::Search)
        updateSearch();
}

void StartWindow::setView(View v)
{
    m_view = v;
    if (v != View::Search && !m_searchFocused)
    {
        m_caretTimer->stop();
        update(m_searchRect.adjusted(-4, -4, 4, 4));
    }
    m_sidebar->setMode(v == View::AllApps ? Sidebar::Mode::AllApps : v == View::Clipboard ? Sidebar::Mode::Clipboard : Sidebar::Mode::Tiles);
    if (v == View::Clipboard)
    {
        m_workspace->closeFolder();
        m_workspace->hide();
        m_list->hide();
        m_clip->reload();
        m_clip->show();
        ClipboardStore::markSeen();
        m_sidebar->setClipboardDot(false);
    }
    else if (v == View::Tiles)
    {
        m_clip->hide();
        m_list->hide();
        m_workspace->reload();
        m_workspace->show();
    }
    else
    {
        m_workspace->closeFolder();
        m_workspace->hide();
        m_clip->hide();
        m_list->show();
        if (v == View::AllApps)
        {
            ensureApps();
            m_list->setItems(m_apps, true, TXT("Все приложения"));
        }
    }
    setFocus();
}

void StartWindow::reloadTiles()
{
    // Не дёргаем раскладку посреди перетаскивания или открытого меню — обновим, когда освободится.
    if (m_busy > 0)
    {
        QTimer::singleShot(300, this, &StartWindow::reloadTiles);
        return;
    }
    if (m_view == View::Tiles)
        m_workspace->reload();
}

void StartWindow::startSearchCaret()
{
    m_caretOn = true;
    m_caretTimer->start();
    update();
}

void StartWindow::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && !m_updateBusy && updateRect().contains(e->position().toPoint()))
    {
        updateMenu();
        return;
    }
    if (e->button() == Qt::LeftButton)
    {
        const int l = layerAt(e->position().toPoint());
        if (l == -2)
        {
            addLayer();
            return;
        }
        if (l >= 0)
        {
            switchLayer(l);
            return;
        }
    }
    // Клик по полю поиска — оно становится активным (курсор), печатать можно сразу.
    if (e->button() == Qt::LeftButton && m_searchRect.contains(e->position().toPoint()))
    {
        m_searchFocused = true;
        setFocus();
        startSearchCaret();
        return;
    }
    if (m_searchFocused && m_query.isEmpty())
    {
        m_searchFocused = false;
        m_caretTimer->stop();
        update();
    }
    QWidget::mousePressEvent(e);
}

void StartWindow::prepareHidden()
{
    IconCache::pruneExact();
    for (const Group& g : Config::instance().groups())
        for (const Tab& t : g.tabs)
            for (const Tile& tile : t.tiles)
                if (!tile.isFolder)
                    WinShell::locationOf(tile.item); // кэш: при наведении путь уже готов
    // Один раз рисуем окно в картинку: Qt загрузит шрифты (≈35 мс), иконки, посчитает раскладку.
    // При показе всё это уже готово.
    setFixedSize(targetGeometry().size());
    QImage warm(size(), QImage::Format_ARGB32_Premultiplied);
    warm.fill(Qt::transparent);
    render(&warm);
}

void StartWindow::showFromStandby()
{
    if (isVisible())
    {
        closeFromHost(); // второе нажатие — закрыть
        return;
    }
    // Клик по значку в трее: сначала меню теряет фокус и прячется, сразу за этим приходит сам клик — не открываем снова.
    // Только для этого случая: если меню закрыли клавишей Win, следующее нажатие открывает его сразу.
    if (m_hiddenByFocus && m_hiddenAt.isValid() && m_hiddenAt.elapsed() < 300)
        return;
    WinShell::perfMark("show_req");
    m_firstPaintLogged = false; // замер (если включён): записать и первый кадр после показа
    // Пока ждали, могли поменяться плитки (закрепление) и настройки.
    Config::instance().load();
    reloadSettings();
    m_workspace->reload();
    popup();
}

void StartWindow::setQuery(const QString& q)
{
    m_viewBeforeSearch = m_view;
    m_query = q;
    updateSearch();
}

void StartWindow::updateSearch()
{
    if (m_query.isEmpty())
    {
        setView(m_viewBeforeSearch);
        return;
    }
    ensureApps();

    startSearchCaret();

    // Сначала закреплённое (включая содержимое папок), потом всё остальное из списка приложений.
    QList<Item> pool;
    for (const Group& g : Config::instance().groups())
        for (const Tab& tab : g.tabs)
            for (const Tile& t : tab.tiles)
            {
                if (t.isFolder)
                    pool += t.children;
                else
                    pool.append(t.item);
            }
    pool += m_apps;

    // Ищем и по самому запросу, и по его вариантам: «фото» → «photo», «ыеуфь» → «steam».
    const QStringList vars = SearchExtras::variants(m_query);
    QList<Item> starts, contains;
    QSet<QString> seen;
    for (const Item& it : pool)
    {
        const QString key = it.name.toLower();
        if (seen.contains(key))
            continue;
        bool st = false, hit = false;
        for (const QString& v : vars)
        {
            st |= it.name.startsWith(v, Qt::CaseInsensitive);
            hit |= it.name.contains(v, Qt::CaseInsensitive);
        }
        // Слово внутри названия тоже считается началом: «Effects» в «Adobe After Effects».
        for (const QString& v : vars)
            st |= it.name.contains(QLatin1Char(' ') + v, Qt::CaseInsensitive);
        if (!hit)
            continue;
        seen.insert(key);
        (st ? starts : contains).append(it);
    }
    QList<Item> apps = starts + contains;
    QList<Item> params = SearchExtras::settings(m_query);
    const QList<Item> calc = SearchExtras::top(m_query);
    const QList<Item> other = SearchExtras::tail(m_query);

    // Разделы: «Лучшее совпадение» (Enter запускает его), «Приложения», «Параметры», «Другое».
    QList<Item> best = calc;
    if (best.isEmpty() && !apps.isEmpty())
        best.append(apps.takeFirst());
    else if (best.isEmpty() && !params.isEmpty())
        best.append(params.takeFirst());
    QList<Item> items;
    QList<QPair<int, QString>> sections;
    auto add = [&](const QList<Item>& part, const QString& title) {
        if (part.isEmpty())
            return;
        sections.append({int(items.size()), title});
        items += part;
    };
    add(best, TXT("Лучшее совпадение"));
    add(apps, TXT("Приложения"));
    add(params, TXT("Параметры"));
    add(other, TXT("Другое"));

    m_view = View::Search;
    m_sidebar->setMode(Sidebar::Mode::Tiles);
    m_workspace->closeFolder();
    m_workspace->hide();
    m_clip->hide();
    m_list->show();
    m_list->setItems(items, false, QString(), sections, vars);
}

void StartWindow::keyPressEvent(QKeyEvent* e)
{
    switch (e->key())
    {
    case Qt::Key_Escape:
        if (m_searchFocused && m_query.isEmpty())
        {
            m_searchFocused = false;
            m_caretTimer->stop();
            update();
        }
        else if (m_view == View::Search)
        {
            m_query.clear();
            setView(m_viewBeforeSearch);
        }
        else if (m_workspace->closeFolder())
        {
        }
        else if (m_view == View::AllApps || m_view == View::Clipboard)
            setView(View::Tiles);
        else
        {
            logClose(QStringLiteral("Esc"));
            close();
        }
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_view == View::Clipboard)
            m_clip->activateSelected();
        else if (m_view != View::Tiles)
            m_list->activateSelected();
        else
            m_workspace->activateFocus();
        return;
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
    {
        const int dx = e->key() == Qt::Key_Left ? -1 : e->key() == Qt::Key_Right ? 1 : 0;
        const int dy = e->key() == Qt::Key_Up ? -1 : e->key() == Qt::Key_Down ? 1 : 0;
        if (m_view == View::Clipboard)
            m_clip->moveSelection(dy ? dy : dx);
        else if (m_view != View::Tiles)
            m_list->moveSelection(dx, dy);
        else
            m_workspace->moveFocus(dx, dy);
        return;
    }
    case Qt::Key_1: case Qt::Key_2: case Qt::Key_3: case Qt::Key_4: case Qt::Key_5:
    case Qt::Key_6: case Qt::Key_7: case Qt::Key_8: case Qt::Key_9:
        // Ctrl+цифра — переключить рабочее пространство
        if (e->modifiers() & Qt::ControlModifier)
        {
            const int i = e->key() - Qt::Key_1;
            if (i < Config::instance().layouts().size())
            {
                switchLayer(i);
            }
            return;
        }
        break;
    case Qt::Key_Backspace:
        if (!m_query.isEmpty())
        {
            if (e->modifiers() & Qt::ControlModifier)
                m_query.clear();
            else
                m_query.chop(1);
            updateSearch();
        }
        return;
    default:
        break;
    }

    // Ctrl+V — вставить текст из буфера в поиск.
    if (e->matches(QKeySequence::Paste))
    {
        const QString clip = QGuiApplication::clipboard()->text().simplified();
        if (!clip.isEmpty())
        {
            if (m_view != View::Search)
                m_viewBeforeSearch = m_view;
            m_query += clip;
            updateSearch();
        }
        return;
    }
    // Любой печатный символ начинает поиск — поле ввода не нужно.
    const QString text = e->text();
    if (!text.isEmpty() && text.at(0).isPrint() && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
    {
        if (m_view != View::Search)
            m_viewBeforeSearch = m_view;
        if (!(m_query.isEmpty() && text.trimmed().isEmpty())) // пробел в начале игнорируем
        {
            m_query += text;
            updateSearch();
        }
        return;
    }
    QWidget::keyPressEvent(e);
}

// Строку из «Всех приложений» или поиска потянули мышью: переключаемся на плитки
// и запускаем перетаскивание — бросить можно в любую группу, на плитку (папка) или в пустое место.
void StartWindow::startDragToTiles(const Item& item)
{
    setBusy(true);
    m_query.clear();
    setView(View::Tiles);

    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(Workspace::itemMime()),
                  QJsonDocument(QJsonObject{{"name", item.name}, {"path", item.path}, {"args", item.args}, {"icon", item.icon}})
                      .toJson(QJsonDocument::Compact));
    auto* drag = new QDrag(m_workspace);
    drag->setMimeData(mime);
    const QPixmap pm = m_icons->get(item, Theme::IconMedium, devicePixelRatioF());
    if (!pm.isNull())
    {
        drag->setPixmap(pm);
        drag->setHotSpot(QPoint(Theme::IconMedium / 2, Theme::IconMedium / 2));
    }
    drag->exec(Qt::CopyAction);
    setBusy(false);
}

void StartWindow::powerMenu(const QPoint& globalPos)
{
    setBusy(true);
    QMenu menu(this);
    // Пункты — те, что выбраны в настройках («Поведение» → «Меню кнопки питания»), в порядке как в Windows
    const Settings& st = Settings::get();
    using P = WinShell::Power;
    struct Entry { bool on; const QString text; P action; };
    const Entry entries[] = {
        {st.powerLock, TXT("Блокировка"), P::Lock},
        {st.powerSignOut, TXT("Выход из системы"), P::SignOut},
        {st.powerSleep, TXT("Спящий режим"), P::Sleep},
        {st.powerHibernate && WinShell::hibernateAvailable(), TXT("Гибернация"), P::Hibernate},
        {st.powerRestart, TXT("Перезагрузка"), P::Restart},
        {st.powerShutdown, TXT("Завершение работы"), P::Shutdown},
    };
    int prevBlock = -1;
    for (const Entry& e : entries)
    {
        if (!e.on)
            continue;
        // Разделители между блоками: сеанс | сон | выключение
        const int block = e.action <= P::SignOut ? 0 : e.action <= P::Hibernate ? 1 : 2;
        if (prevBlock >= 0 && block != prevBlock)
            menu.addSeparator();
        prevBlock = block;
        menu.addAction(e.text)->setData(int(e.action));
    }
    if (menu.isEmpty()) // всё выключили — хотя бы «Завершение работы»
        menu.addAction(TXT("Завершение работы"))->setData(int(P::Shutdown));

    // Меню справа от кнопки, нижним краем на уровне кнопки.
    const QSize sz = menu.sizeHint();
    QAction* chosen = menu.exec(QPoint(globalPos.x() + 8, globalPos.y() + Theme::SidebarButton - sz.height()));
    setBusy(false);
    if (!chosen)
        return;

    close();
    const auto action = WinShell::Power(chosen->data().toInt());
    // Чуть позже: меню успевает скрыться, прежде чем экран погаснет или заблокируется
    QTimer::singleShot(250, [action] { WinShell::power(action); });
}

void StartWindow::setBusy(bool busy)
{
    m_busy = qMax(0, m_busy + (busy ? 1 : -1));
    if (m_busy == 0 && m_closePending)
    {
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }
    if (m_busy == 0 && isVisible())
    {
        activateWindow();
        setFocus();
        // Пока было открыто меню, пользователь мог кликнуть мимо — проверим чуть позже.
        QTimer::singleShot(100, this, &StartWindow::checkFocus);
    }
}

// ---------- изменение размера мышью ----------
// Тянуть можно за верхний, левый и правый края (и верхние углы). Нижний край прижат к панели задач.
// Если меню по центру, ширина меняется симметрично — меню остаётся по центру.

int StartWindow::edgesAt(const QPoint& local) const
{
    constexpr int zone = 6;
    if (!rect().contains(local))
        return 0;
    int edges = 0;
    if (local.x() < zone)
        edges |= EdgeLeft;
    else if (local.x() >= width() - zone)
        edges |= EdgeRight;
    if (local.y() < zone)
        edges |= EdgeTop;
    return edges;
}

void StartWindow::setEdgeCursor(int edges)
{
    if (edges == m_cursorEdges)
        return;
    if (m_cursorEdges)
        QApplication::restoreOverrideCursor();
    m_cursorEdges = edges;
    if (!edges)
        return;
    Qt::CursorShape shape = Qt::SizeHorCursor;
    if (edges == EdgeTop)
        shape = Qt::SizeVerCursor;
    else if (edges == (EdgeTop | EdgeLeft))
        shape = Qt::SizeFDiagCursor;
    else if (edges == (EdgeTop | EdgeRight))
        shape = Qt::SizeBDiagCursor;
    QApplication::setOverrideCursor(shape);
}

void StartWindow::resizeTo(const QPoint& globalPos)
{
    const QPoint d = globalPos - m_resizePress;
    const QRect g = m_resizeStart;
    const QRect avail = screen()->availableGeometry();
    const bool centered = Settings::get().position == Settings::Position::Center;

    int w = g.width();
    int h = g.height();
    if (m_resizeEdges & EdgeRight)
        w += centered ? 2 * d.x() : d.x();
    if (m_resizeEdges & EdgeLeft)
        w -= centered ? 2 * d.x() : d.x();
    if (m_resizeEdges & EdgeTop)
        h -= d.y();
    w = std::clamp(w, 600, qMax(600, avail.width() - 2 * Theme::BottomGap));
    h = std::clamp(h, 300, qMax(300, avail.height() - 2 * Theme::BottomGap));

    int x = g.x();
    if (centered)
        x = g.center().x() + 1 - w / 2;
    else if (m_resizeEdges & EdgeLeft)
        x = g.right() + 1 - w;
    const int y = g.bottom() + 1 - h;

    Theme::WindowWidth = w;
    Theme::WindowHeight = h;
    setFixedSize(w, h);
    move(x, y);
}

void StartWindow::finishResize()
{
    IconCache::s_persistExact = true;
    m_resizeEdges = 0;
    setEdgeCursor(0);
    // Берём свежий файл (его могло поменять окно настроек) и меняем в нём только размер.
    const int w = width(), h = height();
    {
        const FileLock lock; // прочитать-поменять-записать одним куском: окно настроек не вклинится
        Settings::get().load();
        Settings::get().width = w;
        Settings::get().height = h;
        Settings::get().save();
    }
    Theme::WindowWidth = w;
    Theme::WindowHeight = h;
    // Открытое окно настроек должно увидеть новый размер, иначе при следующей правке вернёт старый.
    if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\StartMenu.App.SettingsReload"))
    {
        SetEvent(ev);
        CloseHandle(ev);
    }
    refreshBackdrop();
}

bool StartWindow::eventFilter(QObject* watched, QEvent* e)
{
    const auto type = e->type();
    // Фильтр стоит на всём приложении и видит каждое событие (таймеры, отрисовку) — сначала дешёвая проверка типа.
    if (type != QEvent::Polish && type != QEvent::Show && type != QEvent::MouseMove && type != QEvent::MouseButtonPress
        && type != QEvent::MouseButtonRelease)
        return false;
    // Все всплывающие меню (питание, ПКМ) — полупрозрачные, со стеклом Windows и скруглёнными углами.
    if (auto* menu = qobject_cast<QMenu*>(watched))
    {
        if (type == QEvent::Polish)
        {
            menu->setAttribute(Qt::WA_TranslucentBackground);
            menu->setWindowFlag(Qt::FramelessWindowHint);
        }
        else if (type == QEvent::Show)
        {
            WinShell::setAcrylic(menu, true, Theme::Rgba::from(Theme::MenuBg.color(), 110).color());
            WinShell::setRoundCorners(menu, true);
        }
        return false;
    }
    if (type != QEvent::MouseMove && type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease)
        return false;
    auto* w = qobject_cast<QWidget*>(watched);
    if (!w || w->window() != this)
        return false;
    auto* me = static_cast<QMouseEvent*>(e);
    const QPoint global = me->globalPosition().toPoint();

    if (m_resizeEdges)
    {
        if (type == QEvent::MouseMove)
            resizeTo(global);
        else if (type == QEvent::MouseButtonRelease && me->button() == Qt::LeftButton)
            finishResize();
        return true;
    }
    if (m_busy > 0)
        return false;

    const int edges = edgesAt(mapFromGlobal(global));
    if (type == QEvent::MouseMove && me->buttons() == Qt::NoButton)
    {
        setEdgeCursor(edges);
        return edges != 0;
    }
    if (type == QEvent::MouseButtonPress && me->button() == Qt::LeftButton && edges)
    {
        m_resizeEdges = edges;
        IconCache::s_persistExact = false; // пока тянем — промежуточные размеры иконок на диск не пишем
        m_resizeStart = geometry();
        m_resizePress = global;
        return true;
    }
    return false;
}

void StartWindow::checkFocus()
{
    if (!isVisible() || !m_focusTimer->isActive())
        return;
    if (m_busy > 0)
    {
        // «Занято», а ни меню, ни диалога, ни зажатой кнопки нет уже ~1,5 с — метку забыли снять, снимаем сами
        if (!QApplication::activePopupWidget() && !QApplication::activeModalWidget() && QApplication::mouseButtons() == Qt::NoButton)
        {
            if (++m_busyStale < 6)
                return;
            m_busy = 0;
            WinShell::log(QStringLiteral("метка «занято» зависла — сброшена"));
        }
        else
        {
            m_busyStale = 0;
            return;
        }
    }
    m_busyStale = 0;
    if (WinShell::foregroundIsOurs())
    {
        m_wasActive = true;
        return;
    }
    const quintptr fg = WinShell::foregroundWindow();
    const qint64 sinceOpen = m_openedAt.isValid() ? m_openedAt.elapsed() : 1000000;
    // Фокус «ни у кого» (кнопка мыши ещё зажата после клика по «Пуску», окно переключается) —
    // это не уход пользователя: забираем фокус, а если не выходит — просто ждём.
    if (!fg)
    {
        if (m_regrabs < 8)
        {
            ++m_regrabs;
            WinShell::forceForeground(this);
            activateWindow();
        }
        return;
    }
    // Поиск и «Пуск» самой Windows (SearchHost, StartMenuExperienceHost…) после нажатия Win
    // ещё секунду-две пытаются забрать фокус — это не действие пользователя.
    const QString who = WinShell::describeWindow(fg);
    const bool shellPopup = who.startsWith(QStringLiteral("SearchHost.exe")) || who.startsWith(QStringLiteral("StartMenuExperienceHost.exe"))
                            || who.startsWith(QStringLiteral("ShellExperienceHost.exe")) || who.contains(QStringLiteral("XamlExplorerHostIslandWindow"));
    // Фокус «украло» служебное окно (оверлей записи экрана, уведомление) или что-то перехватило его
    // в первые 0,6 с после открытия без клика пользователя — забираем фокус обратно, а не закрываемся.
    const bool userClick = WinShell::mouseButtonDown();
    const bool justOpened = sinceOpen < 600;
    // Игра на весь экран (WoW и др.) через полсекунды после открытия сама забирает фокус обратно —
    // пользователь при этом ничего не нажимал. Без клика и Alt+Tab это не «ушёл», а «отобрали».
    const bool gameTookBack = fg == m_prevForeground && sinceOpen < 1500 && !userClick && !WinShell::switchKeysDown();
    if ((shellPopup && sinceOpen < 2500 && m_regrabs < 10) ||
        (!userClick && m_regrabs < 4 && (WinShell::isPassiveWindow(fg) || justOpened)) ||
        (gameTookBack && m_regrabs < 8))
    {
        ++m_regrabs;
        WinShell::log(QStringLiteral("фокус перехватил %1 — забираю обратно").arg(who));
        WinShell::forceForeground(this);
        activateWindow();
        return;
    }
    // Если фокус так и не получили, закрываемся, когда пользователь переключился куда-то ещё
    // или кликнул в окно, где был до меню.
    if (m_wasActive || fg != m_prevForeground || userClick)
    {
        logClose(QStringLiteral("фокус ушёл к %1%2").arg(who, userClick ? QStringLiteral(" (клик)") : QString()));
        m_hiddenByFocus = true; // см. showFromStandby: клик по трею сразу после этого не должен открыть меню снова
        close();
    }
}

bool StartWindow::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip)
    {
        const auto* he = static_cast<QHelpEvent*>(e);
        const int l = layerAt(he->pos());
        if (l == -2)
            QToolTip::showText(he->globalPos(), TXT("Добавить слой — ещё одна страница плиток"), this);
        else if (l >= 0)
            QToolTip::showText(he->globalPos(), TXT("Слой «%1» · Ctrl+%2 · колёсико — листать, ПКМ — переименовать")
                                                    .arg(Config::instance().layouts()[l].name).arg(l + 1), this);
        else
            QToolTip::hideText();
        return true;
    }
    if (e->type() == QEvent::Leave && !m_resizeEdges)
        setEdgeCursor(0);
    if (e->type() == QEvent::WindowActivate)
        m_wasActive = true;
    else if (e->type() == QEvent::WindowDeactivate && m_busy == 0)
        QTimer::singleShot(0, this, &StartWindow::checkFocus);
    return QWidget::event(e);
}
