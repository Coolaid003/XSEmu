#include "WarmupController.h"

#include "ProcessGate.h"
#include "ProcessTree.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace {
constexpr int kStderrTail = 40;
}

const QStringList& WarmupController::phases()
{
    static const QStringList p{QStringLiteral("starting"), QStringLiteral("giants"), QStringLiteral("bound"),
                               QStringLiteral("compute"), QStringLiteral("compute_blobs"), QStringLiteral("rest")};
    return p;
}

QString WarmupController::phaseLabel(const QString& phase)
{
    if (phase == QLatin1String("starting")) return QStringLiteral("Starting");
    if (phase == QLatin1String("giants")) return QStringLiteral("Giant shaders");
    if (phase == QLatin1String("bound")) return QStringLiteral("Bound pipelines");
    if (phase == QLatin1String("compute")) return QStringLiteral("Compute programs");
    if (phase == QLatin1String("compute_blobs")) return QStringLiteral("Compute blobs");
    if (phase == QLatin1String("rest")) return QStringLiteral("Other pipelines");
    return phase.isEmpty() ? QStringLiteral("Starting") : phase;
}

int WarmupController::phaseIndex(const QString& phase)
{
    return static_cast<int>(phases().indexOf(phase));
}

WarmupController::WarmupController(AppSettings& settings, ProcessGate& gate, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_gate(gate)
{
}

WarmupController::~WarmupController()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->write(Protocol::Command::cancel());
        m_process->closeWriteChannel();
        if (!m_process->waitForFinished(3000))
            m_process->killTree();
    }
}

QStringList WarmupController::warmupArguments(bool full)
{
    QStringList args{QStringLiteral("warm-up")};
    if (full)
        args << QStringLiteral("--full");
    args << QStringLiteral("--progress-json");
    return args;
}

QString WarmupController::checkStart() const
{
    if (!m_gate.isFree())
        return m_gate.blockedReason(ProcessGate::Action::StartWarmup);
    const QString emulator = m_settings.emulatorPath();
    if (!QFileInfo(emulator).isExecutable())
        return QStringLiteral("XSEmu was not found at %1.").arg(QDir::toNativeSeparators(emulator));
    const QStringList others = runningEmulatorProcesses(QCoreApplication::applicationPid());
    if (!others.isEmpty())
        return QStringLiteral("Another XSEmu process is running (%1). Only one runs at a time.").arg(others.join(QStringLiteral(", ")));
    return {};
}

bool WarmupController::start(bool full, const Game& forGame, QString* error)
{
    const QString why = checkStart();
    if (!why.isEmpty() || !m_gate.acquire(ProcessGate::Owner::Warmup, forGame.title(), [this] { cancel(); })) {
        if (error)
            *error = why.isEmpty() ? m_gate.blockedReason(ProcessGate::Action::StartWarmup) : why;
        return false;
    }
    m_game = forGame;
    m_progress = {};
    m_progress.full = full;
    m_done.reset();
    m_error.clear();
    m_stderr.clear();
    m_cancelRequested = false;
    m_clock.start();

    auto* p = new ProcessTree(this);
    m_process = p;
    p->setWorkingDirectory(QFileInfo(m_settings.emulatorPath()).absolutePath());
    connect(p, &QProcess::readyReadStandardOutput, this, &WarmupController::onStdout);
    connect(p, &QProcess::readyReadStandardError, this, &WarmupController::onStderr);
    connect(p, &QProcess::finished, this, [this](int code, QProcess::ExitStatus s) { onFinished(code, static_cast<int>(s)); });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_error = QStringLiteral("XSEmu could not be started: %1").arg(p->errorString());
            onFinished(-1, static_cast<int>(QProcess::CrashExit));
        }
    });
    setState(State::Running);
    p->startTree(m_settings.emulatorPath(), warmupArguments(full));
    return true;
}

void WarmupController::cancel()
{
    if (!m_process || m_process->state() == QProcess::NotRunning || m_cancelRequested)
        return;
    m_cancelRequested = true;
    m_process->write(Protocol::Command::cancel());
    emit progressChanged();
}

void WarmupController::forceStop()
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    m_cancelRequested = true;
    m_process->killTree();
}

void WarmupController::onStdout()
{
    while (m_process && m_process->canReadLine())
        handle(Protocol::parse(m_process->readLine()));
}

void WarmupController::onStderr()
{
    if (!m_process)
        return;
    const QString text = QString::fromUtf8(m_process->readAllStandardError());
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        m_stderr << line.trimmed();
        if (m_stderr.size() > kStderrTail)
            m_stderr.removeFirst();
    }
}

