#pragma once

#include <QWidget>

class NavItem;
class QStackedWidget;
class QVBoxLayout;
class Services;

// Settings (Settings pages): a sub-navigation of nine sections. Console, Network, Shaders,
// Overlay, Game folders, Graphics (theme) and About are wired to AppSettings; Input and Logs show
// what exists today.
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    enum class Section { Console, Network, Shaders, Overlay, GameFolders, Input, Graphics, Logs, About };
    explicit SettingsPage(Services& services, QWidget* parent = nullptr);
    void showSection(Section section);

signals:
    void addGamesRequested();

private:
    QWidget* buildConsole();
    QWidget* buildNetwork();
    QWidget* buildShaders();
    QWidget* buildOverlay();
    QWidget* buildGameFolders();
    QWidget* buildInput();
    QWidget* buildGraphics();
    QWidget* buildLogs();
    QWidget* buildAbout();
    void refreshFolders();

    Services& m_s;
    QList<NavItem*> m_nav;
    QStackedWidget* m_stack;
    QVBoxLayout* m_folders = nullptr;
};
