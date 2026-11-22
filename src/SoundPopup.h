#pragma once
#include <QWidget>

#include "AudioControl.h"

// Маленькое окошко «Звук» у кнопки на боковой панели: громкость, «без звука», выбор устройства вывода.
class SoundPopup : public QWidget
{
    Q_OBJECT
public:
    explicit SoundPopup(QWidget* parent);
    void popupAt(const QPoint& globalTopLeft); // показать справа от кнопки, не вылезая за экран

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    void reload();
    QRect muteRect() const;
    QRect sliderRect() const;
    QRect deviceRect(int i) const;
    void setFromX(int x);

    QList<Audio::Device> m_devices;
    float m_volume = 0;
    bool m_muted = false;
    bool m_dragging = false;
    int m_hover = -1;          // -2 — кнопка «без звука», 0… — устройство
};
