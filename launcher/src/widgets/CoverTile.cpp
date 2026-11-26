#include "CoverTile.h"

#include "Icon.h"

#include <QEnterEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>

CoverTile::CoverTile(Kind kind, QWidget* parent)
    : QWidget(parent)
    , m_kind(kind)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(sizeHint());
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

QSize CoverTile::sizeHint() const
{
    const int caption = m_kind == Kind::AddGames ? 0 : 10 + 16 + 4 + 16;
    return {ArtWidth + 2 * Ring, ArtHeight + 2 * Ring + caption};
}

void CoverTile::setKind(Kind kind)
{
    m_kind = kind;
    setFixedSize(sizeHint());
    update();
}

void CoverTile::setTitle(const QString& title)
{
    m_title = title;
    setAccessibleName(title);
    update();
}

void CoverTile::setArt(const QPixmap& art)
{
    m_art = art;
    if (m_kind != Kind::AddGames)
        m_kind = art.isNull() ? Kind::NoArt : Kind::Art;
    update();
}

void CoverTile::setStatus(const QString& text, Theme::Tone tone)
{
    m_status = text;
    m_tone = tone;
    update();
}

void CoverTile::setLocked(bool locked, const QString& reason)
{
    m_locked = locked;
    m_reason = reason;
    setToolTip(locked ? reason : QString());
    update();
}

void CoverTile::setSelected(bool selected)
{
    m_selected = selected;
    update();
}

QRectF CoverTile::artRect() const
{
    return QRectF(Ring, Ring, ArtWidth, ArtHeight);
}

QRectF CoverTile::playRect() const
{
    const QRectF a = artRect();
    return QRectF(a.center().x() - 24, a.center().y() - 24, 48, 48);
}

void CoverTile::enterEvent(QEnterEvent* e)
{
    m_hover = true;
    update();
    QWidget::enterEvent(e);
}

void CoverTile::leaveEvent(QEvent* e)
{
    m_hover = false;
    m_hoverPlay = false;
    update();
    QWidget::leaveEvent(e);
}

void CoverTile::mouseMoveEvent(QMouseEvent* e)
{
    const bool over = playRect().contains(e->position());
    if (over != m_hoverPlay) {
        m_hoverPlay = over;
        update();
    }
}

void CoverTile::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !rect().contains(e->position().toPoint()))
        return;
    if (m_kind != Kind::AddGames && !m_locked && playRect().contains(e->position()))
        emit playClicked();
    else
        emit clicked();
}

void CoverTile::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_kind != Kind::AddGames && !m_locked)
        emit doubleClicked();
}

void CoverTile::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (m_kind != Kind::AddGames && !m_locked)
            emit playClicked();
        else
            emit clicked();
        return;
    }
    if (e->key() == Qt::Key_Space) {
        emit clicked();
        return;
    }
    QWidget::keyPressEvent(e);
}

