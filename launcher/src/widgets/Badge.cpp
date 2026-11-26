#include "Badge.h"

#include "Icon.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QtMath>
#include <QPainterPath>

Badge::Badge(const QString& text, Theme::Tone tone, Kind kind, QWidget* parent)
    : QWidget(parent)
    , m_text(text)
    , m_tone(tone)
    , m_kind(kind)
{
    setSizePolicy(m_kind == Kind::Plain ? QSizePolicy::Preferred : QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

// unchanged values return early: a geometry update relays out the whole page around the badge
void Badge::setText(const QString& text)
{
    if (text == m_text)
        return;
    m_text = text;
    updateGeometry();
    update();
}
void Badge::setTone(Theme::Tone tone)
{
    if (tone == m_tone)
        return;
    m_tone = tone;
    update();
}
void Badge::setDotVisible(bool on)
{
    if (on == m_dot)
        return;
    m_dot = on;
    updateGeometry();
    update();
}
void Badge::setIcon(const QString& name)
{
    if (name == m_icon)
        return;
    m_icon = name;
    updateGeometry();
    update();
}

QFont Badge::textFont() const
{
    switch (m_kind) {
    case Kind::Pill: return Theme::font(12, 600);
    case Kind::Plain: return Theme::font(12, 400);
    case Kind::Chip: return Theme::font(12, 500);
    case Kind::Outline: return Theme::font(11, 600);
    }
    return Theme::font(12);
}

QSize Badge::sizeHint() const
{
    const int text = qCeil(QFontMetricsF(textFont()).horizontalAdvance(m_text)) + 1;
    const int dot = !m_icon.isEmpty() ? 12 + 5 : m_dot ? 6 + 6 : 0;
    switch (m_kind) {
    case Kind::Pill: return {(m_dot ? 7 : 9) + dot + text + 9, Metrics::BadgeHeight};
    case Kind::Plain: return {dot + text, 16};
    case Kind::Chip: return {9 + dot + text + 9, 24};
    case Kind::Outline: {
        const int head = m_text.indexOf(QStringLiteral(" \u00B7 "));
        const int width = head < 0 ? text
                                   : qCeil(QFontMetricsF(textFont()).horizontalAdvance(m_text.left(head))
                                           + QFontMetricsF(Theme::font(11, 400)).horizontalAdvance(m_text.mid(head))) + 1;
        return {8 + dot + width + 8, 22};
    }
    }
    return {};
}

void Badge::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor strong = Theme::toneColor(m_tone);
    int x = 0;
    if (m_kind == Kind::Pill) {
        QPainterPath bg;
        bg.addRoundedRect(QRectF(rect()), height() / 2.0, height() / 2.0);
        p.fillPath(bg, Theme::toneSoft(m_tone));
        x = m_dot ? 7 : 9;
    } else if (m_kind == Kind::Chip) {
        QPainterPath bg;
        bg.addRoundedRect(QRectF(rect()), 6, 6);
        p.fillPath(bg, Theme::color(Theme::Role::Sunken));
        x = 9;
    } else if (m_kind == Kind::Outline) {
        p.setPen(QPen(Theme::color(Theme::Role::Border), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        x = 8;
    }
    if (!m_icon.isEmpty()) {
        Icon::paint(p, QRectF(x, height() / 2.0 - 6, 12, 12), m_icon, m_kind == Kind::Pill ? strong : Theme::color(Theme::Role::Text2), 12);
        x += 17;
    } else if (m_dot) {
        p.setPen(Qt::NoPen);
        p.setBrush(strong);
        p.drawEllipse(QRectF(x, height() / 2.0 - 3, 6, 6));
        x += 12;
    }
    p.setFont(textFont());
    if (m_kind == Kind::Outline) {
        const int head = m_text.indexOf(QStringLiteral(" \u00B7 "));
        const QString first = head < 0 ? m_text : m_text.left(head);
        p.setPen(Theme::color(Theme::Role::Text));
        p.drawText(QRect(x, 0, width() - x, height()), Qt::AlignVCenter | Qt::AlignLeft, first);
        if (head >= 0) {
            x += qCeil(QFontMetricsF(p.font()).horizontalAdvance(first));
            p.setFont(Theme::font(11, 400));
            p.setPen(Theme::color(Theme::Role::Text2));
            p.drawText(QRect(x, 0, width() - x, height()), Qt::AlignVCenter | Qt::AlignLeft, m_text.mid(head));
        }
        return;
    }
    p.setPen(m_kind == Kind::Pill ? strong : Theme::color(Theme::Role::Text2));
    const QFontMetrics fm(p.font());
    p.drawText(QRect(x, 0, width() - x, height()), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(m_text, Qt::ElideRight, width() - x));
}
