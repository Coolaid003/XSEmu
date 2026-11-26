#pragma once

#include "core/Game.h"

#include <QWidget>

class Button;
class CoverTile;
class Label;
class PageHeader;
class QGridLayout;
class Services;

// Game details (Game details page): cover, facts from MicrosoftGame.config and the launcher's
// records, and Play. Reached from a cover tile; Back returns to the library.
class GameDetailsPage : public QWidget {
    Q_OBJECT
public:
    explicit GameDetailsPage(Services& services, QWidget* parent = nullptr);
    void setGame(const Game& game);
    Game game() const { return m_game; }

signals:
    void back();
    void playRequested(const Game& game);

private:
    void refresh();

    Services& m_s;
    Game m_game;
    PageHeader* m_header;
    Button* m_play;
    CoverTile* m_cover;
    QGridLayout* m_facts;
    Label* m_blocked;
};
