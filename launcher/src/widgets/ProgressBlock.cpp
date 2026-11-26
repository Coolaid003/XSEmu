#include "ProgressBlock.h"

#include "Button.h"
#include "Icon.h"
#include "Spinner.h"
#include "Text.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace {

class Bar : public QWidget {
public:
    explicit Bar(ProgressBlock* block)
        : QWidget(block)
        , m_block(block)
    {
        setFixedHeight(6);
    }
    double fraction = 0;
    QColor color;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath track;
        track.addRoundedRect(QRectF(rect()), 3, 3);
        p.fillPath(track, Theme::color(Theme::Role::Sunken));
        const qreal w = qRound(width() * qBound(0.0, fraction, 1.0));
        if (w > 0) {
            QPainterPath fill;
            fill.addRoundedRect(QRectF(0, 0, w, height()), 3, 3);
            p.fillPath(fill, color);
        }
    }

private:
    ProgressBlock* m_block;
};

}

ProgressBlock::ProgressBlock(QWidget* parent)
    : QWidget(parent)
    , m_spinner(new Spinner(18))
    , m_glyph(new IconView(QStringLiteral("check"), Theme::Role::Success, 18))
    , m_title(new Label({}, Label::Style::Title))
    , m_subtitle(new Label({}, Label::Style::Small, Theme::Role::Text3))
    , m_percent(new Label({}, Label::Style::Number, Theme::Role::Text2))
    , m_action(new Button(QStringLiteral("Cancel"), Button::Variant::Ghost, Button::Size::Small))
    , m_header(new QHBoxLayout)
    , m_bar(new Bar(this))
    , m_columns(new QHBoxLayout)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(20, 18, 20, 20);
    v->setSpacing(14);

    m_header->setSpacing(10);
    m_header->addWidget(m_spinner);
    m_header->addWidget(m_glyph);
    auto* titles = new QHBoxLayout;
    titles->setSpacing(8);
    titles->addWidget(m_title, 0, Qt::AlignBaseline);
    titles->addWidget(m_subtitle, 1, Qt::AlignBaseline);
    m_header->addLayout(titles, 1);
    m_header->addWidget(m_percent);
    m_header->addWidget(m_action);
    v->addLayout(m_header);
    v->addWidget(m_bar);
    m_columns->setSpacing(16);
    v->addLayout(m_columns);

    m_subtitle->setTextInteractionFlags(Qt::NoTextInteraction);
    connect(m_action, &Button::clicked, this, &ProgressBlock::actionClicked);
    connect(&Theme::instance(), &Theme::changed, this, [this] { setState(m_state); });
    setState(State::Running);
}

void ProgressBlock::setState(State state)
{
    m_state = state;
    auto* bar = static_cast<Bar*>(m_bar);
    m_spinner->setVisible(state == State::Running);
    m_glyph->setVisible(state != State::Running);
    switch (state) {
    case State::Idle:
        m_glyph->setIcon(QStringLiteral("bolt"));
        m_glyph->setRole(Theme::Role::Warning);
        bar->color = Theme::color(Theme::Role::Accent);
        break;
    case State::Running:
        bar->color = Theme::color(Theme::Role::Accent);
        break;
    case State::Paused:
        m_glyph->setIcon(QStringLiteral("pause"));
        m_glyph->setRole(Theme::Role::Warning);
        bar->color = Theme::color(Theme::Role::Warning);
        break;
    case State::Failed:
        m_glyph->setIcon(QStringLiteral("warning"));
        m_glyph->setRole(Theme::Role::Danger);
        bar->color = Theme::color(Theme::Role::Danger);
        break;
    case State::Finished:
        m_glyph->setIcon(QStringLiteral("check"));
        m_glyph->setRole(Theme::Role::Success);
        bar->color = Theme::color(Theme::Role::Success);
        break;
    case State::Cancelled:
        m_glyph->setIcon(QStringLiteral("stop"));
        m_glyph->setRole(Theme::Role::Text3);
        bar->color = Theme::color(Theme::Role::Text3);
        break;
    }
    m_bar->update();
}

void ProgressBlock::setTitle(const QString& title) { m_title->setText(title); }
void ProgressBlock::setSubtitle(const QString& subtitle) { m_subtitle->setText(subtitle); }

void ProgressBlock::setFraction(double fraction)
{
    m_fraction = fraction;
    static_cast<Bar*>(m_bar)->fraction = qMax(0.0, fraction);
    m_percent->setVisible(fraction >= 0);
    m_percent->setText(QStringLiteral("%1%").arg(qRound(qBound(0.0, fraction, 1.0) * 100)));
    m_bar->update();
}

void ProgressBlock::setPercentText(const QString& text)
{
    m_percent->setVisible(!text.isEmpty());
    m_percent->setText(text);
}

void ProgressBlock::setColumns(const QList<Column>& columns, const QList<double>& stretch)
{
    while (QLayoutItem* item = m_columns->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (int i = 0; i < columns.size(); ++i) {
        auto* col = new QWidget;
        auto* v = new QVBoxLayout(col);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(3);
        v->addWidget(new Label(columns[i].first, Label::Style::Caption, Theme::Role::Text3));
        v->addWidget(new Label(columns[i].second, Label::Style::Number, Theme::Role::Text));
        m_columns->addWidget(col, qRound(stretch.value(i, 1.0) * 10));
    }
}

void ProgressBlock::setActionText(const QString& text)
{
    m_action->setVisible(!text.isEmpty());
    m_action->setText(text);
    m_action->updateGeometry();
}

void ProgressBlock::addHeaderWidget(QWidget* w)
{
    m_header->addWidget(w);
}

void ProgressBlock::paintEvent(QPaintEvent*)
{
    const Palette& c = Theme::instance().palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -2);
    QPainterPath s;
    s.addRoundedRect(r.translated(0, 1), 12, 12);
    p.fillPath(s, QColor(0, 0, 0, 0x0A));
    QPainterPath path;
    path.addRoundedRect(r, 12, 12);
    p.fillPath(path, c.surface);
    p.setPen(QPen(c.border, 1));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 11.5, 11.5);
}
