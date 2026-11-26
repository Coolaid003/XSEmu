#include "Dialog.h"

#include "Button.h"
#include "Card.h"
#include "Icon.h"
#include "Text.h"
#include "theme/Theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace {

class IconTile : public QWidget {
public:
    IconTile(const QString& icon, Theme::Tone tone)
        : m_icon(icon)
        , m_tone(tone)
    {
        setFixedSize(36, 36);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::toneSoft(m_tone));
        p.drawRoundedRect(QRectF(rect()), 8, 8);
        Icon::paint(p, QRectF(rect()), m_icon, Theme::toneColor(m_tone), 16);
    }

private:
    QString m_icon;
    Theme::Tone m_tone;
};

}

Dialog::Dialog(QWidget* window, int width)
    : QWidget(window)
    , m_card(new Card(Card::Kind::Surface, 16))
    , m_body(new QVBoxLayout)
    , m_buttons(new QHBoxLayout)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
    m_card->setFixedWidth(width);
    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);
    outer->addWidget(m_card, 0, Qt::AlignHCenter);
    outer->addStretch(1);
    auto* v = new QVBoxLayout(m_card);
    v->setContentsMargins(24, 24, 24, 22);
    v->setSpacing(0);
    v->addLayout(m_body);
    m_body->setSpacing(12);
    v->addSpacing(22);
    m_buttons->setSpacing(8);
    m_buttons->addStretch(1);
    v->addLayout(m_buttons);
    window->installEventFilter(this);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void Dialog::setIcon(const QString& icon, int tone)
{
    delete m_iconTile;
    m_iconTile = new IconTile(icon, Theme::Tone(tone));
    m_body->insertWidget(0, m_iconTile);
    m_body->insertSpacing(1, 6);
}

void Dialog::setTitle(const QString& title)
{
    if (!m_titleLabel) {
        m_titleLabel = new Label(title, Label::Style::SectionTitle);
        m_body->insertWidget(m_iconTile ? 2 : 0, m_titleLabel);
        return;
    }
    static_cast<Label*>(m_titleLabel)->setText(title);
}

void Dialog::addButton(Button* button, bool left)
{
    if (left)
        m_buttons->insertWidget(m_leftButtons++, button);
    else
        m_buttons->addWidget(button);
}

void Dialog::open()
{
    QLayout* card = m_card->layout();
    card->activate();
    m_card->setFixedHeight(card->hasHeightForWidth() ? card->totalHeightForWidth(m_card->width()) : card->totalSizeHint().height());
    setGeometry(parentWidget()->rect());
    raise();
    show();
    setFocus();
}

void Dialog::dismiss()
{
    emit closed();
    close();
}

bool Dialog::eventFilter(QObject* o, QEvent* e)
{
    if (o == parentWidget() && e->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(o, e);
}

void Dialog::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    QWidget::keyPressEvent(e);
}

void Dialog::mousePressEvent(QMouseEvent*)
{
}

void Dialog::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::color(Theme::Role::Scrim));
}
