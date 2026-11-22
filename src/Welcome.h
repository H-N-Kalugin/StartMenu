#pragma once
#include <QImage>
#include <QList>
#include <QWidget>

#include "Config.h"

class QStackedWidget;
class QListWidget;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class StyleCard;

// Мастер первого запуска (StartMenu.exe --welcome; трей открывает его сам, если настроек ещё нет):
// 1) выбрать стиль по живым превью (+ заливка плиток), 2) выбрать приложения или начать с пустого меню,
// 3) подсказки и «Открыть меню».
class WelcomeWindow : public QWidget
{
    Q_OBJECT
public:
    explicit WelcomeWindow(int skin = -1, bool fill = false); // skin/fill — сохранить выбор при смене языка

    // Превью стиля: то же рисование, что и в меню, только с плитками-заглушками.
    static QImage renderPreview(int skin, bool fill, qreal dpr);
    // Для проверки без показа: сохранить страницы мастера в PNG.
    void saveShots(const QString& dir);

private:
    QWidget* buildStylePage();
    QWidget* buildAppsPage();
    QWidget* buildDonePage();
    void refreshPreviews();
    void selectStyle(int skin);
    void goTo(int page);
    void loadApps();
    void loadSomeIcons();
    void setRecommended();
    void finish(bool openMenu);

    QStackedWidget* m_pages = nullptr;
    QList<StyleCard*> m_cards;
    QCheckBox* m_fill = nullptr;
    int m_skin = 1;

    QListWidget* m_apps = nullptr;
    QLineEdit* m_filter = nullptr;
    QCheckBox* m_start11 = nullptr;
    QCheckBox* m_clipboard = nullptr;     // включить историю буфера обмена (по умолчанию нет)
    QCheckBox* m_windowsPins = nullptr;   // перенести закреплённое в «Пуске» Windows
    QLabel* m_count = nullptr;
    QLabel* m_final = nullptr;       // превью выбранного стиля на последней странице
    QList<Item> m_allApps;
    int m_iconNext = 0;              // иконки списка грузятся порциями, чтобы окно не подвисало
    bool m_saved = false;
};
