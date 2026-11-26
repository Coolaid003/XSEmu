#include "AppSettings.h"
#include "EmulatorController.h"
#include "Format.h"
#include "LaunchConfig.h"
#include "TitleConfig.h"
#include "Paths.h"
#include "ProcessGate.h"
#include "Runner.h"
#include "WarmupController.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestSettings : public QObject {
    Q_OBJECT
private slots:
    void defaultsFollowTheHouseRules();
    void changesAreSignalledAndPersist();
    void recentWarmupsAndPlayTime();
    void paths();
    void gate();
    void runCommandLine();
    void titleConfigAtLaunch();
    void warmupClassification();
    void formats();
};

void TestSettings::defaultsFollowTheHouseRules()
{
    QTemporaryDir dir;
    AppSettings s(dir.filePath(QStringLiteral("s.ini")));
    QCOMPARE(s.defaultDeviceType(), QStringLiteral("lockhart"));
    QVERIFY(s.offline());
    QVERIFY(!s.firstLaunchDone());
    QCOMPARE(s.theme(), AppSettings::ThemeMode::System);
    QCOMPARE(s.launchNeedMb(), 0);
    QCOMPARE(s.librarySort(), QStringLiteral("recent"));
    QVERIFY(!s.artLookups());
    QVERIFY(s.steamGridDbKey().isEmpty());
}

void TestSettings::changesAreSignalledAndPersist()
{
    QTemporaryDir dir;
    const QString ini = dir.filePath(QStringLiteral("s.ini"));
    {
        AppSettings s(ini);
        QSignalSpy spy(&s, &AppSettings::changed);
        s.setOffline(false);
        s.setOffline(false);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QString::fromLatin1(AppSettings::Key::Offline));
        s.setGameFolders({QStringLiteral("/a"), QStringLiteral("/b")});
        s.setTheme(AppSettings::ThemeMode::Dark);
        s.setGameOverride(QStringLiteral("T"), QStringLiteral("overlay"), true);
    }
    AppSettings s(ini);
    QVERIFY(!s.offline());
    QCOMPARE(s.gameFolders(), (QStringList{QStringLiteral("/a"), QStringLiteral("/b")}));
    QCOMPARE(s.theme(), AppSettings::ThemeMode::Dark);
    QCOMPARE(s.gameOverrides(QStringLiteral("T")).value(QStringLiteral("overlay")).toBool(), true);
    s.clearGameOverride(QStringLiteral("T"), QStringLiteral("overlay"));
    QVERIFY(s.gameOverrides(QStringLiteral("T")).isEmpty());
}

void TestSettings::recentWarmupsAndPlayTime()
{
    QTemporaryDir dir;
    AppSettings s(dir.filePath(QStringLiteral("s.ini")));
    for (int i = 0; i < AppSettings::kRecentWarmupLimit + 5; ++i) {
        WarmupRun r;
        r.titleId = QStringLiteral("T%1").arg(i);
        r.done = i;
        r.result = WarmupRun::Result::Cancelled;
        r.finishedAt = QDateTime::currentDateTime();
        s.addRecentWarmup(r);
    }
    const QList<WarmupRun> runs = s.recentWarmups();
    QCOMPARE(runs.size(), AppSettings::kRecentWarmupLimit);
    QCOMPARE(runs.first().titleId, QStringLiteral("T24"));
    QCOMPARE(runs.first().result, WarmupRun::Result::Cancelled);
    const QDateTime end = QDateTime::currentDateTime();
    s.addPlayTime(QStringLiteral("G"), 100, end);
    s.addPlayTime(QStringLiteral("G"), 50, end);
    QCOMPARE(s.playSeconds(QStringLiteral("G")), 150);
    QCOMPARE(s.lastPlayed(QStringLiteral("G")).toSecsSinceEpoch(), end.toSecsSinceEpoch());
}

