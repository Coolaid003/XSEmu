#pragma once

#include "core/Game.h"

#include <QWidget>

class Badge;
class Button;
class Card;
class CoverTile;
class Label;
class PageHeader;
class QGridLayout;
class SearchBox;
class Services;

// Library (Library page): the paused-game banner while a game holds its memory, the cover grid
// with an "Add games" tile, and the empty and no-results states. Tiles of other games are locked
// while any XSEmu process runs.
class LibraryPage : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPage(Services& services, QWidget* parent = nullptr);
    SearchBox* searchBox() const { return m_search; }

signals:
    void playRequested(const Game& game);
    void detailsRequested(const Game& game);
    void addGamesRequested();
    void nowPlayingRequested();

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    void rebuild();
    void refreshState();
    void layoutGrid();

    Services& m_s;
    PageHeader* m_header;
    SearchBox* m_search;
    Card* m_banner;
    Label* m_bannerTitle;
    Label* m_bannerText;
    Badge* m_bannerHeld;
    Button* m_resume;
    Button* m_stop;
    QWidget* m_gridLabel;
    QWidget* m_gridHost;
    QGridLayout* m_grid;
    QList<CoverTile*> m_tiles;
    GameList m_shown;
    CoverTile* m_addTile;
    QWidget* m_empty;
    Label* m_emptyTitle;
    Label* m_emptyText;
    Button* m_emptyAction;
    int m_columns = 0;
};
