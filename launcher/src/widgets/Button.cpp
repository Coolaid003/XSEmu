#include "Button.h"

#include "Icon.h"

#include <QEnterEvent>
#include <QFocusEvent>
#include <QPainter>
#include <QPainterPath>

Button::Button(const QString& text, Variant variant, Size size, const QString& icon, QWidget* parent)
    : QAbstractButton(parent)
    , m_variant(variant)
    , m_size(size)
    , m_icon(icon)
{
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_Hover);
    connect(&Theme::instance(), &Theme::changed, this, [this] { updateGeometry(); update(); });
}

void Button::setVariant(Variant v) { m_variant = v; updateGeometry(); update(); }
void Button::setButtonSize(Size s) { m_size = s; updateGeometry(); update(); }
void Button::setIconName(const QString& icon) { m_icon = icon; updateGeometry(); update(); }

QFont Button::textFont() const
{
    const int px = m_size == Size::Large ? 14 : 13;
    const bool strong = m_size == Size::Large && (m_variant == Variant::Primary || m_variant == Variant::Danger);
    return Theme::font(px, strong ? 600 : 500);
}

int Button::padLeft() const
{
    if (m_size == Size::Large)
        return m_icon.isEmpty() ? 16 : 13;
    return m_icon.isEmpty() ? 12 : 10;
}

int Button::padRight() const
{
    return m_size == Size::Large ? 16 : 12;
}

QSize Button::sizeHint() const
{
    const QFontMetrics fm(textFont());
    int w = padLeft() + padRight() + fm.horizontalAdvance(text());
    if (!m_icon.isEmpty())
        w += iconSize() + (text().isEmpty() ? 0 : 7);
    if (text().isEmpty() && !m_icon.isEmpty())
        w = int(m_size);
    return {w, int(m_size)};
}

void Button::enterEvent(QEnterEvent* e)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(e);
}

void Button::leaveEvent(QEvent* e)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(e);
}

void Button::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::EnabledChange)
        setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QAbstractButton::changeEvent(e);
}

void Button::paintEvent(QPaintEvent*)
{
    const Palette& c = Theme::instance().palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled())
        p.setOpacity(Metrics::DisabledOpacity);

    const qreal radius = m_size == Size::Large ? 8 : 7;
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const bool down = isDown() && isEnabled();
    const bool hover = m_hover && isEnabled();

    QColor fill = Qt::transparent;
    QColor fg = c.text;
    QColor ring = Qt::transparent;
    QColor shadow = Qt::transparent;
    switch (m_variant) {
    case Variant::Primary:
        fill = c.accent;
        fg = c.onAccent;
        shadow = QColor(0x14, 0x1E, 0x5A, 0x40);
        if (hover) fill = fill.lighter(108);
        if (down) fill = fill.darker(112);
        break;
    case Variant::Danger:
        fill = c.danger;
        fg = QColor(Qt::white);
        shadow = QColor(0x5A, 0x14, 0x14, 0x40);
        if (hover) fill = fill.lighter(108);
        if (down) fill = fill.darker(112);
        break;
    case Variant::Secondary:
    case Variant::DangerSecondary:
        fill = hover ? c.hover : c.surface;
        if (down) fill = c.sunken;
        fg = m_variant == Variant::DangerSecondary ? c.danger : c.text;
        ring = c.borderStrong;
        shadow = QColor(0, 0, 0, 0x0D);
        break;
    case Variant::Ghost:
        fg = c.text2;
        if (hover) fill = c.hover;
        if (down) fill = c.sunken;
        break;
    case Variant::Link:
        fg = c.accentText;
        if (hover) fill = c.accentSoft;
        break;
    case Variant::OnArt:
        fill = QColor(255, 255, 255, hover ? 46 : 31);
        ring = QColor(255, 255, 255, 46);
        fg = QColor(Qt::white);
        break;
    }

    if (shadow.alpha() > 0) {
        QPainterPath sp;
        sp.addRoundedRect(r.translated(0, 1), radius, radius);
        p.fillPath(sp, shadow);
    }
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    if (fill.alpha() > 0)
        p.fillPath(path, fill);
    if (m_variant == Variant::Primary) {
        p.setPen(QPen(QColor(0, 0, 0, 0x14), 1));
        p.drawPath(path);
    } else if (ring.alpha() > 0) {
        p.setPen(QPen(ring, 1));
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);
    }
    if (m_focusRing && hasFocus() && isEnabled()) {
        p.setPen(QPen(c.accent, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(-0.25, -0.25, 0.25, 0.25), radius, radius);
    }

    const QFont f = textFont();
    const QFontMetrics fm(f);
    const int textW = fm.horizontalAdvance(text());
    const int contentW = (m_icon.isEmpty() ? 0 : iconSize() + (text().isEmpty() ? 0 : 7)) + textW;
    int x = text().isEmpty() ? (width() - contentW) / 2 : padLeft();
    if (sizePolicy().horizontalPolicy() != QSizePolicy::Fixed)
        x = (width() - contentW) / 2;
    if (!m_icon.isEmpty()) {
        Icon::paint(p, QRectF(x, 0, iconSize(), height()), m_icon, fg, iconSize());
        x += iconSize() + 7;
    }
    if (!text().isEmpty()) {
        p.setFont(f);
        p.setPen(fg);
        p.drawText(QRect(x, 0, textW + 2, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
}

void Button::focusInEvent(QFocusEvent* e)
{
    m_focusRing = e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason || e->reason() == Qt::ShortcutFocusReason;
    QAbstractButton::focusInEvent(e);
}

void Button::focusOutEvent(QFocusEvent* e)
{
    m_focusRing = false;
    QAbstractButton::focusOutEvent(e);
}
