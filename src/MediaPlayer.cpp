#include "MediaPlayer.h"
#include "Lang.h"
#include "MediaSession.h"
#include "Settings.h"
#include "Config.h"
#include "Demo.h"
#include "Skin.h"
#include "Theme.h"

#include <QContextMenuEvent>
#include <cmath>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QTimer>

#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

MediaPlayer::MediaPlayer(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_NoSystemBackground);
    m_tick = new QTimer(this);
    m_tick->setInterval(500);
    connect(m_tick, &QTimer::timeout, this, [this] {
        if (isVisible() && m_panel && m_playing && m_durationMs > 0 && !m_anim->isActive())
            update();
    });
    m_anim = new QTimer(this); // таймеры работают, только пока плеер на экране (см. showEvent/hideEvent)
    m_anim->setInterval(33);
    connect(m_anim, &QTimer::timeout, this, &MediaPlayer::animate);
    hide(); // только после таймеров: hide() вызывает hideEvent, а он их останавливает
    if (Demo::active())
    {
        // Снимки для страницы проекта: выдуманный трек вместо того, что играет у пользователя.
        // Позиция замирает на 1:24 (часы позиции не идут).
        apply(Demo::player());
        m_posClock.invalidate();
        return;
    }
    // Ответ приходит из фонового потока — передаём в окно через очередь событий Qt.
    QPointer<MediaPlayer> self(this);
    m_watcher = std::make_unique<Media::Watcher>([self](const Media::State& s) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [self, s] {
            if (self)
                self->apply(s);
        }, Qt::QueuedConnection);
    });
}

MediaPlayer::~MediaPlayer()
{
    m_watcher.reset(); // сначала останавливаем фоновый поток
    if (m_meter)
        m_meter->Release();
}

bool MediaPlayer::hasMedia() const
{
    return Settings::get().mediaPlayer > 0 && m_present && !m_dismissed;
}

bool MediaPlayer::wantsPanel() const
{
    return Settings::get().mediaPlayer == 2;
}

void MediaPlayer::setPanel(bool panel)
{
    if (m_panel == panel)
        return;
    m_panel = panel;
    m_hover = -1;
    updateAnimTimer();
    update();
}

void MediaPlayer::refresh()
{
    if (m_watcher)
        m_watcher->refresh();
}

void MediaPlayer::command(Media::Command c, qint64 ms)
{
    if (m_watcher)
        m_watcher->command(c, ms);
}

void MediaPlayer::setWindowShown(bool shown)
{
    if (m_watcher)
        m_watcher->setActive(shown);
}

