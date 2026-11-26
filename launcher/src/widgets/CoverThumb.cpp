#include "CoverThumb.h"

#include "theme/Theme.h"

#include <QPainter>
#include <QPainterPath>

CoverThumb::CoverThumb(QSize size, QWidget* parent)
    : QWidget(parent)
    , m_size(size)
{
    setFixedSize(size);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void CoverThumb::setCover(const QPixmap& art, const QString& title)
{
    m_art = art;
    m_title = title;
    setAccessibleName(title);
    update();
}

QString CoverThumb::initials(const QString& title)
{
    QString out;
    for (const QString& word : title.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QChar c = word.at(0);
        if (!c.isLetterOrNumber())
            continue;
        out += c.toUpper();
        if (out.size() == 2)
            break;
    }
    return out;
}

void CoverThumb::paint(QPainter& p, const QRectF& rect, const QPixmap& art, const QString& title, qreal radius)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(rect, radius, radius);
    if (!art.isNull()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x22, 0x22, 0x22));
        p.drawPath(clip);
        p.setClipPath(clip);
        const QSizeF scaled = QSizeF(art.size()).scaled(rect.size(), Qt::KeepAspectRatioByExpanding);
        const QRectF target(rect.center().x() - scaled.width() / 2, rect.center().y() - scaled.height() / 2, scaled.width(), scaled.height());
        p.drawPixmap(target, art, QRectF(art.rect()));
        p.setClipping(false);
        p.setPen(QPen(QColor(0, 0, 0, 0x0F), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect.adjusted(-0.5, -0.5, 0.5, 0.5), radius + 0.5, radius + 0.5);
        p.restore();
        return;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::color(Theme::Role::Sunken));
    p.drawPath(clip);
    p.setPen(QPen(Theme::color(Theme::Role::Border), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);
    // the design's initials: 30 px bold on a 96 px cover, 14 px on 44 px, scaled between
    const int px = qMax(9, int(rect.width() * 0.31 + 0.5));
    const QFont f = Theme::font(px, 700);
    p.setFont(f);
    p.setPen(Theme::color(Theme::Role::Text3));
    const qreal pad = rect.width() >= 40 ? 4 : 2;
    p.drawText(rect.adjusted(pad, pad - 1, -pad, -pad), Qt::AlignLeft | Qt::AlignTop, initials(title));
    p.restore();
}

void CoverThumb::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const qreal radius = m_size.width() >= 90 ? 8 : m_size.width() >= 40 ? 6 : 3;
    paint(p, QRectF(rect()), m_art, m_title, radius);
}
