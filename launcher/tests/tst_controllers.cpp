// EmulatorController and WarmupController against fake-xsemu: real processes, real pipe, real JSON.
#include "AppSettings.h"
#include "DevFixtures.h"
#include "EmulatorBuild.h"
#include "EmulatorController.h"
#include "Library.h"
#include "Paths.h"
#include "ProcessGate.h"
#include "Runner.h"
#include "RunnerManager.h"
#include "TitleConfig.h"
#include "WarmupController.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class TestControllers : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void warmupFinishesThroughEveryPhase();
    void warmupCancel();
    void warmupFailure();
    void warmupRefused();
    void runnerPrecacheRunsEachStep();
    void runnerPrecacheCancelClosesStdin();
    void gameRunPauseResumeStop();
    void gameCrash();
    void gameFilesMissing();
    void oneProcessAtATime();
    void emulatorTooOldIsNeverStarted();
    void busyMarkerBlocksPlayAndWarmup();
    void runningEmulatorProcessBlocks();

private:
    Game fixtureGame(const QString& title) const;
    void installFakeKelvaroq() const;
    QTemporaryDir m_fixtures;
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<AppSettings> m_settings;
    std::unique_ptr<ProcessGate> m_gate;
    GameList m_games;
};

void TestControllers::initTestCase()
{
    qputenv("XSEMU_EMULATOR", XSEMU_FAKE_EMULATOR_PATH);
    qputenv("FAKE_XSEMU_TICK_MS", "5");
    // Kelvaroq at its curated pin on any OS, played by a copy of fake-xsemu (read before the list is first used)
    qputenv("XSEMU_RUNNERS_DIR", m_fixtures.filePath(QStringLiteral("runners")).toUtf8());
    QFile feed(m_fixtures.filePath(QStringLiteral("feed.json")));
    QVERIFY(feed.open(QIODevice::WriteOnly));
    feed.write(R"({"kelvaroq": {"keep": true}})");
    feed.close();
    qputenv("XSEMU_RUNNER_FEED", feed.fileName().toUtf8());
    QVERIFY(DevFixtures::materialize(QStringLiteral(XSEMU_FIXTURES_DIR "/games"), m_fixtures.filePath(QStringLiteral("games"))));
    m_games = Library::scan({m_fixtures.filePath(QStringLiteral("games"))}, 3, false);
    QCOMPARE(m_games.size(), 4);
}

void TestControllers::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    qputenv("XCL_DATA_ROOT", m_dir->filePath(QStringLiteral("xcl")).toUtf8());
    qputenv("FAKE_XSEMU_SCENARIO", "ok");
    m_settings = std::make_unique<AppSettings>(m_dir->filePath(QStringLiteral("settings.ini")));
    m_settings->setLaunchNeedMb(0);
    m_gate = std::make_unique<ProcessGate>();
}

void TestControllers::cleanup()
{
    m_gate.reset();
    m_settings.reset();
    m_dir.reset();
}

Game TestControllers::fixtureGame(const QString& title) const
{
    for (const Game& g : m_games) {
        if (g.title() == title)
            return g;
    }
    return {};
}

void TestControllers::installFakeKelvaroq() const
{
    const RunnerSpec* spec = Runners::find(QStringLiteral("kelvaroq"));
    QVERIFY(spec);
    const QString exe = QDir(spec->installDir()).filePath(spec->executable);
    QDir().mkpath(QFileInfo(exe).absolutePath());
    QFile::remove(exe);
    QVERIFY(QFile::copy(QStringLiteral(XSEMU_FAKE_EMULATOR_PATH), exe));
    QFile rec(QDir(spec->dataDir()).filePath(QStringLiteral("runner.json")));
    QVERIFY(rec.open(QIODevice::WriteOnly | QIODevice::Truncate));
    rec.write(QJsonDocument(QJsonObject{{QStringLiteral("id"), spec->id}, {QStringLiteral("version"), spec->pinnedVersion()}, {QStringLiteral("executable"), spec->executable}}).toJson());
}

