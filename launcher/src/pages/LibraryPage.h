#pragma once

#include "core/Game.h"

#include <QWidget>

class Badge;
class Button;
class Card;
class CoverThumb;
class CoverTile;
class JumpBackHero;
class Label;
class PageHeader;
class QGridLayout;
class QVBoxLayout;
class SearchBox;
class Services;
class SortButton;

// Library (Library page): the "Jump back in" hero for the game played last, or the banner while a
// game holds the process; the cover grid sorted by the user's choice with an "Add games" tile; search
// (Ctrl K) over titles, publishers and folder names; the empty and no-results states. Tiles of
// other games are locked while any XSEmu process runs. A folder dropped on the page is added.
class LibraryPage : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPage(Services& services, QWidget* parent = nullptr);
    SearchBox* searchBox() const { return m_search; }

    // Every Play on this page: the cold-cache prompt when shaders are cold, then playRequested.
    void requestPlay(const Game& game);

signals:
    void playRequested(const Game& game);
    void warmupRequested(const Game& game);
    void detailsRequested(const Game& game);
    void addGamesRequested();
    void nowPlayingRequested();

protected:
    void resizeEvent(QResizeEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void rebuild();
    void refreshState();
    void layoutGrid();
    GameList sorted(const GameList& games) const;

    Services& m_s;
    PageHeader* m_header;
    SearchBox* m_search;
    Button* m_addGames;
    JumpBackHero* m_hero;
    Card* m_banner;
    CoverThumb* m_bannerCover;
    Label* m_bannerTitle;
    Label* m_bannerText;
    Badge* m_bannerHeld;
    Button* m_resume;
    Button* m_stop;
    QWidget* m_sectionRow;
    Label* m_sectionLabel;
    Label* m_sectionHint;
    SortButton* m_sort;
    QWidget* m_gridHost;
    QGridLayout* m_grid;
    QList<CoverTile*> m_tiles;
    GameList m_shown;
    CoverTile* m_addTile;
    QWidget* m_empty;
    QWidget* m_noResults;
    Label* m_noResultsTitle;
    QWidget* m_dropOverlay;
    Game m_heroGame;
    int m_columns = 0;
};
