#pragma once
#include <QFont>
#include <QTimer>
#include <QList>
#include <QWidget>

#include "Config.h"

class IconCache;

// Список приложений колонками (как «Все приложения»), он же — результаты поиска.
// Один виджет, рисуется в paintEvent; листается колесом вбок.
class AppList : public QWidget
{
    Q_OBJECT
public:
    explicit AppList(IconCache* icons, QWidget* parent = nullptr);

    // sections: перед элементом с этим индексом — заголовок раздела (поиск: «Лучшее совпадение», «Приложения»…).
    void setItems(QList<Item> items, bool letterHeaders, const QString& title,
                  const QList<QPair<int, QString>>& sections = {}, const QStringList& highlight = {});
    void moveSelection(int dx, int dy);
    void activateSelected();
    int count() const { return int(m_items.size()); }

signals:
    void launched();
    void dragRequested(const Item& item); // потянули строку — её можно бросить в группу плиток
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

private:
    struct Row
    {
        int item = -1;         // -1 — заголовок-буква
        QString letter;
        QRect rect;            // в координатах содержимого (без прокрутки)
    };
    void relayout();
    int rowAt(const QPoint& view) const;
    int rowOfItem(int item) const;
    void setScroll(int x);
    void scrollAnimated(int x);
    void ensureVisible(int item);
    void itemMenu(int item, const QPoint& globalPos);

    IconCache* m_icons;
    QFont m_font;
    QFont m_headerFont;
    QList<Item> m_items;
    QList<Row> m_rows;
    QString m_title;
    QList<QPair<int, QString>> m_sections;
    QStringList m_highlight;
    bool m_letters = false;
    int m_contentW = 0;
    int m_scroll = 0;
    int m_hover = -1;          // строка
    int m_pressed = -1;        // строка
    int m_selected = -1;       // элемент
    QPoint m_pressPos;
    int m_target = 0;          // плавная прокрутка
    QTimer m_anim;
};
