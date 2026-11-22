#pragma once
#include <QImage>
#include <QWidget>

// Отдельное окошко над меню — «панель» большого плеера. Шириной с меню, с промежутком над ним.
// Фокус не берёт (клик по нему не уводит меню в фон), фон — тот же, что у меню (стекло/размытие/цвет).
class PlayerWindow : public QWidget
{
public:
    explicit PlayerWindow(QWidget* owner);

    static constexpr int PanelHeight = 150;
    static constexpr int Gap = 10;
    static QRect geometryAbove(const QRect& menu);
    static bool fitsAbove(const QRect& menu);   // помещается ли над меню на этом экране

    void prepareBackdrop(const QRect& geo);     // для «своего размытия»: снимок экрана под панелью
    void applyEffect();

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent* e) override;

private:
    QImage m_backdrop;
};
