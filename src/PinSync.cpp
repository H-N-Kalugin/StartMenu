#include "PinSync.h"
#include "Lang.h"
#include "Config.h"
#include "Theme.h"

#include "Shell.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>

#include <windows.h>

namespace
{
    const QString kPinnedGroupRu = QStringLiteral("Закреплённые"); // так группа называлась в старых версиях
    bool isPinnedGroup(const QString& title) { return Lang::localizeDefault(title) == Lang::tr(kPinnedGroupRu); }

    QString norm(const QString& path)
    {
        return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
    }

    bool isPinnedAnywhere(const QString& path)
    {
        const QString key = norm(path);
        for (const Layout& l : Config::instance().layouts())
            for (const Group& g : l.groups)
                for (const Tab& tab : g.tabs)
                    for (const Tile& t : tab.tiles)
                    {
                        if (norm(t.item.path) == key)
                            return true;
                        for (const Item& c : t.children)
                            if (norm(c.path) == key)
                                return true;
                    }
        return false;
    }

    // Группа «Закреплённые» в активном пространстве; нет — создаём в первой колонке.
    Group& pinnedGroup()
    {
        auto& groups = Config::instance().groups();
        for (Group& g : groups)
            for (const Tab& tab : g.tabs)
                if (isPinnedGroup(tab.title))
                    return g;
        Group g;
        g.title() = Lang::tr(kPinnedGroupRu);
        g.column = 0;
        groups.append(g);
        return groups.last();
    }

    void addItem(const Item& item)
    {
        Group& g = pinnedGroup();
        // Плитку кладём во вкладку «Закреплённые», даже если сейчас активна другая.
        for (Tab& tab : g.tabs)
            if (isPinnedGroup(tab.title))
            {
                tab.tiles.append({item, TileSize::Medium, false, {}});
                return;
            }
        g.tiles().append({item, TileSize::Medium, false, {}});
    }

    void addTile(const QString& path)
    {
        addItem(Config::itemFromPath(path));
    }

    QString start2Bin()
    {
        return qEnvironmentVariable("LOCALAPPDATA")
               + QStringLiteral("/Packages/Microsoft.Windows.StartMenuExperienceHost_cw5n1h2txyewy/LocalState/start2.bin");
    }

    // %APPDATA%\… → C:\Users\…
    QString expandEnv(QString s)
    {
        static const QRegularExpression re(QStringLiteral("%([^%]+)%"));
        auto m = re.match(s);
        while (m.hasMatch())
        {
            s.replace(m.capturedStart(), m.capturedLength(), qEnvironmentVariable(m.captured(1).toUtf8().constData()));
            m = re.match(s);
        }
        return QDir::fromNativeSeparators(s);
    }

    int removeEverywhere(const QString& path)
    {
        const QString key = norm(path);
        int removed = 0;
        for (Layout& l : Config::instance().layouts())
            for (Group& g : l.groups)
                for (Tab& tab : g.tabs)
                {
                    for (int i = int(tab.tiles.size()) - 1; i >= 0; --i)
                    {
                        Tile& t = tab.tiles[i];
                        if (!t.isFolder && norm(t.item.path) == key)
                        {
                            tab.tiles.removeAt(i);
                            ++removed;
                            continue;
                        }
                        removed += int(t.children.removeIf([&](const Item& c) { return norm(c.path) == key; }));
                    }
                }
        return removed;
    }

    // Куда пишем пункт контекстного меню: все файлы и папки.
    const char* kVerbRoots[] = {"HKEY_CURRENT_USER\\Software\\Classes\\*\\shell\\StartMenu.App.Pin",
                                "HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\StartMenu.App.Pin"};
}

