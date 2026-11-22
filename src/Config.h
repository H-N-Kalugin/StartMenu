#pragma once
#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

struct Item
{
    QString name;
    QString path;   // exe, .lnk, .uproject, папка, shell:AppsFolder\... — всё, что понимает ShellExecute
    QString args;
    QString icon;   // откуда брать иконку, если не из path (например, для страниц «Параметров»)
};

enum class TileSize { Small, Medium, Wide, Large };

struct Tile
{
    Item item;                 // у папки: item.name — название, path пустой
    TileSize size = TileSize::Medium;
    bool isFolder = false;
    int label = -1;            // подпись: -1 — как у группы, 0 — скрыта, 1 — видна
    QString image;             // своё изображение вместо фона и иконки (путь к файлу)
    QList<Item> children;      // содержимое папки
};

struct Tab
{
    QString title;
    QList<Tile> tiles;
};

// Группа — это одна или несколько вкладок с общей рамкой. Видны плитки только активной вкладки.
struct Group
{
    QList<Tab> tabs{Tab{}};    // всегда минимум одна вкладка
    int activeTab = 0;
    bool clean = false;        // true — у группы нет фона, зато у каждой плитки свой
    QColor color;              // свой цвет фона группы (с прозрачностью); пустой — как в настройках
    int width = 8;             // ширина в ячейках сетки
    int column = 0;            // в какой колонке рабочего пространства стоит группа
    bool joinPrev = false;     // стоит справа от предыдущей группы своей колонки (в одном ряду)
    int labels = -1;           // подписи плиток: -1 — как в настройках, 0 — скрыты, 1 — видны
    bool listMode = false;     // вид «список»: строки (значок + название) вместо квадратных плиток
    bool loose = false;        // «свободные ярлыки»: без рамки и заголовка, плитки сами выстраиваются рядами
    int indentL = 0;           // свободные ярлыки: отступ слева, в ячейках сетки (2 ячейки = одна обычная плитка)
    int indentR = 0;           // и справа
    int alignV = 1;            // по вертикали между соседними группами — 0 сверху, 1 по центру, 2 снизу

    // Короткий доступ к активной вкладке — почти весь код работает именно с ней.
    QList<Tile>& tiles() { return tabs[activeTab].tiles; }
    const QList<Tile>& tiles() const { return tabs[activeTab].tiles; }
    QString& title() { return tabs[activeTab].title; }
    const QString& title() const { return tabs[activeTab].title; }
};

class QJsonObject;

// Замок на файлы настроек между процессами: трей, меню, окно настроек и «Закрепить» из Проводника
// читают и пишут одни и те же settings.json / items.json / историю буфера. Без замка запись одного процесса
// могла не пройти, пока другой держит файл открытым на чтение, а два «прочитал-поменял-записал» — затереть друг друга.
// Можно вкладывать (в одном потоке замок берётся повторно). Ждём не дольше 3 с — потом работаем без него.
class FileLock
{
public:
    FileLock();
    ~FileLock();
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

private:
    bool m_owned = false;
};

// Чтение JSON без потери данных: битый файл копируется в «имя.broken-<время>», а не затирается при следующей записи.
QJsonObject readJsonFile(const QString& path);
// Перед перезаписью — копия «имя.bak» (одна, последняя).
void keepBackup(const QString& path);

// Рабочее пространство (набор раскладки): свои группы плиток. Например, «Работа» и «Игры».
struct Layout
{
    QString name;
    QList<Group> groups;
};

// Всё закреплённое: пространства с группами плиток и папки боковой панели.
// Файл: %APPDATA%/StartMenu/items.json
class Config
{
public:
    static Config& instance();

    // Почти весь код работает с группами активного пространства.
    QList<Group>& groups() { return m_layouts[m_active].groups; }
    QList<Layout>& layouts() { return m_layouts; }
    int activeLayout() const { return m_active; }
    void setActiveLayout(int i);
    QList<Item>& sidebar() { return m_sidebar; }

    // Синхронизация с «Закрепить на начальном экране» Windows (см. PinSync).
    QStringList& pinSeen() { return m_pinSeen; }
    bool pinFolderKnown() const { return m_pinKnown; }
    void setPinFolderKnown() { m_pinKnown = true; }
    // То же для закреплённого «Пуска» Windows 11 (start2.bin): что уже видели и когда файл менялся.
    QStringList& startSeen() { return m_startSeen; }
    qint64 startStamp() const { return m_startStamp; }
    void setStartStamp(qint64 v) { m_startStamp = v; }

    void load();
    void save() const;
    void normalize();          // чинит индексы колонок, пустые папки и ширины после правок
    void resetLabelOverrides();// убрать частные «подписи вкл/выкл» у всех групп и плиток — главной становится общая настройка
    QString filePath() const;
    // Для --render --items: читать раскладку из другого файла (копии), а не из настоящего items.json
    void setFileOverride(const QString& path) { m_fileOverride = path; }

    static Item itemFromPath(const QString& path);
    static Item itemFromUrl(const QString& url);   // ссылка на сайт: название — по адресу («youtube.com» → «YouTube»)
    static bool isWebLink(const QString& path);
    static void normalizeGroups(QList<Group>& groups);

private:
    QList<Layout> m_layouts{Layout{}};
    int m_active = 0;
    QList<Item> m_sidebar;
    QStringList m_pinSeen;
    bool m_pinKnown = false;
    QStringList m_startSeen;
    qint64 m_startStamp = 0;   // 0 — ещё ни разу не синхронизировали
    QString m_fileOverride;
};
