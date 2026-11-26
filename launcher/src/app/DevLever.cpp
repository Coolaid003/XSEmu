#include "DevLever.h"

#ifdef XSEMU_DEV_TOOLS

#include "Services.h"
#include "core/DevFixtures.h"
#include "core/EmulatorController.h"
#include "pages/FirstLaunchPage.h"
#include "pages/SettingsPage.h"
#include "shell/LaunchErrors.h"
#include "shell/MainWindow.h"
#include "widgets/SearchBox.h"
#include "pages/LibraryPage.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <memory>

namespace {

std::unique_ptr<QTemporaryDir> g_root;

QString root(const QString& sub) { return QDir(g_root->path()).filePath(sub); }

Game findGame(const Services& s, const QString& title)
{
    for (const Game& g : s.library.games()) {
        if (g.title() == title)
            return g;
    }
    return s.library.games().value(0);
}

void seedWarmups(Services& s)
{
    const QDateTime now = QDateTime::currentDateTime();
    auto run = [&](const QString& name, qint64 done, qint64 total, double took, WarmupRun::Result result, qint64 agoS) {
        WarmupRun r;
        const Game g = findGame(s, name);
        r.titleId = g.titleId();
        r.titleName = g.title();
        r.done = done;
        r.total = total;
        r.tookS = took;
        r.result = result;
        r.finishedAt = now.addSecs(-agoS);
        return r;
    };
    WarmupRun stopped = run(QStringLiteral("Signal Garden"), 9874, 39552, 125, WarmupRun::Result::StoppedPartWay, 3 * 86400);
    stopped.stoppedForRam = true;
    WarmupRun failed = run(QStringLiteral("Signal Garden"), 31044, 41906, 160, WarmupRun::Result::Failed, 2 * 86400);
    failed.exitCode = 3;
    s.settings.addRecentWarmup(stopped);
    s.settings.addRecentWarmup(failed);
    s.settings.addRecentWarmup(run(QStringLiteral("Coastline Drive"), 1240, 3120, 22, WarmupRun::Result::Cancelled, 30 * 3600));
    s.settings.addRecentWarmup(run(QStringLiteral("Signal Garden"), 41906, 41906, 192, WarmupRun::Result::Finished, 2 * 3600));
}

WarmupController::Progress runningProgress()
{
    WarmupController::Progress p;
    p.targets = 41906;
    p.threads = 16;
    p.phase = QStringLiteral("bound");
    p.done = 27292;
    p.total = 41906;
    p.keysPerS = 243;
    p.etaS = 60;
    p.ramFreeMb = 12 * 1024;
    p.giantsCompiling = 2;
    p.giantsMax = 3;
    p.inFlight = 14;
    p.elapsedS = 124;
    return p;
}

// A small shader cache and one crash dump in the temporary fixture folders, so Settings has numbers.
void seedShaderCache()
{
    QDir dxil(root(QStringLiteral("dxil")));
    dxil.mkpath(QStringLiteral("pipelines"));
    dxil.mkpath(QStringLiteral("driver/10de-2786-fixture"));
    QFile manifest(dxil.filePath(QStringLiteral("pipelines/pipelines.bin")));
    if (manifest.open(QIODevice::WriteOnly)) {
        for (int i = 0; i < 3120; ++i) {
            const QByteArray entry = QByteArray("fixture pipeline ") + QByteArray::number(i) + QByteArray(64, 'x');
            const quint32 n = quint32(entry.size());
            const char len[4] = {char(n & 0xff), char((n >> 8) & 0xff), char((n >> 16) & 0xff), char((n >> 24) & 0xff)};
            manifest.write(len, 4);
            manifest.write(entry);
        }
    }
    for (int i = 0; i < 40; ++i) {
        QFile blob(dxil.filePath(QStringLiteral("driver/10de-2786-fixture/%1.pso").arg(i)));
        if (blob.open(QIODevice::WriteOnly))
            blob.write(QByteArray(256 * 1024, '\0'));
    }
}

void seedCrashDump(const Game& game)
{
    const QString dir = QDir(root(QStringLiteral("xcl"))).filePath(game.titleId() + QStringLiteral("/logs/dumps"));
    QDir().mkpath(dir);
    QFile dump(QDir(dir).filePath(QStringLiteral("xcl-small-4242.dmp")));
    if (dump.open(QIODevice::WriteOnly))
        dump.write(QByteArray(3 * 1024 * 1024 + 123456, '\0'));
}

EmulatorController::Stats pausedStats()
{
    EmulatorController::Stats st;
    st.heldBytes = qint64(8.6 * 1024 * 1024 * 1024);
    return st;
}

}

