#include "Welcome.h"
#include "Demo.h"
#include "Updater.h"
#include "Lang.h"
#include "Glyphs.h"
#include "PinSync.h"
#include "Settings.h"
#include "Shell.h"
#include "Skin.h"
#include "Theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include "SegSwitch.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QRadialGradient>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <windows.h>

namespace
{
    // ---------- плитки-заглушки для превью ----------
    struct Stub
    {
        const char* name;
        QColor color;
        const char* glyph;
    };

    const QList<Stub>& stubs()
    {
        static const QList<Stub> s{
            {"Браузер", QColor(0x3B, 0x82, 0xF6), "B"},  {"Почта", QColor(0x0E, 0xA5, 0xE9), "@"},
            {"Календарь", QColor(0xEF, 0x44, 0x44), "31"}, {"Заметки", QColor(0xF5, 0x9E, 0x0B), "N"},
            {"Файлы", QColor(0xEA, 0xB3, 0x08), "F"},     {"Код", QColor(0x8B, 0x5C, 0xF6), "</>"},
            {"Дизайн", QColor(0xEC, 0x48, 0x99), "D"},    {"Фото", QColor(0x10, 0xB9, 0x81), "P"},
            {"Музыка", QColor(0x22, 0xC5, 0x5E), "♪"},    {"Видео", QColor(0xF9, 0x73, 0x16), "▶"},
            {"Игры", QColor(0x63, 0x66, 0xF1), "G"},      {"Чат", QColor(0x06, 0xB6, 0xD4), "C"},
        };
        return s;
    }

    const Stub& stubFor(const QString& name)
    {
        for (const Stub& s : stubs())
            if (name == Lang::tr(QString::fromUtf8(s.name)))
                return s;
        return stubs().first();
    }

    void paintStubIcon(QPainter& p, const Item& it, const QRect& r)
    {
        const Stub& s = stubFor(it.name);
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, s.color.lighter(125));
        g.setColorAt(1, s.color.darker(115));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRoundedRect(QRectF(r), r.width() * 0.24, r.width() * 0.24);
        QFont f(QStringLiteral("Segoe UI"));
        f.setPixelSize(qMax(6, int(r.height() * (strlen(s.glyph) > 2 ? 0.32 : 0.48))));
        f.setWeight(QFont::Bold);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, QString::fromUtf8(s.glyph));
        p.restore();
    }

    // Обои «под стеклом» — мягкие цветные пятна, чтобы было видно прозрачность и размытие.
    QImage wallpaper(const QSize& size)
    {
        QImage img(size, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(0x15, 0x17, 0x22));
        QPainter p(&img);
        auto blob = [&](qreal x, qreal y, qreal rad, QColor c) {
            QRadialGradient g(QPointF(size.width() * x, size.height() * y), size.width() * rad);
            g.setColorAt(0, c);
            c.setAlpha(0);
            g.setColorAt(1, c);
            p.fillRect(img.rect(), g);
        };
        blob(0.18, 0.25, 0.45, QColor(0x7C, 0x3A, 0xED, 150));
        blob(0.85, 0.80, 0.50, QColor(0x25, 0x63, 0xEB, 140));
        blob(0.55, 0.05, 0.30, QColor(0xF9, 0x73, 0x16, 90));
        return img;
    }

    // Обложка-заглушка для плеера в превью
    QImage stubArt()
    {
        QImage img(64, 64, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&img);
        QLinearGradient g(0, 0, 64, 64);
        g.setColorAt(0, QColor(0xF4, 0x3F, 0x5E));
        g.setColorAt(1, QColor(0x8B, 0x5C, 0xF6));
        p.fillRect(img.rect(), g);
        return img;
    }

    struct PlacedTile
    {
        const char* name;
        int col, row, cols, rows;
    };

    TileSize sizeFor(int cols, int rows)
    {
        if (cols == 1)
            return TileSize::Small;
        if (rows == 4)
            return TileSize::Large;
        return cols == 4 ? TileSize::Wide : TileSize::Medium;
    }

    void signalEvent(const wchar_t* name)
    {
        if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, name))
        {
            SetEvent(ev);
            CloseHandle(ev);
        }
    }

    // Импорт из Start11 возможен, если он стоял и рядом с программой лежит скрипт переноса.
    QString start11Script()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Stardock\\Start8\\Start8.ini", 0, KEY_READ, &key) != ERROR_SUCCESS)
            return {};
        RegCloseKey(key);
        const QString script = QCoreApplication::applicationDirPath() + QStringLiteral("/tools/import-start11.ps1");
        return QFileInfo::exists(script) ? script : QString();
    }

    const char* kStyleDesc[] = {
        "Спокойный и привычный, как Start11",
        "Графит, сетка и один акцент — как панель редактора",
        "Острова-карточки с мягким свечением цветов иконок",
        "Стекло и мягкие формы в духе Windows 11",
    };
}

