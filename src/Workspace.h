#pragma once
#include <QFont>
#include <QHash>
#include <QTimer>
#include <QList>
#include <QWidget>

#include "Config.h"
#include "Theme.h"

class IconCache;
class QMenu;
class QAction;

// Рабочее пространство: колонки групп с плитками. Всё рисуется одним виджетом в paintEvent.
// Мышью: перетаскивание плиток (между группами, в пустое место — новая группа, на центр другой плитки — папка),
// перетаскивание групп за заголовок, ширина группы — за правый край, файлы из Проводника — прямо в группу.
class Workspace : public QWidget
{
    Q_OBJECT
public:
    explicit Workspace(IconCache* icons, QWidget* parent = nullptr);

    void reload();
    void applyTheme();         // шрифты и размеры поменялись в настройках
    bool closeFolder();        // true, если была открыта папка
    QStringList statsLine() const; // «СЕТКА 46·4», «07 ГРУПП», «15 ПЛИТОК»
    void moveFocus(int dx, int dy);   // стрелки: выбор плитки с клавиатуры
    bool activateFocus();      // Enter по выбранной плитке
    static const char* itemMime();    // перетаскивание из «Всех приложений»
    QRect tileViewRect(int g, int t) const { return toView(m_geo.value(g).tiles.value(t)); } // для самопроверки

signals:
    void launched();
    void busyChanged(bool busy);
    void allAppsRequested();   // кнопка «Все приложения» в подсказке пустого меню
    void hoverInfo(const QString& name, const QString& path, const QString& location); // для строки состояния (Studio)

protected:
    QRect emptyButtonRect() const; // кнопка в подсказке пустого меню
    bool event(QEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    enum class Hit { None, Tile, Header, Tab, Grip, Group, FolderItem, FolderTitle, FolderPanel };
    struct HitInfo
    {
        Hit kind = Hit::None;
        int g = -1;
        int t = -1;
        bool operator==(const HitInfo& o) const { return kind == o.kind && g == o.g && t == o.t; }
    };
    enum class DropKind { None, Insert, IntoFolder, NewGroup, MoveGroup, JoinRow };
    struct Drop
    {
        DropKind kind = DropKind::None;
        int g = -1;
        int index = -1;
        int column = -1;
        QRect indicator;       // в координатах содержимого
    };
    struct Source               // что тащим: плитку (child = -1) или элемент папки
    {
        int g = -1;
        int t = -1;
        int child = -1;
    };
    enum class Mode { Idle, Pressed, DragTile, DragGroup, Resize };

    struct GroupGeo
    {
        QRect frame;
        QRect header;
        QRect body;
        QList<QRect> tiles;
    };
    struct ColumnGeo
    {
        int x;
        int width;
        int bottom;
    };

    void relayout();
    // Свободные ярлыки не имеют ни заголовка, ни рамки-подложки: высота шапки и отступы у них нулевые
    // Небольшой запас вокруг — чтобы уголки подсветки наведения у плиток не обрезались краем окна и соседями
    static int headerOf(const Group& g) { return g.loose ? 8 : Theme::GroupHeader; }
    static int padOf(const Group& g) { return g.loose ? 8 : Theme::GroupPad; }
    void finish();                         // нормализовать, сохранить, перестроить
    void setScroll(int x, int y);
    QPoint toContent(const QPoint& view) const { return view + QPoint(m_scrollX, m_scrollY); }
    QRect toView(const QRect& r) const { return r.translated(-m_scrollX, -m_scrollY); }

    HitInfo hitTest(const QPoint& view) const;
    QRect titleRect(int g) const;
    QRect tabRect(int g, int tab) const;
    QString tabLabel(int g, int tab) const;
    QRect gripRect(int g) const;
    int columnAt(int contentX) const;

    Drop computeTileDrop(const QPoint& view, const Source& src) const;
    Drop computeGroupDrop(const QPoint& view, int srcGroup) const;
    void applyTileDrop(const Drop& d, QList<Tile> moving, const Source& src);
    void applyGroupDrop(const Drop& d, int srcGroup);
    Tile sourceTile(const Source& src) const;

    void paintTile(QPainter& p, const Tile& tile, const QRect& r, bool withBg, qreal hover, bool pressed,
                   bool showLabel = true);
    static bool labelVisible(const Group& g, const Tile& t); // плитка → группа → общая настройка
    QPixmap tileImage(const QString& path, const QSize& size); // своё изображение, обрезанное под плитку
    QHash<QString, QPixmap> m_images;
    void animate();            // один кадр анимаций (подсветка, прокрутка)
    void startAnimation();
    void scrollAnimated(int x, int y);
    void ensureVisible(const QRect& contentRect);
    static quint64 tileKey(int g, int t) { return (quint64(quint32(g)) << 32) | quint32(t); }
    void paintIcon(QPainter& p, const Item& item, const QRect& r);

    bool folderOpen() const;
    const Tile& openFolder() const;
    int folderColumns() const;
    QRect folderPanelRect() const;
    QRect folderItemRect(int i) const;

    void tileMenu(int g, int t, const QPoint& globalPos);
    void groupMenu(int g, const QPoint& globalPos);
    void looseMenu(int g, const QPoint& globalPos);   // меню блока свободных ярлыков
    void addLooseTiles(const QList<Tile>& tiles, int savedColumn); // свободные ярлыки в колонку: в её блок свободных или в новый
    void emptyMenu(const QPoint& view, const QPoint& globalPos);
    void folderItemMenu(int child, const QPoint& globalPos);
    void launchItem(const Item& item, bool admin = false);
    bool askText(const QString& title, const QString& label, QString& value);
    bool askLink(Item& out, const QString& current = {}); // адрес и название ссылки на сайт
    QAction* addCheck(QMenu& menu, const QString& text, bool checked);
    void setAllLabels(bool on);   // общая настройка + сброс частных выборов в групп и плиток

    IconCache* m_icons;
    QFont m_titleFont;
    QFont m_tileFont;

    QList<GroupGeo> m_geo;     // индекс = индекс группы в Config
    QList<int> m_visualCol;    // в какой колонке группа стоит на экране (при узком окне может отличаться от сохранённой)
    QList<bool> m_rowStart;    // группа начинает ряд (а не стоит справа от соседней)
    QList<int> m_number;       // сквозной номер группы по колонкам сверху вниз
    void emitHoverInfo();
    bool m_reflowed = false;   // окно узкое — группы перестроены в меньшее число колонок
    int savedColumnFor(int visualCol) const; // экранная колонка → сохранённая (правки не портят исходную раскладку)
    // Пока тянут край группы, ячейку не пересчитываем — иначе ширина «убегает» от курсора.
    int m_frozenCell = 0;
    QList<int> m_frozenVisual;
    bool m_frozenReflowed = false;
    QList<ColumnGeo> m_columns;
    QSize m_content;
    int m_scrollX = 0;
    int m_scrollY = 0;

    HitInfo m_hover;
    Mode m_mode = Mode::Idle;
    HitInfo m_press;
    QPoint m_pressPos;
    QPoint m_dragPos;
    QPoint m_dragOffset;
    QSize m_dragSize;
    Source m_dragSrc;
    Drop m_drop;

    int m_folderG = -1;
    int m_folderT = -1;

    // Плавность: уровень подсветки каждой плитки (0…1) и целевая прокрутка.
    QHash<quint64, qreal> m_fade;
    QTimer m_anim;
    int m_targetX = 0;
    int m_targetY = 0;

    // Выбор с клавиатуры
    int m_focusG = -1;
    int m_focusT = -1;
    bool m_emptyHover = false;
};
