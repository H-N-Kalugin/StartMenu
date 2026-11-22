#include "Sidebar.h"
#include "Lang.h"
#include "AudioControl.h"
#include "Config.h"
#include "Glyphs.h"
#include "Settings.h"
#include "Skin.h"
#include "Shell.h"
#include "Theme.h"
#include "Updater.h"

#include <QContextMenuEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHelpEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardPaths>
#include <QToolTip>
#include <QWheelEvent>

Sidebar::Sidebar(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAcceptDrops(true);
}

void Sidebar::setMode(Mode m)
{
    m_mode = m;
    update();
}

void Sidebar::setUpdateDot(bool on)
{
    if (m_updateDot == on)
        return;
    m_updateDot = on;
    update();
}

void Sidebar::setClipboardDot(bool on)
{
    if (m_clipDot == on)
        return;
    m_clipDot = on;
    update();
}

void Sidebar::relayout()
{
    m_muted = Audio::muted(); // значок звука: перечёркнутый, если звук выключен
    m_buttons.clear();
    const int b = Theme::SidebarButton;
    const int x = (width() - b) / 2;

    int y = 12;
    m_buttons.append({Kind::Tiles, QRect(x, y, b, b)});
    y += b + 4;
    m_buttons.append({Kind::AllApps, QRect(x, y, b, b)});
    if (Settings::get().clipboardHistory)
    {
        y += b + 4;
        m_buttons.append({Kind::Clipboard, QRect(x, y, b, b)});
    }
    const int topEnd = y + b + 4;

    // Снизу вверх: питание, «Выполнить», «Параметры», папки, звук.
    y = height() - 12 - b;
    m_buttons.append({Kind::Power, QRect(x, y, b, b)});
    y -= b + 2;
    m_buttons.append({Kind::Run, QRect(x, y, b, b)});
    y -= b + 2;
    m_buttons.append({Kind::Settings, QRect(x, y, b, b)});
    const auto& folders = Config::instance().sidebar();
    for (int i = int(folders.size()) - 1; i >= 0; --i)
    {
        y -= b + 2;
        if (y < topEnd)
            break; // не влезает — лишние папки не показываем
        m_buttons.append({Kind::Folder, QRect(x, y, b, b), i});
    }
    y -= b + 2;
    if (y >= topEnd)
        m_buttons.append({Kind::Sound, QRect(x, y, b, b)});
}

void Sidebar::resizeEvent(QResizeEvent*)
{
    relayout();
}

int Sidebar::buttonAt(const QPoint& p) const
{
    for (int i = 0; i < m_buttons.size(); ++i)
        if (m_buttons[i].rect.contains(p))
            return i;
    return -1;
}

