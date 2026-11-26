#include "WindowButtons.h"

#include "theme/Theme.h"
#include "widgets/Icon.h"

#include <QAbstractButton>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>

class WindowButton : public QAbstractButton {
public:
    WindowButton(const QString& icon, bool danger, int height)
        : m_icon(icon)
        , m_danger(danger)
    {
        setFixedSize(46, height);
        setFocusPolicy(Qt::NoFocus);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }
    void setIconName(const QString& icon)
    {
        m_icon = icon;
        update();
    }

protected:
    void enterEvent(QEnterEvent* e) override
    {
        m_hover = true;
        update();
        QAbstractButton::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override
    {
        m_hover = false;
        update();
        QAbstractButton::leaveEvent(e);
    }
    void paintEvent(QPaintEvent*) override
    {
        const Palette& c = Theme::instance().palette();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor fg = c.text2;
        if (m_hover && m_danger) {
            p.fillRect(rect(), QColor(0xC4, 0x2B, 0x1C));
            fg = Qt::white;
        } else if (m_hover) {
            p.fillRect(rect(), c.hover);
            fg = c.text;
        }
        Icon::paint(p, QRectF(rect()), m_icon, fg, 10);
    }

private:
    QString m_icon;
    bool m_danger;
    bool m_hover = false;
};

WindowButtons::WindowButtons(int height, QWidget* parent)
    : QWidget(parent)
    , m_min(new WindowButton(QStringLiteral("win-min"), false, height))
    , m_max(new WindowButton(QStringLiteral("win-max"), false, height))
    , m_close(new WindowButton(QStringLiteral("win-close"), true, height))
{
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(m_min);
    h->addWidget(m_max);
    h->addWidget(m_close);
    m_min->setAccessibleName(QStringLiteral("Minimise"));
    m_max->setAccessibleName(QStringLiteral("Maximise"));
    m_close->setAccessibleName(QStringLiteral("Close"));
    connect(m_min, &QAbstractButton::clicked, this, [this] { window()->showMinimized(); });
    connect(m_max, &QAbstractButton::clicked, this, [this] {
        window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
    });
    connect(m_close, &QAbstractButton::clicked, this, [this] { window()->close(); });
}

void WindowButtons::showEvent(QShowEvent*)
{
    if (m_watched != window()) {
        if (m_watched)
            m_watched->removeEventFilter(this);
        m_watched = window();
        m_watched->installEventFilter(this);
    }
    syncMaximised();
}

bool WindowButtons::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_watched && e->type() == QEvent::WindowStateChange)
        syncMaximised();
    return QWidget::eventFilter(o, e);
}

void WindowButtons::syncMaximised()
{
    const bool max = window()->isMaximized();
    m_max->setIconName(max ? QStringLiteral("win-restore") : QStringLiteral("win-max"));
    m_max->setAccessibleName(max ? QStringLiteral("Restore") : QStringLiteral("Maximise"));
}
