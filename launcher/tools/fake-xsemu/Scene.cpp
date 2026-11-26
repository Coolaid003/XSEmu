#include "Scene.h"

#include <QKeyEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <cmath>

Scene::Scene(QWindow* parent)
    : QRasterWindow(parent)
{
    m_clock.start();
    m_tick.setInterval(16);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        const qint64 now = m_clock.elapsed();
        if (!m_paused && !m_booting)
            m_t += (now - m_last) / 1000.0;
        m_last = now;
        update();
    });
    m_tick.start();
}

void Scene::setPaused(bool paused)
{
    m_paused = paused;
    m_lastFrame = -1;
    update();
}

Scene::Window Scene::takeWindow()
{
    Window w = m_window;
    w.meanMs = w.frames > 0 ? m_windowSum / w.frames : 0;
    m_window = {};
    m_windowSum = 0;
    return w;
}

void Scene::keyPressEvent(QKeyEvent* e)
{
    if (!m_forwardKeys) {
        QRasterWindow::keyPressEvent(e);
        return;
    }
    switch (e->key()) {
    case Qt::Key_F1: emit launcherKey(QStringLiteral("F1")); return;
    case Qt::Key_F3: emit launcherKey(QStringLiteral("F3")); return;
    case Qt::Key_F11: emit launcherKey(QStringLiteral("F11")); return;
    case Qt::Key_Escape: emit launcherKey(QStringLiteral("Escape")); return;
    default: break;
    }
    QRasterWindow::keyPressEvent(e);
}

