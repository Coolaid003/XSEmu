#include "TitleBar.h"

#include "WindowButtons.h"
#include "theme/Theme.h"
#include "widgets/Icon.h"
#include "widgets/Text.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

void WindowDrag::press(QWidget* w, QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && w->window()->windowHandle())
        w->window()->windowHandle()->startSystemMove();
}

void WindowDrag::doubleClick(QWidget* w, QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    QWidget* win = w->window();
    win->isMaximized() ? win->showNormal() : win->showMaximized();
}

TitleBar::TitleBar(QWidget* parent)
    : QWidget(parent)
    , m_mark(new BrandMark(16))
    , m_caption(new Label({}, Label::Style::SmallMedium))
{
    setFixedHeight(Metrics::TitleBar);
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(12, 0, 0, 0);
    h->setSpacing(8);
    h->addWidget(m_mark);
    h->addWidget(m_caption);
    h->addStretch(1);
    h->addWidget(new WindowButtons(Metrics::TitleBar));
    setCaption({});
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void TitleBar::setCaption(const QString& caption)
{
    m_caption->setText(caption);
    m_caption->setVisible(!caption.isEmpty());
    m_mark->setVisible(!caption.isEmpty());
}

void TitleBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::color(Theme::Role::Surface));
    p.fillRect(QRect(0, height() - 1, width(), 1), Theme::color(Theme::Role::Border));
}

void TitleBar::mousePressEvent(QMouseEvent* e) { WindowDrag::press(this, e); }
void TitleBar::mouseDoubleClickEvent(QMouseEvent* e) { WindowDrag::doubleClick(this, e); }
