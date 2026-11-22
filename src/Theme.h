#pragma once
#include <QColor>
#include <QString>

// Размеры и цвета меню. Значения с пометкой «настройка» перезаписываются из settings.json
// (Settings::apply), остальные — просто правь здесь и пересобирай.
namespace Theme
{
    struct Rgba
    {
        int r, g, b, a = 255;
        QColor color() const { return QColor(r, g, b, a); }
        QString css() const { return QStringLiteral("rgba(%1,%2,%3,%4)").arg(r).arg(g).arg(b).arg(a); }
        static Rgba from(const QColor& c, int alpha) { return {c.red(), c.green(), c.blue(), alpha}; }
    };

    // Окно
    inline int WindowWidth   = 1400;  // настройка; если экран уже — окно сузится
    inline int WindowHeight  = 520;   // настройка
    inline int Radius        = 12;    // настройка
    inline int BottomGap     = 12;    // настройка: отступ от панели задач
    inline int ContentMargin    = 16;
    inline int ContentInsetX    = 0;     // отступ рабочей области слева (у стиля Studio — свой)

    // Боковая панель
    constexpr int SidebarWidth  = 56;
    constexpr int SidebarButton = 44;
    constexpr int AvatarSize    = 28;

    // Сетка плиток: всё меряется в ячейках. Маленькая = 1×1, средняя = 2×2, широкая = 4×2, большая = 4×4.
    inline int Cell             = 46;    // настройка
    inline int CellGap          = 4;
    inline int TileRadius       = 4;
    inline int IconSmall        = 28;    // считаются от Cell
    inline int IconMedium       = 48;
    inline int IconLarge        = 80;
    inline bool ShowLabels      = true;  // настройка
    inline int TileFill         = 0;     // настройка: 0 — нет, 1 — цвет иконки, 2 — размытая иконка
    inline int TileFillOpacity  = 85;    // настройка, %
    inline bool Adaptive        = true;  // настройка: плитки подстраиваются под размер окна
    constexpr int MinCell       = 32;    // пределы адаптивной ячейки
    constexpr int MaxCell       = 96;
    constexpr int ReflowCell    = 40;    // меньше — перестраиваем колонки, а не мельчим плитки
    constexpr int LabelMinCell  = 38;    // меньше — подписи на средних плитках скрываются

    // Группы
    inline int GroupPad             = 10;
    inline int GroupHeader          = 32;
    inline int GroupRadius          = 8;
    inline int GroupGapX            = 16;
    inline int GroupGapY            = 14;
    constexpr int GroupMinCells     = 2;
    constexpr int GroupMaxCells     = 16;
    constexpr int GroupDefaultCells = 8;

    // Список «Все приложения» / поиск
    inline int ListTitle          = 36;
    inline int ListRow            = 36;
    inline int ListHeader         = 30;
    inline int ListIcon           = 24;
    inline int ListColumnWidth    = 270;

    // Шрифты (px)
    inline int TitleFontPx    = 13;
    inline int TileFontPx     = 12;   // настройка
    inline int TileFontScaled = 12;   // с учётом адаптивной ячейки (считает Workspace)
    inline int ListFontPx     = 13;

    inline Rgba PanelBg      {20, 20, 22, 245};   // настройка: цвет + непрозрачность
    inline Rgba PanelBorder  {255, 255, 255, 22};
    inline Rgba GroupBg      {255, 255, 255, 14}; // настройка
    inline Rgba TileBg       {255, 255, 255, 16}; // настройка
    inline Rgba TileHover    {255, 255, 255, 30};
    inline Rgba TilePressed  {255, 255, 255, 48};
    inline Rgba ChipBg       {255, 255, 255, 40};
    inline Rgba TextColor    {237, 239, 244};     // настройка
    inline Rgba SubText      {154, 160, 174};
    inline Rgba Accent       {76, 141, 255};      // настройка
    inline Rgba ScrollHandle {255, 255, 255, 50};
    inline Rgba MenuBg       {32, 35, 43};
    inline Rgba Overlay      {0, 0, 0, 130};
    inline Rgba FolderBg     {38, 40, 46, 252};
}
