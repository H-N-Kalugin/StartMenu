#pragma once
#include <QCheckBox>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include "Lang.h"

// Переключатель-«таблетка» вместо галочки: два сегмента («Вкл | Выкл» или свои подписи),
// выбранный подсвечен акцентной кнопкой, второй — тусклый текст. Это обычный QCheckBox
// (toggled / isChecked / setChecked работают как раньше), поэтому код вокруг не меняется.
class SegSwitch : public QCheckBox
{
public:
    explicit SegSwitch(const QString& text = QString(), const QString& on = TXT("Вкл"),
                       const QString& off = TXT("Выкл"), QWidget* parent = nullptr)
        : QCheckBox(text, parent), m_on(on), m_off(off)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        int w = pillWidth();
        if (!text().isEmpty())
            w += kGap + fm.horizontalAdvance(text());
        return {w + 2, qMax(fm.height() + 12, 28)};
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    bool hitButton(const QPoint& p) const override { return rect().contains(p); }

    void enterEvent(QEnterEvent* e) override { m_hover = true; update(); QCheckBox::enterEvent(e); }
    void leaveEvent(QEvent* e) override { m_hover = false; update(); QCheckBox::leaveEvent(e); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor fg = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText);
        const QColor accent(0x4C, 0x8D, 0xFF);
        const QFontMetrics fm(font());
        const int segW = segmentWidth();
        const int h = qMin(height() - 4, fm.height() + 10);
        const QRectF pill(1, (height() - h) / 2.0, segW * 2, h);

        QColor track = fg; track.setAlpha(28);
        QColor edge = fg;  edge.setAlpha(60);
        p.setPen(QPen(edge, 1));
        p.setBrush(track);
        p.drawRoundedRect(pill.adjusted(0.5, 0.5, -0.5, -0.5), h / 2.0, h / 2.0);

        const QRectF act = isChecked() ? QRectF(pill.left(), pill.top(), segW, h)
                                       : QRectF(pill.left() + segW, pill.top(), segW, h);
        QColor fill = isEnabled() ? accent : QColor(0x3A, 0x45, 0x60);
        if (m_hover && isEnabled()) fill = fill.lighter(112);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(act.adjusted(2, 2, -2, -2), (h - 4) / 2.0, (h - 4) / 2.0);

        QFont bold = font(); bold.setWeight(QFont::DemiBold);
        const QRectF onR(pill.left(), pill.top(), segW, h), offR(pill.left() + segW, pill.top(), segW, h);
        QColor dim = fg; dim.setAlpha(m_hover ? 190 : 140);
        p.setFont(isChecked() ? bold : font());
        p.setPen(isChecked() ? Qt::white : dim);
        p.drawText(onR, Qt::AlignCenter, m_on);
        p.setFont(isChecked() ? font() : bold);
        p.setPen(isChecked() ? dim : Qt::white);
        p.drawText(offR, Qt::AlignCenter, m_off);

        if (!text().isEmpty()) {
            p.setFont(font());
            p.setPen(fg);
            const QRect tr(int(pill.right()) + kGap, 0, width() - int(pill.right()) - kGap, height());
            p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, text());
        }
        if (hasFocus()) {
            p.setPen(QPen(accent, 1, Qt::DotLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
        }
    }

private:
    int segmentWidth() const
    {
        QFont bold = font(); bold.setWeight(QFont::DemiBold);
        const QFontMetrics fm(bold);
        return qMax(fm.horizontalAdvance(m_on), fm.horizontalAdvance(m_off)) + 26;
    }
    int pillWidth() const { return segmentWidth() * 2 + 2; }

    static constexpr int kGap = 12;
    QString m_on, m_off;
    bool m_hover = false;
};
