#include "Spinner.h"

#include "theme/Theme.h"

#include <QPainter>

Spinner::Spinner(int size, QWidget* parent)
    : QWidget(parent)
    , m_size(size)
{
    setFixedSize(size, size);
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_angle = (m_angle + 6) % 360;
        update();
    });
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void Spinner::setRunning(bool running)
{
    m_running = running;
    if (running && isVisible())
        m_timer.start();
    else
        m_timer.stop();
    update();
}

void Spinner::showEvent(QShowEvent*)
{
    if (m_running)
        m_timer.start();
}

void Spinner::hideEvent(QHideEvent*)
{
    m_timer.stop();
}

void Spinner::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = m_size / 16.0;
    const QRectF r(2 * s, 2 * s, 12 * s, 12 * s);
    p.setPen(QPen(Theme::color(Theme::Role::Sunken), 2 * s));
    p.drawEllipse(r);
    QPen arc(Theme::color(Theme::Role::Accent), 2 * s);
    arc.setCapStyle(Qt::RoundCap);
    p.setPen(arc);
    p.drawArc(r, (90 - m_angle) * 16, -90 * 16);
}