namespace PinSync
{
QString windowsPinFolder()
{
    return qEnvironmentVariable("APPDATA")
           + QStringLiteral("/Microsoft/Internet Explorer/Quick Launch/User Pinned/StartMenu");
}

bool syncFromWindows()
{
    auto& cfg = Config::instance();
    QSet<QString> now;
    for (const QFileInfo& fi : QDir(windowsPinFolder()).entryInfoList({QStringLiteral("*.lnk")}, QDir::Files))
        now.insert(QDir::fromNativeSeparators(fi.absoluteFilePath()));

    // Первый запуск: всё, что уже лежит в папке, считаем «увиденным» и не добавляем
    // (эти ярлыки уже перенесены из Start11 или сознательно не закреплены у нас).
    if (!cfg.pinFolderKnown())
    {
        cfg.pinSeen() = QStringList(now.begin(), now.end());
        cfg.setPinFolderKnown();
        return true;
    }

    bool changed = false;
    QSet<QString> seen;
    for (const QString& s : cfg.pinSeen())
        seen.insert(norm(s));

    for (const QString& path : now)
        if (!seen.contains(norm(path)))
        {
            if (!isPinnedAnywhere(path))
                addTile(path);
            cfg.pinSeen().append(path);
            changed = true;
        }

    QSet<QString> nowKeys;
    for (const QString& p : now)
        nowKeys.insert(norm(p));
    for (int i = int(cfg.pinSeen().size()) - 1; i >= 0; --i)
        if (!nowKeys.contains(norm(cfg.pinSeen()[i])))
        {
            // Открепили через Windows — убираем и у нас.
            removeEverywhere(cfg.pinSeen()[i]);
            cfg.pinSeen().removeAt(i);
            changed = true;
        }

    if (changed)
        cfg.normalize();
    return changed;
}

void syncStartLayoutAsync(QObject* context, std::function<void()> onChanged)
{
    const QFileInfo bin(start2Bin());
    if (!bin.exists())
        return;
    const qint64 stamp = bin.lastModified().toMSecsSinceEpoch();
    if (stamp == Config::instance().startStamp())
        return; // с прошлого раза ничего не закрепляли и не открепляли — ничего не делаем

    const QString out = QDir::tempPath() + QStringLiteral("/StartMenu-startlayout.json");
    QFile::remove(out);
    auto* proc = new QProcess(context);
    proc->setProgram(QStringLiteral("powershell.exe"));
    proc->setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
                        QStringLiteral("Export-StartLayout -Path '%1'").arg(QDir::toNativeSeparators(out))});
    proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
        a->flags |= 0x08000000; // CREATE_NO_WINDOW — без мигающей консоли
        a->startupInfo->dwFlags |= STARTF_FORCEOFFFEEDBACK; // и без «крутящегося» курсора
    });
    QObject::connect(proc, &QProcess::finished, context, [proc, out, stamp, onChanged] {
        proc->deleteLater();
        QFile f(out);
        if (!f.open(QIODevice::ReadOnly))
            return;
        const QJsonArray list = QJsonDocument::fromJson(f.readAll()).object()["pinnedList"].toArray();
        f.close();
        QFile::remove(out);

        // Что сейчас закреплено в «Пуске» Windows: ярлыки и приложения Store.
        QStringList now;
        for (const auto& v : list)
        {
            const QJsonObject o = v.toObject();
            if (o.contains("desktopAppLink"))
                now.append(expandEnv(o["desktopAppLink"].toString()));
            else if (o.contains("packagedAppId"))
                now.append(QStringLiteral("shell:AppsFolder\\") + o["packagedAppId"].toString());
        }

        auto& cfg = Config::instance();
        bool changed = false;
        if (cfg.startStamp() == 0)
        {
            // Первый раз: всё уже закреплённое в Windows считаем «увиденным», чтобы не засыпать группы
            // старыми закреплениями. Дальше в «Закреплённые» попадает только новое.
            cfg.startSeen() = now;
        }
        else
        {
            QSet<QString> seen;
            for (const QString& s : cfg.startSeen())
                seen.insert(norm(s));
            QSet<QString> nowKeys;
            QList<Item> apps; // названия приложений Store — по запросу
            for (const QString& p : now)
            {
                nowKeys.insert(norm(p));
                if (seen.contains(norm(p)) || isPinnedAnywhere(p))
                    continue;
                if (p.startsWith(QStringLiteral("shell:"), Qt::CaseInsensitive))
                {
                    if (apps.isEmpty())
                        apps = WinShell::allApps();
                    QString name = p.section(QLatin1Char('\\'), -1);
                    for (const Item& a : apps)
                        if (a.path.compare(p, Qt::CaseInsensitive) == 0)
                            name = a.name;
                    addItem({name, p, {}, {}});
                }
                else if (QFileInfo::exists(p))
                    addTile(p);
                changed = true;
            }
            for (const QString& s : cfg.startSeen())
                if (!nowKeys.contains(norm(s)))
                    changed |= removeEverywhere(s) > 0; // открепили в Windows — убираем и у нас
            cfg.startSeen() = now;
        }
        cfg.setStartStamp(stamp);
        cfg.normalize();
        cfg.save();
        if (changed && onChanged)
            onChanged();
    });
    proc->start();
}

