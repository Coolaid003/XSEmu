#pragma once

#include "core/EmulatorController.h"
#include "core/Game.h"

#include <QWidget>

class Badge;
class Button;
class DetailsHero;
class InfoCard;
class Label;
class RunsWithSelector;
class Services;

// Game details (Game details page): the hero band with cover, title and play record; the action bar
// (Play / Warm up, shader state, Open folder, more); and four cards: the game's facts, the shader
// cache as far as the launcher can tell, the last run, and per-game overrides that write the title's
// own xcl.toml (or the launcher's per-game settings). Reached from a cover tile; Back returns.
class GameDetailsPage : public QWidget {
    Q_OBJECT
public:
    explicit GameDetailsPage(Services& services, QWidget* parent = nullptr);
    void setGame(const Game& game);
    RunsWithSelector* runsWith() const { return m_runsWith; }
    Game game() const { return m_game; }

signals:
    void back();
    void playRequested(const Game& game);
    void warmupRequested(const Game& game);
    void manageRunners();

private:
    void refresh();
    void refreshOverrides();
    void requestPlay();
    void onStats();
    void onExit(const EmulatorController::ExitInfo& info);

    Services& m_s;
    Game m_game;
    DetailsHero* m_hero;
    Button* m_primary;
    Button* m_secondary;
    Badge* m_state;
    RunsWithSelector* m_runsWith;
    Button* m_openFolder;
    Button* m_more;
    InfoCard* m_gameCard;
    InfoCard* m_cacheCard;
    InfoCard* m_runCard;
    InfoCard* m_overrides;
    Label* m_overridesNote;

    // the run in flight, for the last-run record
    QString m_runId;
    double m_fpsSum = 0;
    int m_fpsSamples = 0;
    qint64 m_stalls = 0;
    double m_worstMs = 0;
    qint64 m_heldPeak = -1;
    qint64 m_freeMinMb = -1;
};
