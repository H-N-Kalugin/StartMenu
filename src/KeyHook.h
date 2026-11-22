#pragma once
#include <QObject>

// Глобальный перехват ввода (процесс трея, свой поток — см. KeyHook.cpp):
//  • одиночное нажатие Win открывает наше меню, комбинации (Win+E, Win+D…) работают как обычно;
//  • клик по кнопке «Пуск» на панели задач тоже открывает наше меню (по желанию);
//  • сторожок: если Windows молча сняла хук (так бывает при подвисании системы), ставим заново.
class KeyHook : public QObject
{
    Q_OBJECT
public:
    static KeyHook& instance();
    bool install();
    void uninstall();
    bool installed() const;

    void setStartButtonEnabled(bool on);
    void setWinKeyEnabled(bool on);
    void checkAlive();         // звать по таймеру раз в несколько секунд
    void requestRelocate();    // экраны/панель задач поменялись — найти кнопку «Пуск» заново
    void reinstallOnTop();     // переставить перехват клавиатуры первым (поверх чужих, например Start11)
    void resyncState();        // сбросить «залипшую» Win/кнопку мыши, если отпускание до нас не дошло
    static bool otherStartMenuRunning(); // запущен ли Start11 или похожая программа

signals:
    void winTapped();

private:
    KeyHook() = default;
};
