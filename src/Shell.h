#pragma once
#include <QList>
#include <QPixmap>
#include <QString>
#include <QWidget>

#include "Config.h"

// Всё, что касается Windows Shell: запуск, иконки, список приложений, фокус окна.
namespace WinShell
{
    void launch(const Item& item, bool asAdmin = false);
    void open(const QString& target);              // ms-settings:, папка, URL
    void showInFolder(const QString& path);
    void openRunDialog();
    // Питание
    enum class Power { Lock, SignOut, Sleep, Hibernate, Restart, Shutdown };
    void power(Power action);
    bool hibernateAvailable();                     // гибернация включена в Windows
    bool isLocalFile(const QString& path);         // false для shell:AppsFolder\... и несуществующих путей
    QString mediaSourcePath(const QString& appId); // источник звука (SourceAppUserModelId) → путь, из которого грузится значок; пусто — не нашли
    QString fileLocation(const Item& item);        // настоящий файл или папка программы (для «Открыть расположение файла»); пусто — не найти

    QPixmap loadIcon(const QString& path, int px); // пустой QPixmap, если не вышло
    QList<Item> allApps();                         // всё из shell:AppsFolder, включая приложения Store
    QString userPicturePath();
    QList<QPair<QString, QString>> installedBrowsers(); // название и путь к exe (из реестра Windows)
    QString defaultBrowserExe();                   // exe браузера по умолчанию (для иконки)

    // Матовое стекло средствами Windows (живое размытие того, что под окном). tint — цвет и прозрачность подложки.
    void setAcrylic(QWidget* window, bool enabled, const QColor& tint);
    void setRoundCorners(QWidget* window, bool round);
    void setCornerRadius(QWidget* window, int px); // Windows 11: 0 — острые, ≤6 — малые, иначе — обычные
    void setDarkTitleBar(QWidget* window);
    QString locationOf(const Item& item);          // куда ведёт плитка: цель ярлыка, путь программы и т.п.
    bool raiseTaskbar(const QRect& onScreen);     // над игрой на весь экран: показать панель задач (true — это была игра)
    void keepTaskbarAbove(const QRect& onScreen); // держать панель задач поверх игры, пока открыто меню
    void startSelf(const QString& arg);
    void startSelf(const QStringList& args);            // запустить StartMenu.exe с ключом (--settings, --menu …)

    // Замер скорости открытия (только если задана переменная STARTMENU_PERF=<файл>):
    // perfMark("этап") запоминает мс от запуска процесса, perfFlush дописывает строку в файл.
    void perfMark(const char* stage);
    void perfFlush(const QString& extra = {});

    // Журнал открытий/закрытий меню: %LOCALAPPDATA%/StartMenu/menu.log (последние ~64 КБ).
    void log(const QString& line);
    QString describeWindow(quintptr hwnd);      // «программа.exe · класс окна» — для журнала
    bool isPassiveWindow(quintptr hwnd);        // оверлей/всплывашка, которые не должны закрывать меню
    bool mouseButtonDown();                     // пользователь сейчас кликает (значит, это его выбор)
    bool switchKeysDown();                      // зажаты Alt или Win — пользователь сам переключает окна (Alt+Tab)

    // Жёсткий минимум рабочего набора: Windows не выгружает нашу память на диск даже при нехватке ОЗУ.
    // headroomMb — запас сверх уже занятого. Права администратора не нужны.
    bool lockWorkingSet(int headroomMb);

    void forceForeground(QWidget* window);
    quintptr foregroundWindow();
    bool foregroundIsOurs();
}
