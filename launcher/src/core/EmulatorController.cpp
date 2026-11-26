#include "EmulatorController.h"

#include "AppSettings.h"
#include "HardwareInfo.h"
#include "Paths.h"
#include "ProcessGate.h"
#include "ProcessTree.h"
#include "LaunchConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>

#include <atomic>

namespace {
std::atomic<int> g_pipeCounter{0};

bool GameManifestMissing(const Game& game)
{
    return !QFileInfo(game.configPath).isFile();
}
}

EmulatorController::EmulatorController(AppSettings& settings, ProcessGate& gate, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_gate(gate)
{
    qRegisterMetaType<EmulatorController::ExitInfo>();
    m_killTimer.setSingleShot(true);
    m_killTimer.setInterval(kStopGraceMs);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            m_killed = true;
            m_process->killTree();
        }
    });
    m_memoryTimer.setInterval(2000);
    connect(&m_memoryTimer, &QTimer::timeout, this, &EmulatorController::pollMemory);
}

EmulatorController::~EmulatorController()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->killTree();
        m_process->waitForFinished(2000);
    }
}

qint64 EmulatorController::launchNeedMb() const
{
    return m_settings.launchNeedMb() + kRamFloorMb;
}

EmulatorController::LaunchCheck EmulatorController::checkLaunch(const Game& game) const
{
    LaunchCheck c;
    if (!m_gate.isFree()) {
        c.problem = LaunchCheck::Problem::Busy;
        c.reason = m_gate.blockedReason(ProcessGate::Action::LaunchGame);
        return c;
    }
    const QString emulator = m_settings.emulatorPath();
    if (!QFileInfo(emulator).isExecutable()) {
        c.problem = LaunchCheck::Problem::EmulatorMissing;
        c.reason = QStringLiteral("XSEmu was not found at %1.").arg(QDir::toNativeSeparators(emulator));
        return c;
    }
    if (!QFileInfo(game.folder).isDir() || GameManifestMissing(game)) {
        c.problem = LaunchCheck::Problem::GameFilesMissing;
        c.reason = QStringLiteral("The game folder %1 is missing or has no MicrosoftGame.config.").arg(QDir::toNativeSeparators(game.folder));
        return c;
    }
    if (!game.exeExists) {
        c.problem = LaunchCheck::Problem::GameFilesMissing;
        c.reason = game.executables.isEmpty()
            ? QStringLiteral("MicrosoftGame.config names no executable.")
            : QStringLiteral("MicrosoftGame.config names %1, but there is no such file in the game folder.").arg(game.executables.join(QStringLiteral(", ")));
        return c;
    }
    const QStringList others = runningEmulatorProcesses(QCoreApplication::applicationPid());
    if (!others.isEmpty()) {
        c.problem = LaunchCheck::Problem::OtherEmulatorRunning;
        c.processes = others;
        c.reason = QStringLiteral("Another XSEmu process is running (%1). Only one runs at a time.").arg(others.join(QStringLiteral(", ")));
        return c;
    }
    const quint64 avail = HardwareInfo::availableRam();
    c.needMb = launchNeedMb();
    c.freeMb = static_cast<qint64>(avail / (1024 * 1024));
    if (avail > 0 && c.freeMb < c.needMb) {
        c.problem = LaunchCheck::Problem::NotEnoughRam;
        c.reason = QStringLiteral("Not enough free memory to start: %1 needs about %2 GB free and this PC has %3 GB free.")
                       .arg(game.title(), QString::number(c.needMb / 1024.0, 'f', 1), QString::number(c.freeMb / 1024.0, 'f', 1));
    }
    return c;
}

QString EmulatorController::hwndToHex(quintptr hwnd)
{
    return QString::number(static_cast<qulonglong>(hwnd), 16);
}

QStringList EmulatorController::runArguments(const QString& gameFolder, quintptr hostWindow, const QString& pipeName)
{
    QStringList args{QStringLiteral("run")};
    if (hostWindow != 0)
        args << QStringLiteral("--host-hwnd") << hwndToHex(hostWindow);
    if (!pipeName.isEmpty())
        args << QStringLiteral("--control-pipe") << pipeName;
    args << QDir::toNativeSeparators(gameFolder);
    return args;
}

