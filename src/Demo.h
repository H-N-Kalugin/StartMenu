#pragma once
#include <QImage>
#include <QList>
#include <QString>

#include "ClipboardStore.h"
#include "MediaSession.h"

// Демо-режим для снимков (StartMenu.exe --render <png> --demo): вместо личных данных пользователя —
// нейтральная раскладка, выдуманная история буфера, трек в плеере и цифры монитора.
// Всё живёт только в памяти: settings.json, items.json и история буфера не читаются и не пишутся.
namespace Demo
{
    void enable();
    bool active();

    void fillConfig();                             // демо-плитки в Config::instance() (на диск не сохраняется)
    QImage icon(const QString& source, int px);    // значок для «demo:…» (пустая картинка — не демо-значок)
    QList<ClipboardStore::Entry> clipboard();      // записи истории буфера
    QImage clipboardImage(const QString& name);    // картинка записи буфера
    Media::State player();                         // «играющий» трек
}