void Sidebar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor bg = Theme::Rgba::from(Theme::PanelBg.color(), 255).color();
    const QString docs = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    const QString downloads = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));

    static bool first = true;
    if (first)
        WinShell::perfMark("side_paint");
    first = false;
    Skin::current().paintSidebarBack(p, rect());
    for (int i = 0; i < m_buttons.size(); ++i)
    {
        const Button& b = m_buttons[i];
        const bool active = (b.kind == Kind::Tiles && m_mode == Mode::Tiles) || (b.kind == Kind::AllApps && m_mode == Mode::AllApps) ||
                            (b.kind == Kind::Clipboard && m_mode == Mode::Clipboard);
        SidebarButtonPaint sb;
        sb.r = b.rect;
        sb.hover = (i == m_hover);
        sb.pressed = (i == m_pressed);
        sb.active = active;
        Skin::current().paintSidebarButton(p, sb);
        // Неактивные значки чуть приглушены (у Bento активная кнопка залита акцентом — значок белый).
        const QColor fg = (active && Skin::current().kind() == Skin::Bento) ? QColor(Qt::white)
                          : (active || sb.hover) ? Theme::TextColor.color()
                                                 : Theme::Rgba::from(Theme::TextColor.color(), 185).color();

        const QRectF g(b.rect.center().x() - 10, b.rect.center().y() - 10, 20, 20);
        switch (b.kind)
        {
        case Kind::Tiles:    Glyphs::draw(p, Glyphs::Kind::Tiles, g, fg, bg); break;
        case Kind::AllApps:  Glyphs::draw(p, Glyphs::Kind::List, g, fg, bg); break;
        case Kind::Clipboard:
            Glyphs::draw(p, Glyphs::Kind::Clipboard, g, fg, bg);
            if (m_clipDot && !active)
            {
                // Скопировали что-то новое — маленькая акцентная точка в углу
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::Accent.color());
                p.drawEllipse(QPointF(b.rect.right() - 11, b.rect.top() + 11), 3.5, 3.5);
            }
            break;
        case Kind::Sound:
            Glyphs::draw(p, m_muted ? Glyphs::Kind::VolumeMute : Glyphs::Kind::Volume, g, fg, bg);
            break;
        case Kind::Settings:
            Glyphs::draw(p, Glyphs::Kind::Settings, g, fg, bg);
            if (m_updateDot)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::Accent.color());
                p.drawEllipse(QPointF(b.rect.right() - 11, b.rect.top() + 11), 3.5, 3.5);
            }
            break;
        case Kind::Run:      Glyphs::draw(p, Glyphs::Kind::Run, g, fg, bg); break;
        case Kind::Power:    Glyphs::draw(p, Glyphs::Kind::Power, g, fg, bg); break;
        case Kind::Folder:
        {
            const QString path = QDir::cleanPath(Config::instance().sidebar()[b.folder].path);
            const auto kind = path.compare(docs, Qt::CaseInsensitive) == 0        ? Glyphs::Kind::Documents
                              : path.compare(downloads, Qt::CaseInsensitive) == 0 ? Glyphs::Kind::Downloads
                                                                                  : Glyphs::Kind::Folder;
            Glyphs::draw(p, kind, g, fg, bg);
            break;
        }
        }
    }
}

void Sidebar::mouseMoveEvent(QMouseEvent* e)
{
    const int h = buttonAt(e->position().toPoint());
    if (h != m_hover)
    {
        m_hover = h;
        setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void Sidebar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
    {
        m_pressed = buttonAt(e->position().toPoint());
        update();
    }
}

void Sidebar::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const int i = buttonAt(e->position().toPoint());
    const bool click = (i >= 0 && i == m_pressed);
    m_pressed = -1;
    update();
    if (click)
        trigger(m_buttons[i]);
}

void Sidebar::wheelEvent(QWheelEvent* e)
{
    const int i = buttonAt(e->position().toPoint());
    if (i < 0 || m_buttons[i].kind != Kind::Sound)
        return;
    const float v = qBound(0.0f, Audio::volume() + (e->angleDelta().y() > 0 ? 0.02f : -0.02f), 1.0f);
    Audio::setVolume(v);
    if (v > 0 && m_muted)
    {
        Audio::setMuted(false);
        m_muted = false;
    }
    QToolTip::showText(e->globalPosition().toPoint(), TXT("Громкость: %1%").arg(qRound(v * 100)), this);
    update();
}

void Sidebar::leaveEvent(QEvent*)
{
    m_hover = m_pressed = -1;
    update();
}

void Sidebar::trigger(const Button& b)
{
    switch (b.kind)
    {
    case Kind::Tiles:
        emit tilesRequested();
        break;
    case Kind::AllApps:
        emit allAppsRequested();
        break;
    case Kind::Clipboard:
        emit clipboardRequested();
        break;
    case Kind::Sound:
        emit soundRequested(mapToGlobal(QPoint(b.rect.right() + 8, b.rect.top())));
        break;
    case Kind::Folder:
        WinShell::launch(Config::instance().sidebar()[b.folder]);
        emit launched();
        break;
    case Kind::Settings:
        settingsMenu(mapToGlobal(b.rect.topRight()));
        break;
    case Kind::Run:
        WinShell::openRunDialog();
        emit launched();
        break;
    case Kind::Power:
        emit powerRequested(mapToGlobal(b.rect.topRight()));
        break;
    }
}