// ---------- карточка стиля ----------

class StyleCard : public QAbstractButton
{
public:
    StyleCard(const QString& name, const QString& desc, QWidget* parent)
        : QAbstractButton(parent), m_name(name), m_desc(desc)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(540, 318);
    }
    void setPreview(const QImage& img)
    {
        m_preview = img;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        p.setPen(QPen(isChecked() ? QColor(0x4C, 0x8D, 0xFF) : underMouse() ? QColor(0x4A, 0x4D, 0x57) : QColor(0x33, 0x35, 0x3C),
                      isChecked() ? 2.5 : 1));
        p.setBrush(QColor(0x22, 0x24, 0x2A));
        p.drawRoundedRect(r, 12, 12);
        const QRectF pr(12, 12, width() - 24, (width() - 24) * 360.0 / 780.0);
        QPainterPath clip;
        clip.addRoundedRect(pr, 8, 8);
        p.save();
        p.setClipPath(clip);
        p.drawImage(pr, m_preview);
        p.restore();
        QFont f = font();
        f.setPixelSize(16);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(0xF0, 0xF1, 0xF5));
        p.drawText(QRectF(16, pr.bottom() + 10, width() - 60, 22), Qt::AlignLeft | Qt::AlignVCenter, m_name);
        f.setPixelSize(12);
        f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(QColor(0x9A, 0xA0, 0xAE));
        p.drawText(QRectF(16, pr.bottom() + 34, width() - 32, 20), Qt::AlignLeft | Qt::AlignVCenter, m_desc);
        if (isChecked())
        {
            // Галочка выбранного стиля
            const QRectF c(width() - 40, pr.bottom() + 12, 22, 22);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x4C, 0x8D, 0xFF));
            p.drawEllipse(c);
            p.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const QPointF pts[3] = {{c.x() + 6, c.center().y()}, {c.x() + 10, c.y() + 15}, {c.x() + 16.5, c.y() + 7}};
            p.drawPolyline(pts, 3);
        }
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    QString m_name, m_desc;
    QImage m_preview;
};

// ---------- превью ----------