void CoverTile::paintEvent(QPaintEvent*)
{
    const Palette& c = Theme::instance().palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF a = artRect();
    const qreal radius = 10;

    if (m_kind == Kind::AddGames) {
        QPen pen(m_hover ? c.text3 : c.borderStrong, 1.5, Qt::DashLine);
        pen.setDashPattern({3, 3});
        p.setPen(pen);
        p.setBrush(m_hover ? c.hover : Qt::transparent);
        p.drawRoundedRect(a.adjusted(0.75, 0.75, -0.75, -0.75), radius, radius);
        const qreal cy = a.center().y();
        Icon::paint(p, QRectF(a.left(), cy - 34, a.width(), 20), QStringLiteral("plus"), c.text3, 20);
        p.setFont(Theme::font(13, 500));
        p.setPen(c.text2);
        p.drawText(QRectF(a.left(), cy - 8, a.width(), 16), Qt::AlignCenter, QStringLiteral("Add games"));
        p.setFont(Theme::font(12));
        p.setPen(c.text3);
        p.drawText(QRectF(a.left(), cy + 15, a.width(), 16), Qt::AlignCenter, QStringLiteral("or drop a folder here"));
        if (hasFocus()) {
            p.setPen(QPen(c.accent, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(a.adjusted(-2, -2, 2, 2), radius + 2, radius + 2);
        }
        return;
    }

    if (m_selected || hasFocus()) {
        QPainterPath outer;
        outer.addRoundedRect(a.adjusted(-4, -4, 4, 4), radius + 4, radius + 4);
        QPainterPath gap;
        gap.addRoundedRect(a.adjusted(-2, -2, 2, 2), radius + 2, radius + 2);
        p.fillPath(outer.subtracted(gap), m_selected ? c.accent : c.accentSoft);
    }

    QPainterPath clip;
    clip.addRoundedRect(a, radius, radius);
    if (m_kind == Kind::Art && !m_art.isNull()) {
        p.fillPath(clip.translated(0, 1), QColor(0, 0, 0, 0x14));
        p.save();
        p.setClipPath(clip);
        p.fillRect(a, QColor(0x22, 0x22, 0x22));
        const QSizeF scaled = QSizeF(m_art.size()).scaled(a.size(), Qt::KeepAspectRatioByExpanding);
        const QRectF target(a.center().x() - scaled.width() / 2, a.center().y() - scaled.height() / 2, scaled.width(), scaled.height());
        p.drawPixmap(target, m_art, QRectF(m_art.rect()));
        p.restore();
        p.setPen(QPen(QColor(0, 0, 0, 0x0F), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(a.adjusted(-0.5, -0.5, 0.5, 0.5), radius, radius);
    } else {
        p.fillPath(clip, c.sunken);
        p.setPen(QPen(c.border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(a.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);
        p.setFont(Theme::font(12, 500, 0.06));
        p.setPen(c.text3);
        p.drawText(QRectF(a.left() + 16, a.top() + 16, a.width() - 32, 16), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("NO COVER ART"));
        // title, bottom-aligned, wrapped
        const QFont tf = Theme::font(22, 600, -0.02);
        QTextLayout layout(m_title, tf);
        QTextOption opt;
        opt.setWrapMode(QTextOption::WordWrap);
        layout.setTextOption(opt);
        layout.beginLayout();
        QList<QTextLine> lines;
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(a.width() - 32);
            lines << line;
        }
        layout.endLayout();
        const int maxLines = 4;
        const int shown = int(qMin<qsizetype>(lines.size(), maxLines));
        const qreal lh = 26;
        qreal y = a.bottom() - 16 - shown * lh;
        p.setPen(c.text2);
        for (int i = 0; i < shown; ++i) {
            lines[i].setPosition(QPointF(0, 0));
            const qreal baseline = y + (lh - lines[i].height()) / 2;
            lines[i].draw(&p, QPointF(a.left() + 16, baseline - lines[i].y()));
            y += lh;
        }
    }

    if (m_locked) {
        const QRectF chip(a.right() - 8 - 26, a.top() + 8, 26, 26);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x0A, 0x0A, 0x0E, 0xB3));
        p.drawEllipse(chip);
        Icon::paint(p, chip, QStringLiteral("lock"), Qt::white, 13);
    } else if (m_hover) {
        const QRectF pr = playRect();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 40));
        p.drawEllipse(pr.translated(0, 2));
        p.setBrush(m_hoverPlay ? c.accent.lighter(110) : c.accent);
        p.drawEllipse(pr);
        Icon::paint(p, pr.translated(1.5, 0), QStringLiteral("play"), c.onAccent, 20);
    }

    // caption
    const qreal top = a.bottom() + Ring + 10 - Ring;
    p.setFont(Theme::font(13, 600));
    p.setPen(c.text);
    const QFontMetrics fm(p.font());
    p.drawText(QRectF(a.left() + 2, top, a.width() - 4, 16), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(m_title, Qt::ElideRight, int(a.width() - 4)));
    const QString status = m_locked && !m_reason.isEmpty() ? m_reason : m_status;
    const Theme::Tone tone = m_locked && !m_reason.isEmpty() ? Theme::Tone::Neutral : m_tone;
    if (!status.isEmpty()) {
        const qreal sy = top + 16 + 4;
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::toneColor(tone));
        p.drawEllipse(QRectF(a.left() + 2, sy + 5, 6, 6));
        p.setFont(Theme::font(12));
        p.setPen(c.text2);
        const QFontMetrics sm(p.font());
        p.drawText(QRectF(a.left() + 14, sy, a.width() - 14, 16), Qt::AlignLeft | Qt::AlignVCenter, sm.elidedText(status, Qt::ElideRight, int(a.width() - 14)));
    }
}
