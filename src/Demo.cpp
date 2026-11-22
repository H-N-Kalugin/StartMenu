#include "Demo.h"
#include "Config.h"
#include "Lang.h"

#include <QBuffer>
#include <QDateTime>
#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>

namespace
{
    bool g_active = false;

    struct Stub
    {
        QColor color;
        const char* glyph;
    };

    // Цветные значки-заглушки (как в превью мастера первого запуска) — без чужих логотипов
    const QHash<QString, Stub>& stubs()
    {
        static const QHash<QString, Stub> s{
            {QStringLiteral("browser"), {QColor(0x3B, 0x82, 0xF6), "B"}},
            {QStringLiteral("mail"), {QColor(0x0E, 0xA5, 0xE9), "@"}},
            {QStringLiteral("calendar"), {QColor(0xEF, 0x44, 0x44), "31"}},
            {QStringLiteral("notes"), {QColor(0xF5, 0x9E, 0x0B), "N"}},
            {QStringLiteral("files"), {QColor(0xEA, 0xB3, 0x08), "F"}},
            {QStringLiteral("code"), {QColor(0x8B, 0x5C, 0xF6), "</>"}},
            {QStringLiteral("design"), {QColor(0xEC, 0x48, 0x99), "D"}},
            {QStringLiteral("photos"), {QColor(0x10, 0xB9, 0x81), "P"}},
            {QStringLiteral("music"), {QColor(0x22, 0xC5, 0x5E), "\u266A"}},
            {QStringLiteral("video"), {QColor(0xF9, 0x73, 0x16), "\u25B6"}},
            {QStringLiteral("games"), {QColor(0x63, 0x66, 0xF1), "G"}},
            {QStringLiteral("chat"), {QColor(0x06, 0xB6, 0xD4), "C"}},
            {QStringLiteral("wiki"), {QColor(0x64, 0x74, 0x8B), "W"}},
            {QStringLiteral("docs"), {QColor(0x25, 0x63, 0xEB), "?"}},
            {QStringLiteral("news"), {QColor(0xDC, 0x26, 0x26), "N"}},
        };
        return s;
    }

    Tile tile(const QString& name, const char* key, TileSize size = TileSize::Medium)
    {
        Tile t;
        t.item = {name, QStringLiteral("demo:") + QLatin1String(key), {}, {}};
        t.size = size;
        return t;
    }

    Group group(const QString& title, int column, int width, const QList<Tile>& tiles)
    {
        Group g;
        g.title() = title;
        g.column = column;
        g.width = width;
        g.tiles() = tiles;
        return g;
    }

    // Картинка-заглушка: мягкий градиент с «пятнами» (для обложки и для картинки в буфере)
    QImage art(const QSize& size, const QColor& a, const QColor& b)
    {
        QImage img(size, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, size.width(), size.height());
        g.setColorAt(0, a);
        g.setColorAt(1, b);
        p.fillRect(img.rect(), g);
        auto blob = [&](qreal x, qreal y, qreal r, QColor c) {
            QRadialGradient rg(QPointF(size.width() * x, size.height() * y), size.width() * r);
            rg.setColorAt(0, c);
            c.setAlpha(0);
            rg.setColorAt(1, c);
            p.fillRect(img.rect(), rg);
        };
        blob(0.25, 0.30, 0.45, QColor(255, 255, 255, 70));
        blob(0.80, 0.75, 0.40, QColor(0, 0, 0, 60));
        p.setPen(QPen(QColor(255, 255, 255, 150), qMax(2, size.width() / 60)));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(size.width() * 0.62, size.height() * 0.45), size.height() * 0.22, size.height() * 0.22);
        return img;
    }
}

