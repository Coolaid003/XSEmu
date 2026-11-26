#include "PageScaffold.h"

#include "theme/Theme.h"
#include "widgets/PageHeader.h"
#include "widgets/Text.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QVBoxLayout>

PageScaffold makePageScaffold(QWidget* page, const QString& title)
{
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto* header = new PageHeader(title);
    v->addWidget(header);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* inner = new QWidget;
    inner->setObjectName(QStringLiteral("scrollContents"));
    auto* content = new QVBoxLayout(inner);
    content->setContentsMargins(Metrics::PagePadding, Metrics::S6, Metrics::PagePadding, Metrics::PagePadding);
    content->setSpacing(Metrics::S4);
    scroll->setWidget(inner);
    v->addWidget(scroll, 1);
    return {header, content};
}

QWidget* makeSectionLabel(const QString& text, QWidget* trailing)
{
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 8, 0, 0);
    h->addWidget(new Label(text, Label::Style::CapsMedium, Theme::Role::Text3));
    h->addStretch(1);
    if (trailing)
        h->addWidget(trailing);
    return w;
}
