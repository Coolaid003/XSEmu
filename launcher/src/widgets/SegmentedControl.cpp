#include "SegmentedControl.h"

#include "theme/Theme.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

SegmentedControl::SegmentedControl(const QStringList& items, QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setItems(items);
    connect(&Theme::instance(), &Theme::changed, this, [this] { updateGeometry(); update(); });
}

void SegmentedControl::setItems(const QStringList& items)
{
    m_items = items;
    m_enabled = QList<bool>(items.size(), true);
    m_current = qBound(0, m_current, qMax(0, int(items.size()) - 1));
    updateGeometry();
    update();
}

void SegmentedControl::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_items.size() || index == m_current)
        return;
    m_current = index;
    update();
    emit currentChanged(index);
}

void SegmentedControl::setItemEnabled(int index, bool enabled)
{
    if (index >= 0 && index < m_enabled.size()) {
        m_enabled[index] = enabled;
        update();
    }
}

QList<QRect> SegmentedControl::segmentRects() const
{
    QList<QRect> out;
    int x = 2;
    for (int i = 0; i < m_items.size(); ++i) {
        const QFontMetrics fm(Theme::font(12, i == m_current ? 600 : 500));
        const int w = fm.horizontalAdvance(m_items[i]) + 20;
        out << QRect(x, 2, w, 24);
        x += w;
    }
    return out;
}

QSize SegmentedControl::sizeHint() const
{
    int w = 4;
    for (const QString& s : m_items)
        w += QFontMetrics(Theme::font(12, 600)).horizontalAdvance(s) + 20;
    return {w, 28};
}

void SegmentedControl::paintEvent(QPaintEvent*)
{
    const Palette& c = Theme::instance().palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled())
        p.setOpacity(Metrics::DisabledOpacity);
    QPainterPath track;
    track.addRoundedRect(QRectF(rect()), 7, 7);
    p.fillPath(track, c.sunken);
    const QList<QRect> rects = segmentRects();
    for (int i = 0; i < rects.size(); ++i) {
        QRectF r = rects[i];
        const bool sel = i == m_current;
        if (sel) {
            QPainterPath sh;
            sh.addRoundedRect(r.translated(0, 1), 5, 5);
            p.fillPath(sh, QColor(0, 0, 0, 0x0F));
            QPainterPath seg;
            seg.addRoundedRect(r, 5, 5);
            p.fillPath(seg, c.surface);
            p.setPen(QPen(c.border, 1));
            p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 4.5, 4.5);
        }
        p.setFont(Theme::font(12, sel ? 600 : 500));
        QColor fg = sel ? c.text : c.text2;
        if (!m_enabled.value(i, true))
            fg = c.text3;
        p.setPen(fg);
        p.drawText(r, Qt::AlignCenter, m_items[i]);
    }
    if (hasFocus()) {
        p.setPen(QPen(c.accent, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
    }
}

void SegmentedControl::mousePressEvent(QMouseEvent* e)
{
    const QList<QRect> rects = segmentRects();
    for (int i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(e->position().toPoint()) && m_enabled.value(i, true)) {
            setCurrentIndex(i);
            return;
        }
    }
}

void SegmentedControl::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
        const int step = e->key() == Qt::Key_Left ? -1 : 1;
        for (int i = m_current + step; i >= 0 && i < m_items.size(); i += step) {
            if (m_enabled.value(i, true)) {
                setCurrentIndex(i);
                break;
            }
        }
        return;
    }
    QWidget::keyPressEvent(e);
}
