#pragma once
#include <QIcon>
#include <QImage>
#include <QString>

// Иконка программы: четыре плитки на тёмной скруглённой подложке. Рисуется кодом —
// для трея, окон и для app.ico, который встраивается в exe (см. app.rc).
namespace AppIcon
{
    QImage render(int px);
    QIcon icon();                        // все размеры сразу — для окон и трея
    bool writeIco(const QString& path);  // StartMenu.exe --write-ico <путь>
}