QStringList DevLever::states()
{
    return {
        QStringLiteral("library"), QStringLiteral("library-empty"), QStringLiteral("library-search"),
        QStringLiteral("library-search-none"), QStringLiteral("library-paused"),
        QStringLiteral("details"),
        QStringLiteral("warmup-idle"), QStringLiteral("warmup-running"), QStringLiteral("warmup-paused-ram"),
        QStringLiteral("warmup-failed"), QStringLiteral("warmup-finished"), QStringLiteral("warmup-blocked"),
        QStringLiteral("warmup-cancelled"), QStringLiteral("warmup-stopped"),
        QStringLiteral("settings"), QStringLiteral("settings-network"), QStringLiteral("settings-shaders"),
        QStringLiteral("settings-shaders-clear"), QStringLiteral("settings-overlay"), QStringLiteral("settings-folders"),
        QStringLiteral("settings-input"), QStringLiteral("settings-graphics"), QStringLiteral("settings-logs"),
        QStringLiteral("settings-about"),
        QStringLiteral("first-launch"), QStringLiteral("first-launch-network"), QStringLiteral("first-launch-folders"),
        QStringLiteral("error-files-missing"), QStringLiteral("error-not-enough-ram"), QStringLiteral("error-crash"),
        QStringLiteral("error-device-removed"),
        QStringLiteral("in-game-booting"), QStringLiteral("in-game-paused"),
    };
}

bool DevLever::parse(const QStringList& args, Options* out, QString* error)
{
    bool any = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args[i];
        auto value = [&]() -> QString {
            if (i + 1 >= args.size()) {
                *error = QStringLiteral("%1 needs a value").arg(a);
                return {};
            }
            return args[++i];
        };
        if (a == QLatin1String("--list-states")) {
            out->listStates = true;
            any = true;
        } else if (a == QLatin1String("--theme")) {
            out->theme = value();
            if (out->theme != QLatin1String("light") && out->theme != QLatin1String("dark"))
                *error = QStringLiteral("--theme takes light or dark");
            any = true;
        } else if (a == QLatin1String("--size")) {
            const QStringList wh = value().split(QLatin1Char('x'));
            out->size = wh.size() == 2 ? QSize(wh[0].toInt(), wh[1].toInt()) : QSize();
            if (out->size.width() < 640 || out->size.height() < 400)
                *error = QStringLiteral("--size takes WIDTHxHEIGHT, at least 640x400");
            any = true;
        } else if (a == QLatin1String("--state")) {
            out->state = value();
            if (!states().contains(out->state))
                *error = QStringLiteral("unknown state %1 (see --list-states)").arg(out->state);
            any = true;
        } else if (a == QLatin1String("--grab")) {
            out->grab = value();
            any = true;
        }
        if (!error->isEmpty())
            return false;
    }
    if (any && out->state.isEmpty() && !out->listStates)
        out->state = QStringLiteral("library");
    return any;
}

bool DevLever::prepare(QString* error)
{
    g_root = std::make_unique<QTemporaryDir>();
    if (!g_root->isValid()) {
        *error = QStringLiteral("cannot create a temporary folder");
        return false;
    }
    const QString fixtures = QStringLiteral(XSEMU_FIXTURES_DIR);
    if (!DevFixtures::materialize(QDir(fixtures).filePath(QStringLiteral("games")), root(QStringLiteral("games")))
        || !DevFixtures::materialize(QDir(fixtures).filePath(QStringLiteral("xcl-data")), root(QStringLiteral("xcl")))) {
        *error = QStringLiteral("cannot copy the fixtures from %1").arg(fixtures);
        return false;
    }
    QDir().mkpath(root(QStringLiteral("dxil")));
    qputenv("XSEMU_LAUNCHER_DATA", root(QStringLiteral("launcher")).toUtf8());
    qputenv("XCL_DATA_ROOT", root(QStringLiteral("xcl")).toUtf8());
    qputenv("XCL_DXIL_CACHE", root(QStringLiteral("dxil")).toUtf8());
    return true;
}

