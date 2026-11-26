#pragma once

#include <QWidget>

class WindowButton;

// Minimise, maximise/restore and close for the frameless window, 46 px wide each. Used by the title
// bar (32 px) and the in-game bar (40 px).
class WindowButtons : public QWidget {
    Q_OBJECT
public:
    explicit WindowButtons(int height, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent*) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void syncMaximised();
    WindowButton* m_min;
    WindowButton* m_max;
    WindowButton* m_close;
    QWidget* m_watched = nullptr;
};
