#include "ProfileCard.h"

#include "widgets/Badge.h"
#include "widgets/Icon.h"
#include "widgets/Text.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QVBoxLayout>

namespace {

class Hairline : public QWidget {
public:
    Hairline()
    {
        setFixedHeight(1);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), Theme::color(Theme::Role::Border));
    }
};

Label* wrapped(const QString& text, Label::Style style, Theme::Role role)
{
    auto* l = new Label(text, style, role);
    l->setWordWrap(true);
    return l;
}

}

ProfileCard::ProfileCard(const QString& title, QWidget* parent)
    : Card(Card::Kind::Surface, Metrics::RadiusXl, parent)
    , m_rows(new QVBoxLayout)
    , m_trailing(new Label({}, Label::Style::Caption, Theme::Role::Text3))
{
    setShadow(false);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(18, 14, 18, 4);
    v->setSpacing(0);
    auto* head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 6);
    head->addWidget(new Label(title, Label::Style::CapsMedium, Theme::Role::Text3));
    head->addStretch(1);
    head->addWidget(m_trailing);
    v->addLayout(head);
    m_rows->setSpacing(0);
    v->addLayout(m_rows);
}

void ProfileCard::setTrailing(const QString& text)
{
    m_trailing->setText(text);
}

void ProfileCard::clear()
{
    while (QLayoutItem* item = m_rows->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    m_count = 0;
}

void ProfileCard::addRow(const QString& label, const QString& value, const QString& from, const QString& notApplied, QWidget* action, const QString& caption)
{
    if (m_count++ > 0)
        m_rows->addWidget(new Hairline);
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 9, 0, 9);
    h->setSpacing(12);
    auto* name = new Label(label, Label::Style::Small, Theme::Role::Text2);
    name->setFixedWidth(140);
    h->addWidget(name, 0, Qt::AlignTop);

    auto* right = new QVBoxLayout;
    right->setSpacing(3);
    auto* line = new QHBoxLayout;
    line->setSpacing(6);
    const bool muted = !notApplied.isEmpty();
    if (!from.isEmpty()) {
        line->addWidget(new Label(from, Label::Style::Small, Theme::Role::Text3));
        line->addWidget(new IconView(QStringLiteral("chevron-right"), Theme::Role::Text3, 12));
    }
    line->addWidget(new Label(value, muted ? Label::Style::Small : Label::Style::SmallMedium, muted ? Theme::Role::Text3 : Theme::Role::Text));
    line->addStretch(1);
    if (muted)
        line->addWidget(new Badge(QStringLiteral("Not applied"), Theme::Tone::Warning));
    right->addLayout(line);
    if (muted)
        right->addWidget(wrapped(notApplied, Label::Style::Caption, Theme::Role::Text3));
    if (action) {
        right->addSpacing(4);
        right->addWidget(action, 0, Qt::AlignLeft);
    }
    if (!caption.isEmpty())
        right->addWidget(wrapped(caption, Label::Style::Caption, Theme::Role::Text3));
    h->addLayout(right, 1);
    m_rows->addWidget(row);
}

void ProfileCard::addMessage(const QString& text)
{
    if (m_count++ > 0)
        m_rows->addWidget(new Hairline);
    auto* l = wrapped(text, Label::Style::Small, Theme::Role::Text3);
    l->setContentsMargins(0, 9, 0, 11);
    m_rows->addWidget(l);
}
