#include "Config.h"
#include "Backups.h"
#include "Lang.h"
#include "Theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <QHash>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
    const char* sizeName(TileSize s)
    {
        switch (s)
        {
        case TileSize::Small: return "small";
        case TileSize::Wide:  return "wide";
        case TileSize::Large: return "large";
        default:              return "medium";
        }
    }

    TileSize sizeFromName(const QString& s)
    {
        if (s == QLatin1String("small")) return TileSize::Small;
        if (s == QLatin1String("wide"))  return TileSize::Wide;
        if (s == QLatin1String("large")) return TileSize::Large;
        return TileSize::Medium;
    }

    Item itemFromJson(const QJsonObject& o)
    {
        return {o["name"].toString(), o["path"].toString(), o["args"].toString(), o["icon"].toString()};
    }

    QJsonObject itemToJson(const Item& it)
    {
        QJsonObject o{{"name", it.name}, {"path", it.path}};
        if (!it.args.isEmpty())
            o["args"] = it.args;
        if (!it.icon.isEmpty())
            o["icon"] = it.icon;
        return o;
    }

    Tab tabFromJson(const QJsonObject& o)
    {
        Tab tab;
        tab.title = o["title"].toString();
        for (const auto& tv : o["tiles"].toArray())
        {
            const QJsonObject to = tv.toObject();
            Tile t;
            t.item = itemFromJson(to);
            t.size = sizeFromName(to["size"].toString());
            t.isFolder = to["folder"].toBool();
            t.label = to["label"].toInt(-1);
            t.image = to["image"].toString();
            for (const auto& cv : to["items"].toArray())
                t.children.append(itemFromJson(cv.toObject()));
            tab.tiles.append(t);
        }
        return tab;
    }

    QJsonObject tabToJson(const Tab& tab)
    {
        QJsonArray tiles;
        for (const Tile& t : tab.tiles)
        {
            QJsonObject o = itemToJson(t.item);
            o["size"] = sizeName(t.size);
            if (t.label >= 0)
                o["label"] = t.label;
            if (!t.image.isEmpty())
                o["image"] = t.image;
            if (t.isFolder)
            {
                o["folder"] = true;
                QJsonArray children;
                for (const Item& c : t.children)
                    children.append(itemToJson(c));
                o["items"] = children;
            }
            tiles.append(o);
        }
        return QJsonObject{{"title", tab.title}, {"tiles", tiles}};
    }

    QList<Group> groupsFromJson(const QJsonArray& arr)
    {
        QList<Group> out;
        for (const auto& gv : arr)
        {
            const QJsonObject go = gv.toObject();
            Group g;
            g.clean = go["clean"].toBool();
            g.color = QColor::fromString(go["color"].toString()); // "#AARRGGBB"; нет ключа — цвет по умолчанию
            g.width = go["width"].toInt(Theme::GroupDefaultCells);
            g.column = go["column"].toInt();
            g.joinPrev = go["joinPrev"].toBool();
            g.labels = go["labels"].toInt(-1);
            g.listMode = go["list"].toBool();
            g.loose = go["loose"].toBool();
            g.alignH = std::clamp(go["alignH"].toInt(1), 0, 2);
            g.alignV = std::clamp(go["alignV"].toInt(1), 0, 2);
            // Вкладки; группа без "tabs" (ранний формат) — одна вкладка из title/tiles.
            const QJsonArray tabs = go.contains("tabs") ? go["tabs"].toArray() : QJsonArray{go};
            g.tabs.clear();
            for (const auto& tabv : tabs)
                g.tabs.append(tabFromJson(tabv.toObject()));
            if (g.tabs.isEmpty())
                g.tabs.append(Tab{});
            g.activeTab = go["activeTab"].toInt();
            out.append(g);
        }
        return out;
    }

    QJsonArray groupsToJson(const QList<Group>& groups)
    {
        QJsonArray arr;
        for (const Group& g : groups)
        {
            QJsonArray tabs;
            for (const Tab& tab : g.tabs)
                tabs.append(tabToJson(tab));
            QJsonObject go{{"tabs", tabs}, {"activeTab", g.activeTab}, {"clean", g.clean},
                           {"width", g.width}, {"column", g.column}};
            if (g.color.isValid())
                go["color"] = g.color.name(QColor::HexArgb);
            if (g.joinPrev)
                go["joinPrev"] = true;
            if (g.labels >= 0)
                go["labels"] = g.labels;
            if (g.listMode)
                go["list"] = true;
            if (g.loose)
            {
                go["loose"] = true;
                go["alignH"] = g.alignH;
                go["alignV"] = g.alignV;
            }
            arr.append(go);
        }
        return arr;
    }

    QList<Item> defaultSidebar()
    {
        QList<Item> out;
        for (auto loc : {QStandardPaths::DownloadLocation})
        {
            const QString path = QStandardPaths::writableLocation(loc);
            if (!path.isEmpty())
                out.append({QStandardPaths::displayName(loc), path, {}});
        }
        return out;
    }
}