void TestControllers::runnerPrecacheRunsEachStep()
{
    installFakeKelvaroq();
    const Game game = fixtureGame(QStringLiteral("Signal Garden"));
    m_settings->setGameOverride(game.titleId(), QStringLiteral("runner"), QStringLiteral("kelvaroq"));
    RunnerManager runners;
    WarmupController w(*m_settings, *m_gate);
    w.setRunners(&runners);
    QCOMPARE(w.runnerFor(game).id, QStringLiteral("kelvaroq"));
    QVERIFY2(w.checkStart(game).isEmpty(), qPrintable(w.checkStart(game)));
    QStringList steps;
    connect(&w, &WarmupController::progressChanged, this, [&] {
        if (steps.isEmpty() || steps.last() != w.progress().phase)
            steps << w.progress().phase;
    });
    QSignalSpy finished(&w, &WarmupController::finished);
    QString error;
    QVERIFY2(w.start(false, game, &error), qPrintable(error));
    QCOMPARE(m_gate->owner(), ProcessGate::Owner::Warmup);
    QVERIFY2(finished.wait(20000), qPrintable(w.stderrTail().join(QLatin1Char('|'))));
    QCOMPARE(w.state(), WarmupController::State::Finished);
    QCOMPARE(steps, (QStringList{QStringLiteral("Checking what to build"), QStringLiteral("Building pipelines")}));
    const WarmupRun run = *w.lastRun();
    QCOMPARE(run.runnerId, QStringLiteral("kelvaroq"));
    QCOMPARE(run.done, 812);
    QCOMPARE(run.total, 812);
    QCOMPARE(run.exitCode, 0);
    QVERIFY(m_gate->isFree());
    QCOMPARE(m_settings->recentWarmups().first().runnerId, QStringLiteral("kelvaroq"));
}

void TestControllers::runnerPrecacheCancelClosesStdin()
{
    qputenv("FAKE_XSEMU_TICK_MS", "40");
    installFakeKelvaroq();
    const Game game = fixtureGame(QStringLiteral("Signal Garden"));
    m_settings->setGameOverride(game.titleId(), QStringLiteral("runner"), QStringLiteral("kelvaroq"));
    RunnerManager runners;
    WarmupController w(*m_settings, *m_gate);
    w.setRunners(&runners);
    QSignalSpy finished(&w, &WarmupController::finished);
    QVERIFY(w.start(false, game));
    QTRY_VERIFY_WITH_TIMEOUT(w.progress().step == 2 && w.progress().done > 0, 10000);
    w.cancel();
    QVERIFY(finished.wait(10000));
    QCOMPARE(w.state(), WarmupController::State::Cancelled);
    QCOMPARE(w.lastRun()->exitCode, 0); // closing stdin let it stop on its own; nothing was killed
    QVERIFY(w.lastRun()->done < 812);
    qputenv("FAKE_XSEMU_TICK_MS", "5");
}

void TestControllers::warmupFinishesThroughEveryPhase()
{
    WarmupController w(*m_settings, *m_gate);
    QStringList phases;
    connect(&w, &WarmupController::progressChanged, this, [&] {
        if (!w.progress().phase.isEmpty() && (phases.isEmpty() || phases.last() != w.progress().phase))
            phases << w.progress().phase;
    });
    QSignalSpy finished(&w, &WarmupController::finished);
    QString error;
    QVERIFY2(w.start(false, fixtureGame(QStringLiteral("Signal Garden")), &error), qPrintable(error));
    QCOMPARE(m_gate->owner(), ProcessGate::Owner::Warmup);
    QVERIFY2(finished.wait(20000), qPrintable(QStringLiteral("state %1 phase %2 done %3 stderr %4").arg(int(w.state())).arg(w.progress().phase).arg(w.progress().done).arg(w.stderrTail().join(QLatin1Char('|')))));
    QCOMPARE(w.state(), WarmupController::State::Finished);
    QCOMPARE(phases, WarmupController::phases());
    QCOMPARE(w.progress().targets, 3120 + 96);
    QVERIFY(m_gate->isFree());
    const QList<WarmupRun> runs = m_settings->recentWarmups();
    QCOMPARE(runs.size(), 1);
    QCOMPARE(runs.first().result, WarmupRun::Result::Finished);
    QCOMPARE(runs.first().titleId, QStringLiteral("NorthwindInteractive.SignalGarden"));
    QVERIFY(runs.first().done > 3000);
}