QImage WelcomeWindow::renderPreview(int kind, bool fill, bool labels, qreal dpr)
{
    // Тема стиля — как у пресета «Стандартный для стиля», только в памяти.
    Settings tmp;
    tmp.skin = kind;
    Skin::select(kind);
    Skin::current().preset(tmp);
    tmp.showLabels = labels;
    tmp.adaptive = false;
    tmp.cell = 46;
    tmp.tileFill = fill ? 1 : 0;
    tmp.tileFillOpacity = 85;
    tmp.apply();
    Theme::TileFontScaled = Theme::TileFontPx;
    const Skin& sk = Skin::current();

    const QSize logical(780, 360);
    QImage img(logical * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRect full(QPoint(0, 0), logical);
    p.drawImage(full, wallpaper(logical));
    sk.paintPanel(p, full, wallpaper(logical), 2); // «своё размытие» поверх обоев — так видно стекло

    // Боковая панель
    sk.paintSidebarBack(p, QRect(0, 0, Theme::SidebarWidth, logical.height()));
    const QColor fg = Theme::TextColor.color();
    auto sideButton = [&](int y, Glyphs::Kind g, bool active) {
        SidebarButtonPaint b;
        b.r = QRect((Theme::SidebarWidth - Theme::SidebarButton) / 2, y, Theme::SidebarButton, Theme::SidebarButton);
        b.active = active;
        sk.paintSidebarButton(p, b);
        const QColor c = active && sk.kind() == Skin::Bento ? QColor(Qt::white) : active ? fg : Theme::Rgba::from(fg, 185).color();
        Glyphs::draw(p, g, QRectF(b.r.center().x() - 10, b.r.center().y() - 10, 20, 20), c, Theme::PanelBg.color());
    };
    // Как в настоящем меню: плитки, все приложения, буфер; снизу звук, загрузки, параметры, «Выполнить», питание
    sideButton(8, Glyphs::Kind::Tiles, true);
    sideButton(56, Glyphs::Kind::List, false);
    sideButton(104, Glyphs::Kind::Clipboard, false);
    const int sb = Theme::SidebarButton + 2;
    sideButton(logical.height() - 8 - sb * 5 + 2, Glyphs::Kind::Volume, false);
    sideButton(logical.height() - 8 - sb * 4 + 2, Glyphs::Kind::Downloads, false);
    sideButton(logical.height() - 8 - sb * 3 + 2, Glyphs::Kind::Settings, false);
    sideButton(logical.height() - 8 - sb * 2 + 2, Glyphs::Kind::Run, false);
    sideButton(logical.height() - 8 - sb + 2, Glyphs::Kind::Power, false);

    // Поиск, плеер, сведения
    const int left = Theme::SidebarWidth + 10;
    const QRect search(left + Theme::ContentInsetX, Theme::ContentMargin, 300, sk.searchFieldHeight());
    sk.paintSearchField(p, search, QString(), false, false);
    MediaPaint m;
    m.r = QRect(search.right() + 15, search.y(), 290, search.height());
    m.title = TXT("Любимый трек");
    m.artist = TXT("Исполнитель");
    m.art = stubArt();
    m.playing = true;
    sk.paintMediaPlayer(p, m);
    const int infoLeft = m.r.right() + 24;
    sk.paintToolbarInfo(p, QRect(infoLeft, search.y(), logical.width() - Theme::ContentMargin - 10 - infoLeft, search.height()),
                        {}); // счётчики по умолчанию выключены

    // Группы с плитками-заглушками
    const int cell = Theme::Cell, gap = Theme::CellGap, pad = Theme::GroupPad;
    auto span = [&](int n) { return n * cell + (n - 1) * gap; };
    const int x0 = left + Theme::ContentInsetX;
    const int y0 = search.bottom() + 1 + 14;
    const PlacedTile workTiles[] = {{"Браузер", 0, 0, 4, 2}, {"Почта", 4, 0, 2, 2},  {"Календарь", 0, 2, 2, 2},
                                    {"Заметки", 2, 2, 1, 1}, {"Файлы", 3, 2, 1, 1}, {"Код", 2, 3, 1, 1},
                                    {"Дизайн", 3, 3, 1, 1},  {"Фото", 4, 2, 2, 2}};
    const PlacedTile funTiles[] = {{"Музыка", 0, 0, 2, 2}, {"Видео", 2, 0, 2, 2}, {"Игры", 0, 2, 2, 2}, {"Чат", 2, 2, 2, 2}};

    struct GroupDef
    {
        QString title;
        bool clean;
        int cols;
        const PlacedTile* tiles;
        int n;
    };
    const GroupDef defs[] = {{TXT("Работа"), false, 6, workTiles, int(std::size(workTiles))},
                             {TXT("Отдых"), true, 4, funTiles, int(std::size(funTiles))}};
    QList<QRect> frames;
    int gx = x0;
    for (const GroupDef& d : defs)
    {
        frames.append(QRect(gx, y0, span(d.cols) + 2 * pad, Theme::GroupHeader + span(4) + pad));
        gx = frames.last().right() + 1 + Theme::GroupGapX;
    }
    const int status = sk.statusBarHeight();
    sk.paintCanvas(p, QRect(left, y0, logical.width() - left, logical.height() - y0 - status), QPoint(x0 + pad, y0 + Theme::GroupHeader),
                   cell + gap, frames);

    for (int gi = 0; gi < 2; ++gi)
    {
        const GroupDef& d = defs[gi];
        Group group;
        group.title() = d.title;
        group.clean = d.clean;
        QList<Tile>& tiles = group.tiles();
        for (int i = 0; i < d.n; ++i)
            tiles.append(Tile{Item{Lang::tr(QString::fromUtf8(d.tiles[i].name)), {}, {}, {}}, sizeFor(d.tiles[i].cols, d.tiles[i].rows)});
        GroupPaint gp;
        gp.group = &group;
        gp.frame = frames[gi];
        gp.header = QRect(gp.frame.x(), gp.frame.y(), gp.frame.width(), Theme::GroupHeader);
        gp.body = QRect(gp.frame.x() + pad, gp.frame.y() + Theme::GroupHeader, span(d.cols), span(4));
        gp.number = gi + 1;
        gp.count = d.n;
        gp.auto1 = stubFor(tiles[0].item.name).color;
        gp.auto2 = stubFor(tiles[1].item.name).color;
        gp.tabRect = [&gp](int) { return gp.header; };
        sk.paintGroup(p, gp);
        for (int i = 0; i < d.n; ++i)
        {
            const PlacedTile& pt = d.tiles[i];
            const QRect r(gp.body.x() + pt.col * (cell + gap), gp.body.y() + pt.row * (cell + gap), span(pt.cols), span(pt.rows));
            const QColor c = stubFor(tiles[i].item.name).color;
            if (fill)
            {
                // Заливка плитки цветом иконки — как в меню
                p.save();
                QPainterPath clip;
                clip.addRoundedRect(QRectF(r), Theme::TileRadius, Theme::TileRadius);
                p.setClipPath(clip);
                p.setOpacity(0.85);
                QLinearGradient g(r.topLeft(), r.bottomRight());
                g.setColorAt(0, c.lighter(108));
                g.setColorAt(1, c.darker(118));
                p.fillRect(r, g);
                p.restore();
            }
            TilePaint t;
            t.tile = &tiles[i];
            t.r = r;
            t.clean = d.clean;
            t.showLabel = labels;
            t.hover = (gi == 0 && i == 0) ? 1.0 : 0.0; // плитка «под курсором» — для строки внизу
            t.tint = c;
            t.icon = paintStubIcon;
            sk.paintTile(p, t);
        }
    }

    if (status > 0)
        sk.paintStatusBar(p, QRect(Theme::SidebarWidth, logical.height() - status, logical.width() - Theme::SidebarWidth, status),
                          TXT("Браузер"), TXT("РАБОТА / 01 · 4×2"),
                          QStringLiteral("C:\\Program Files\\Browser\\browser.exe"),
                          {}); // подсказки клавиш по умолчанию выключены
    p.end();
    Settings::get().apply(); // вернуть тему процесса как была
    return img;
}

// ---------- окно ----------

WelcomeWindow::WelcomeWindow(int skin, bool fill, bool labels)
{
    setWindowTitle(TXT("Добро пожаловать в StartMenu"));
    setAttribute(Qt::WA_DeleteOnClose);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 20);
    m_pages = new QStackedWidget;
    m_pages->addWidget(buildStylePage());
    m_pages->addWidget(buildAppsPage());
    m_pages->addWidget(buildDonePage());
    root->addWidget(m_pages, 1);

    setStyleSheet(QStringLiteral(R"(
        QWidget { background: #1A1B20; color: #E6E8EE; font-size: 13px; }
        QLabel#title { font-size: 24px; font-weight: 600; color: #FFFFFF; }
        QLabel#sub { color: #9AA0AE; font-size: 13px; }
        QLabel#tip { font-size: 14px; padding: 6px 0; }
        QPushButton { background: #2A2C33; border: 1px solid #3A3D45; border-radius: 8px; padding: 8px 18px; }
        QPushButton:hover { background: #33363E; }
        QPushButton#primary { background: #4C8DFF; border: none; color: white; font-weight: 600; }
        QPushButton#primary:hover { background: #5E9AFF; }
        QComboBox { background: #24262C; border: 1px solid #3A3D45; border-radius: 8px; padding: 6px 10px; min-width: 150px; }
        QComboBox QAbstractItemView { background: #24262C; selection-background-color: #4C8DFF; }
        QLineEdit { background: #24262C; border: 1px solid #3A3D45; border-radius: 8px; padding: 7px 10px; }
        QListWidget { background: #202227; border: 1px solid #33353C; border-radius: 8px; padding: 4px; }
        QListWidget::item { padding: 5px; border-radius: 6px; }
        QListWidget::item:hover { background: #2A2C33; }
        QCheckBox::indicator, QListWidget::indicator { width: 15px; height: 15px; border: 1px solid #5A5E6A;
                                                       border-radius: 4px; background: #24262C; }
        QCheckBox::indicator:checked, QListWidget::indicator:checked { background: #4C8DFF; border-color: #4C8DFF; image: url(:/res/check.png); }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: #3A3D45; border-radius: 3px; min-height: 30px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    )"));
    m_fill->setChecked(fill);
    m_labels->setChecked(labels && !qEnvironmentVariableIsSet("STARTMENU_WELCOME_NOLABELS")); // переменная — для снимков
    selectStyle(skin >= 0 ? skin : Settings::get().skin);
    refreshPreviews();
    WinShell::setDarkTitleBar(this);
}

QWidget* WelcomeWindow::buildStylePage()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(TXT("Как будет выглядеть ваше меню «Пуск»?"));
    title->setObjectName("title");
    // Язык — сразу на первом шаге; окно пересоздаётся на выбранном языке
    auto* langBox = new QComboBox;
    langBox->addItem(TXT("Как в Windows"), QStringLiteral("auto"));
    for (const Lang::Info& l : Lang::available())
        langBox->addItem(l.native, l.code);
    langBox->setCurrentIndex(qMax(0, langBox->findData(Settings::get().language)));
    connect(langBox, &QComboBox::activated, this, [this, langBox](int i) {
        Settings& s = Settings::get();
        s.language = langBox->itemData(i).toString();
        s.save();
        Lang::load(s.language);
        auto* w = new WelcomeWindow(m_skin, m_fill->isChecked(), m_labels->isChecked());
        w->setGeometry(geometry());
        w->show();
        close();
    });
    auto* head = new QHBoxLayout;
    head->addWidget(title, 1);
    head->addWidget(new QLabel(QStringLiteral("🌐")));
    head->addWidget(langBox);
    auto* sub = new QLabel(TXT("Выберите стиль — потом его можно поменять в настройках (ПКМ по меню → «Настройки меню…»)."));
    sub->setObjectName("sub");
    v->addLayout(head);
    v->addWidget(sub);
    v->addSpacing(12);

    auto* grid = new QGridLayout;
    grid->setSpacing(16);
    const QStringList names = Skin::names();
    for (int i = 0; i < 4; ++i)
    {
        auto* card = new StyleCard(names.value(i), Lang::tr(QString::fromUtf8(kStyleDesc[i])), page);
        connect(card, &QAbstractButton::clicked, this, [this, i] { selectStyle(i); });
        m_cards.append(card);
        grid->addWidget(card, i / 2, i % 2);
    }
    v->addLayout(grid);
    v->addSpacing(6);
    // Всё выбранное здесь не навсегда — подсказываем, где это поменять потом
    auto* note = new QLabel(TXT("Всё это можно изменить в любой момент: «Настройки меню» → «Плитки и окно» или правый клик по пустому месту меню."));
    note->setObjectName("sub");
    note->setWordWrap(true);
    v->addWidget(note);
    v->addSpacing(6);

    auto* bottom = new QHBoxLayout;
    m_labels = new SegSwitch(QString(), TXT("С подписями"), TXT("Без подписей"));
    m_labels->setToolTip(TXT("Название приложения под значком. Без подписей плитки выглядят чище — значок по центру."));
    connect(m_labels, &QCheckBox::toggled, this, &WelcomeWindow::refreshPreviews);
    bottom->addWidget(m_labels);
    bottom->addSpacing(16);
    m_fill = new SegSwitch(TXT("Заливать плитки цветом иконки"));
    m_fill->setToolTip(TXT("Фон каждой плитки — в цвет её иконки. Потом можно выбрать и «размытую иконку»."));
    connect(m_fill, &QCheckBox::toggled, this, &WelcomeWindow::refreshPreviews);
    bottom->addWidget(m_fill);
    bottom->addStretch();
    auto* step = new QLabel(TXT("Шаг 1 из 3"));
    step->setObjectName("sub");
    bottom->addWidget(step);
    bottom->addSpacing(12);
    auto* next = new QPushButton(TXT("Далее"));
    next->setObjectName("primary");
    connect(next, &QPushButton::clicked, this, [this] { goTo(1); });
    bottom->addWidget(next);
    v->addLayout(bottom);
    return page;
}

QWidget* WelcomeWindow::buildAppsPage()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(TXT("Что сразу добавить в меню?"));
    title->setObjectName("title");
    auto* sub = new QLabel(TXT("Отмеченные приложения появятся плитками в группе «Избранное». "
                                          "Можно ничего не выбирать — меню будет пустым, и вы добавите всё сами."));
    sub->setObjectName("sub");
    sub->setWordWrap(true);
    v->addWidget(title);
    v->addWidget(sub);
    v->addSpacing(10);

    auto* tools = new QHBoxLayout;
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(TXT("Найти приложение…"));
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& t) {
        for (int i = 0; i < m_apps->count(); ++i)
            m_apps->item(i)->setHidden(!t.isEmpty() && !m_apps->item(i)->text().contains(t, Qt::CaseInsensitive));
    });
    auto* rec = new QPushButton(TXT("Отметить популярные"));
    connect(rec, &QPushButton::clicked, this, &WelcomeWindow::setRecommended);
    auto* none = new QPushButton(TXT("Снять все"));
    connect(none, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_apps->count(); ++i)
            m_apps->item(i)->setCheckState(Qt::Unchecked);
    });
    tools->addWidget(m_filter, 1);
    tools->addWidget(rec);
    tools->addWidget(none);
    v->addLayout(tools);

    m_apps = new QListWidget;
    m_apps->setIconSize(QSize(24, 24));
    m_apps->setUniformItemSizes(true);
    connect(m_apps, &QListWidget::itemChanged, this, [this] {
        int n = 0;
        for (int i = 0; i < m_apps->count(); ++i)
            n += m_apps->item(i)->checkState() == Qt::Checked;
        m_count->setText(n ? TXT("Выбрано: %1").arg(n) : TXT("Ничего не выбрано — меню будет пустым"));
    });
    connect(m_apps, &QListWidget::itemClicked, this, [](QListWidgetItem* it) {
        // Клик по строке (а не только по галочке) тоже отмечает
        it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    });
    v->addWidget(m_apps, 1);

    // То, что человек уже закрепил в «Пуске» Windows, — сразу к нам (по умолчанию да)
    m_windowsPins = new SegSwitch(TXT("Перенести то, что закреплено в «Пуске» Windows"));
    m_windowsPins->setChecked(true);
    v->addWidget(m_windowsPins);

    const QString script = start11Script();
    if (!script.isEmpty())
    {
        m_start11 = new SegSwitch(TXT("Перенести группы и плитки из Start11"));
        m_start11->setChecked(true);
        v->addWidget(m_start11);
    }

    auto* bottom = new QHBoxLayout;
    m_count = new QLabel;
    m_count->setObjectName("sub");
    bottom->addWidget(m_count);
    bottom->addStretch();
    auto* step = new QLabel(TXT("Шаг 2 из 3"));
    step->setObjectName("sub");
    bottom->addWidget(step);
    bottom->addSpacing(12);
    auto* back = new QPushButton(TXT("Назад"));
    connect(back, &QPushButton::clicked, this, [this] { goTo(0); });
    auto* empty = new QPushButton(TXT("Начать с пустого меню"));
    connect(empty, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_apps->count(); ++i)
            m_apps->item(i)->setCheckState(Qt::Unchecked);
        goTo(2);
    });
    auto* next = new QPushButton(TXT("Далее"));
    next->setObjectName("primary");
    connect(next, &QPushButton::clicked, this, [this] { goTo(2); });
    bottom->addWidget(back);
    bottom->addWidget(empty);
    bottom->addWidget(next);
    v->addLayout(bottom);
    return page;
}

QWidget* WelcomeWindow::buildDonePage()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(TXT("Готово! Несколько подсказок"));
    title->setObjectName("title");
    v->addWidget(title);
    v->addSpacing(14);
    const QStringList tips{
        TXT("⊞  <b>Win</b> или кнопка «Пуск» — открыть и закрыть меню. Родной «Пуск» Windows — <b>Ctrl+Esc</b>."),
        TXT("⌕  Просто начните печатать — поиск приложений, файлов и параметров, а ещё калькулятор: <b>(12+3)*2</b>."),
        TXT("＋  Перетащите приложение из «Все приложения» (значок списка слева) или файл из Проводника — появится плитка."),
        TXT("☰  <b>ПКМ</b> по пустому месту — новая группа, подписи, настройки. ПКМ по плитке — размер, цвет, своя картинка."),
        TXT("♪  Когда играет музыка или видео, в меню появляется плеер. Клик по треку — большой плеер над меню с перемоткой, ПКМ — его вид."),
        TXT("⧉  Слева — звук: громкость и наушники/колонки. Историю буфера обмена можно включить галочкой ниже."),
        TXT("⊕  «+ Слой» рядом с поиском — ещё одна страница плиток. ПКМ по группе → «Добавить» — папка или ссылка на сайт."),
        TXT("⤢  Размер меню меняется мышкой за край окна — плитки подстроятся сами."),
    };
    for (const QString& t : tips)
    {
        auto* l = new QLabel(t);
        l->setObjectName("tip");
        l->setWordWrap(true);
        v->addWidget(l);
    }
    v->addSpacing(8);
    // История буфера обмена — только с явного согласия
    m_clipboard = new SegSwitch(TXT("Запоминать, что я копирую (история буфера обмена в меню)"));
    m_clipboard->setToolTip(TXT("Хранится только на этом компьютере. Пароли из менеджеров паролей не сохраняются. Можно выключить в настройках."));
    v->addWidget(m_clipboard);
    v->addSpacing(6);
    // Крупно — выбранный стиль, каким меню откроется
    m_final = new QLabel;
    m_final->setAlignment(Qt::AlignCenter);
    v->addWidget(m_final, 1);
    auto* bottom = new QHBoxLayout;
    bottom->addStretch();
    auto* step = new QLabel(TXT("Шаг 3 из 3"));
    step->setObjectName("sub");
    bottom->addWidget(step);
    bottom->addSpacing(12);
    auto* back = new QPushButton(TXT("Назад"));
    connect(back, &QPushButton::clicked, this, [this] { goTo(1); });
    auto* close = new QPushButton(TXT("Закрыть"));
    connect(close, &QPushButton::clicked, this, [this] { finish(false); });
    auto* open = new QPushButton(TXT("Открыть меню"));
    open->setObjectName("primary");
    connect(open, &QPushButton::clicked, this, [this] { finish(true); });
    bottom->addWidget(back);
    bottom->addWidget(close);
    bottom->addWidget(open);
    v->addLayout(bottom);
    return page;
}

void WelcomeWindow::refreshPreviews()
{
    const qreal dpr = devicePixelRatioF() * 1.0;
    const bool fill = m_fill && m_fill->isChecked();
    const bool labels = !m_labels || m_labels->isChecked();
    for (int i = 0; i < m_cards.size(); ++i)
        m_cards[i]->setPreview(renderPreview(i, fill, labels, qMax(1.0, dpr)));
}

void WelcomeWindow::selectStyle(int skin)
{
    m_skin = std::clamp(skin, 0, 3);
    for (int i = 0; i < m_cards.size(); ++i)
        m_cards[i]->setChecked(i == m_skin);
}

void WelcomeWindow::goTo(int page)
{
    if (page == 1 && m_allApps.isEmpty())
        loadApps();
    if (page == 2)
    {
        const qreal dpr = qMax(1.0, devicePixelRatioF());
        QPixmap pm = QPixmap::fromImage(renderPreview(m_skin, m_fill->isChecked(), m_labels->isChecked(), dpr * 1.2));
        pm.setDevicePixelRatio(dpr * 1.2);
        m_final->setPixmap(pm);
    }
    m_pages->setCurrentIndex(page);
}

void WelcomeWindow::loadApps()
{
    m_allApps = WinShell::allApps();
    if (Demo::active())
    {
        // Снимки для страницы проекта: нейтральный список вместо установленных у человека программ
        m_allApps.clear();
        const std::pair<const char*, const char*> demo[] = {
            {"Браузер", "browser"}, {"Почта", "mail"}, {"Календарь", "calendar"}, {"Заметки", "notes"}, {"Файлы", "files"},
            {"Дизайн", "design"}, {"Фото", "photos"}, {"Видео", "video"}, {"Код", "code"}, {"Музыка", "music"},
            {"Игры", "games"}, {"Чат", "chat"}};
        for (const auto& d : demo)
            m_allApps.append({Lang::tr(QString::fromUtf8(d.first)), QStringLiteral("demo:") + QLatin1String(d.second), {}, {}});
    }
    std::sort(m_allApps.begin(), m_allApps.end(), [](const Item& a, const Item& b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    m_apps->blockSignals(true);
    for (int i = 0; i < m_allApps.size(); ++i)
    {
        auto* it = new QListWidgetItem(m_allApps[i].name, m_apps);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Unchecked);
        it->setData(Qt::UserRole, i);
    }
    m_apps->blockSignals(false);
    setRecommended();
    // Иконки — порциями, чтобы окно отвечало сразу.
    m_iconNext = 0;
    QTimer::singleShot(0, this, &WelcomeWindow::loadSomeIcons);
}

void WelcomeWindow::loadSomeIcons()
{
    const int end = qMin(m_iconNext + 8, m_apps->count());
    for (; m_iconNext < end; ++m_iconNext)
    {
        QListWidgetItem* it = m_apps->item(m_iconNext);
        const QString path = m_allApps[it->data(Qt::UserRole).toInt()].path;
        const QPixmap pm = Demo::active() ? QPixmap::fromImage(Demo::icon(path, 48)) : WinShell::loadIcon(path, 48);
        if (!pm.isNull())
            it->setIcon(QIcon(pm));
    }
    if (m_iconNext < m_apps->count())
        QTimer::singleShot(0, this, &WelcomeWindow::loadSomeIcons);
}

void WelcomeWindow::setRecommended()
{
    // Самое частое, что обычно закрепляют. Ищем по началу названия.
    static const QStringList popular{
        QStringLiteral("Google Chrome"), QStringLiteral("Firefox"), QStringLiteral("Microsoft Edge"), QStringLiteral("Opera"),
        QStringLiteral("Yandex"), TXT("Яндекс"), TXT("Проводник"), QStringLiteral("File Explorer"),
        QStringLiteral("Telegram"), QStringLiteral("Discord"), QStringLiteral("Spotify"), QStringLiteral("Steam"),
        QStringLiteral("Visual Studio Code"), TXT("Блокнот"), QStringLiteral("Notepad"),
        TXT("Калькулятор"), QStringLiteral("Calculator"), TXT("Параметры"), QStringLiteral("Settings"),
        QStringLiteral("Adobe Photoshop"), QStringLiteral("Zoom"), QStringLiteral("WhatsApp"), QStringLiteral("VLC"),
        QStringLiteral("Microsoft Word"), QStringLiteral("Microsoft Excel"), QStringLiteral("Word"), QStringLiteral("Excel"),
        QStringLiteral("Epic Games Launcher"), QStringLiteral("Figma"), QStringLiteral("OBS Studio"),
        TXT("Браузер"), TXT("Почта"), TXT("Календарь"), TXT("Фото"), TXT("Музыка"), TXT("Чат"),
    };
    for (int i = 0; i < m_apps->count(); ++i)
    {
        QListWidgetItem* it = m_apps->item(i);
        bool hit = false;
        for (const QString& p : popular)
            if (it->text().startsWith(p, Qt::CaseInsensitive))
            {
                hit = true;
                break;
            }
        if (hit)
            it->setCheckState(Qt::Checked);
    }
}

void WelcomeWindow::finish(bool openMenu)
{
    if (m_saved)
        return;
    m_saved = true;

    // Стиль и заливка
    Settings& s = Settings::get();
    s.load();
    if (!s.restoreStyle(m_skin))
    {
        Skin::select(m_skin);
        Skin::current().preset(s);
        s.preset = 1;
    }
    s.tileFill = m_fill->isChecked() ? 1 : 0;
    s.showLabels = m_labels->isChecked();
    s.clipboardHistory = m_clipboard && m_clipboard->isChecked();
    s.lastSeenVersion = Updater::currentVersion(); // свежая установка: карточку «Что нового» не показываем
    s.save();
    s.apply();

    // Группы из Start11 — тем же скриптом, что в папке tools
    if (m_start11 && m_start11->isChecked())
    {
        QProcess ps;
        ps.start(QStringLiteral("powershell.exe"), {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                                                    QStringLiteral("Bypass"), QStringLiteral("-File"),
                                                    QDir::toNativeSeparators(start11Script())});
        ps.waitForFinished(30000);
    }

    // Выбранные приложения — группа «Избранное» первой
    Config& c = Config::instance();
    c.load();
    if (m_windowsPins && m_windowsPins->isChecked())
    {
        QApplication::setOverrideCursor(Qt::WaitCursor); // экспорт закреплений Windows — пара секунд
        PinSync::importWindowsPins();
        QApplication::restoreOverrideCursor();
    }
    Group fav;
    fav.title() = TXT("Избранное");
    fav.width = 8;
    for (int i = 0; i < m_apps->count(); ++i)
        if (m_apps->item(i)->checkState() == Qt::Checked)
            fav.tiles().append(Tile{m_allApps[m_apps->item(i)->data(Qt::UserRole).toInt()], TileSize::Medium});
    if (!fav.tiles().isEmpty())
        c.groups().prepend(fav);
    c.normalize();
    c.save();

    // Сообщить меню и трею
    signalEvent(L"Local\\StartMenu.App.MenuReload");
    signalEvent(L"Local\\StartMenu.App.HostReload");
    if (openMenu)
    {
        if (HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\StartMenu.App.Menu"))
        {
            CloseHandle(m);
            signalEvent(L"Local\\StartMenu.App.Show"); // меню наготове — просто показать
        }
        else
            WinShell::startSelf(QStringLiteral("--menu"));
    }
    close();
}

void WelcomeWindow::saveShots(const QString& dir)
{
    QDir().mkpath(dir);
    resize(1140, 820);
    for (int i = 0; i < 3; ++i)
    {
        goTo(i);
        QCoreApplication::processEvents();
        grab().save(dir + QStringLiteral("/welcome%1.png").arg(i + 1));
    }
}