void MediaPlayer::apply(const Media::State& s)
{
    const bool before = hasMedia();
    m_present = s.present;
    m_title = QString::fromStdWString(s.title);
    m_artist = QString::fromStdWString(s.artist);
    m_playing = s.playing;
    m_canPrev = s.canPrev;
    m_canNext = s.canNext;
    m_canSeek = s.canSeek;
    m_durationMs = s.durationMs;
    if (m_seekFrac < 0) // пока тянут ползунок, позицию от плеера не принимаем
    {
        m_positionMs = s.positionMs;
        m_posClock.start();
    }
    const QString key = m_title + QLatin1Char('|') + m_artist;
    if (key != m_key)
    {
        m_key = key;
        m_art = s.art.empty() ? QImage() : QImage::fromData(s.art.data(), int(s.art.size()));
        // Размытая обложка для фона панели: сжать до крошечной и растянуть обратно — получается мягкое пятно.
        m_artBlur = m_art.isNull() ? QImage()
                                   : m_art.scaled(24, 24, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                                         .scaled(240, 240, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        // Цвета подсветки: левая и правая половины обложки, насыщеннее и светлее; без обложки — акцент.
        auto vivid = [](QColor c) {
            float h, s, l;
            c.getHslF(&h, &s, &l);
            return QColor::fromHslF(h < 0 ? 0 : h, qMin(1.0f, s * 1.4f + 0.15f), qBound(0.45f, l * 1.2f, 0.68f));
        };
        if (!m_art.isNull())
        {
            // Самые «сочные» цвета обложки: из уменьшенной до 16×16 картинки берём насыщенный и заметный цвет,
            // второй — тоже яркий, но другого оттенка. Среднее по картинке дало бы серый.
            const QImage small16 = m_art.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
            QColor best, second;
            qreal bestScore = -1, secondScore = -1;
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                {
                    const QColor c = small16.pixelColor(x, y);
                    const qreal score = c.hsvSaturationF() * (0.35 + 0.65 * c.valueF());
                    if (score > bestScore)
                    {
                        bestScore = score;
                        best = c;
                    }
                }
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                {
                    const QColor c = small16.pixelColor(x, y);
                    qreal dh = std::abs(c.hsvHueF() - best.hsvHueF());
                    dh = qMin(dh, 1.0 - dh);
                    if (dh < 0.08)
                        continue; // слишком похож на первый
                    const qreal score = c.hsvSaturationF() * (0.35 + 0.65 * c.valueF());
                    if (score > secondScore)
                    {
                        secondScore = score;
                        second = c;
                    }
                }
            m_glowA = vivid(best);
            m_glowB = (second.isValid() && secondScore > 0.12) ? vivid(second) : [&] {
                QColor h = m_glowA.toHsl(); // обложка почти одного цвета — берём соседний оттенок
                h.setHslF(std::fmod(h.hslHueF() + 0.08f + 1.0f, 1.0f), h.hslSaturationF(), h.lightnessF());
                return h.toRgb();
            }();
        }
        else
        {
            m_glowA = QColor();
            m_glowB = QColor();
        }
    }
    // Скрыт крестиком — пока где-нибудь снова не включат музыку или видео.
    // Сначала ждём, что пауза дошла до плеера (иначе первый же ответ «ещё играет» вернул бы плеер обратно).
    if (m_dismissed)
    {
        if (!s.present || !s.playing)
            m_sawPause = true;
        else if (m_sawPause)
            m_dismissed = false;
    }
    updateAnimTimer();
    if (before != hasMedia())
        emit availabilityChanged();
    update();
}

QRect MediaPlayer::partRect(int part) const
{
    return m_panel ? Skin::mediaPanelRect(rect(), part, Settings::get().mediaArt == 1) : Skin::mediaPartRect(rect(), part);
}

qint64 MediaPlayer::position() const
{
    qint64 pos = m_positionMs;
    if (m_playing && m_posClock.isValid())
        pos += m_posClock.elapsed();
    return qBound<qint64>(0, pos, m_durationMs);
}

int MediaPlayer::partAt(const QPoint& pos) const
{
    for (int part : {int(Skin::MediaPrev), int(Skin::MediaPlay), int(Skin::MediaNext), int(Skin::MediaClose), int(Skin::MediaCollapse)})
        if (partRect(part).contains(pos))
            return part;
    if (m_panel && m_durationMs > 0 && partRect(Skin::MediaSeek).adjusted(-4, -6, 4, 6).contains(pos))
        return Skin::MediaSeek;
    return rect().contains(pos) ? int(Skin::MediaBody) : -1;
}

qreal MediaPlayer::seekFracAt(const QPoint& pos) const
{
    const QRect sr = partRect(Skin::MediaSeek);
    return qBound(0.0, double(pos.x() - sr.left()) / qMax(1, sr.width()), 1.0);
}

void MediaPlayer::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    MediaPaint m;
    m.r = rect();
    m.title = m_title;
    m.artist = m_artist;
    m.art = m_art;
    m.playing = m_playing;
    m.canPrev = m_canPrev;
    m.canNext = m_canNext;
    m.hover = m_hover;
    m.pressed = m_pressed;
    // Цвет плеера: акцент стиля или цвета обложки (если обложки нет — тоже акцент).
    const Settings& s = Settings::get();
    QColor ca, cb;
    if (s.mediaColor == 1 && m_glowA.isValid())
    {
        ca = m_glowA;
        cb = m_glowB;
    }
    else
    {
        ca = Theme::Accent.color();
        QColor h = ca.toHsl();
        h.setHslF(std::fmod(h.hslHueF() + 0.10f + 1.0f, 1.0f), h.hslSaturationF(), h.lightnessF());
        cb = h.toRgb(); // соседний оттенок — чтобы у подсветки и градиентов было два цвета
    }
    if (s.mediaColor == 1 && m_glowA.isValid())
    {
        m.tintA = ca;
        m.tintB = cb;
    }
    else if (Skin::current().kind() != Skin::Bento)
        m.tintA = m.tintB = ca; // у Bento свой градиент акцента — его и оставляем
    if (!m_panel)
    {
        Skin::current().paintMediaPlayer(p, m);
        return;
    }
    m.panel = true;
    m.positionMs = position();
    m.durationMs = m_durationMs;
    m.canSeek = m_canSeek;
    m.seekFrac = m_seekFrac;
    m.artBackground = s.mediaArt == 1 && !m_artBlur.isNull();
    m.artOpacity = s.mediaArtOpacity / 100.0;
    m.artBlur = m_artBlur;
    m.wavePhase = m_phase;
    static const bool demo = qEnvironmentVariableIsSet("STARTMENU_DEMO_GLOW");
    if (demo)
    {
        const_cast<MediaPlayer*>(this)->m_amp = 1.0;
        const_cast<MediaPlayer*>(this)->m_glow = 0.85;
        const_cast<MediaPlayer*>(this)->m_phase = 2.0;
    }
    m.waveAmp = s.mediaWave ? m_amp : 0.0;
    if (s.mediaGlow)
    {
        m.glow = m_glow;
        m.glowA = ca;
        m.glowB = cb;
    }
    m.panelRadius = s.effect == Settings::Effect::Acrylic ? Skin::glassRadius() : Theme::Radius;
    Skin::current().paintMediaPanel(p, m);
}

// ---------- волна ----------

float MediaPlayer::audioLevel()
{
    if (!m_meter)
    {
        // Общий уровень звука устройства вывода — по нему «дышит» волна.
        IMMDeviceEnumerator* en = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))))
        {
            IMMDevice* dev = nullptr;
            if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)))
            {
                dev->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&m_meter));
                dev->Release();
            }
            en->Release();
        }
        if (!m_meter)
            return 0.5f;
    }
    float v = 0;
    if (FAILED(m_meter->GetPeakValue(&v)))
    {
        m_meter->Release(); // устройство сменили (наушники) — переподключимся в следующий раз
        m_meter = nullptr;
        return 0.5f;
    }
    return v;
}

