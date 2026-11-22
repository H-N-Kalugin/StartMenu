#pragma once
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QRect>
#include <QString>
#include <QStringList>
#include <functional>

#include "Config.h"
#include "Theme.h"

class QPainter;
struct Settings;

// Стиль оформления («скин»). Логика меню (группы, перетаскивание, поиск, адаптивность) общая,
// а всё, чем стили отличаются, — размеры, шрифты и рисование отдельных элементов — здесь.
// Базовый класс = «Классический» стиль; Studio, Bento и Fluent переопределяют нужное (SkinStyles.cpp).

using IconPainter = std::function<void(QPainter&, const Item&, const QRect&)>;

struct TilePaint
{
    const Tile* tile = nullptr;
    QRect r;
    bool clean = false;        // группа без фона — у плитки своя подложка
    qreal hover = 0;           // 0…1, плавная подсветка
    bool pressed = false;
    bool focused = false;      // выбрана с клавиатуры
    bool showLabel = true;     // подпись видна (плитка → группа → общая настройка)
    QColor tint;               // основной цвет иконки (Bento красит им плитку)
    IconPainter icon;
};

struct GroupPaint
{
    const Group* group = nullptr;
    QRect frame, header, body;
    bool hover = false;
    int hoverTab = -1;
    QColor auto1, auto2;       // цвета из иконок группы (Bento)
    int number = 0;            // сквозной номер группы (Studio: «01», «02»…)
    int count = 0;             // сколько плиток во вкладке
    std::function<QRect(int)> tabRect;
};

struct SidebarButtonPaint
{
    QRect r;
    bool hover = false, pressed = false, active = false;
};

struct ListRowPaint
{
    QRect r;
    const Item* item = nullptr;
    QStringList highlight;     // что подсветить в названии (запрос и его транслит)
    bool hover = false, selected = false, pressed = false;
    IconPainter icon;
};

// Мини-плеер справа от поиска (что играет в Windows: Spotify, браузер…).
struct MediaPaint
{
    QRect r;
    QString title, artist;
    QImage art;                // обложка; пустая — рисуем ноту
    bool playing = false, canPrev = true, canNext = true;
    int hover = -1;            // Skin::MediaPart под курсором
    int pressed = -1;
    // Большая панель над меню: ползунок времени, волна, обложка фоном.
    bool panel = false;
    qint64 positionMs = 0, durationMs = 0;
    bool canSeek = false;
    qreal seekFrac = -1;       // тянут ползунок: 0…1, иначе -1
    bool artBackground = false; // обложка на весь фон панели
    qreal artOpacity = 0.4;
    QImage artBlur;            // размытая обложка для фона
    qreal wavePhase = 0, waveAmp = 0; // волна на пройденной части ползунка (амплитуда 0…1)
    qreal glow = -1;           // подсветка снизу в ритм музыки: 0…1, меньше нуля — выключена
    QColor glowA, glowB;       // её цвета
    // Цвет плеера (полоса, кнопка «играть»): пусто — как задумано стилем, иначе — эти цвета (акцент или из обложки)
    QColor tintA, tintB;
    qreal panelRadius = 12;
};

// Как выглядит мини-плеер в конкретном стиле. Рисует его общий код (Skin::paintMediaPlayer).
struct MediaStyle
{
    qreal radius = 6, artRadius = 4;
    bool roundButtons = true;  // круглые кнопки или скруглённые квадраты
    QColor bg, border, hoverBg;
    QBrush playBg;             // подложка кнопки «играть/пауза»
    QColor playGlyph, glyph;
    QFont title, sub;
    QBrush progress;           // заполненная часть ползунка времени
    QColor titleColor, subColor;
    bool upperSub = false;     // исполнитель капсом (Studio)
};

class Skin
{
public:
    enum Kind { Classic = 0, Studio = 1, Bento = 2, Fluent = 3 };
    enum class Font { Tile, TileBig, GroupTitle, ListRow, ListSection, ListTitle, Search, Meta };

    virtual ~Skin() = default;
    static Skin& current();
    static void select(int kind);
    static QStringList names();

    virtual int kind() const { return Classic; }
    virtual void applyMetrics() const;            // размеры и отступы в Theme
    virtual void preset(Settings& s) const;       // цвета и фон по умолчанию для стиля
    virtual QFont font(Font role) const;
    virtual int searchFieldHeight() const { return 36; }
    virtual int titleOffset() const { return 0; } // отступ названия группы слева (Studio: место под номер)
    // Панель группы нарисована не точно по рамке: сверху на top пикселей ниже её края, снизу на bottom пикселей дальше.
    // Нужно, чтобы свободные ярлыки центровались по тому, что человек видит, а не по невидимой рамке.
    virtual void groupPanelInset(int& top, int& bottom) const { top = 0; bottom = 0; }

