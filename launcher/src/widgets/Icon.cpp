#include "Icon.h"

#include <QFile>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

namespace {

QByteArray svgSource(const QString& name)
{
    static QHash<QString, QByteArray> cache;
    auto it = cache.find(name);
    if (it == cache.end()) {
        QFile f(QStringLiteral(":/icons/%1.svg").arg(name));
        QByteArray data;
        if (f.open(QIODevice::ReadOnly))
            data = f.readAll();
        else
            qWarning("Icon: no icon named %s", qPrintable(name));
        it = cache.insert(name, data);
    }
    return it.value();
}

QPixmap render(const QByteArray& svg, int size, qreal dpr)
{
    QPixmap pm(QSize(size, size) * dpr);
    pm.fill(Qt::transparent);
    QSvgRenderer r(svg);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    r.render(&p, QRectF(0, 0, size * dpr, size * dpr));
    p.end();
    pm.setDevicePixelRatio(dpr);
    return pm;
}

}

namespace Icon {

QPixmap pixmap(const QString& name, const QColor& color, int size, qreal dpr)
{
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(name, color.name(QColor::HexArgb)).arg(size).arg(dpr);
    auto it = cache.find(key);
    if (it != cache.end())
        return it.value();
    QByteArray svg = svgSource(name);
    // currentColor carries the colour; opacity goes through stroke/fill-opacity on the root
    const QByteArray hex = color.name(QColor::HexRgb).toLatin1();
    svg.replace("currentColor", hex);
    if (color.alpha() < 255)
        svg.replace("<svg ", "<svg opacity=\"" + QByteArray::number(color.alphaF(), 'f', 3) + "\" ");
    const QPixmap pm = render(svg, size, dpr);
    cache.insert(key, pm);
    return pm;
}

QIcon icon(const QString& name, const QColor& color, int size)
{
    QIcon ic;
    ic.addPixmap(pixmap(name, color, size, 1.0));
    ic.addPixmap(pixmap(name, color, size, 2.0));
    return ic;
}

void paint(QPainter& p, const QRectF& rect, const QString& name, const QColor& color, int size)
{
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const QPixmap pm = pixmap(name, color, size, dpr);
    const QPointF tl(rect.center().x() - size / 2.0, rect.center().y() - size / 2.0);
    p.drawPixmap(QPointF(qRound(tl.x()), qRound(tl.y())), pm);
}

QPixmap appMark(int size, qreal dpr)
{
    QFile f(Theme::instance().isDark() ? QStringLiteral(":/branding/mark-dark.svg") : QStringLiteral(":/branding/mark.svg"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return render(f.readAll(), size, dpr);
}

QIcon appIcon()
{
    QIcon ic;
    QFile f(QStringLiteral(":/branding/mark.svg"));
    if (!f.open(QIODevice::ReadOnly))
        return ic;
    const QByteArray svg = f.readAll();
    for (int s : {16, 20, 24, 32, 48, 64, 128, 256})
        ic.addPixmap(render(svg, s, 1.0));
    return ic;
}

}

IconView::IconView(const QString& name, Theme::Role role, int size, QWidget* parent)
    : QWidget(parent)
    , m_name(name)
    , m_role(role)
    , m_size(size)
{
    setFixedSize(size, size);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void IconView::setIcon(const QString& name)
{
    m_name = name;
    update();
}

void IconView::setRole(Theme::Role role)
{
    m_role = role;
    update();
}

void IconView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    Icon::paint(p, rect(), m_name, Theme::color(m_role), m_size);
}

BrandMark::BrandMark(int size, QWidget* parent)
    : QWidget(parent)
    , m_size(size)
{
    setFixedSize(size, size);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void BrandMark::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.drawPixmap(0, 0, Icon::appMark(m_size, devicePixelRatioF()));
}