int importWindowsPins()
{
    // Всё, что сейчас закреплено в «Пуске» Windows (и в его папке ярлыков), — в группу «Закреплённые».
    // Для мастера первого запуска: чтобы человек не начинал с пустого меню. Ждём экспорт до 20 с.
    auto& cfg = Config::instance();
    QStringList paths;
    const QFileInfo bin(start2Bin());
    if (bin.exists())
    {
        const QString out = QDir::tempPath() + QStringLiteral("/StartMenu-startlayout-import.json");
        QFile::remove(out);
        QProcess ps;
        ps.setProgram(QStringLiteral("powershell.exe"));
        ps.setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
                         QStringLiteral("Export-StartLayout -Path '%1'").arg(QDir::toNativeSeparators(out))});
        ps.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
            a->flags |= 0x08000000; // без окна консоли
            a->startupInfo->dwFlags |= STARTF_FORCEOFFFEEDBACK;
        });
        ps.start();
        ps.waitForFinished(20000);
        QFile f(out);
        if (f.open(QIODevice::ReadOnly))
        {
            for (const auto& v : QJsonDocument::fromJson(f.readAll()).object()["pinnedList"].toArray())
            {
                const QJsonObject o = v.toObject();
                if (o.contains("desktopAppLink"))
                    paths.append(expandEnv(o["desktopAppLink"].toString()));
                else if (o.contains("packagedAppId"))
                    paths.append(QStringLiteral("shell:AppsFolder\\") + o["packagedAppId"].toString());
            }
            f.close();
            QFile::remove(out);
        }
        // Эти закрепления уже перенесены — фоновая синхронизация не должна добавить их второй раз
        cfg.startSeen() = paths;
        cfg.setStartStamp(bin.lastModified().toMSecsSinceEpoch());
    }
    QStringList folder;
    for (const QFileInfo& fi : QDir(windowsPinFolder()).entryInfoList({QStringLiteral("*.lnk")}, QDir::Files))
        folder.append(QDir::fromNativeSeparators(fi.absoluteFilePath()));
    cfg.pinSeen() = folder;
    cfg.setPinFolderKnown();
    paths += folder;

    int added = 0;
    QList<Item> apps; // названия приложений Store — по запросу
    for (const QString& p : paths)
    {
        if (isPinnedAnywhere(p))
            continue;
        if (p.startsWith(QStringLiteral("shell:"), Qt::CaseInsensitive))
        {
            if (apps.isEmpty())
                apps = WinShell::allApps();
            QString name;
            for (const Item& a : apps)
                if (a.path.compare(p, Qt::CaseInsensitive) == 0)
                    name = a.name;
            if (name.isEmpty())
                continue; // приложения уже нет
            addItem({name, p, {}, {}});
        }
        else if (QFileInfo::exists(p))
            addTile(p);
        else
            continue;
        ++added;
    }
    cfg.normalize();
    return added;
}

bool pinPath(const QString& path)
{
    const QFileInfo fi(path);
    if (!fi.exists() || isPinnedAnywhere(fi.absoluteFilePath()))
        return false;
    addTile(fi.absoluteFilePath());
    Config::instance().normalize();
    return true;
}

bool explorerVerbInstalled()
{
    return QSettings(QString::fromLatin1(kVerbRoots[0]), QSettings::NativeFormat).contains(QStringLiteral("MUIVerb"));
}

void setExplorerVerb(bool on)
{
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    for (const char* root : kVerbRoots)
    {
        QSettings key(QString::fromLatin1(root), QSettings::NativeFormat);
        if (!on)
        {
            key.remove(QString()); // весь ключ вместе с command
            continue;
        }
        key.setValue(QStringLiteral("MUIVerb"), TXT("Закрепить в StartMenu"));
        key.setValue(QStringLiteral("Icon"), QStringLiteral("%1,0").arg(exe)); // без кавычек — иначе Проводник не находит иконку
        key.setValue(QStringLiteral("command/."), QStringLiteral("\"%1\" --pin \"%2\"").arg(exe, QStringLiteral("%1")));
    }
}
}
