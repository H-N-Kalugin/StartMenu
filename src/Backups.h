#pragma once
#include <QDateTime>
#include <QList>
#include <QString>

// Резервные копии плиток и групп (items.json).
// Раз в день перед сохранением — копия в %APPDATA%/StartMenu/backups, хранятся последние 7.
namespace Backups
{
struct Entry
{
    QString file;
    QDateTime time;
};

QString dir();
void daily(const QString& itemsPath);       // вызывается перед каждым сохранением items.json
QList<Entry> list();                         // новые сверху
bool isValidMenuFile(const QString& file);  // это действительно файл меню StartMenu?
bool exportTo(const QString& file);          // «Сохранить меню в файл…»
bool restore(const QString& file);           // подменить текущее меню; текущее сначала уходит в копии
}
