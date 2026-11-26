#include "Card.h"

#include <QPainter>
#include <QPainterPath>

Card::Card(Kind kind, int radius, QWidget* parent)
    : QFrame(parent)
    , m_kind(kind)
    , m_radius(radius)
{
    m_shadow = kind == Kind::Surface;
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void Card::setSelected(bool selected)
{
    m_selected = selected;
    update();
}

void Card::paintEvent(QPaintEvent*)
{
    const Palette& c = Theme::instance().palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -2);
    if (m_shadow) {
        QPainterPath s;
        s.addRoundedRect(r.translated(0, 1), m_radius, m_radius);
        p.fillPath(s, QColor(0, 0, 0, 0x0A));
    }
    QPainterPath path;
    path.addRoundedRect(r, m_radius, m_radius);
    const QColor fill = m_kind == Kind::Surface ? c.surface : m_kind == Kind::Panel ? c.panel : c.sunken;
    p.fillPath(path, fill);
    if (m_selected) {
        p.setPen(QPen(c.accentSoft, 3));
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), m_radius, m_radius);
        p.setPen(QPen(c.accent, 1.5));
    } else {
        p.setPen(QPen(m_kind == Kind::Sunken ? Qt::transparent : c.border, 1));
    }
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), m_radius - 0.5, m_radius - 0.5);
}
