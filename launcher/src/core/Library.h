#pragma once

#include "Game.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

// The games in the configured folders. Scans in the background, re-scans when a folder changes.
//
//   connect(&library, &Library::gamesChanged, this, [&] { show(library.games()); });
//   library.setFolders(settings.gameFolders());   // scans
class Library : public QObject {
    Q_OBJECT
public:
    static constexpr int kDefaultDepth = 3;

    explicit Library(QObject* parent = nullptr);

    void setFolders(const QStringList& folders);
    QStringList folders() const { return m_folders; }

    const GameList& games() const { return m_games; }
    Game game(const QString& titleId) const; // invalid Game when unknown
    bool isScanning() const { return m_scanning; }

    // Start a scan now (also done by setFolders and by the watcher).
    void rescan();
    // Scan synchronously (tests, screenshot fixtures).
    void rescanNow();

    // The scan itself: every folder up to depth levels down holding a MicrosoftGame.config, each
    // package root once (a game root is not searched further), in folder order.
    static GameList scan(const QStringList& folders, int depth = kDefaultDepth, bool measureSize = true);
    // One game folder, or an invalid Game when it has no readable MicrosoftGame.config.
    static Game readGame(const QString& folder);

signals:
    void scanStarted();
    void gamesChanged();

private:
    void apply(const GameList& games, quint64 generation);
    void rewatch();

    QStringList m_folders;
    GameList m_games;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    quint64 m_generation = 0;
    bool m_scanning = false;
};
