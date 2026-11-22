#include "Backups.h"
#include "Config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <windows.h>

namespace
{
constexpr int kKeep = 7;

void prune()
{
    QDir d(Backups::dir());
    const QFileInfoList files = d.entryInfoList({QStringLiteral("items-*.json")}, QDir::Files, QDir::Time);
    for (int i = kKeep; i < files.size(); ++i)
        QFile::remove(files[i].absoluteFilePath());
}

QString itemsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/items.json");
}
}

namespace Backups
{
QString dir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/backups");
    QDir().mkpath(d);
    return d;
}

void daily(const QString& path)
{
    if (!QFile::exists(path))
        return;
    const QString copy = dir() + QStringLiteral("/items-%1.json").arg(QDate::currentDate().toString(Qt::ISODate));
    if (QFile::exists(copy))
        return; // сегодня копия уже есть
    QFile::copy(path, copy);
    prune();
}

QList<Entry> list()
{
    QList<Entry> out;
    const QFileInfoList files = QDir(dir()).entryInfoList({QStringLiteral("items-*.json")}, QDir::Files, QDir::Time);
    for (const QFileInfo& fi : files)
        out.append({fi.absoluteFilePath(), fi.lastModified()});
    return out;
}

bool isValidMenuFile(const QString& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 32 * 1024 * 1024)
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isObject() && (doc.object().contains(QStringLiteral("layouts")) || doc.object().contains(QStringLiteral("groups")));
}

bool exportTo(const QString& file)
{
    QFile::remove(file);
    return QFile::copy(itemsPath(), file);
}

bool restore(const QString& file)
{
    if (!isValidMenuFile(file))
        return false;
    QFile src(file);
    if (!src.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = src.readAll();

    const FileLock lock;
    // Текущее меню — в копии с отметкой времени, чтобы восстановление тоже можно было отменить
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
    QFile::copy(itemsPath(), dir() + QStringLiteral("/items-%1.json").arg(now));
    prune();
    QSaveFile f(itemsPath());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    if (!f.commit())
        return false;
    // Открытое меню перечитает плитки
    if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\StartMenu.App.Items"))
    {
        SetEvent(ev);
        CloseHandle(ev);
    }
    return true;
}
}
