#include "ChoiceCard.h"

#include "theme/Theme.h"

#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kPad = 18;
constexpr int kTextX = 44;
}

ChoiceCard::ChoiceCard(const QString& title, const QString& facts, const QString& advice, const QString& tag, QWidget* parent)
    : QAbstractButton(parent)
    , m_facts(facts)
    , m_advice(advice)
    , m_tag(tag)
{
    setText(title);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

QSize ChoiceCard::sizeHint() const
{
    return {300, heightForWidth(300)};
}

int ChoiceCard::heightForWidth(int w) const
{
    const int tw = w - kTextX - kPad;
    const QFontMetrics fm(Theme::font(Metrics::TextSm));
    const QFontMetrics sm(Theme::font(Metrics::TextXs));
    int h = kPad + 20 + 4 + fm.boundingRect(QRect(0, 0, tw, 1000), Qt::TextWordWrap, m_facts).height();
    if (!m_advice.isEmpty())
        h += 8 + sm.boundingRect(QRect(0, 0, tw, 1000), Qt::TextWordWrap, m_advice).height();
    return h + kPad;
}

void ChoiceCard::paintEvent(QPaintEvent*)
{
    const Palette& c = Theme::instance().palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    p.setPen(QPen(isChecked() ? c.accent : c.borderStrong, isChecked() ? 2 : 1));
    p.setBrush(c.surface);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), Metrics::RadiusXl, Metrics::RadiusXl);

    const QRectF radio(kPad, kPad + 2, 16, 16);
    p.setPen(QPen(isChecked() ? c.accent : c.borderStrong, 1.5));
    p.setBrush(c.surface);
    p.drawEllipse(radio.adjusted(0.75, 0.75, -0.75, -0.75));
    if (isChecked()) {
        p.setPen(Qt::NoPen);
        p.setBrush(c.accent);
        p.drawEllipse(radio.adjusted(4, 4, -4, -4));
    }

    const qreal tw = r.width() - kTextX - kPad;
    p.setFont(Theme::font(Metrics::TextLg, 600));
    p.setPen(c.text);
    p.drawText(QRectF(kTextX, kPad, tw, 20), Qt::AlignLeft | Qt::AlignVCenter, text());
    if (!m_tag.isEmpty()) {
        const int x = kTextX + p.fontMetrics().horizontalAdvance(text()) + 10;
        p.setFont(Theme::font(11, 600, 0.06));
        p.setPen(c.accentText);
        p.drawText(QRectF(x, kPad, tw, 20), Qt::AlignLeft | Qt::AlignVCenter, m_tag.toUpper());
    }
    qreal y = kPad + 24;
    p.setFont(Theme::font(Metrics::TextSm));
    p.setPen(c.text2);
    const QRect facts = p.fontMetrics().boundingRect(QRect(kTextX, int(y), int(tw), 1000), Qt::TextWordWrap, m_facts);
    p.drawText(QRectF(kTextX, y, tw, facts.height()), Qt::TextWordWrap, m_facts);
    y += facts.height() + 8;
    if (!m_advice.isEmpty()) {
        p.setFont(Theme::font(Metrics::TextXs));
        p.setPen(c.text3);
        p.drawText(QRectF(kTextX, y, tw, r.bottom() - y), Qt::TextWordWrap, m_advice);
    }
}
