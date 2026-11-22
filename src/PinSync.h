#pragma once
#include <QString>
#include <functional>

class QObject;

// Связь с «Закрепить на начальном экране» Windows и с нашим пунктом в контекстном меню Проводника.
namespace PinSync
{
    // Папка, куда Windows (и Start11) кладут ярлыки закреплённого в «Пуске».
    QString windowsPinFolder();

    // При открытии меню: новые ярлыки из папки Windows → плитки в группе «Закреплённые»,
    // исчезнувшие (открепили через Windows) → убрать плитки. true, если что-то поменялось.
    bool syncFromWindows();

    // «Пуск» Windows 11 хранит закреплённое в зашифрованном start2.bin. Если файл поменялся с прошлого раза,
    // в фоне просим Windows выгрузить список (Export-StartLayout, ~0,5 с) и сверяем с нашими плитками.
    // onChanged вызывается, если плитки добавились/убрались (уже сохранено).
    void syncStartLayoutAsync(QObject* context, std::function<void()> onChanged);

    // StartMenu.exe --pin "<путь>" — закрепить файл/папку/ярлык (пункт контекстного меню Проводника).
    bool pinPath(const QString& path);
    // Мастер первого запуска: перенести всё закреплённое в «Пуске» Windows. Возвращает, сколько плиток добавлено.
    int importWindowsPins();

    // Пункт «Закрепить в StartMenu» в контекстном меню Проводника (HKCU, без прав администратора).
    bool explorerVerbInstalled();
    void setExplorerVerb(bool on);
}
