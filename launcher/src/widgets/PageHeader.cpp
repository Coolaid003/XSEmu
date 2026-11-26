#include "PageHeader.h"

#include "Text.h"
#include "theme/Theme.h"

#include <QHBoxLayout>
#include <QPainter>

PageHeader::PageHeader(const QString& title, QWidget* parent)
    : QWidget(parent)
    , m_row(new QHBoxLayout(this))
    , m_title(new Label(title, Label::Style::PageTitle))
    , m_subtitle(new Label({}, Label::Style::Small, Theme::Role::Text3))
{
    setFixedHeight(Metrics::PageHeader);
    m_row->setContentsMargins(Metrics::PagePadding, 0, Metrics::PagePadding - 1, 1);
    m_row->setSpacing(10);
    m_row->addWidget(m_title, 0, Qt::AlignVCenter);
    m_row->addWidget(m_subtitle, 0, Qt::AlignVCenter);
    m_row->addStretch(1);
    m_subtitle->hide();
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void PageHeader::setTitle(const QString& title) { m_title->setText(title); }

void PageHeader::setSubtitle(const QString& subtitle)
{
    m_subtitle->setText(subtitle);
    m_subtitle->setVisible(!subtitle.isEmpty());
}

void PageHeader::addAction(QWidget* w)
{
    m_row->addWidget(w, 0, Qt::AlignVCenter);
}

void PageHeader::addLeading(QWidget* w)
{
    m_row->insertWidget(m_leading++, w, 0, Qt::AlignVCenter);
}

void PageHeader::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(QRect(0, height() - 1, width(), 1), Theme::color(Theme::Role::Border));
}
