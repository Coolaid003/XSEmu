#include "Toggle.h"

#include "theme/Theme.h"

#include <QPainter>

Toggle::Toggle(QWidget* parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(36, 20);
    setFocusPolicy(Qt::StrongFocus);
    m_anim.setDuration(120);
    m_anim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_pos = v.toReal();
        update();
    });
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void Toggle::checkStateSet()
{
    m_anim.stop();
    m_pos = isChecked() ? 1 : 0;
    update();
}

void Toggle::nextCheckState()
{
    QAbstractButton::nextCheckState();
    m_anim.stop();
    m_anim.setStartValue(m_pos);
    m_anim.setEndValue(isChecked() ? 1.0 : 0.0);
    m_anim.start();
}

void Toggle::paintEvent(QPaintEvent*)
{
    const Palette& c = Theme::instance().palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled())
        p.setOpacity(Metrics::DisabledOpacity);
    const QColor off = c.borderStrong;
    const QColor on = c.accent;
    QColor track(qRound(off.red() + (on.red() - off.red()) * m_pos), qRound(off.green() + (on.green() - off.green()) * m_pos),
                 qRound(off.blue() + (on.blue() - off.blue()) * m_pos));
    track.setAlphaF(static_cast<float>(off.alphaF() + (on.alphaF() - off.alphaF()) * m_pos));
    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(QRectF(0, 0, 36, 20), 10, 10);
    const qreal x = 2 + m_pos * 16;
    p.setBrush(QColor(0, 0, 0, 0x33));
    p.drawEllipse(QRectF(x, 3, 16, 16));
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(x, 2, 16, 16));
    if (hasFocus()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c.accentSoft, 2));
        p.drawRoundedRect(QRectF(1, 1, 34, 18), 9, 9);
    }
}