#ifdef _WIN32
namespace
{
    HANDLE filesMutex()
    {
        static const HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\StartMenu.App.Files");
        return mutex;
    }
}
#endif

FileLock::FileLock()
{
#ifdef _WIN32
    if (const HANDLE mutex = filesMutex())
    {
        const DWORD r = WaitForSingleObject(mutex, 3000);
        m_owned = (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED); // ABANDONED — прошлый владелец упал, замок наш
    }
#endif
}

FileLock::~FileLock()
{
#ifdef _WIN32
    if (m_owned)
        ReleaseMutex(filesMutex());
#endif
}

QJsonObject readJsonFile(const QString& path)
{
    const FileLock lock; // не читать, пока другой процесс подменяет файл
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    f.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError && !data.trimmed().isEmpty())
    {
        // Файл правили руками и ошиблись — сохраняем как есть, чтобы ничего не пропало.
        QFile::copy(path, path + QStringLiteral(".broken-")
                              + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        return {};
    }
    return doc.object();
}

void keepBackup(const QString& path)
{
    if (!QFile::exists(path))
        return;
    QFile::remove(path + QStringLiteral(".bak"));
    QFile::copy(path, path + QStringLiteral(".bak"));
}

Config& Config::instance()
{
    static Config c;
    return c;
}

QString Config::filePath() const
{
    if (!m_fileOverride.isEmpty())
        return m_fileOverride;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/items.json";
}

bool Config::isWebLink(const QString& path)
{
    return path.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) || path.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
}

Item Config::itemFromUrl(const QString& url)
{
    QString u = url.trimmed();
    if (!isWebLink(u))
        u = QStringLiteral("https://") + u;
    const QUrl q(u);
    QString host = q.host();
    if (host.startsWith(QStringLiteral("www.")))
        host = host.mid(4);
    // «youtube.com» → «Youtube», «music.yandex.ru» → «Music.yandex»
    QString name = host.section(QLatin1Char('.'), 0, -2);
    if (name.isEmpty())
        name = host;
    // Известные сайты — так, как они пишут себя сами
    static const QHash<QString, QString> brands{
        {QStringLiteral("youtube"), QStringLiteral("YouTube")}, {QStringLiteral("github"), QStringLiteral("GitHub")},
        {QStringLiteral("chatgpt"), QStringLiteral("ChatGPT")}, {QStringLiteral("tiktok"), QStringLiteral("TikTok")},
        {QStringLiteral("linkedin"), QStringLiteral("LinkedIn")}, {QStringLiteral("whatsapp"), QStringLiteral("WhatsApp")},
        {QStringLiteral("paypal"), QStringLiteral("PayPal")}, {QStringLiteral("vk"), QStringLiteral("VK")},
        {QStringLiteral("deepl"), QStringLiteral("DeepL")}, {QStringLiteral("soundcloud"), QStringLiteral("SoundCloud")},
        {QStringLiteral("netflix"), QStringLiteral("Netflix")}, {QStringLiteral("twitch"), QStringLiteral("Twitch")},
        {QStringLiteral("x"), QStringLiteral("X")}, {QStringLiteral("ok"), QStringLiteral("OK")}};
    if (const auto it = brands.constFind(name.toLower()); it != brands.constEnd())
        name = *it;
    else if (!name.isEmpty())
        name[0] = name[0].toUpper();
    return {name, u, {}};
}

Item Config::itemFromPath(const QString& path)
{
    const QFileInfo fi(path);
    const QString abs = fi.absoluteFilePath();
    QString name = fi.isDir() ? fi.fileName() : fi.completeBaseName();
    if (name.isEmpty())
        name = QDir::toNativeSeparators(abs); // например, корень диска C:/
    return {name, abs, {}};
}

void Config::setActiveLayout(int i)
{
    m_active = std::clamp(i, 0, int(m_layouts.size()) - 1);
}

