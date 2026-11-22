#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QWidget>
#include <memory>

namespace Media { class Watcher; struct State; enum class Command; }
class QTimer;
struct IAudioMeterInformation;

// Плеер: что сейчас играет в Windows (Spotify, браузер и т.п.) через системные медиа-кнопки.
// Два вида одного и того же виджета:
//  - компактный — в строке поиска; клик по треку раскрывает панель;
//  - панель — отдельным окном над меню: крупно, ползунок-волна с перемоткой, обложка миниатюрой или фоном.
// ✕ — пауза и скрыть, пока музыка или видео снова не заиграют. ПКМ — вид и обложка.
class MediaPlayer : public QWidget
{
    Q_OBJECT
public:
    explicit MediaPlayer(QWidget* parent);
    ~MediaPlayer() override;

    bool hasMedia() const;    // есть что показать (играет/на паузе, не скрыт крестиком, плеер включён)
    bool wantsPanel() const;  // в настройках выбран большой плеер над меню
    void setPanel(bool panel);
    void refresh();           // перечитать (при открытии меню)
    void setWindowShown(bool shown); // меню показано/спрятано — спрятанному не нужны частые обновления позиции

signals:
    void availabilityChanged(); // появился/пропал трек или сменился вид — окну нужно переразложиться
    void busyChanged(bool busy); // открыто контекстное меню — окну нельзя закрываться от потери фокуса

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    void apply(const Media::State& s);
    int partAt(const QPoint& pos) const;
    QRect partRect(int part) const;
    qint64 position() const;               // позиция сейчас (досчитанная от последнего ответа плеера)
    qreal seekFracAt(const QPoint& pos) const;
    void setMode(int mode);                // 0 — выключен, 1 — компактный, 2 — панель (сохраняется в настройки)
    void animate();                        // кадр волны
    float audioLevel();                    // громкость того, что звучит сейчас (0…1)
    void updateAnimTimer();
    void command(Media::Command c, qint64 ms = 0); // команда плееру (в демо-режиме плеера нет — ничего)

    std::unique_ptr<Media::Watcher> m_watcher;
    QTimer* m_tick = nullptr;              // перерисовка времени, пока играет
    QTimer* m_anim = nullptr;              // волна (~30 кадров/с, только панель)
    IAudioMeterInformation* m_meter = nullptr;
    bool m_panel = false;
    bool m_present = false, m_dismissed = false;
    QString m_title, m_artist, m_key;
    bool m_sawPause = false; // после ✕ музыка уже стояла на паузе — следующее «играет» снова покажет плеер
    QImage m_art, m_artBlur;
    QString m_app;                         // источник звука (Spotify.exe, Chrome…)
    QImage m_appIcon, m_appSoft;           // его значок и размытый фон — показываем вместо отсутствующей обложки
    QColor m_appPlate;
    void resolveApp(const QString& appId);
    bool m_playing = false, m_canPrev = false, m_canNext = false, m_canSeek = false;
    qint64 m_positionMs = 0, m_durationMs = 0;
    QElapsedTimer m_posClock;
    qreal m_seekFrac = -1;                 // тянем ползунок
    qreal m_phase = 0, m_amp = 0, m_level = 0;
    qreal m_env = 0, m_glow = 0;           // огибающая громкости (быстро растёт, медленно спадает) и яркость подсветки
    QColor m_glowA, m_glowB;               // цвета подсветки из обложки
    int m_hover = -1, m_pressed = -1;
};
