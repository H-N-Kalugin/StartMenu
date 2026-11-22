#pragma once
#include <QColor>
#include <QRectF>

class QPainter;

// Собственные значки боковой панели, нарисованные линиями (без картинок).
namespace Glyphs
{
    enum class Kind { Tiles, List, Layers, Documents, Folder, Downloads, Settings, Run, Power, Clipboard, Volume, VolumeMute };
    void draw(QPainter& p, Kind kind, const QRectF& box, const QColor& color, const QColor& background);
}
