#pragma once

#include "AppSettings.h"
#include "Game.h"
#include "ProcessTree.h"
#include "Protocol.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <optional>

class ProcessGate;

// Shader warm-ups: "XSEmu.exe warm-up [--full] --progress-json", its JSON progress and cancel.
// The cache is machine-wide (the emulator takes no game argument); the game picked in the UI is
// recorded with the run so the Warm-up page can say which game it was for.
//
//   warmup.start(/*full*/ false, game);
//   connect(&warmup, &WarmupController::progressChanged, ...);  // warmup.progress()
//   connect(&warmup, &WarmupController::stateChanged, ...);     // warmup.state()
//   connect(&warmup, &WarmupController::finished, ...);         // the WarmupRun just recorded
class WarmupController : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Running, PausedForRam, Finished, Cancelled, StoppedPartWay, Failed };
    Q_ENUM(State)

    struct Progress {
        qint64 targets = 0;      // warmup_start.total: kept pipelines this pass builds
        bool full = false;
        int threads = 0;
        QString phase;           // starting, giants, bound, compute, compute_blobs, rest
        qint64 done = 0;         // within the phase
        qint64 total = 0;        // within the phase
        double keysPerS = 0;     // the phase's average
        std::optional<qint64> etaS; // rest of the phase; absent while unknown
        std::optional<qint64> ramFreeMb;
        int giantsCompiling = 0;
        int giantsMax = 0;
        int inFlight = 0;
        qint64 failed = 0;       // all phases so far
        bool pausedForRam = false;
        double elapsedS = 0;

        double phaseFraction() const { return total > 0 ? double(done) / double(total) : 0.0; }
    };

    static const QStringList& phases(); // in order
    static QString phaseLabel(const QString& phase);
    static int phaseIndex(const QString& phase); // 0-based, -1 unknown

    WarmupController(AppSettings& settings, ProcessGate& gate, QObject* parent = nullptr);
    ~WarmupController() override;

    // Why a warm-up cannot start now (empty: it can).
    QString checkStart() const;
    bool start(bool full, const Game& forGame = {}, QString* error = nullptr);
    // Ask it to stop starting compiles; builds in flight finish (can take minutes for giants).
    void cancel();
    // Kill the warm-up tree now (what is built stays built).
    void forceStop();

    State state() const { return m_state; }
    bool isActive() const { return m_state == State::Running || m_state == State::PausedForRam; }
    bool cancelRequested() const { return m_cancelRequested; }
    Progress progress() const { return m_progress; }
    Game game() const { return m_game; }
    std::optional<Protocol::WarmupDone> doneLine() const { return m_done; }
    QString errorMessage() const { return m_error; }
    QStringList stderrTail() const { return m_stderr; }
    std::optional<WarmupRun> lastRun() const { return m_lastRun; }

    static QStringList warmupArguments(bool full);
    // Record of a finished process (tested): how the exit code, done line and last progress add up.
    static WarmupRun classify(int exitCode, bool crashed, const std::optional<Protocol::WarmupDone>& done,
                              const Progress& last, const QString& error, bool cancelRequested);

    // Screenshot fixtures only.
    void simulateForScreenshot(State state, const Game& game, const Progress& progress, const QString& error = {});

signals:
    void stateChanged(WarmupController::State state);
    void progressChanged();
    void finished(const WarmupRun& run);

private:
    void setState(State s);
    void onStdout();
    void onStderr();
    void onFinished(int exitCode, int exitStatus);
    void handle(const Protocol::Event& ev);

    AppSettings& m_settings;
    ProcessGate& m_gate;
    QPointer<ProcessTree> m_process;
    State m_state = State::Idle;
    Game m_game;
    Progress m_progress;
    std::optional<Protocol::WarmupDone> m_done;
    std::optional<WarmupRun> m_lastRun;
    QString m_error;
    QStringList m_stderr;
    bool m_cancelRequested = false;
    QElapsedTimer m_clock;
};
