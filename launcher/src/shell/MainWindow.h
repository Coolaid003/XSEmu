#pragma once

#include "Page.h"
#include "core/Game.h"

#include <QWidget>

class FirstLaunchPage;
class GameDetailsPage;
class InGameView;
class LibraryPage;
class QStackedWidget;
class Services;
class SettingsPage;
class Sidebar;
class TitleBar;
class WarmupPage;

// The frameless main window. Three modes share it: setup (first launch, no sidebar), the shell
// (title bar, sidebar and the page router) and in-game (the game bar and the host the game window
// docks into, without the launcher chrome).
class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(Services& services, QWidget* parent = nullptr);

    void showPage(Page page);
    void showGameDetails(const Game& game);
    void play(const Game& game);
    void enterInGame();
    void leaveInGame();
    void addGameFolder();

    LibraryPage* libraryPage() const { return m_library; }
    SettingsPage* settingsPage() const { return m_settings; }
    FirstLaunchPage* firstLaunchPage() const { return m_firstLaunch; }

protected:
    void paintEvent(QPaintEvent*) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#else
    bool eventFilter(QObject* o, QEvent* e) override;
#endif

private:
    void syncMode();

    Services& m_s;
    TitleBar* m_titleBar;
    QStackedWidget* m_modes;
    QWidget* m_shell;
    Sidebar* m_sidebar;
    QStackedWidget* m_pages;
    LibraryPage* m_library;
    WarmupPage* m_warmup;
    SettingsPage* m_settings;
    GameDetailsPage* m_details;
    FirstLaunchPage* m_firstLaunch;
    InGameView* m_inGame;
    bool m_wantInGame = false;
};
