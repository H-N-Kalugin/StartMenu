#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>
#include <memory>

#include "Config.h"

class AppList;
class IconCache;
class QTimer;
class Sidebar;
class Workspace;
class MediaPlayer;
class PlayerWindow;
class ClipboardView;
class SystemStats;
namespace ClipboardStore { struct Entry; }

// Своё «матовое стекло» (размытие снимка экрана) — общее для меню и панели плеера.
QImage frostedGlass(const QImage& src, int radius);

// Окно меню. Живёт в отдельном процессе (StartMenu.exe --menu): закрылось окно — процесс завершился,
// и вся память, которую успели занять Shell и иконки, вернулась системе.
class StartWindow : public QWidget
{
    Q_OBJECT
public:
    // preview — не закрываться при потере фокуса (для окна настроек); persistent — режим «наготове»:
    // при закрытии окно прячется и ждёт следующего Win, процесс не завершается
    explicit StartWindow(bool preview = false, bool persistent = false);
    ~StartWindow() override;

    void popup();
    void closeFromHost();      // повторное нажатие Win пришло из процесса трея
    void reloadSettings();     // окно настроек сохранило изменения
    void setQuery(const QString& q); // для проверки: открыть сразу с поиском (--query)
    void reloadTiles();        // конфигурацию плиток поменяли снаружи (синхронизация с Windows)
    void prefetchApps();       // после показа: список приложений из кэша + обновление в фоне
    void prepareHidden();      // режим «наготове»: всё загрузить и отрисовать в памяти, но не показывать
    void showFromStandby();    // сигнал «показать»: свежие настройки и плитки — и на экран
    QImage renderWithPlayer(); // для --render: меню + панель плеера над ним
    void clipboardChanged();   // трей записал новое в историю буфера обмена
    void itemsChangedOutside(); // плитки изменили другим процессом — перечитать
    void prepareRender(bool clipboard); // для --render: вид буфера и значения монитора без показа окна

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void moveEvent(QMoveEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    bool event(QEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override; // края окна: изменение размера мышью

private:
    enum Edge { EdgeLeft = 1, EdgeRight = 2, EdgeTop = 4 };
    int edgesAt(const QPoint& local) const;
    void setEdgeCursor(int edges);
    void resizeTo(const QPoint& globalPos);
    void finishResize();
    void refreshBackdrop();
    enum class View { Tiles, AllApps, Search, Clipboard };
    void applyStyle();
    void layoutChildren();     // поле поиска сверху, под ним плитки/список
    void applyEffect();
    QRect targetGeometry() const;
    void grabBackdrop(const QRect& geo);
    void setView(View v);
    void updateSearch();
    void ensureApps();
    void powerMenu(const QPoint& globalPos);
    void startDragToTiles(const Item& item);
    void setBusy(bool busy);
    void checkFocus();

    IconCache* m_icons = nullptr;
    Sidebar* m_sidebar = nullptr;
    Workspace* m_workspace = nullptr;
    MediaPlayer* m_media = nullptr;  // плеер: компактный в строке поиска или панелью над меню
    PlayerWindow* m_playerWin = nullptr;
    ClipboardView* m_clip = nullptr;
    std::unique_ptr<SystemStats> m_stats;
    QTimer* m_statsTimer = nullptr;
    QString m_statsLine;
    void startStats();
    void logClose(const QString& reason);
    bool m_closeLogged = false;
    int m_busyStale = 0;       // сколько проверок подряд «занято» висит без открытого меню
    // Слои (страницы плиток): переключатель рядом с поиском
    QList<QRect> m_layerRects;
    QRect m_layerAdd;
    // «Доступна версия X» — тихая надпись внизу справа
    bool m_updateNew = false;        // есть версия новее (и о ней не попросили молчать): рядом с номером пульсирует кружок
    QString m_updateVersion;         // какая именно
    bool m_updateBusy = false;
    bool m_updateHover = false;
    qreal m_pulse = 0;               // фаза пульсации 0..1
    QTimer* m_pulseTimer = nullptr;
    QElapsedTimer m_pulseClock;      // время с начала пульсации — фаза не «прыгает», если кадр запоздал
    QRect updateRect() const;        // номер версии (+ кружок)
    class WhatsNewCard* m_whatsNew = nullptr;  // «Что нового» после обновления
    void maybeShowWhatsNew(bool force = false);
    void refreshUpdate();
    void updateMenu();
    int m_layerHover = -1;     // индекс слоя, -2 — «+»
    int layerAt(const QPoint& pos) const;
    void switchLayer(int i);
    void addLayer();
    void paintLayers(QPainter& p);
    void sampleStats();
    void pasteFromHistory(const ClipboardStore::Entry& entry);
    bool m_panelMode = false;
    void syncPlayer();
    QRect m_mediaRect;
    AppList* m_list = nullptr;
    QTimer* m_focusTimer = nullptr;

    View m_view = View::Tiles;
    View m_viewBeforeSearch = View::Tiles;
    QString m_query;
    QList<Item> m_apps;
    bool m_appsLoaded = false;
    bool m_appsRefreshing = false;
    void applyFreshApps(const QList<Item>& apps);

    int m_busy = 0;            // открыто контекстное меню/диалог — не закрываемся
    bool m_wasActive = false;
    quintptr m_prevForeground = 0;
    QElapsedTimer m_openedAt;   // сколько прошло с открытия (первые 0,6 с фокус забираем обратно)
    int m_regrabs = 0;
    bool m_firstPaintLogged = false;
    bool m_preview = false;
    bool m_persistent = false;
    QPointer<QVariantAnimation> m_showAnim; // текущее появление окна
    QElapsedTimer m_hiddenAt;
    bool m_hiddenByFocus = false; // спрятались из-за ухода фокуса (а не клавишей Win)  // когда спрятали — чтобы щелчок по трею сразу после этого не открыл меню снова
    bool m_closePending = false; // трей попросил закрыться, пока было открыто меню/диалог
    QImage m_backdrop;         // размытый снимок экрана под окном (своё матовое стекло)
    QRect m_searchRect;        // поле поиска (видно всегда)
    QTimer* m_caretTimer = nullptr;
    bool m_caretOn = true;
    bool m_searchFocused = false; // по полю поиска кликнули — курсор мигает и без текста
    QString m_statusName, m_statusPath, m_statusLocation; // для строки состояния (Studio)
    void startSearchCaret();

    int m_resizeEdges = 0;     // какие края тянем сейчас (0 — не тянем)
    int m_cursorEdges = 0;     // над какими краями сейчас курсор
    QRect m_resizeStart;
    QPoint m_resizePress;
};