void TestSettings::paths()
{
    QTemporaryDir dir;
    qputenv("XCL_DATA_ROOT", dir.path().toUtf8());
    qputenv("XCL_DXIL_CACHE", dir.filePath(QStringLiteral("dxil")).toUtf8());
    QCOMPARE(Paths::dataRoot(), dir.path());
    QCOMPARE(Paths::xclToml(QStringLiteral("A.B")), dir.filePath(QStringLiteral("A.B/xcl.toml")));
    QCOMPARE(Paths::logsDir(QStringLiteral("A.B")), dir.filePath(QStringLiteral("A.B/logs")));
    QCOMPARE(Paths::cacheDir(QStringLiteral("A.B")), dir.filePath(QStringLiteral("A.B/cache")));
    QCOMPARE(Paths::pipelineManifestDir(), dir.filePath(QStringLiteral("dxil/pipelines")));
    qunsetenv("XCL_DATA_ROOT");
    qunsetenv("XCL_DXIL_CACHE");
    QVERIFY(Paths::dataRoot().endsWith(QStringLiteral("/XCL")));
}

void TestSettings::gate()
{
    ProcessGate gate;
    int stops = 0;
    QVERIFY(gate.isFree());
    QVERIFY(gate.blockedReason(ProcessGate::Action::StartWarmup).isEmpty());
    QVERIFY(gate.acquire(ProcessGate::Owner::Game, QStringLiteral("Signal Garden"), [&] { ++stops; }));
    QVERIFY(!gate.acquire(ProcessGate::Owner::Warmup, QStringLiteral("x"), [] {}));
    gate.setPaused(true);
    QVERIFY(gate.blockedReason(ProcessGate::Action::StartWarmup).startsWith(QStringLiteral("Signal Garden is paused.")));
    QCOMPARE(gate.stopActionLabel(), QStringLiteral("Stop game"));
    gate.stopOwner();
    QCOMPARE(stops, 1);
    gate.release(ProcessGate::Owner::Warmup);
    QVERIFY(!gate.isFree());
    gate.release(ProcessGate::Owner::Game);
    QVERIFY(gate.isFree());
}

void TestSettings::runCommandLine()
{
    QCOMPARE(EmulatorController::runArguments(QStringLiteral("/games/A"), 0x1a2b3c, QStringLiteral("xsemu-1-1")),
             (QStringList{QStringLiteral("run"), QStringLiteral("--host-hwnd"), QStringLiteral("1a2b3c"), QStringLiteral("--control-pipe"),
                          QStringLiteral("xsemu-1-1"), QDir::toNativeSeparators(QStringLiteral("/games/A"))}));
    QCOMPARE(EmulatorController::runArguments(QStringLiteral("/g"), 0, QString()), (QStringList{QStringLiteral("run"), QDir::toNativeSeparators(QStringLiteral("/g"))}));
    QTemporaryDir dir;
    qputenv("XCL_DATA_ROOT", dir.filePath(QStringLiteral("xcl")).toUtf8());
    AppSettings s(dir.filePath(QStringLiteral("s.ini")));
    QProcessEnvironment base;
    base.insert(QStringLiteral("XCL_NETWORK"), QStringLiteral("1"));
    const QProcessEnvironment off = LaunchConfig::runEnvironment(base, s, QStringLiteral("T"));
    QCOMPARE(off.value(QStringLiteral("XCL_NETWORK")), QStringLiteral("0"));
    QCOMPARE(off.value(QStringLiteral("XCL_STORE")), QStringLiteral("offline-error"));
    QCOMPARE(off.value(QStringLiteral("XCL_FULL_DUMP")), QDir::toNativeSeparators(LaunchConfig::crashDumpDir(QStringLiteral("T"))));
    QVERIFY(LaunchConfig::crashDumpDir(QStringLiteral("T")).endsWith(QStringLiteral("T/logs/dumps")));
    s.setOffline(false);
    s.setStoreOfflineError(false);
    s.setCrashDumps(false);
    base.insert(QStringLiteral("XCL_STORE"), QStringLiteral("offline-error"));
    const QProcessEnvironment on = LaunchConfig::runEnvironment(base, s, QStringLiteral("T"));
    QVERIFY(!on.contains(QStringLiteral("XCL_NETWORK")));
    QVERIFY(!on.contains(QStringLiteral("XCL_STORE")));
    QVERIFY(!on.contains(QStringLiteral("XCL_FULL_DUMP")));
    QCOMPARE(WarmupController::warmupArguments(true), (QStringList{QStringLiteral("warm-up"), QStringLiteral("--full"), QStringLiteral("--progress-json")}));
}

