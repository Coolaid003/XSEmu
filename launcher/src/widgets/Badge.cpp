#include "Badge.h"

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

void Badge::setText(const QString& text) { m_text = text; updateGeometry(); update(); }
void Badge::setTone(Theme::Tone tone) { m_tone = tone; update(); }
void Badge::setDotVisible(bool on) { m_dot = on; updateGeometry(); update(); }

QFont Badge::textFont() const
{
    switch (m_kind) {
    case Kind::Pill: return Theme::font(12, 600);
    case Kind::Plain: return Theme::font(12, 400);
    case Kind::Chip: return Theme::font(12, 500);
    }
    return Theme::font(12);
}

QSize Badge::sizeHint() const
{
    const int text = qCeil(QFontMetricsF(textFont()).horizontalAdvance(m_text)) + 1;
    const int dot = m_dot ? 6 + 6 : 0;
    switch (m_kind) {
    case Kind::Pill: return {(m_dot ? 7 : 9) + dot + text + 9, Metrics::BadgeHeight};
    case Kind::Plain: return {dot + text, 16};
    case Kind::Chip: return {9 + dot + text + 9, 24};
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
    }
    if (m_dot) {
        p.setPen(Qt::NoPen);
        p.setBrush(strong);
        p.drawEllipse(QRectF(x, height() / 2.0 - 3, 6, 6));
        x += 12;
    }
    p.setFont(textFont());
    p.setPen(m_kind == Kind::Pill ? strong : Theme::color(Theme::Role::Text2));
    const QFontMetrics fm(p.font());
    p.drawText(QRect(x, 0, width() - x, height()), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(m_text, Qt::ElideRight, width() - x));
}
