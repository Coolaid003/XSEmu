#pragma once

#include "Game.h"
#include "ProcessTree.h"
#include "Protocol.h"
#include "Runner.h"

#include <QLocalSocket>

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QProcessEnvironment>
#include <QTimer>

#include <optional>

class AppSettings;
class ProcessGate;
class QLocalServer;
class RunnerManager;

// Game runs: the one game process tree, XSEmu.exe "run" with its control pipe and what it reports, or
// a third-party runner (RunnerSpec) started hidden, whose own top-level window we find by process.
//
//   auto check = emu.checkLaunch(game);          // why it cannot start, if it cannot
//   if (check.ok()) emu.start(game, hostWidget->winId());
//   connect(&emu, &EmulatorController::stateChanged, ...);   // Idle/Booting/Running/Paused/Stopping
//   connect(&emu, &EmulatorController::statsChanged, ...);   // emu.stats()
//   connect(&emu, &EmulatorController::exited, ...);         // emu.lastExit()
class EmulatorController : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Booting, Running, Paused, Stopping };
    Q_ENUM(State)

    static constexpr qint64 kRamFloorMb = 1024;     // memwatch stops a run below this
    static constexpr int kStopGraceMs = 5000;

    struct Stats {
        std::optional<double> fps;
        std::optional<double> frameMs;
        std::optional<qint64> ramFreeMb;
        std::optional<qint64> vramUsedMb;
        std::optional<qint64> vramBudgetMb;
        std::optional<qint64> stalls;
        std::optional<double> worstMs;
        std::optional<qint64> pipelines;
        std::optional<qint64> compiling;
        qint64 heldBytes = -1; // private memory of the XSEmu tree (-1: unknown)
        quint64 sample = 0;    // which protocol stats message these are; grows across runs, 0: none yet
    };

    struct ExitInfo {
        enum class Kind { Stopped, TitleExit, Crash, DeviceRemoved, Error };
        Kind kind = Kind::Stopped;
        int exitCode = 0;
        QString message;     // the emulator's message (crash module, removal reason, boot error)
        bool killed = false; // the tree was killed after the stop grace period
        bool sawExitLine = false;
        Game game;
        qint64 playedSeconds = 0;
        QDateTime at;
    };

    struct LaunchCheck {
        enum class Problem { None, EmulatorMissing, EmulatorTooOld, RunnerNotInstalled, GameFilesMissing, Busy, OtherEmulatorRunning, NotEnoughRam };
        Problem problem = Problem::None;
        QString reason;          // a human sentence, empty when ok
        QStringList processes;   // OtherEmulatorRunning: "name pid"
        QString marker;          // OtherEmulatorRunning: the busy marker file, when it exists
        qint64 needMb = 0;       // NotEnoughRam: launch need + floor
        qint64 freeMb = 0;
        bool ok() const { return problem == Problem::None; }
    };

    EmulatorController(AppSettings& settings, ProcessGate& gate, QObject* parent = nullptr);
    ~EmulatorController() override;

    // Third-party runners come from here; without it every game runs with XSEmu.
    void setRunners(RunnerManager* runners) { m_runners = runners; }
    // The runner a game would start with now (its setting, or XSEmu when that runner is unknown).
    const RunnerSpec& runnerFor(const Game& game) const;
    // The runner of the current (or last) run.
    const RunnerSpec& runner() const;
    bool canPause() const { return runner().canPause; }
    bool canOverlay() const { return runner().canOverlay; }
    // A third-party runner's own game window once it is up (0 before, and always 0 for XSEmu).
    quintptr gameWindow() const { return m_gameWindow; }

    LaunchCheck checkLaunch(const Game& game) const;
    // Free RAM a launch needs: the 1 GB floor plus AppSettings::launchNeedMb() (0 unless set).
    qint64 launchNeedMb() const;

    // Start the game. hostWindow is the native handle of the in-game view's host widget (0: the
    // emulator opens its own window). Returns false with *error set when it cannot start.
    bool start(const Game& game, quintptr hostWindow = 0, QString* error = nullptr);
    void pause();
    void resume();
    void stop();
    void setOverlay(bool on);
    void focusGame();

    State state() const { return m_state; }
    bool isActive() const { return m_state != State::Idle; }
    Game game() const { return m_game; }
    Stats stats() const { return m_stats; }
    std::optional<Protocol::Hello> hello() const { return m_hello; }
    qint64 helloAfterMs() const { return m_helloAfterMs; } // from start() to the hello line, -1 before it
    bool overlayOn() const { return m_overlay; }
    bool pipeConnected() const { return m_socket != nullptr; }
    std::optional<ExitInfo> lastExit() const { return m_lastExit; }
    QString pipeName() const { return m_pipeName; }
    // Seconds since the game first reported running this session (pauses included), 0 before.
    qint64 sessionSeconds() const { return m_playClock.isValid() ? m_playClock.elapsed() / 1000 : 0; }

    // The command line and environment start() uses (tested).
    static QStringList runArguments(const QString& gameFolder, quintptr hostWindow, const QString& pipeName);
    static QString hwndToHex(quintptr hwnd);

    // Screenshot fixtures only: put the controller into a state without a process.
    void simulateForScreenshot(State state, const Game& game, const Stats& stats);

signals:
    void stateChanged(EmulatorController::State state);
    void statsChanged();
    void helloReceived();
    void gameWindowChanged();
    void keyPressed(const QString& key); // F1, F3, F11, Escape (from the game window)
    void exited(const EmulatorController::ExitInfo& info);
    void logLine(const QString& line);   // the emulator's stdout/stderr, for a log view

private:
    void setState(State s);
    void onNewConnection();
    void onSocketReadyRead();
    void handle(const Protocol::Event& ev);
    void onFinished(int exitCode, int exitStatus);
    void send(const QByteArray& line);
    void pollMemory();
    bool startRunner(const RunnerSpec& spec, const Game& game, QString* error);
    void pollGameWindow();
    void onRunnerOutput(const QString& line);

    AppSettings& m_settings;
    ProcessGate& m_gate;
    QPointer<ProcessTree> m_process;
    QLocalServer* m_server = nullptr;
    QPointer<QLocalSocket> m_socket;
    QByteArray m_buffer;
    QString m_pipeName;
    State m_state = State::Idle;
    Game m_game;
    Stats m_stats;
    std::optional<Protocol::Hello> m_hello;
    std::optional<Protocol::Exit> m_exitLine;
    std::optional<ExitInfo> m_lastExit;
    bool m_overlay = false;
    bool m_stopRequested = false;
    bool m_killed = false;
    QTimer m_killTimer;
    QTimer m_memoryTimer;
    QElapsedTimer m_playClock;
    quint64 m_statsSamples = 0; // protocol stats messages received, never reset
    QElapsedTimer m_launchClock;
    qint64 m_helloAfterMs = -1;
    qint64 m_playedMs = 0;
    RunnerManager* m_runners = nullptr;
    QString m_runnerId = QStringLiteral("xsemu");
    quintptr m_gameWindow = 0;
    bool m_windowReady = false;
    QTimer m_windowTimer;
    QStringList m_recentOutput; // a runner's last output lines, for its exit message
};

Q_DECLARE_METATYPE(EmulatorController::ExitInfo)
