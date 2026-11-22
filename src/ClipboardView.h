#pragma once
#include <QHash>
#include <QPixmap>
#include <QWidget>

#include "ClipboardStore.h"

// Список истории буфера обмена (вид меню, как «Все приложения»).
// Клик / Enter — вставить в окно, где был курсор до открытия меню; ПКМ — скопировать, закрепить, удалить.
class ClipboardView : public QWidget
{
    Q_OBJECT
public:
    explicit ClipboardView(QWidget* parent = nullptr);
    void reload();
    void moveSelection(int dy);
    void activateSelected();
    int count() const { return int(m_items.size()); }

signals:
    void pasteRequested(const ClipboardStore::Entry& entry); // вставить в предыдущее окно
    void busyChanged(bool busy);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void wheelEvent(QWheelEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    int rowAt(const QPoint& pos) const;
    QRect rowRect(int i) const;
    QRect clearRect() const;
    int rowHeight(int i) const;
    int contentHeight() const;
    void ensureVisible(int i);
    QPixmap thumb(const ClipboardStore::Entry& e);

    QList<ClipboardStore::Entry> m_items;
    QStringList m_previews;           // первые 400 символов каждой записи — для отрисовки
    QHash<QString, QPixmap> m_thumbs;
    int m_hover = -1, m_selected = 0;
    bool m_clearHover = false;
    int m_scroll = 0;
};
