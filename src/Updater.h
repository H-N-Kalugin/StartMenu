#pragma once
#include <QString>
#include <functional>
#include <optional>

// Проверка обновлений: раз в сутки спрашиваем GitHub о последнем выпуске.
// Ответ лежит в %LOCALAPPDATA%/StartMenu/update.json — его читают меню (точка на «Параметрах») и окно настроек.
namespace Updater
{
struct Info
{
    QString version;   // «1.0.1»
    QString page;      // страница выпуска на GitHub
    QString installer; // прямая ссылка на установщик .exe (может быть пустой)
};

QString currentVersion();
QString issuesUrl();                 // «Сообщить об ошибке / идея»
std::optional<Info> available();     // есть версия новее текущей (по сохранённому ответу)
// Проверить в фоне. force=false — не чаще раза в сутки. done вызывается в главном потоке.
void check(bool force, std::function<void(bool ok)> done = {});
// Скачать установщик и запустить; не вышло — открыть страницу выпуска. done — в главном потоке.
void install(const Info& info, std::function<void(bool ok)> done = {});
}