void MediaPlayer::updateAnimTimer()
{
    if (!m_anim)
        return;
    const Settings& s = Settings::get();
    const bool want = m_panel && isVisible() && (s.mediaWave || s.mediaGlow) && (m_playing || m_amp > 0.01 || m_glow > 0.01);
    if (want && !m_anim->isActive())
        m_anim->start();
    else if (!want && m_anim->isActive())
    {
        m_anim->stop();
        m_amp = 0;
    }
}

void MediaPlayer::animate()
{
    const qreal peak = m_playing ? qMin(1.0, double(audioLevel()) * 1.6) : 0.0;
    const qreal target = m_playing ? 0.3 + 0.7 * peak : 0.0;
    m_level += (target - m_level) * 0.25;
    m_amp += (m_level - m_amp) * 0.3;
    // Подсветка «в ритм»: удар — вспышка сразу, затухание плавное.
    m_env = qMax(peak, m_env * 0.86);
    const qreal glowTarget = m_playing ? 0.15 + 0.85 * std::pow(m_env, 1.6) : 0.0;
    m_glow += (glowTarget - m_glow) * (glowTarget > m_glow ? 0.55 : 0.12);
    if (m_playing)
        m_phase += 0.16;
    if (!m_playing && m_amp < 0.01 && m_glow < 0.01)
        updateAnimTimer();
    if (Settings::get().mediaGlow)
        update(); // подсветка занимает всю панель
    else
        update(partRect(Skin::MediaSeek).adjusted(-60, -12, 60, 12));
}

void MediaPlayer::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    m_tick->start();
    updateAnimTimer();
}

void MediaPlayer::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    if (m_tick)
        m_tick->stop();
    if (m_anim)
        m_anim->stop();
    if (m_meter)
    {
        m_meter->Release();
        m_meter = nullptr;
    }
}

// ---------- мышь ----------

void MediaPlayer::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint pos = e->position().toPoint();
    if (m_seekFrac >= 0)
    {
        m_seekFrac = seekFracAt(pos);
        update();
        return;
    }
    const int h = partAt(pos);
    if (h != m_hover)
    {
        m_hover = h;
        // Подсказка у каждой кнопки: что она сделает
        switch (h)
        {
        case Skin::MediaPrev:     setToolTip(TXT("Предыдущий трек")); break;
        case Skin::MediaPlay:     setToolTip(m_playing ? TXT("Пауза") : TXT("Играть")); break;
        case Skin::MediaNext:     setToolTip(TXT("Следующий трек")); break;
        case Skin::MediaClose:    setToolTip(TXT("Пауза и скрыть плеер — появится снова, когда заиграет музыка")); break;
        case Skin::MediaCollapse: setToolTip(TXT("Свернуть в строку поиска")); break;
        case Skin::MediaSeek:     setToolTip(TXT("Перемотать")); break;
        default:
            setToolTip(m_panel ? QString() : (m_artist.isEmpty() ? m_title : m_title + QStringLiteral(" — ") + m_artist) +
                                                 TXT("\nКлик — открыть большой плеер"));
        }
        const bool clickable = (h > int(Skin::MediaBody) && (h != Skin::MediaSeek || m_canSeek)) ||
                               (!m_panel && h == Skin::MediaBody);
        setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void MediaPlayer::leaveEvent(QEvent*)
{
    if (m_seekFrac >= 0)
        return;
    m_hover = -1;
    update();
}

void MediaPlayer::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_pressed = partAt(e->position().toPoint());
    if (m_pressed == Skin::MediaSeek && m_canSeek)
        m_seekFrac = seekFracAt(e->position().toPoint());
    update();
}

