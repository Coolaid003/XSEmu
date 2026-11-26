#pragma once

#include <QElapsedTimer>
#include <QList>
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
    void setForwardKeys(bool on) { m_forwardKeys = on; }
    bool framePresentedYet() const { return m_presentedAny; }

    // What the protocol's stats line reports for the time since the last call.
    struct Window {
        int frames = 0;
        double meanMs = 0;
        double worstMs = 0;
        int stalls = 0; // frames shown 100 ms or more after the one before
    };
    Window takeWindow();

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
    qint64 m_lastFrame = -1;
    QList<double> m_history; // last 120 frame times, ms
    Window m_window;
    double m_windowSum = 0;
    bool m_presentedAny = false;
    bool m_forwardKeys = false;
    bool m_paused = false;
    bool m_overlay = false;
    bool m_booting = true;
};