void Scene::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(QRect(QPoint(0, 0), size()), Qt::black);

    // 16:9 inside whatever the parent gives us, black bars around it
    const QSizeF full = size();
    QSizeF s(full.width(), full.width() * 9.0 / 16.0);
    if (s.height() > full.height())
        s = QSizeF(full.height() * 16.0 / 9.0, full.height());
    const QRectF r((full.width() - s.width()) / 2.0, (full.height() - s.height()) / 2.0, s.width(), s.height());
    p.setClipRect(r);
    const double w = r.width(), h = r.height(), t = m_t;

    if (m_booting) {
        p.fillRect(r, QColor(10, 12, 20));
        const double phase = std::fmod(m_clock.elapsed() / 1000.0, 1.2) / 1.2;
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 3; ++i) {
            const double a = std::sin((phase + i * 0.18) * 3.14159265358979);
            p.setBrush(QColor(120, 150, 255, int(80 + 175 * std::max(0.0, a))));
            p.drawEllipse(QPointF(r.center().x() + (i - 1) * h * 0.05, r.center().y()), h * 0.012, h * 0.012);
        }
    } else {
        const double horizon = r.top() + h * 0.58;
        QLinearGradient sky(r.topLeft(), QPointF(r.left(), horizon));
        sky.setColorAt(0.0, QColor(18, 22, 64));
        sky.setColorAt(0.55, QColor(92, 54, 140));
        sky.setColorAt(1.0, QColor(244, 140, 96));
        p.fillRect(QRectF(r.left(), r.top(), w, horizon - r.top()), sky);

        const QPointF sun(r.left() + w * (0.5 + 0.18 * std::sin(t * 0.07)), horizon - h * 0.12);
        QRadialGradient glow(sun, h * 0.35);
        glow.setColorAt(0.0, QColor(255, 214, 150, 160));
        glow.setColorAt(1.0, QColor(255, 214, 150, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(sun, h * 0.35, h * 0.35);
        p.setBrush(QColor(255, 226, 170));
        p.drawEllipse(sun, h * 0.08, h * 0.08);

        // rolling ridges, each layer slower and paler towards the horizon
        const QColor ridge[3] = {QColor(70, 46, 110), QColor(46, 30, 84), QColor(26, 18, 52)};
        for (int layer = 0; layer < 3; ++layer) {
            QPainterPath path;
            path.moveTo(r.left(), horizon);
            const double speed = 0.02 + layer * 0.03;
            const double amp = h * (0.05 + layer * 0.03);
            for (int x = 0; x <= 64; ++x) {
                const double fx = x / 64.0;
                const double y = horizon - amp * (0.6 + 0.4 * std::sin(fx * (5 + layer * 2) + t * speed * 6 + layer))
                    * (0.7 + 0.3 * std::sin(fx * 13 + layer * 3 + t * speed * 3));
                path.lineTo(r.left() + fx * w, y);
            }
            path.lineTo(r.right(), horizon);
            path.closeSubpath();
            p.setBrush(ridge[layer]);
            p.drawPath(path);
        }

        QLinearGradient floor(QPointF(r.left(), horizon), r.bottomLeft());
        floor.setColorAt(0.0, QColor(36, 20, 60));
        floor.setColorAt(1.0, QColor(8, 6, 18));
        p.fillRect(QRectF(r.left(), horizon, w, r.bottom() - horizon), floor);

        p.setPen(QPen(QColor(255, 120, 200, 140), 1.2));
        const QPointF vanish(r.center().x(), horizon);
        for (int i = -12; i <= 12; ++i)
            p.drawLine(vanish, QPointF(r.center().x() + i * w * 0.12, r.bottom()));
        const double scroll = std::fmod(t * 0.6, 1.0);
        for (int i = 0; i < 14; ++i) {
            const double z = (i + scroll) / 14.0;
            const double y = horizon + (r.bottom() - horizon) * z * z;
            p.setPen(QPen(QColor(255, 120, 200, int(40 + 160 * z)), 1.2));
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }

        // a few drifting shapes
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 5; ++i) {
            const double px = std::fmod(0.15 + i * 0.21 + t * (0.01 + i * 0.004), 1.0);
            const double py = 0.12 + 0.08 * std::sin(t * 0.5 + i);
            p.setBrush(QColor(255, 255, 255, 30 + i * 12));
            p.drawRoundedRect(QRectF(r.left() + px * w, r.top() + py * h, h * 0.06, h * 0.012), h * 0.006, h * 0.006);
        }
    }

    if (m_overlay) {
        // the frame-time graph of the protocol's overlay: last 120 frames, green to 60 fps,
        // amber to 30, red slower, a line at 16.7 ms; no text
        const double gw = h * 0.36, gh = h * 0.12;
        const QRectF panel(r.left() + h * 0.02, r.top() + h * 0.02, gw, gh);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(18, 18, 20, 200));
        p.drawRoundedRect(panel, 6, 6);
        const double maxMs = 50.0;
        const double bw = gw / 120.0;
        for (int i = 0; i < m_history.size(); ++i) {
            const double ms = m_history[i];
            const QColor col = ms <= 16.7 ? QColor(63, 185, 80) : ms <= 33.3 ? QColor(227, 179, 65) : QColor(240, 96, 90);
            const double bh = std::min(1.0, ms / maxMs) * (gh - 8);
            p.setBrush(col);
            p.drawRect(QRectF(panel.left() + i * bw, panel.bottom() - 4 - bh, std::max(1.0, bw - 0.5), bh));
        }
        p.setPen(QPen(QColor(255, 255, 255, 120), 1, Qt::DashLine));
        const double y60 = panel.bottom() - 4 - (16.7 / maxMs) * (gh - 8);
        p.drawLine(QPointF(panel.left() + 4, y60), QPointF(panel.right() - 4, y60));
    }
    p.setClipping(false);

    if (m_booting || m_paused)
        return;
    const qint64 now = m_clock.elapsed();
    if (m_lastFrame >= 0) {
        const double ms = double(now - m_lastFrame);
        m_history.append(ms);
        if (m_history.size() > 120)
            m_history.removeFirst();
        ++m_window.frames;
        m_windowSum += ms;
        m_window.worstMs = std::max(m_window.worstMs, ms);
        if (ms >= 100.0)
            ++m_window.stalls;
    }
    m_lastFrame = now;
    m_presentedAny = true;
    emit framePresented();
}