void WarmupController::handle(const Protocol::Event& ev)
{
    if (const auto* s = std::get_if<Protocol::WarmupStart>(&ev)) {
        m_progress.targets = s->total;
        m_progress.full = s->full;
        m_progress.threads = s->threads;
        emit progressChanged();
    } else if (const auto* p = std::get_if<Protocol::WarmupProgress>(&ev)) {
        m_progress.phase = p->phase;
        m_progress.done = p->done;
        m_progress.total = p->total;
        m_progress.keysPerS = p->keysPerS;
        m_progress.etaS = p->etaS;
        m_progress.ramFreeMb = p->ramFreeMb;
        m_progress.giantsCompiling = p->giantsCompiling;
        m_progress.giantsMax = p->giantsMax;
        m_progress.inFlight = p->inFlight;
        m_progress.failed = p->failed;
        m_progress.pausedForRam = p->pausedForRam;
        m_progress.elapsedS = m_clock.isValid() ? m_clock.elapsed() / 1000.0 : 0;
        setState(p->pausedForRam ? State::PausedForRam : State::Running);
        emit progressChanged();
    } else if (const auto* d = std::get_if<Protocol::WarmupDone>(&ev)) {
        m_done = *d;
    } else if (const auto* e = std::get_if<Protocol::Error>(&ev)) {
        m_error = e->message;
    }
}

WarmupRun WarmupController::classify(int exitCode, bool crashed, const std::optional<Protocol::WarmupDone>& done,
                                     const Progress& last, const QString& error, bool cancelRequested)
{
    WarmupRun r;
    r.exitCode = exitCode;
    r.full = last.full;
    r.total = last.targets;
    r.finishedAt = QDateTime::currentDateTime();
    r.tookS = last.elapsedS;
    r.failed = last.failed;
    if (done) {
        r.done = done->built + done->failed;
        r.failed = done->failed;
        r.tookS = done->tookS;
        if (done->cancelled)
            r.result = WarmupRun::Result::Cancelled;
        else if (done->complete)
            r.result = WarmupRun::Result::Finished;
        else {
            r.result = WarmupRun::Result::StoppedPartWay;
            r.stoppedForRam = last.pausedForRam;
        }
        if (!crashed && (exitCode == 0 || exitCode == 4))
            return r;
    }
    if (cancelRequested && !done) {
        r.result = WarmupRun::Result::Cancelled;
        r.message = QStringLiteral("Stopped before it reported a result.");
        return r;
    }
    r.result = WarmupRun::Result::Failed;
    r.message = error;
    if (r.message.isEmpty())
        r.message = crashed ? QStringLiteral("XSEmu stopped unexpectedly.") : QStringLiteral("XSEmu ended with exit code %1.").arg(exitCode);
    return r;
}

void WarmupController::onFinished(int exitCode, int exitStatus)
{
    if (m_state != State::Running && m_state != State::PausedForRam)
        return;
    onStdout();
    onStderr();
    m_progress.elapsedS = m_clock.isValid() ? m_clock.elapsed() / 1000.0 : m_progress.elapsedS;
    if (m_error.isEmpty() && exitCode != 0 && exitCode != 4 && !m_stderr.isEmpty())
        m_error = m_stderr.last();
    WarmupRun run = classify(exitCode, exitStatus == static_cast<int>(QProcess::CrashExit), m_done, m_progress, m_error, m_cancelRequested);
    run.titleId = m_game.isValid() ? m_game.titleId() : QString();
    run.titleName = m_game.isValid() ? m_game.title() : QString();
    m_lastRun = run;
    m_settings.addRecentWarmup(run);
    if (m_process) {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_gate.release(ProcessGate::Owner::Warmup);
    switch (run.result) {
    case WarmupRun::Result::Finished: setState(State::Finished); break;
    case WarmupRun::Result::Cancelled: setState(State::Cancelled); break;
    case WarmupRun::Result::StoppedPartWay: setState(State::StoppedPartWay); break;
    case WarmupRun::Result::Failed: setState(State::Failed); break;
    }
    emit finished(run);
}

void WarmupController::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

void WarmupController::simulateForScreenshot(State state, const Game& game, const Progress& progress, const QString& error)
{
    m_game = game;
    m_progress = progress;
    m_error = error;
    const bool active = state == State::Running || state == State::PausedForRam;
    if (active && m_gate.isFree())
        m_gate.acquire(ProcessGate::Owner::Warmup, game.title(), [this] { simulateForScreenshot(State::Cancelled, m_game, m_progress); });
    if (!active)
        m_gate.release(ProcessGate::Owner::Warmup);
    setState(state);
    emit progressChanged();
}
