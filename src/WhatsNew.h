#pragma once
#include <QRect>
#include <QString>
#include <QStringList>
#include <QWidget>

// Карточка «Что нового» — показывается один раз в углу меню после обновления программы.
// Короткие пункты и кнопка «Понятно». Тексты версий лежат в WhatsNew::bulletsFor.
class WhatsNewCard : public QWidget
{
    Q_OBJECT
public:
    explicit WhatsNewCard(QWidget* parent);
    void setContent(const QString& version, const QStringList& bullets);
    void placeIn(const QRect& area); // правый нижний угол области (с отступом)

signals:
    void dismissed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    QString m_version;
    QStringList m_bullets;
    QRect m_button, m_link, m_close;
    int m_hover = 0; // 1 — кнопка, 2 — ссылка, 3 — крестик
    void relayout();
};

namespace WhatsNew
{
// Пункты «что нового» для версии; пусто — для этой версии карточку не показываем.
QStringList bulletsFor(const QString& version);
}
