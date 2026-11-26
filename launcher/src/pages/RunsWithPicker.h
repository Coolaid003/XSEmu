#pragma once

#include "core/Game.h"

#include <QWidget>

class Services;

// "Runs with" on Game details: the runner this game uses as a compact selector. A click opens the
// picker over the window: every runner with its vendor badge, the recommendation for this PC, its
// install state and how it shows the game, an Install action for one that is not installed, and
// "Manage runners". Choosing the default runner clears the game's override.
//   auto* runsWith = new RunsWithSelector(services);
//   runsWith->setGame(game);
//   connect(runsWith, &RunsWithSelector::manageRunners, ...);
class RunsWithSelector : public QWidget {
    Q_OBJECT
public:
    explicit RunsWithSelector(Services& services, QWidget* parent = nullptr);
    void setGame(const Game& game);
    void openPicker();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    void manageRunners();

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QString runnerId() const;
    QString trailing() const; // "Default" when the game follows the default runner

    Services& m_s;
    Game m_game;
    bool m_hover = false;
};