void TestControllers::warmupCancel()
{
    qputenv("FAKE_XSEMU_TICK_MS", "40");
    WarmupController w(*m_settings, *m_gate);
    QSignalSpy progress(&w, &WarmupController::progressChanged);
    QSignalSpy finished(&w, &WarmupController::finished);
    QVERIFY(w.start(true, {}));
    QVERIFY(progress.wait(5000));
    w.cancel();
    QVERIFY(w.cancelRequested());
    QVERIFY(finished.wait(10000));
    QCOMPARE(w.state(), WarmupController::State::Cancelled);
    QVERIFY(w.doneLine().has_value());
    QVERIFY(w.doneLine()->cancelled);
    QVERIFY(w.progress().full);
    qputenv("FAKE_XSEMU_TICK_MS", "5");
}

void TestControllers::warmupFailure()
{
    qputenv("FAKE_XSEMU_SCENARIO", "warmup_fail");
    WarmupController w(*m_settings, *m_gate);
    QSignalSpy finished(&w, &WarmupController::finished);
    QVERIFY(w.start(false, {}));
    QVERIFY(finished.wait(20000));
    QCOMPARE(w.state(), WarmupController::State::Failed);
    QCOMPARE(w.lastRun()->exitCode, 3);
    QVERIFY2(w.lastRun()->message.contains(QStringLiteral("driver compile")), qPrintable(w.lastRun()->message));
}

void TestControllers::warmupRefused()
{
    qputenv("FAKE_XSEMU_SCENARIO", "warmup_refuse");
    WarmupController w(*m_settings, *m_gate);
    QSignalSpy finished(&w, &WarmupController::finished);
    QVERIFY(w.start(false, {}));
    QVERIFY(finished.wait(10000));
    QCOMPARE(w.state(), WarmupController::State::Failed);
    QCOMPARE(w.lastRun()->exitCode, 3);
    QCOMPARE(w.lastRun()->message, QStringLiteral("another XSEmu process is running (XSEmu.exe 1234)"));
}

void TestControllers::gameRunPauseResumeStop()
{
    EmulatorController emu(*m_settings, *m_gate);
    const Game game = fixtureGame(QStringLiteral("Signal Garden"));
    QVERIFY(emu.checkLaunch(game).ok());
    QSignalSpy states(&emu, &EmulatorController::stateChanged);
    QSignalSpy exited(&emu, &EmulatorController::exited);
    QString error;
    QVERIFY2(emu.start(game, 0, &error), qPrintable(error));
    QCOMPARE(emu.state(), EmulatorController::State::Booting);
    QCOMPARE(TitleConfig(Paths::xclToml(game.titleId())).value(QStringLiteral("device_type")).toString(), QStringLiteral("lockhart"));
    QTRY_COMPARE_WITH_TIMEOUT(emu.state(), EmulatorController::State::Running, 10000);
    QVERIFY(emu.hello().has_value());
    QCOMPARE(emu.hello()->proto, 1);
    QTRY_VERIFY_WITH_TIMEOUT(emu.stats().fps.has_value(), 5000);
    QVERIFY(emu.stats().ramFreeMb.has_value());

    emu.pause();
    QTRY_COMPARE_WITH_TIMEOUT(emu.state(), EmulatorController::State::Paused, 5000);
    QVERIFY(m_gate->ownerPaused());
    QTRY_VERIFY_WITH_TIMEOUT(emu.stats().heldBytes > 0, 5000);
    emu.resume();
    QTRY_COMPARE_WITH_TIMEOUT(emu.state(), EmulatorController::State::Running, 5000);
    emu.setOverlay(true);
    emu.stop();
    QCOMPARE(emu.state(), EmulatorController::State::Stopping);
    QVERIFY(exited.wait(10000));
    const auto info = exited.first().first().value<EmulatorController::ExitInfo>();
    QCOMPARE(info.kind, EmulatorController::ExitInfo::Kind::Stopped);
    QVERIFY(info.sawExitLine);
    QVERIFY(!info.killed);
    QCOMPARE(emu.state(), EmulatorController::State::Idle);
    QVERIFY(m_gate->isFree());
    QVERIFY(m_settings->lastPlayed(game.titleId()).isValid());
}

void TestControllers::gameCrash()
{
    qputenv("FAKE_XSEMU_SCENARIO", "crash");
    EmulatorController emu(*m_settings, *m_gate);
    QSignalSpy exited(&emu, &EmulatorController::exited);
    QVERIFY(emu.start(fixtureGame(QStringLiteral("Coastline Drive"))));
    QVERIFY(exited.wait(15000));
    const auto info = exited.first().first().value<EmulatorController::ExitInfo>();
    QCOMPARE(info.kind, EmulatorController::ExitInfo::Kind::Crash);
    QVERIFY(info.message.contains(QStringLiteral("ACCESS_VIOLATION")));
}