void MediaPlayer::mouseReleaseEvent(QMouseEvent* e)
{
    const int part = partAt(e->position().toPoint());
    const int pressed = m_pressed;
    m_pressed = -1;
    if (m_seekFrac >= 0)
    {
        // Отпустили ползунок — перематываем туда.
        const qint64 ms = qint64(m_seekFrac * m_durationMs);
        m_seekFrac = -1;
        m_positionMs = ms;
        m_posClock.start();
        command(Media::Command::Seek, ms);
        update();
        return;
    }
    update();
    if (e->button() != Qt::LeftButton || part != pressed)
        return;
    switch (part)
    {
    case Skin::MediaPlay:
        m_positionMs = position(); // зафиксировать позицию на паузе
        m_posClock.start();
        m_playing = !m_playing; // сразу показываем новое состояние, не дожидаясь плеера
        command(Media::Command::PlayPause);
        updateAnimTimer();
        break;
    case Skin::MediaNext:
        if (m_canNext)
            command(Media::Command::Next);
        break;
    case Skin::MediaPrev:
        if (m_canPrev)
            command(Media::Command::Previous);
        break;
    case Skin::MediaClose:
        // Ставим на паузу и прячем; приложение-плеер не закрываем.
        m_sawPause = !m_playing;
        if (m_playing)
            command(Media::Command::Pause);
        m_playing = false;
        m_dismissed = true;
        m_hover = -1;
        emit availabilityChanged();
        break;
    case Skin::MediaCollapse:
        setMode(1); // свернуть в строку поиска
        break;
    case Skin::MediaBody:
        if (!m_panel)
            setMode(2); // клик по треку в компактном — раскрыть панель
        break;
    default:
        break;
    }
}

void MediaPlayer::setMode(int mode)
{
    // Настройку берём свежую из файла (её могло поменять окно настроек) и сообщаем окну настроек.
    Settings& s = Settings::get();
    {
        const FileLock lock;
        s.load();
        s.mediaPlayer = mode;
        s.save();
    }
    s.apply();
    if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\StartMenu.App.SettingsReload"))
    {
        SetEvent(ev);
        CloseHandle(ev);
    }
    m_hover = -1;
    emit availabilityChanged();
}

void MediaPlayer::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu menu(this);
    const Settings& s = Settings::get();
    auto add = [&](const QString& text, bool checked) {
        QAction* a = menu.addAction(text);
        a->setCheckable(true);
        a->setChecked(checked);
        return a;
    };
    QAction* big = add(TXT("Большой — над меню"), s.mediaPlayer == 2);
    QAction* compactAct = add(TXT("Компактный — в строке поиска"), s.mediaPlayer == 1);
    menu.addSeparator();
    QAction* artThumb = add(TXT("Обложка миниатюрой"), s.mediaArt == 0);
    QAction* artBg = add(TXT("Обложка на весь фон"), s.mediaArt == 1);
    QAction* wave = add(TXT("Волна на ползунке"), s.mediaWave);
    QAction* glow = add(TXT("Подсветка в ритм музыки"), s.mediaGlow);
    glow->setEnabled(s.mediaPlayer == 2);
    menu.addSeparator();
    QAction* colorAccent = add(TXT("Цвет — акцент стиля"), s.mediaColor == 0);
    QAction* colorArt = add(TXT("Цвет — из обложки"), s.mediaColor == 1);
    artThumb->setEnabled(s.mediaPlayer == 2);
    artBg->setEnabled(s.mediaPlayer == 2);
    wave->setEnabled(s.mediaPlayer == 2);
    menu.addSeparator();
    QAction* off = menu.addAction(TXT("Не показывать плеер"));
    emit busyChanged(true);
    QAction* chosen = menu.exec(e->globalPos());
    emit busyChanged(false);
    if (!chosen)
        return;
    if (chosen == big)
        setMode(2);
    else if (chosen == compactAct)
        setMode(1);
    else if (chosen == off)
        setMode(0);
    else
    {
        Settings& w = Settings::get();
        const FileLock lock;
        w.load();
        if (chosen == artThumb)
            w.mediaArt = 0;
        else if (chosen == artBg)
            w.mediaArt = 1;
        else if (chosen == wave)
            w.mediaWave = !w.mediaWave;
        else if (chosen == glow)
            w.mediaGlow = !w.mediaGlow;
        else if (chosen == colorAccent)
            w.mediaColor = 0;
        else if (chosen == colorArt)
            w.mediaColor = 1;
        w.save();
        w.apply();
        if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\StartMenu.App.SettingsReload"))
        {
            SetEvent(ev);
            CloseHandle(ev);
        }
        updateAnimTimer();
        update();
    }
}
