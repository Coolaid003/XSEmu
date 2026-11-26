#pragma once

#include <QWidget>

class Button;
class InfoNote;
class ProgressBlock;
class QComboBox;
class QGridLayout;
class Services;

// Shader warm-up (Warm-up page): the progress block for the run in flight or the last one, a note
// when a game blocks the warm-up, and the recent runs the launcher recorded. Progress is per phase
// and names the phase.
class WarmupPage : public QWidget {
    Q_OBJECT
public:
    explicit WarmupPage(Services& services, QWidget* parent = nullptr);

private:
    void refresh();
    void refreshGames();
    void refreshRuns();

    Services& m_s;
    QComboBox* m_game;
    Button* m_start;
    InfoNote* m_note;
    ProgressBlock* m_block;
    QGridLayout* m_runs;
    QWidget* m_runsHost;
};