bool EmulatorController::start(const Game& game, quintptr hostWindow, QString* error)
{
    const LaunchCheck check = checkLaunch(game);
    if (!check.ok()) {
        if (error)
            *error = check.reason;
        return false;
    }
    if (!m_gate.acquire(ProcessGate::Owner::Game, game.title(), [this] { stop(); })) {
        if (error)
            *error = m_gate.blockedReason(ProcessGate::Action::LaunchGame);
        return false;
    }

    LaunchConfig::prepareTitle(m_settings, game.titleId());

    m_game = game;
    m_stats = {};
    m_hello.reset();
    m_exitLine.reset();
    m_buffer.clear();
    m_stopRequested = false;
    m_killed = false;
    m_playedMs = 0;
    m_playClock.invalidate();
    m_overlay = m_settings.overlayDefault();

    m_pipeName = QStringLiteral("xsemu-%1-%2").arg(QCoreApplication::applicationPid()).arg(++g_pipeCounter);
    delete m_server;
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(m_pipeName);
    if (!m_server->listen(m_pipeName)) {
        if (error)
            *error = QStringLiteral("Could not open the control pipe: %1").arg(m_server->errorString());
        m_gate.release(ProcessGate::Owner::Game);
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &EmulatorController::onNewConnection);

    auto* p = new ProcessTree(this);
    m_process = p;
    p->setProcessEnvironment(LaunchConfig::runEnvironment(QProcessEnvironment::systemEnvironment(), m_settings, game.titleId()));
    p->setWorkingDirectory(QFileInfo(m_settings.emulatorPath()).absolutePath());
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::readyReadStandardOutput, this, [this, p] {
        while (p->canReadLine())
            emit logLine(QString::fromUtf8(p->readLine()).trimmed());
    });
    connect(p, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) { onFinished(code, static_cast<int>(status)); });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            onFinished(-1, static_cast<int>(QProcess::CrashExit));
        Q_UNUSED(p);
    });

    setState(State::Booting);
    p->startTree(m_settings.emulatorPath(), runArguments(game.folder, hostWindow, m_pipeName));
    m_memoryTimer.start();
    return true;
}

void EmulatorController::send(const QByteArray& line)
{
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState)
        m_socket->write(line);
}

void EmulatorController::pause()
{
    if (m_state == State::Running || m_state == State::Booting)
        send(Protocol::Command::pause());
}

void EmulatorController::resume()
{
    if (m_state == State::Paused)
        send(Protocol::Command::resume());
}

void EmulatorController::setOverlay(bool on)
{
    m_overlay = on;
    send(Protocol::Command::overlay(on));
    emit statsChanged();
}

void EmulatorController::focusGame()
{
    send(Protocol::Command::focus());
}

void EmulatorController::stop()
{
    if (!m_process || m_process->state() == QProcess::NotRunning || m_stopRequested)
        return;
    m_stopRequested = true;
    setState(State::Stopping);
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState)
        send(Protocol::Command::stop());
    else
        m_process->terminate();
    m_killTimer.start();
}

void EmulatorController::onNewConnection()
{
    while (QLocalSocket* s = m_server->nextPendingConnection()) {
        if (m_socket) {
            s->disconnectFromServer();
            s->deleteLater();
            continue;
        }
        m_socket = s;
        connect(s, &QLocalSocket::readyRead, this, &EmulatorController::onSocketReadyRead);
        connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
        if (m_overlay)
            send(Protocol::Command::overlay(true));
        if (m_stopRequested)
            send(Protocol::Command::stop());
    }
}

void EmulatorController::onSocketReadyRead()
{
    if (!m_socket)
        return;
    m_buffer += m_socket->readAll();
    qsizetype nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        handle(Protocol::parse(line));
    }
}