void TestSettings::titleConfigAtLaunch()
{
    QTemporaryDir dir;
    qputenv("XCL_DATA_ROOT", dir.filePath(QStringLiteral("xcl")).toUtf8());
    AppSettings s(dir.filePath(QStringLiteral("s.ini")));
    const QString id = QStringLiteral("Northwind.Title");

    // a fresh title gets the Series S default and nothing the user never chose
    QVariantMap v = LaunchConfig::titleValues(s, id);
    QCOMPARE(v, (QVariantMap{{QStringLiteral("device_type"), QStringLiteral("lockhart")}}));

    // launcher defaults come back with their TOML types after a round trip through the ini
    s.setXclDefault(QStringLiteral("keyboard_mouse"), false);
    s.setXclDefault(QStringLiteral("mouse_sensitivity"), 1.5);
    s.setXclDefault(QStringLiteral("log_level"), QStringLiteral("debug"));
    AppSettings reread(dir.filePath(QStringLiteral("s.ini")));
    v = LaunchConfig::titleValues(reread, id);
    QCOMPARE(v.value(QStringLiteral("keyboard_mouse")).typeId(), int(QMetaType::Bool));
    QCOMPARE(v.value(QStringLiteral("keyboard_mouse")).toBool(), false);
    QCOMPARE(v.value(QStringLiteral("mouse_sensitivity")).typeId(), int(QMetaType::Double));
    QCOMPARE(v.value(QStringLiteral("mouse_sensitivity")).toDouble(), 1.5);

    // a per-game override wins over the default
    s.setDefaultDeviceType(QStringLiteral("lockhart"));
    s.setGameOverride(id, QStringLiteral("device_type"), QStringLiteral("anaconda"));
    QCOMPARE(LaunchConfig::titleValues(s, id).value(QStringLiteral("device_type")).toString(), QStringLiteral("anaconda"));
    s.clearGameOverride(id, QStringLiteral("device_type"));

    // the file keeps the user's other keys and gets the launcher's values
    TitleConfig cfg(Paths::xclToml(id));
    QDir().mkpath(QFileInfo(cfg.path()).absolutePath());
    QFile f(cfg.path());
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# mine\ndevice_type = \"anaconda\"\ngamertag = \"Me\"\nmouse_sensitivity = 1.0\n");
    f.close();
    QVERIFY(LaunchConfig::prepareTitle(s, id));
    const QVariantMap after = cfg.values();
    QCOMPARE(after.value(QStringLiteral("device_type")).toString(), QStringLiteral("lockhart"));
    QCOMPARE(after.value(QStringLiteral("gamertag")).toString(), QStringLiteral("Me"));
    QCOMPARE(after.value(QStringLiteral("keyboard_mouse")).toBool(), false);
    QCOMPARE(after.value(QStringLiteral("mouse_sensitivity")).toDouble(), 1.5);
    QCOMPARE(after.value(QStringLiteral("log_level")).toString(), QStringLiteral("debug"));
    QVERIFY(QFileInfo(LaunchConfig::crashDumpDir(id)).isDir());

    // a per-game choice made on Game details (the overrides store) survives the launch rewrite;
    // launcher-only choices stay out of xcl.toml
    s.setGameOverride(id, QStringLiteral("device_type"), QStringLiteral("anaconda"));
    s.setGameOverride(id, QStringLiteral("log_level"), QStringLiteral("trace"));
    s.setGameOverride(id, QStringLiteral("runner"), QStringLiteral("kelvaroq"));
    QVERIFY(LaunchConfig::prepareTitle(s, id));
    const QVariantMap chosen = cfg.values();
    QCOMPARE(chosen.value(QStringLiteral("device_type")).toString(), QStringLiteral("anaconda"));
    QCOMPARE(chosen.value(QStringLiteral("log_level")).toString(), QStringLiteral("trace"));
    QVERIFY(!chosen.contains(QStringLiteral("runner")));
    // Use defaults clears the store; the next launch writes the defaults back
    for (const char* key : {"device_type", "log_level", "runner"})
        s.clearGameOverride(id, QLatin1String(key));
    QVERIFY(LaunchConfig::prepareTitle(s, id));
    QCOMPARE(cfg.values().value(QStringLiteral("device_type")).toString(), QStringLiteral("lockhart"));
    QCOMPARE(cfg.values().value(QStringLiteral("log_level")).toString(), QStringLiteral("debug"));

    // a runner gets the console from the same store: a Series X override with no xcl.toml at all,
    // and after Use defaults the default, whatever a stale xcl.toml still says
    const QString other = QStringLiteral("Runner.Only");
    QVERIFY(!QFileInfo::exists(Paths::xclToml(other)));
    s.setGameOverride(other, QStringLiteral("device_type"), QStringLiteral("anaconda"));
    QCOMPARE(LaunchConfig::deviceType(s, other), QStringLiteral("anaconda"));
    const RunnerSpec& kelvaroq = *Runners::find(QStringLiteral("kelvaroq"));
    const QStringList args = RunnerSpec::expand(kelvaroq.launchArgs, kelvaroq.tokens(dir.path(), dir.filePath(QStringLiteral("game.exe")), other, LaunchConfig::deviceType(s, other), 0));
    QVERIFY2(args.contains(QStringLiteral("series-x")), qPrintable(args.join(QLatin1Char(' '))));
    QVERIFY(!QFileInfo::exists(Paths::xclToml(other)));
    TitleConfig(Paths::xclToml(other)).set(QStringLiteral("device_type"), QStringLiteral("anaconda"));
    s.clearGameOverride(other, QStringLiteral("device_type"));
    QCOMPARE(LaunchConfig::deviceType(s, other), QStringLiteral("lockhart"));
}

