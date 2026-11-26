#pragma once

#include <QWidget>

class Button;
class Label;
class Services;
class Spinner;

// The window while a game owns it (In-game: booting/playing/paused): a 40 px bar and, below it, the
// host widget that XSEmu's 16:9 game window docks into. The host is painted black, so whatever the
// game window does not cover reads as letterbox bars.
class InGameView : public QWidget {
    Q_OBJECT
public:
    explicit InGameView(Services& services, QWidget* parent = nullptr);
    QWidget* host() const { return m_host; }

signals:
    void backToLibrary();

private:
    void refresh();

    Services& m_s;
    Label* m_title;
    Spinner* m_spinner;
    Label* m_status;
    Button* m_pause;
    Button* m_stop;
    QWidget* m_host;
    Label* m_hostTitle;
    Label* m_hostDetail;
};
