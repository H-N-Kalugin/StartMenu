#pragma once
#include <QList>
#include <QWidget>

// Левая панель. Сверху: «Плитки», «Все приложения», «Буфер обмена» (точка — есть новое).
// Снизу: звук (клик — громкость и устройство, колёсико — громкость), папки быстрого доступа,
// «Параметры», «Выполнить», питание. Папку можно добавить, перетащив её из Проводника на панель.
class Sidebar : public QWidget
{
    Q_OBJECT
public:
    explicit Sidebar(QWidget* parent = nullptr);
    enum class Mode { Tiles, AllApps, Clipboard };
    void setMode(Mode m);
    void setClipboardDot(bool on);
    void setUpdateDot(bool on);      // вышла новая версия — точка на «Параметрах»
    void relayout();           // включили/выключили кнопки в настройках

signals:
    void tilesRequested();
    void allAppsRequested();
    void clipboardRequested();
    void soundRequested(const QPoint& globalPos);
    void powerRequested(const QPoint& globalPos);
    void launched();
    void busyChanged(bool busy);

protected:
    bool event(QEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    enum class Kind { Tiles, AllApps, Clipboard, Sound, Folder, Settings, Run, Power };
    struct Button
    {
        Kind kind;
        QRect rect;
        int folder = -1;       // индекс в Config::sidebar()
    };
    int buttonAt(const QPoint& p) const;
    void trigger(const Button& b);
    QString tipFor(const Button& b) const;
    void settingsMenu(const QPoint& globalPos);

    QList<Button> m_buttons;
    int m_hover = -1;
    int m_pressed = -1;
    Mode m_mode = Mode::Tiles;
    bool m_clipDot = false;
    bool m_updateDot = false;
    bool m_muted = false;
};
