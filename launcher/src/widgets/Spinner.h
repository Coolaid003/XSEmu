#pragma once

#include <QTimer>
#include <QWidget>

// The progress block's spinner: an accent arc on a sunken ring.
class Spinner : public QWidget {
    Q_OBJECT
public:
    explicit Spinner(int size = 18, QWidget* parent = nullptr);
    void setRunning(bool running);
    QSize sizeHint() const override { return {m_size, m_size}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    int m_size;
    int m_angle = 0;
    bool m_running = true;
    QTimer m_timer;
};