void TestControllers::gameFilesMissing()
{
    EmulatorController emu(*m_settings, *m_gate);
    const Game noExe = fixtureGame(QStringLiteral("Tide Pools"));
    const auto check = emu.checkLaunch(noExe);
    QCOMPARE(check.problem, EmulatorController::LaunchCheck::Problem::GameFilesMissing);
    QVERIFY(check.reason.contains(QStringLiteral("TidePools.exe")));
    QVERIFY(!emu.start(noExe));

    qputenv("FAKE_XSEMU_SCENARIO", "files_missing");
    QSignalSpy exited(&emu, &EmulatorController::exited);
    QVERIFY(emu.start(fixtureGame(QStringLiteral("Signal Garden"))));
    QVERIFY(exited.wait(10000));
    const auto info = exited.first().first().value<EmulatorController::ExitInfo>();
    QCOMPARE(info.kind, EmulatorController::ExitInfo::Kind::Error);
    QCOMPARE(info.exitCode, 2);
    QVERIFY(info.message.contains(QStringLiteral("game files missing")));
}

void TestControllers::oneProcessAtATime()
{
    m_settings->setLaunchNeedMb(1 << 30);
    EmulatorController emu(*m_settings, *m_gate);
    const auto ram = emu.checkLaunch(fixtureGame(QStringLiteral("Signal Garden")));
    QVERIFY2(ram.problem == EmulatorController::LaunchCheck::Problem::NotEnoughRam,
             qPrintable(QStringLiteral("problem %1 need %2 free %3 setting %4: %5").arg(int(ram.problem)).arg(ram.needMb).arg(ram.freeMb).arg(m_settings->launchNeedMb()).arg(ram.reason)));
    QVERIFY(ram.needMb > ram.freeMb);
    m_settings->setLaunchNeedMb(0);

    WarmupController w(*m_settings, *m_gate);
    QVERIFY(emu.start(fixtureGame(QStringLiteral("Signal Garden"))));
    QString error;
    QVERIFY(!w.start(false, {}, &error));
    QVERIFY2(error.contains(QStringLiteral("Stop it to warm up")), qPrintable(error));
    QCOMPARE(emu.checkLaunch(fixtureGame(QStringLiteral("Coastline Drive"))).problem, EmulatorController::LaunchCheck::Problem::Busy);
    QSignalSpy exited(&emu, &EmulatorController::exited);
    m_gate->stopOwner();
    QVERIFY(exited.wait(10000));
    QVERIFY(m_gate->isFree());
    QVERIFY(w.checkStart().isEmpty());
}

void TestControllers::emulatorTooOldIsNeverStarted()
{
    QVERIFY(EmulatorBuild::speaksProtocol(QStringLiteral("XSEmu 0.9.0"), QStringLiteral("run [--control-pipe <name>] ... warm-up [--progress-json]")));
    QVERIFY(!EmulatorBuild::speaksProtocol(QStringLiteral("xcl 0.1.0"), QStringLiteral("--control-pipe --progress-json")));
    QVERIFY(!EmulatorBuild::speaksProtocol(QStringLiteral("XSEmu 0.8.0"), QStringLiteral("run <game folder>")));
    const EmulatorBuild fake = EmulatorBuild::probe(QStringLiteral(XSEMU_FAKE_EMULATOR_PATH));
    QVERIFY2(fake.capable, qPrintable(fake.version));
#ifdef Q_OS_WIN
    QSKIP("the old emulator stand-in is a shell script");
#else
    // an older xcl: answers --version and --help, and would start the game for anything else
    const QString started = m_dir->filePath(QStringLiteral("started"));
    const QString old = m_dir->filePath(QStringLiteral("xcl.exe"));
    QFile script(old);
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write(QStringLiteral("#!/bin/sh\ncase \"$1\" in\n--version) echo 'xcl 0.1.0' ;;\n--help) echo 'usage: xcl run <game folder>' ;;\n*) touch '%1' ;;\nesac\n").arg(started).toUtf8());
    script.close();
    script.setPermissions(script.permissions() | QFileDevice::ExeOwner);
    const QByteArray saved = qgetenv("XSEMU_EMULATOR");
    qputenv("XSEMU_EMULATOR", old.toUtf8());

    const EmulatorBuild build = EmulatorBuild::probe(old);
    QVERIFY(build.found);
    QCOMPARE(build.version, QStringLiteral("xcl 0.1.0"));
    QVERIFY(!build.capable);
    EmulatorController emu(*m_settings, *m_gate);
    const Game game = fixtureGame(QStringLiteral("Signal Garden"));
    const auto check = emu.checkLaunch(game);
    QCOMPARE(check.problem, EmulatorController::LaunchCheck::Problem::EmulatorTooOld);
    QVERIFY2(check.reason.contains(QStringLiteral("too old")), qPrintable(check.reason));
    QString error;
    QVERIFY(!emu.start(game, 0, &error));
    WarmupController w(*m_settings, *m_gate);
    QVERIFY2(w.checkStart(game).contains(QStringLiteral("too old")), qPrintable(w.checkStart(game)));
    QVERIFY(!w.start(false, game, &error));
    QTest::qWait(200);
    QVERIFY(!QFile::exists(started));
    QVERIFY(m_gate->isFree());
    qputenv("XSEMU_EMULATOR", saved);
#endif
}

