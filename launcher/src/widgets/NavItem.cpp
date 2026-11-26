#include "NavItem.h"

#include "Icon.h"

#include <QPainter>

NavItem::NavItem(const QString& text, const QString& icon, QWidget* parent)
    : QAbstractButton(parent)
    , m_icon(icon)
{
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

QSize NavItem::sizeHint() const
{
    return {160, Metrics::NavItem};
}

void NavItem::setTrailingText(const QString& text, Theme::Role role)
{
    m_trailing = text;
    m_trailingRole = role;
    m_trailingIcon.clear();
    update();
}

void NavItem::setTrailingIcon(const QString& icon)
{
    m_trailingIcon = icon;
    if (!icon.isEmpty())
        m_trailing.clear();
    update();
}

void NavItem::enterEvent(QEnterEvent* e)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(e);
}

void NavItem::leaveEvent(QEvent* e)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(e);
}

void NavItem::paintEvent(QPaintEvent*)
{
    const Palette& c = Theme::instance().palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect());
    if (isChecked() || m_hover || hasFocus()) {
        p.setPen(Qt::NoPen);
        p.setBrush(isChecked() ? c.sunken : c.hover);
        p.drawRoundedRect(r, Metrics::RadiusLg, Metrics::RadiusLg);
    }
    qreal x = 10;
    if (!m_icon.isEmpty()) {
        Icon::paint(p, QRectF(x, 0, 16, r.height()), m_icon, isChecked() ? c.text : c.text2, 16);
        x += 16 + 10;
    }
    p.setFont(Theme::font(Metrics::TextBase, isChecked() ? 500 : 400));
    p.setPen(isChecked() ? c.text : c.text2);
    qreal right = r.width() - 12;
    if (!m_trailingIcon.isEmpty()) {
        Icon::paint(p, QRectF(right - 12, 0, 12, r.height()), m_trailingIcon, c.text3, 12);
        right -= 20;
    } else if (!m_trailing.isEmpty()) {
        const QFont f = Theme::font(Metrics::TextXs, 500);
        const int w = QFontMetrics(f).horizontalAdvance(m_trailing);
        p.save();
        p.setFont(f);
        p.setPen(Theme::color(m_trailingRole));
        p.drawText(QRectF(right - w, 0, w, r.height()), Qt::AlignRight | Qt::AlignVCenter, m_trailing);
        p.restore();
        right -= w + 8;
    }
    p.drawText(QRectF(x, 0, right - x, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(text(), Qt::ElideRight, int(right - x)));
}
