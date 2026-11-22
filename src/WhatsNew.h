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
    void placeIn(const QRect& area); // правый нижний угол области (экранные координаты, с отступом)

signals:
    void dismissed(bool dontShowAgain); // закрыли; true — поставили «Больше не показывать»

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    QString m_version;
    QStringList m_bullets;
    QRect m_button, m_link, m_close, m_check;
    bool m_dontShow = false;
    int m_hover = 0; // 1 — кнопка, 2 — ссылка, 3 — крестик, 4 — галочка
    void relayout();
};

namespace WhatsNew
{
// Пункты «что нового» для версии; пусто — для этой версии карточку не показываем.
QStringList bulletsFor(const QString& version);
}
