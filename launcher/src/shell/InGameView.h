#pragma once

#include "core/EmulatorController.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class Badge;
class BootPanel;
class Button;
class GameCover;
class ExitPanel;
class GameHost;
class KeyHint;
class Label;
class PausedPanel;
class Services;
class Spinner;

// The window while a game owns it (In-game renders): the 40 px game bar, which is also the title bar,
// and below it the host widget XSEmu's game window docks into (`run --host-hwnd`). The host is native,
// painted black and kept at the largest centred 16:9 rectangle of the area; anything the game window
// leaves uncovered reads as letterbox bars.
//
// Over the game window the launcher draws only in native child panels of the host, which stack above
// the game's own child window: the boot screen while booting and the paused card while paused. After a
// crash, a GPU device removal or a failed boot the area shows the exit screen instead.
//
// Keys: F1 pauses or resumes, F3 toggles the emulator's overlay, F11 toggles fullscreen and Esc leaves
// fullscreen, both from the emulator's `key` events and while the launcher itself has focus.
class InGameView : public QWidget {
    Q_OBJECT
public:
    explicit InGameView(Services& services, QWidget* parent = nullptr);

    // The native host's window id, created on first call; pass it as --host-hwnd.
    quintptr hostWinId();
    QWidget* host() const;

    void setFullScreen(bool on); // frame only, plus the key hint on entry
    bool isFullScreen() const { return m_fullScreen; }

    void showExit(const EmulatorController::ExitInfo& info);
    void clearExit();
    bool showingExit() const { return m_exit.has_value(); }

public slots:
    void handleKey(const QString& key);

signals:
    void backToLibrary();
    void relaunchRequested(const Game& game);
    void fullScreenToggled();

protected:
    void showEvent(QShowEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void refresh();
    void layoutArea();
    void raisePanels();
    void focusGameSoon();
    void updateAutoHide();
    Game currentGame() const;

    Services& m_s;
    QWidget* m_bar;
    GameCover* m_cover;
    Label* m_title;
    Spinner* m_spinner;
    Badge* m_paused;
    Badge* m_place;
    Badge* m_stopped;
    QWidget* m_perfSep;
    Label* m_perf;
    QWidget* m_ramSep;
    Label* m_ramLabel;
    Label* m_ramValue;
    Button* m_pause;
    Button* m_overlay;
    Button* m_full;
    Button* m_stop;
    Button* m_relaunch;
    QWidget* m_area;
    GameHost* m_host;
    BootPanel* m_boot;
    PausedPanel* m_pausedPanel;
    ExitPanel* m_exitPanel;
    KeyHint* m_hint;
    std::optional<EmulatorController::ExitInfo> m_exit;
    bool m_fullScreen = false;
    bool m_barHidden = false;
    QTimer m_autoHide;
    QTimer m_restack;
    QElapsedTimer m_away;
};
