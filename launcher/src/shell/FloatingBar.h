#pragma once

#include <QPoint>
#include <QTimer>
#include <QWidget>

class Badge;
class Button;
class CoverThumb;
class Label;
class Services;

// The bar over a third-party runner's own game window (such runners are never embedded): the game,
// the runner, how long it has run, Show XSEmu and Stop. A frameless tool window that stays on top
// only while the runner's window (or the bar) has the keyboard; it sits centred above that window
// until dragged, and Ctrl+Shift+H hides or shows it (Windows).
class FloatingBar : public QWidget {
    Q_OBJECT
public:
    explicit FloatingBar(Services& services);
    ~FloatingBar() override;

    // Screenshot fixtures only: show it without a runner window to follow.
    void showForScreenshot();

signals:
    void showLauncher();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override;

private:
    void sync();
    void place();

    Services& m_s;
    QWidget* m_pill;
    CoverThumb* m_cover;
    Label* m_title;
    Label* m_runner;
    Label* m_time;
    Button* m_show;
    Button* m_stop;
    QTimer m_poll;
    QString m_coverTitle;
    QPoint m_dragFrom;
    bool m_dragging = false;
    bool m_moved = false;
    bool m_userHidden = false;
    bool m_hotkey = false;
    bool m_screenshot = false;
};