    virtual void paintPanel(QPainter& p, const QRect& r, const QImage& backdrop, int effect) const;
    virtual void paintSidebarBack(QPainter& p, const QRect& r) const;
    virtual void paintSidebarButton(QPainter& p, const SidebarButtonPaint& b) const;
    // focused — по полю кликнули (курсор мигает и без текста).
    virtual void paintSearchField(QPainter& p, const QRect& r, const QString& text, bool caret, bool focused) const;
    // Справа от поиска — служебная строка (Studio: «СЕТКА 46·4 | 07 ГРУПП | 15 ПЛИТОК»).
    virtual void paintToolbarInfo(QPainter& p, const QRect& r, const QStringList& parts) const;
    // Нижняя строка состояния: элемент под курсором и подсказки клавиш («клавиша|подпись»). 0 — строки нет.
    virtual int statusBarHeight() const { return 26; }
    // name — название, path — «ГРУППА / 01 · 2×2», location — где лежит файл (цель ярлыка).
    virtual void paintStatusBar(QPainter& p, const QRect& r, const QString& name, const QString& path,
                                const QString& location, const QStringList& hints) const;
    // Фон рабочей области под группами (Studio: крестики в узлах сетки).
    virtual void paintCanvas(QPainter&, const QRect&, const QPoint&, int, const QList<QRect>&) const {}

    virtual void paintGroup(QPainter& p, const GroupPaint& g) const;
    virtual void paintTile(QPainter& p, const TilePaint& t) const;
    virtual void paintFocus(QPainter& p, const QRect& r) const;
    virtual void paintInsert(QPainter& p, const QRect& r) const;
    virtual void paintFolderTarget(QPainter& p, const QRect& r) const;
    virtual void paintNewGroup(QPainter& p, const QRect& r) const;

    virtual void paintListTitle(QPainter& p, const QRect& r, const QString& title) const;
    virtual void paintListSection(QPainter& p, const QRect& r, const QString& title, bool letter) const;
    virtual void paintListRow(QPainter& p, const ListRowPaint& row) const;

    // Мини-плеер: раскладка общая для рисования и кликов, вид — из mediaStyle() стиля.
    // Компактный — в строке поиска; панель — отдельным окном над меню (крупно, с перемоткой).
    enum MediaPart { MediaBody = 0, MediaPrev, MediaPlay, MediaNext, MediaClose, MediaArt, MediaText,
                     MediaSeek, MediaTimeLeft, MediaTimeRight, MediaCollapse };
    static QRect mediaPartRect(const QRect& r, int part);
    static QRect mediaPanelRect(const QRect& r, int part, bool artBackground);
    virtual MediaStyle mediaStyle(const QRect& r) const;
    void paintMediaPlayer(QPainter& p, const MediaPaint& m) const;   // компактный
    void paintMediaPanel(QPainter& p, const MediaPaint& m) const;    // большая панель

    // Общие помощники для стилей
    static QFont makeFont(const QStringList& families, int px, int weight = QFont::Normal);
    static void drawLabel(QPainter& p, const QRect& r, const QString& text, const QFont& f, const QColor& c,
                          int maxLines, Qt::Alignment align);
    static void drawHighlighted(QPainter& p, const QRect& r, const QString& text, const QStringList& needles,
                                const QFont& f, const QColor& c, const QColor& hi);
    static QColor mix(const QColor& a, const QColor& b, qreal k);
    // При стекле Windows углы скругляет сама система, и бывает только 0 / 4 / 8 px.
    static int glassRadius() { return Theme::Radius == 0 ? 0 : Theme::Radius <= 6 ? 4 : 8; }
    // «05 ГРУПП» → «5 групп», «ESC» → «Esc»: для стилей с обычным регистром.
    static QString humanize(const QString& s);
    // Подсказки клавиш справа налево: keycap рисует клавишу в заданном прямоугольнике. Возвращает левый край.
    using KeycapPainter = std::function<void(QPainter&, const QRectF&, const QString&)>;
    static int paintHints(QPainter& p, const QRect& r, const QStringList& hints, const QFont& f, const QColor& label,
                          bool human, int keyH, const KeycapPainter& keycap);
    // Название и путь элемента под курсором — слева, не заходя на подсказки (до right).
    static void paintHoverName(QPainter& p, const QRect& r, int x, int right, const QString& name, const QString& path,
                               const QString& location, const QFont& nf, const QColor& nc, const QFont& pf, const QColor& pc);
    static void paintFolderIcons(QPainter& p, const Tile& tile, const QRect& area, int iconPx, const IconPainter& icon);
};
