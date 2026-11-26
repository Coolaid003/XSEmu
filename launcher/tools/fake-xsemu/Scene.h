#pragma once

#include <QElapsedTimer>
#include <QRasterWindow>
#include <QTimer>

// The fake game's frame: an original abstract scene (sky gradient, sun, rolling ridges, a moving
// floor grid) letterboxed to 16:9, plus an optional in-frame performance panel.
class Scene : public QRasterWindow {
    Q_OBJECT
public:
    explicit Scene(QWindow* parent = nullptr);

    void setPaused(bool paused);
    void setOverlay(bool on) { m_overlay = on; update(); }
    void setBooting(bool booting) { m_booting = booting; update(); }
    void setStatsText(const QString& t) { m_stats = t; }
    double fps() const { return m_fps; }
    double frameMs() const { return m_fps > 0 ? 1000.0 / m_fps : 0; }

signals:
    void launcherKey(const QString& key);
    void framePresented();

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QTimer m_tick;
    QElapsedTimer m_clock;
    double m_t = 0;
    qint64 m_last = 0;
    double m_fps = 0;
    int m_frames = 0;
    qint64 m_fpsWindowStart = 0;
    bool m_paused = false;
    bool m_overlay = false;
    bool m_booting = true;
    QString m_stats;
};