void Config::load()
{
    m_layouts.clear();
    m_active = 0;
    m_sidebar.clear();

    const QJsonObject root = readJsonFile(filePath());

    if (root.contains("layouts"))
    {
        for (const auto& lv : root["layouts"].toArray())
        {
            const QJsonObject lo = lv.toObject();
            m_layouts.append({lo["name"].toString(), groupsFromJson(lo["groups"].toArray())});
        }
        m_active = root["activeLayout"].toInt();
    }
    else if (root.contains("groups"))
    {
        // Формат до пространств: одна раскладка.
        m_layouts.append({TXT("Основное"), groupsFromJson(root["groups"].toArray())});
    }
    else if (root.contains("items"))
    {
        // Старый формат первой версии: плоский список → одна группа.
        Group g;
        g.title() = TXT("Закреплённые");
        for (const auto& v : root["items"].toArray())
            g.tiles().append({itemFromJson(v.toObject()), TileSize::Medium, false, {}});
        m_layouts.append({TXT("Основное"), {}});
        if (!g.tiles().isEmpty())
            m_layouts.last().groups.append(g);
    }
    if (m_layouts.isEmpty())
        m_layouts.append({TXT("Основное"), {}});
    m_active = std::clamp(m_active, 0, int(m_layouts.size()) - 1);
    // Стандартные названия слоёв и групп («Основное», «Закреплённые»…) — на языке интерфейса,
    // даже если их сохранили на другом языке. Свои названия не меняются.
    for (Layout& l : m_layouts)
    {
        l.name = Lang::localizeDefault(l.name);
        for (Group& g : l.groups)
            for (Tab& t : g.tabs)
                t.title = Lang::localizeDefault(t.title);
    }

    m_pinKnown = root.contains("pinSeen");
    m_pinSeen.clear();
    for (const auto& v : root["pinSeen"].toArray())
        m_pinSeen.append(v.toString());
    m_startStamp = root["startStamp"].toInteger();
    m_startSeen.clear();
    for (const auto& v : root["startSeen"].toArray())
        m_startSeen.append(v.toString());

    if (root.contains("sidebar"))
        for (const auto& v : root["sidebar"].toArray())
            m_sidebar.append(itemFromJson(v.toObject()));
    else
        m_sidebar = defaultSidebar();

    normalize();
}

void Config::save() const
{
    QJsonArray layouts;
    for (const Layout& l : m_layouts)
        layouts.append(QJsonObject{{"name", l.name}, {"groups", groupsToJson(l.groups)}});
    QJsonArray sidebar;
    for (const Item& it : m_sidebar)
        sidebar.append(itemToJson(it));

    // QSaveFile пишет во временный файл и подменяет — при сбое старый items.json не испортится.
    const FileLock lock;
    keepBackup(filePath());
    if (m_fileOverride.isEmpty())
        Backups::daily(filePath()); // раз в день — копия в backups (последние 7)
    QSaveFile f(filePath());
    if (f.open(QIODevice::WriteOnly))
    {
        f.write(QJsonDocument(QJsonObject{{"version", 3}, {"layouts", layouts}, {"activeLayout", m_active},
                                          {"sidebar", sidebar}, {"pinSeen", QJsonArray::fromStringList(m_pinSeen)},
                                          {"startStamp", m_startStamp}, {"startSeen", QJsonArray::fromStringList(m_startSeen)}})
                    .toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void Config::resetLabelOverrides()
{
    for (Layout& l : m_layouts)
        for (Group& g : l.groups)
        {
            g.labels = -1;
            for (Tab& tab : g.tabs)
                for (Tile& t : tab.tiles)
                    t.label = -1;
        }
}

void Config::normalize()
{
    for (Layout& l : m_layouts)
        normalizeGroups(l.groups);
}

void Config::normalizeGroups(QList<Group>& groups)
{
    QList<int> used;
    for (Group& g : groups)
    {
        g.width = std::clamp(g.width, Theme::GroupMinCells, Theme::GroupMaxCells);
        g.column = qMax(0, g.column);
        if (!used.contains(g.column))
            used.append(g.column);
        if (g.tabs.isEmpty())
            g.tabs.append(Tab{});
        if (g.loose)
        {
            // Свободные ярлыки: одна «вкладка», без фона группы и без режима списка
            g.clean = true;
            g.listMode = false;
            g.joinPrev = false;
            while (g.tabs.size() > 1)
                g.tabs.removeLast();
        }
        g.activeTab = std::clamp(g.activeTab, 0, int(g.tabs.size()) - 1);

        for (Tab& tab : g.tabs)
            for (int i = int(tab.tiles.size()) - 1; i >= 0; --i)
            {
                Tile& t = tab.tiles[i];
                if (!t.isFolder)
                    continue;
                if (t.children.isEmpty())
                    tab.tiles.removeAt(i);
                else if (t.children.size() == 1)
                {
                    // Папка из одного элемента превращается обратно в обычную плитку.
                    t.item = t.children.first();
                    t.children.clear();
                    t.isFolder = false;
                }
            }
    }
    // Опустевший блок свободных ярлыков исчезает сам (как и обычная группа без плиток)
    for (int i = int(groups.size()) - 1; i >= 0; --i)
        if (groups[i].loose && groups[i].tiles().isEmpty())
            groups.removeAt(i);
    // Номера колонок без дыр: 0, 1, 2…
    std::sort(used.begin(), used.end());
    for (Group& g : groups)
        g.column = int(used.indexOf(g.column));
    // Первой группе колонки не к кому «присоединяться» справа.
    QSet<int> seenColumn;
    for (Group& g : groups)
    {
        if (!seenColumn.contains(g.column))
            g.joinPrev = false;
        seenColumn.insert(g.column);
    }
}
