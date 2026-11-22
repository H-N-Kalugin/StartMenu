#include "WhatsNew.h"
#include "Lang.h"
#include "Skin.h"
#include "Theme.h"

#include <QDesktopServices>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>

namespace
{
constexpr int kWidth = 400;
constexpr int kPad = 18;
constexpr int kGap = 8;
}

namespace WhatsNew
{
QStringList bulletsFor(const QString& version)
{
    // Коротко: что человек заметит. Каждая версия — свой список; для остальных карточка не показывается.
    if (version == QLatin1String("1.0.4"))
        return {TXT("Ярлыки без группы выравниваются по плиткам соседних групп: ПКМ по ним → «Выравнивание»"),
                TXT("Папки и ссылки можно добавлять без группы: ПКМ по пустому месту → «Добавить ярлык»"),
                TXT("Плеер: запоздавшая обложка Spotify теперь появляется, а без обложки виден значок приложения"),
                TXT("Установщик с нашей иконкой, проверка обновлений каждые 30 минут")};
    return {};
}
}

WhatsNewCard::WhatsNewCard(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    hide();
}

void WhatsNewCard::setContent(const QString& version, const QStringList& bullets)
{
    m_version = version;
    m_bullets = bullets;
    relayout();
}

// Высота считается по тексту: заголовок, пункты с переносом по словам, строка с кнопкой.
void WhatsNewCard::relayout()
{
    const QFont titleF = Skin::current().font(Skin::Font::GroupTitle);
    QFont bodyF = Skin::current().font(Skin::Font::ListRow);
    bodyF.setPixelSize(13);
    const int textW = kWidth - 2 * kPad - 16;
    int h = kPad + QFontMetrics(titleF).height() + 12;
    for (const QString& b : m_bullets)
        h += QFontMetrics(bodyF).boundingRect(QRect(0, 0, textW, 4000), Qt::TextWordWrap, b).height() + kGap;
    h += 6;
    const int rowH = 30;
    const QFontMetrics fm(bodyF);
    const int btnW = fm.horizontalAdvance(TXT("Понятно")) + 36;
    m_button = QRect(kWidth - kPad - btnW, h, btnW, rowH);
    const int linkW = fm.horizontalAdvance(TXT("Все изменения"));
    m_link = QRect(kPad, h, linkW, rowH);
    m_close = QRect(kWidth - 30, 8, 22, 22);
    h += rowH + kPad;
    resize(kWidth, h);
}

void WhatsNewCard::placeIn(const QRect& area)
{
    move(area.right() - width() - 16, area.bottom() - height() - 16);
}

void WhatsNewCard::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    // Плотная подложка цвета панели (чтобы текст читался поверх плиток) и акцентная рамка
    QColor bg = Theme::PanelBg.color();
    bg.setAlpha(255);
    bg = Skin::mix(bg, Theme::TextColor.color(), Theme::Light ? 0.04 : 0.08);
    p.setPen(QPen(Theme::Accent.color(), 1.2));
    p.setBrush(bg);
    p.drawRoundedRect(r, 12, 12);

    const QFont titleF = Skin::current().font(Skin::Font::GroupTitle);
    QFont bodyF = Skin::current().font(Skin::Font::ListRow);
    bodyF.setPixelSize(13);
    p.setPen(Theme::TextColor.color());
    p.setFont(titleF);
    const QFontMetrics tfm(titleF);
    p.drawText(QRect(kPad, kPad, kWidth - 2 * kPad - 24, tfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
               TXT("Что нового в версии %1").arg(m_version));
    // Крестик
    {
        const QPointF c = QRectF(m_close).center();
        QColor x = Theme::SubText.color();
        if (m_hover == 3)
            x = Theme::TextColor.color();
        p.setPen(QPen(x, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(c.x() - 4, c.y() - 4), QPointF(c.x() + 4, c.y() + 4));
        p.drawLine(QPointF(c.x() + 4, c.y() - 4), QPointF(c.x() - 4, c.y() + 4));
    }

    p.setFont(bodyF);
    const int textW = kWidth - 2 * kPad - 16;
    int y = kPad + tfm.height() + 12;
    for (const QString& b : m_bullets)
    {
        const QRect br = QFontMetrics(bodyF).boundingRect(QRect(0, 0, textW, 4000), Qt::TextWordWrap, b);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::Accent.color());
        p.drawEllipse(QPointF(kPad + 3.5, y + QFontMetrics(bodyF).height() / 2.0), 3, 3);
        p.setPen(Theme::TextColor.color());
        p.drawText(QRect(kPad + 16, y, textW, br.height()), Qt::TextWordWrap, b);
        y += br.height() + kGap;
    }

    // «Все изменения» — ссылка слева, «Понятно» — кнопка справа
    QFont linkF = bodyF;
    linkF.setUnderline(m_hover == 2);
    p.setFont(linkF);
    p.setPen(m_hover == 2 ? Theme::TextColor.color() : Theme::SubText.color());
    p.drawText(m_link, Qt::AlignLeft | Qt::AlignVCenter, TXT("Все изменения"));

    p.setFont(bodyF);
    p.setPen(Qt::NoPen);
    QColor btn = Theme::Accent.color();
    if (m_hover == 1)
        btn = btn.lighter(112);
    p.setBrush(btn);
    p.drawRoundedRect(QRectF(m_button), m_button.height() / 2.0, m_button.height() / 2.0);
    // Подпись на акценте — белая или тёмная, что контрастнее
    const QColor a = Theme::Accent.color();
    const bool darkText = (0.2126 * a.redF() + 0.7152 * a.greenF() + 0.0722 * a.blueF()) > 0.62;
    p.setPen(darkText ? QColor(0x10, 0x12, 0x18) : QColor(Qt::white));
    p.drawText(m_button, Qt::AlignCenter, TXT("Понятно"));
}

void WhatsNewCard::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint pos = e->position().toPoint();
    const int h = m_button.contains(pos) ? 1 : m_link.contains(pos) ? 2 : m_close.contains(pos) ? 3 : 0;
    if (h != m_hover)
    {
        m_hover = h;
        setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void WhatsNewCard::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QPoint pos = e->position().toPoint();
    if (m_link.contains(pos))
    {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/NKF-XXX/StartMenu/releases/tag/v%1").arg(m_version)));
        return;
    }
    if (m_button.contains(pos) || m_close.contains(pos))
    {
        hide();
        emit dismissed();
    }
}
