#include "InfoNote.h"

#include "Icon.h"
#include "Text.h"

#include <QHBoxLayout>
#include <QPainter>

InfoNote::InfoNote(const QString& text, Theme::Tone tone, const QString& icon, QWidget* parent)
    : QWidget(parent)
    , m_tone(tone)
    , m_icon(icon)
    , m_label(new Label(text, Label::Style::Small, Theme::Role::Text2))
{
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(44, 11, 16, 11);
    h->addWidget(m_label);
    m_label->setWordWrap(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void InfoNote::setText(const QString& text) { m_label->setText(text); }

void InfoNote::setTone(Theme::Tone tone)
{
    m_tone = tone;
    update();
}

void InfoNote::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(m_tone == Theme::Tone::Neutral ? Theme::color(Theme::Role::Sunken) : Theme::toneSoft(m_tone));
    p.drawRoundedRect(QRectF(rect()), Metrics::RadiusLg, Metrics::RadiusLg);
    const QColor ic = m_tone == Theme::Tone::Neutral ? Theme::color(Theme::Role::Text2) : Theme::toneColor(m_tone);
    Icon::paint(p, QRectF(16, 12, 16, 18), m_icon, ic, 15);
}