void TestControllers::busyMarkerBlocksPlayAndWarmup()
{
    const QString marker = m_dir->filePath(QStringLiteral("game-pending"));
    m_settings->setBusyMarker(marker);
    QCOMPARE(m_settings->busyMarker(), marker);
    qputenv("XSEMU_BUSY_MARKER", "/elsewhere/marker");
    QCOMPARE(m_settings->busyMarker(), QStringLiteral("/elsewhere/marker"));
    qunsetenv("XSEMU_BUSY_MARKER");
    m_gate->setBusyMarker(m_settings->busyMarker());

    EmulatorController emu(*m_settings, *m_gate);
    WarmupController w(*m_settings, *m_gate);
    const Game game = fixtureGame(QStringLiteral("Signal Garden"));
    QVERIFY(emu.checkLaunch(game).ok());
    QSignalSpy changed(m_gate.get(), &ProcessGate::changed);
    QFile f(marker);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    QVERIFY(changed.wait(3000));
    const auto check = emu.checkLaunch(game);
    QCOMPARE(check.problem, EmulatorController::LaunchCheck::Problem::OtherEmulatorRunning);
    QCOMPARE(check.marker, marker);
    QVERIFY2(check.reason.startsWith(QStringLiteral("Emulator busy")), qPrintable(check.reason));
    QVERIFY(w.checkStart(game).startsWith(QStringLiteral("Emulator busy")));
    QString error;
    QVERIFY(!emu.start(game, 0, &error));
    QVERIFY(!w.start(false, game, &error));
    QVERIFY(m_gate->isFree());
    QVERIFY(QFile::remove(marker));
    QVERIFY(changed.wait(3000));
    QVERIFY(emu.checkLaunch(game).ok());
    QVERIFY(w.checkStart(game).isEmpty());
}

void TestControllers::runningEmulatorProcessBlocks()
{
#ifdef Q_OS_WIN
    QSKIP("needs a long-running stand-in named xcl.exe");
#else
    const QString sleep = QStandardPaths::findExecutable(QStringLiteral("sleep"));
    if (sleep.isEmpty())
        QSKIP("no sleep binary");
    const QString xcl = m_dir->filePath(QStringLiteral("xcl.exe"));
    QVERIFY(QFile::copy(sleep, xcl));
    QSignalSpy changed(m_gate.get(), &ProcessGate::changed);
    QProcess p;
    p.start(xcl, {QStringLiteral("30")});
    QVERIFY(p.waitForStarted());
    EmulatorController emu(*m_settings, *m_gate);
    const auto check = emu.checkLaunch(fixtureGame(QStringLiteral("Signal Garden")));
    QCOMPARE(check.problem, EmulatorController::LaunchCheck::Problem::OtherEmulatorRunning);
    QVERIFY2(check.processes.contains(QStringLiteral("xcl.exe %1").arg(p.processId())), qPrintable(check.processes.join(QLatin1Char(','))));
    QVERIFY(changed.wait(3000));
    p.kill();
    p.waitForFinished();
    QVERIFY(emu.checkLaunch(fixtureGame(QStringLiteral("Signal Garden"))).ok());
#endif
}

QTEST_GUILESS_MAIN(TestControllers)
#include "tst_controllers.moc"