void EmulatorController::handle(const Protocol::Event& ev)
{
    if (const auto* h = std::get_if<Protocol::Hello>(&ev)) {
        m_hello = *h;
        emit helloReceived();
    } else if (const auto* s = std::get_if<Protocol::State>(&ev)) {
        if (m_stopRequested)
            return;
        switch (s->state) {
        case Protocol::GameState::Booting: setState(State::Booting); break;
        case Protocol::GameState::Running:
            if (!m_playClock.isValid())
                m_playClock.start();
            setState(State::Running);
            break;
        case Protocol::GameState::Paused: setState(State::Paused); break;
        case Protocol::GameState::Stopping: setState(State::Stopping); break;
        case Protocol::GameState::Unknown: break;
        }
    } else if (const auto* st = std::get_if<Protocol::Stats>(&ev)) {
        const qint64 held = m_stats.heldBytes;
        m_stats = Stats{st->fps, st->frameMs, st->ramFreeMb, st->vramUsedMb, st->vramBudgetMb, st->stalls, st->worstMs, st->pipelines, st->compiling, held};
        emit statsChanged();
    } else if (const auto* k = std::get_if<Protocol::Key>(&ev)) {
        emit keyPressed(k->key);
    } else if (const auto* e = std::get_if<Protocol::Exit>(&ev)) {
        m_exitLine = *e;
    }
}

void EmulatorController::pollMemory()
{
    if (!m_process)
        return;
    const qint64 held = m_process->heldBytes();
    if (held != m_stats.heldBytes) {
        m_stats.heldBytes = held;
        emit statsChanged();
    }
}

void EmulatorController::onFinished(int exitCode, int exitStatus)
{
    if (m_state == State::Idle)
        return;
    // read whatever the pipe still holds (the exit line is the last thing written)
    if (m_socket) {
        m_socket->waitForReadyRead(50);
        onSocketReadyRead();
    }
    m_killTimer.stop();
    m_memoryTimer.stop();

    ExitInfo info;
    info.game = m_game;
    info.exitCode = exitCode;
    info.killed = m_killed;
    info.at = QDateTime::currentDateTime();
    if (m_playClock.isValid())
        info.playedSeconds = m_playClock.elapsed() / 1000;
    if (m_exitLine) {
        info.sawExitLine = true;
        info.message = m_exitLine->message;
        switch (m_exitLine->reason) {
        case Protocol::ExitReason::Stopped: info.kind = ExitInfo::Kind::Stopped; break;
        case Protocol::ExitReason::TitleExit: info.kind = ExitInfo::Kind::TitleExit; break;
        case Protocol::ExitReason::Crash: info.kind = ExitInfo::Kind::Crash; break;
        case Protocol::ExitReason::DeviceRemoved: info.kind = ExitInfo::Kind::DeviceRemoved; break;
        case Protocol::ExitReason::Error: info.kind = ExitInfo::Kind::Error; break;
        case Protocol::ExitReason::Unknown: info.kind = ExitInfo::Kind::Crash; break;
        }
        if (m_exitLine->code != 0 && exitCode == 0)
            info.exitCode = m_exitLine->code;
    } else if (m_stopRequested) {
        info.kind = ExitInfo::Kind::Stopped;
    } else if (exitCode == -1 && exitStatus == static_cast<int>(QProcess::CrashExit) && m_process && m_process->error() == QProcess::FailedToStart) {
        info.kind = ExitInfo::Kind::Error;
        info.message = QStringLiteral("XSEmu could not be started: %1").arg(m_process->errorString());
    } else {
        info.kind = ExitInfo::Kind::Crash;
    }

    if (m_playClock.isValid())
        m_settings.addPlayTime(m_game.titleId(), info.playedSeconds, info.at);

    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    if (m_process) {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_lastExit = info;
    m_gate.release(ProcessGate::Owner::Game);
    setState(State::Idle);
    emit exited(info);
}

void EmulatorController::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    m_gate.setPaused(s == State::Paused);
    emit stateChanged(s);
}

void EmulatorController::simulateForScreenshot(State state, const Game& game, const Stats& stats)
{
    m_game = game;
    m_stats = stats;
    if (state != State::Idle && m_gate.isFree())
        m_gate.acquire(ProcessGate::Owner::Game, game.title(), [this] { simulateForScreenshot(State::Idle, m_game, {}); });
    if (state == State::Idle)
        m_gate.release(ProcessGate::Owner::Game);
    setState(state);
    emit statsChanged();
}