void TestSettings::warmupClassification()
{
    WarmupController::Progress last;
    last.targets = 100;
    Protocol::WarmupDone done{98, 2, 0, 12.5, true, false, 0};
    WarmupRun r = WarmupController::classify(0, false, done, last, {}, false);
    QCOMPARE(r.result, WarmupRun::Result::Finished);
    QCOMPARE(r.done, 100);
    QCOMPARE(r.failed, 2);
    done = {40, 0, 0, 5, false, true, 4};
    QCOMPARE(WarmupController::classify(4, false, done, last, {}, true).result, WarmupRun::Result::Cancelled);
    done = {40, 0, 0, 5, false, false, 0};
    last.pausedForRam = true;
    r = WarmupController::classify(0, false, done, last, {}, false);
    QCOMPARE(r.result, WarmupRun::Result::StoppedPartWay);
    QVERIFY(r.stoppedForRam);
    r = WarmupController::classify(3, false, std::nullopt, last, QStringLiteral("another XSEmu process is running"), false);
    QCOMPARE(r.result, WarmupRun::Result::Failed);
    QCOMPARE(r.exitCode, 3);
    QCOMPARE(r.message, QStringLiteral("another XSEmu process is running"));
    QCOMPARE(WarmupController::classify(9, true, std::nullopt, last, {}, true).result, WarmupRun::Result::Cancelled);
    QCOMPARE(WarmupController::phaseIndex(QStringLiteral("compute_blobs")), 4);
}

void TestSettings::formats()
{
    QCOMPARE(Format::count(41906), QStringLiteral("41,906"));
    QCOMPARE(Format::duration(60), QStringLiteral("1 min 00 s"));
    QCOMPARE(Format::shortDuration(192), QStringLiteral("3m 12s"));
    QCOMPARE(Format::shortDuration(22), QStringLiteral("22s"));
    QCOMPARE(Format::gigabytesFromMb(12288), QStringLiteral("12.0 GB"));
    QCOMPARE(Format::exitCode(3), QStringLiteral("3"));
    QCOMPARE(Format::exitCode(int(0xC0000005u)), QStringLiteral("0xC0000005"));
    const QDateTime now = QDateTime::currentDateTime();
    QCOMPARE(Format::ago(now.addSecs(-30), now), QStringLiteral("Just now"));
    QCOMPARE(Format::ago(now.addDays(-1), now), QStringLiteral("Yesterday"));
}

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