namespace Demo
{
void enable()
{
    g_active = true;
}

bool active()
{
    return g_active;
}

void fillConfig()
{
    auto& cfg = Config::instance();
    cfg.layouts().clear();

    Layout home{TXT("Основное"), {}};
    home.groups.append(group(TXT("Работа"), 0, 8,
                             {tile(TXT("Браузер"), "browser", TileSize::Wide), tile(TXT("Почта"), "mail"),
                              tile(TXT("Календарь"), "calendar"), tile(TXT("Заметки"), "notes"), tile(TXT("Файлы"), "files")}));
    home.groups.append(group(TXT("Творчество"), 1, 8,
                             {tile(TXT("Дизайн"), "design", TileSize::Wide), tile(TXT("Фото"), "photos"),
                              tile(TXT("Видео"), "video"), tile(TXT("Код"), "code"), tile(TXT("Музыка"), "music")}));
    home.groups.append(group(TXT("Игры"), 2, 6, {tile(TXT("Игры"), "games"), tile(TXT("Чат"), "chat"), tile(TXT("Видео"), "video")}));
    Group links = group(TXT("Ссылки"), 2, 6, {});
    links.listMode = true;
    const char* sites[][2] = {{"https://www.wikipedia.org", "wiki"}, {"https://example.com", "docs"}, {"https://news.example.org", "news"}};
    for (const auto& s : sites)
    {
        Item it = Config::itemFromUrl(QString::fromLatin1(s[0]));
        it.icon = QStringLiteral("demo:") + QLatin1String(s[1]); // значок-заглушка вместо скачивания с сайта
        links.tiles().append({it, TileSize::Medium, false, {}});
    }
    home.groups.append(links);
    cfg.layouts().append(home);

    Layout second{TXT("Слой %1").arg(2), {}};
    second.groups.append(group(TXT("Игры"), 0, 8, {tile(TXT("Игры"), "games", TileSize::Wide), tile(TXT("Чат"), "chat")}));
    cfg.layouts().append(second);
    cfg.setActiveLayout(0);
    cfg.normalize();
}

QImage icon(const QString& source, int px)
{
    if (!source.startsWith(QStringLiteral("demo:")))
        return {};
    const Stub s = stubs().value(source.mid(5), Stub{QColor(0x4C, 0x8D, 0xFF), "?"});
    QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QLinearGradient g(0, 0, px, px);
    g.setColorAt(0, s.color.lighter(125));
    g.setColorAt(1, s.color.darker(115));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(0, 0, px, px), px * 0.24, px * 0.24);
    if (qstrcmp(s.glyph, "▶") == 0)
    {
        // «▶» в шрифте мелкий — рисуем треугольник сами
        const qreal c = px / 2.0, h = px * 0.36;
        const QPointF tri[3] = {{c - h * 0.38, c - h / 2}, {c - h * 0.38, c + h / 2}, {c + h * 0.5, c}};
        p.setBrush(Qt::white);
        p.drawPolygon(tri, 3);
        return img;
    }
    QFont f(QStringLiteral("Segoe UI"));
    const bool symbol = qstrcmp(s.glyph, "♪") == 0; // нота в шрифте тоже мелкая
    f.setPixelSize(qMax(6, int(px * (strlen(s.glyph) > 2 && !symbol ? 0.32 : symbol ? 0.62 : 0.48))));
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(0, 0, px, px), Qt::AlignCenter, QString::fromUtf8(s.glyph));
    return img;
}

QList<ClipboardStore::Entry> clipboard()
{
    using ClipboardStore::Entry;
    using ClipboardStore::Type;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 min = 60 * 1000;
    QList<Entry> out;
    auto text = [&](const QString& t, qint64 ago, bool pinned = false) {
        Entry e;
        e.type = Type::Text;
        e.text = t;
        e.time = now - ago;
        e.pinned = pinned;
        out.append(e);
    };
    text(TXT("Встреча в 15:00 — обсудить новый макет"), 2 * min);
    text(QStringLiteral("https://example.com/start-menu"), 9 * min);
    text(QStringLiteral("git commit -m \"Add tile animations\"\ngit push origin main"), 26 * min);
    Entry img;
    img.type = Type::Image;
    img.image = QStringLiteral("demo-1.png");
    img.size = QSize(1920, 1080);
    img.time = now - 52 * min;
    out.append(img);
    text(QStringLiteral("#4C8DFF"), 3 * 60 * min);
    Entry files;
    files.type = Type::Files;
    files.files = {QStringLiteral("C:/Projects/Demo/scene.blend"), QStringLiteral("C:/Projects/Demo/render.png")};
    files.time = now - 26 * 60 * min;
    files.pinned = true;
    out.append(files);
    return out;
}

QImage clipboardImage(const QString& name)
{
    Q_UNUSED(name)
    return art(QSize(480, 270), QColor(0x25, 0x63, 0xEB), QColor(0x7C, 0x3A, 0xED));
}

Media::State player()
{
    Media::State s;
    s.present = true;
    s.title = L"Northern Lights";
    s.artist = L"Aurora Drift";
    s.app = L"Demo";
    s.playing = true;
    s.canPrev = s.canNext = s.canSeek = true;
    s.positionMs = 84 * 1000;   // 1:24
    s.durationMs = 210 * 1000;  // 3:30
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    art(QSize(300, 300), QColor(0xF4, 0x3F, 0x5E), QColor(0x8B, 0x5C, 0xF6)).save(&buf, "PNG");
    s.art.assign(png.constBegin(), png.constEnd());
    return s;
}
}