void DevLever::apply(const Options& o, Services& s, MainWindow& w)
{
    s.settings.setTheme(o.theme == QLatin1String("dark") ? AppSettings::ThemeMode::Dark : AppSettings::ThemeMode::Light);
    s.settings.setDefaultDeviceType(QStringLiteral("lockhart"));
    s.settings.setOffline(true);
    const bool firstLaunch = o.state.startsWith(QLatin1String("first-launch"));
    s.settings.setGameFolders(o.state == QLatin1String("library-empty") || o.state == QLatin1String("first-launch")
                                  || o.state == QLatin1String("first-launch-network")
                                  ? QStringList()
                                  : QStringList{root(QStringLiteral("games"))});
    s.library.rescanNow();
    s.settings.setFirstLaunchDone(!firstLaunch);
    if (!firstLaunch && o.state != QLatin1String("library-empty"))
        seedWarmups(s);

    const Game signal = findGame(s, QStringLiteral("Signal Garden"));
    const Game coast = findGame(s, QStringLiteral("Coastline Drive"));
    const QString& st = o.state;

    if (st == QLatin1String("library-search"))
        w.libraryPage()->searchBox()->setText(QStringLiteral("coast"));
    else if (st == QLatin1String("library-search-none"))
        w.libraryPage()->searchBox()->setText(QStringLiteral("racing"));
    else if (st == QLatin1String("library-paused"))
        s.emulator.simulateForScreenshot(EmulatorController::State::Paused, signal, pausedStats());
    else if (st == QLatin1String("details"))
        w.showGameDetails(signal);
    else if (st.startsWith(QLatin1String("warmup"))) {
        w.showPage(Page::Warmup);
        if (st == QLatin1String("warmup-running"))
            s.warmup.simulateForScreenshot(WarmupController::State::Running, signal, runningProgress());
        else if (st == QLatin1String("warmup-paused-ram")) {
            WarmupController::Progress p = runningProgress();
            p.pausedForRam = true;
            p.ramFreeMb = 980;
            p.etaS.reset();
            s.warmup.simulateForScreenshot(WarmupController::State::PausedForRam, signal, p);
        } else if (st == QLatin1String("warmup-failed")) {
            WarmupController::Progress p = runningProgress();
            p.done = 31044;
            WarmupRun run;
            run.titleId = signal.titleId();
            run.titleName = signal.title();
            run.done = 31044;
            run.total = 41906;
            run.tookS = 160;
            run.result = WarmupRun::Result::Failed;
            run.exitCode = 3;
            run.finishedAt = QDateTime::currentDateTime();
            s.warmup.simulateForScreenshot(WarmupController::State::Failed, signal, p,
                                           QStringLiteral("warm-up: the driver compile of DXIL 3f2a91c07d1e4b55 crashed (0xC0000005)"), run);
        } else if (st == QLatin1String("warmup-finished")) {
            WarmupController::Progress p = runningProgress();
            p.phase = QStringLiteral("rest");
            p.done = p.total;
            p.ramFreeMb = 11674;
            WarmupRun run;
            run.titleId = signal.titleId();
            run.titleName = signal.title();
            run.done = 41906;
            run.total = 41906;
            run.tookS = 192;
            run.finishedAt = QDateTime::currentDateTime();
            s.warmup.simulateForScreenshot(WarmupController::State::Finished, signal, p, {}, run);
        } else if (st == QLatin1String("warmup-cancelled") || st == QLatin1String("warmup-stopped")) {
            const bool stopped = st == QLatin1String("warmup-stopped");
            WarmupController::Progress p = runningProgress();
            WarmupRun run;
            run.titleId = signal.titleId();
            run.titleName = signal.title();
            run.done = 27292;
            run.total = 41906;
            run.tookS = 124;
            run.result = stopped ? WarmupRun::Result::StoppedPartWay : WarmupRun::Result::Cancelled;
            run.exitCode = stopped ? 0 : 4;
            run.finishedAt = QDateTime::currentDateTime();
            s.warmup.simulateForScreenshot(stopped ? WarmupController::State::StoppedPartWay : WarmupController::State::Cancelled, signal, p, {}, run);
        } else if (st == QLatin1String("warmup-blocked"))
            s.emulator.simulateForScreenshot(EmulatorController::State::Paused, signal, pausedStats());
    } else if (st.startsWith(QLatin1String("settings"))) {
        w.showPage(Page::Settings);
        if (st.startsWith(QLatin1String("settings-shaders"))) {
            seedShaderCache();
            w.settingsPage()->showSection(SettingsPage::Section::Shaders);
        } else if (st == QLatin1String("settings-network"))
            w.settingsPage()->showSection(SettingsPage::Section::Network);
        else if (st == QLatin1String("settings-overlay"))
            w.settingsPage()->showSection(SettingsPage::Section::Overlay);
        else if (st == QLatin1String("settings-folders"))
            w.settingsPage()->showSection(SettingsPage::Section::GameFolders);
        else if (st == QLatin1String("settings-input"))
            w.settingsPage()->showSection(SettingsPage::Section::Input);
        else if (st == QLatin1String("settings-graphics"))
            w.settingsPage()->showSection(SettingsPage::Section::Graphics);
        else if (st == QLatin1String("settings-logs")) {
            seedCrashDump(signal);
            w.settingsPage()->showSection(SettingsPage::Section::Logs);
        } else if (st == QLatin1String("settings-about"))
            w.settingsPage()->showSection(SettingsPage::Section::About);
    } else if (st == QLatin1String("first-launch-network"))
        w.firstLaunchPage()->setStep(1);
    else if (st == QLatin1String("first-launch-folders"))
        w.firstLaunchPage()->setStep(2);
    else if (st == QLatin1String("in-game-booting") || st == QLatin1String("in-game-paused")) {
        s.emulator.simulateForScreenshot(st == QLatin1String("in-game-booting") ? EmulatorController::State::Booting : EmulatorController::State::Paused,
                                         signal, pausedStats());
        w.enterInGame();
    }

    w.resize(o.size);
    w.show();

    if (st == QLatin1String("settings-shaders-clear")) {
        if (auto* clear = w.settingsPage()->sectionPage(SettingsPage::Section::Shaders)->findChild<QAbstractButton*>(QStringLiteral("clearShaderCache")))
            QTimer::singleShot(300, clear, &QAbstractButton::click);
    }

    if (st.startsWith(QLatin1String("error"))) {
        if (st == QLatin1String("error-files-missing")) {
            Game missing = findGame(s, QStringLiteral("Tide Pools"));
            EmulatorController::LaunchCheck check;
            check.problem = EmulatorController::LaunchCheck::Problem::GameFilesMissing;
            LaunchErrors::showLaunchProblem(&w, s, missing, check, [] {});
        } else if (st == QLatin1String("error-not-enough-ram")) {
            EmulatorController::LaunchCheck check;
            check.problem = EmulatorController::LaunchCheck::Problem::NotEnoughRam;
            check.needMb = 6 * 1024 + EmulatorController::kRamFloorMb;
            check.freeMb = 4300;
            check.reason = QStringLiteral("%1 needs about 7.0 GB of free RAM to start, and XSEmu keeps 1 GB free while it runs.").arg(coast.title());
            LaunchErrors::showLaunchProblem(&w, s, coast, check, [] {});
        } else {
            EmulatorController::ExitInfo info;
            info.game = signal;
            if (st == QLatin1String("error-crash")) {
                info.kind = EmulatorController::ExitInfo::Kind::Crash;
                info.exitCode = int(0xC0000005u);
                info.message = QStringLiteral("XSEmu stopped with an access violation while the game was loading.");
            } else {
                info.kind = EmulatorController::ExitInfo::Kind::DeviceRemoved;
                info.exitCode = int(0x887A0006u);
                info.message = QStringLiteral("DXGI_ERROR_DEVICE_HUNG (0x887A0006)");
            }
            LaunchErrors::showExit(&w, info, [] {});
        }
    }

    if (o.grab.isEmpty())
        return;
    const QString path = o.grab;
    QTimer::singleShot(500, &w, [&w, path] {
        const bool ok = w.grab().save(path);
        QTextStream(ok ? stdout : stderr) << (ok ? "saved " : "cannot save ") << path << Qt::endl;
        QApplication::exit(ok ? 0 : 1);
    });
}

#endif