void Sidebar::settingsMenu(const QPoint& globalPos)
{
    // Два вида настроек — сразу оба на выбор, чтобы настройки самого меню было легко найти
    emit busyChanged(true);
    QMenu menu(this);
    QAction* upd = nullptr;
    const auto info = Settings::get().checkUpdates ? Updater::available() : std::nullopt;
    if (info)
    {
        upd = menu.addAction(TXT("Обновить до версии %1").arg(info->version));
        menu.addSeparator();
    }
    QAction* own = menu.addAction(TXT("Настройки меню…"));
    QAction* win = menu.addAction(TXT("Параметры Windows"));
    QAction* chosen = menu.exec(globalPos);
    emit busyChanged(false);
    if (upd && chosen == upd)
    {
        Updater::install(*info);
        emit launched();
    }
    else if (chosen == own)
        WinShell::startSelf(QStringLiteral("--settings"));
    else if (chosen == win)
    {
        WinShell::open(QStringLiteral("ms-settings:"));
        emit launched();
    }
}

QString Sidebar::tipFor(const Button& b) const
{
    switch (b.kind)
    {
    case Kind::Tiles:    return TXT("Плитки");
    case Kind::AllApps:  return TXT("Все приложения");
    case Kind::Clipboard: return TXT("Буфер обмена — что вы копировали");
    case Kind::Sound:    return TXT("Звук: %1% (колёсико — громкость, клик — устройство)").arg(qRound(Audio::volume() * 100));
    case Kind::Folder:   return Config::instance().sidebar()[b.folder].name;
    case Kind::Settings: return TXT("Настройки меню и параметры Windows");
    case Kind::Run:      return TXT("Выполнить");
    case Kind::Power:    return TXT("Питание");
    }
    return {};
}

bool Sidebar::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip)
    {
        auto* he = static_cast<QHelpEvent*>(e);
        const int i = buttonAt(he->pos());
        if (i >= 0)
            QToolTip::showText(he->globalPos(), tipFor(m_buttons[i]), this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

void Sidebar::contextMenuEvent(QContextMenuEvent* e)
{
    const int i = buttonAt(e->pos());
    if (i >= 0 && m_buttons[i].kind == Kind::Settings)
    {
        settingsMenu(e->globalPos());
        return;
    }
    if (i < 0 || m_buttons[i].kind != Kind::Folder)
        return;
    const int folder = m_buttons[i].folder;

    emit busyChanged(true);
    QMenu menu(this);
    QAction* open = menu.addAction(TXT("Открыть"));
    QAction* up = folder > 0 ? menu.addAction(TXT("Выше")) : nullptr;
    QAction* down = folder < Config::instance().sidebar().size() - 1 ? menu.addAction(TXT("Ниже")) : nullptr;
    QAction* remove = menu.addAction(TXT("Убрать с панели"));
    QAction* chosen = menu.exec(e->globalPos());
    emit busyChanged(false);
    if (!chosen)
        return;

    auto& folders = Config::instance().sidebar();
    if (chosen == open)
    {
        trigger(m_buttons[i]);
        return;
    }
    if (chosen == up)
        folders.move(folder, folder - 1);
    else if (chosen == down)
        folders.move(folder, folder + 1);
    else if (chosen == remove)
        folders.removeAt(folder);
    Config::instance().save();
    m_hover = -1;
    relayout();
    update();
}

void Sidebar::dragEnterEvent(QDragEnterEvent* e)
{
    for (const QUrl& url : e->mimeData()->urls())
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir())
        {
            e->acceptProposedAction();
            return;
        }
}

void Sidebar::dropEvent(QDropEvent* e)
{
    auto& folders = Config::instance().sidebar();
    for (const QUrl& url : e->mimeData()->urls())
    {
        if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isDir())
            continue;
        const Item it = Config::itemFromPath(url.toLocalFile());
        const bool exists = std::any_of(folders.cbegin(), folders.cend(), [&](const Item& f) {
            return QDir::cleanPath(f.path).compare(QDir::cleanPath(it.path), Qt::CaseInsensitive) == 0;
        });
        if (!exists)
            folders.append(it);
    }
    Config::instance().save();
    relayout();
    update();
    e->acceptProposedAction();
}
