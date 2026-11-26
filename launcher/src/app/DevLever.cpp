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

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
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

EmulatorController::Stats pausedStats()
{
    EmulatorController::Stats st;
    st.heldBytes = qint64(8.6 * 1024 * 1024 * 1024);
    return st;
}


// On X11 the window is grabbed from the screen, so the game's child window (another process) is in
// the picture; QWidget::grab() only sees the launcher's own widgets.
void grabAndQuit(MainWindow& w, const QString& path)
{
    QPixmap shot;
    if (QGuiApplication::platformName() == QLatin1String("xcb") && w.screen()) {
        const QRect g = w.frameGeometry();
        shot = w.screen()->grabWindow(0, g.x(), g.y(), g.width(), g.height());
    } else {
        shot = w.grab();
    }
    const bool ok = shot.save(path);
    QTextStream(ok ? stdout : stderr) << (ok ? "saved " : "cannot save ") << path << Qt::endl;
    QApplication::exit(ok ? 0 : 1);
}

// The in-game states run the real thing: XSEmu (fake-xsemu on Linux) launched through
// EmulatorController with --host-hwnd and --control-pipe, driven to the state, then grabbed.
void playForReal(const DevLever::Options& o, Services& s, MainWindow& w, const Game& game)
{
    const QString& st = o.state;
    const QString scenario = st == QLatin1String("in-game-booting")          ? QStringLiteral("slow_boot")
                           : st == QLatin1String("in-game-crashed")          ? QStringLiteral("crash")
                           : st == QLatin1String("in-game-device-removed")   ? QStringLiteral("device_removed")
                           : st == QLatin1String("in-game-boot-error")       ? QStringLiteral("files_missing")
                                                                             : QStringLiteral("ok");
    qputenv("FAKE_XSEMU_SCENARIO", scenario.toUtf8());
    const QString path = o.grab;
    auto done = std::make_shared<bool>(false);
    auto grabIn = [&w, path, done](int ms) {
        if (*done)
            return;
        *done = true;
        if (!path.isEmpty())
            QTimer::singleShot(ms, &w, [&w, path] { grabAndQuit(w, path); });
    };
    auto* ctx = new QObject(&w);
    if (st == QLatin1String("in-game-booting")) {
        QObject::connect(&s.emulator, &EmulatorController::helloReceived, ctx, [grabIn] { grabIn(900); });
    } else if (st == QLatin1String("in-game-crashed") || st == QLatin1String("in-game-device-removed") || st == QLatin1String("in-game-boot-error")) {
        QObject::connect(&s.emulator, &EmulatorController::exited, ctx, [grabIn] { grabIn(600); });
    } else {
        auto once = std::make_shared<bool>(false);
        QObject::connect(&s.emulator, &EmulatorController::stateChanged, ctx, [&s, &w, st, grabIn, once](EmulatorController::State state) {
            if (state == EmulatorController::State::Paused && st == QLatin1String("in-game-paused"))
                grabIn(600);
            if (state == EmulatorController::State::Paused && st == QLatin1String("in-game-back-to-library"))
                grabIn(600);
            if (state != EmulatorController::State::Running || *once)
                return;
            *once = true;
            if (st == QLatin1String("in-game-overlay"))
                s.emulator.setOverlay(true);
            if (st == QLatin1String("in-game-paused"))
                QTimer::singleShot(1500, &w, [&s] { s.emulator.pause(); });
            else if (st == QLatin1String("in-game-back-to-library"))
                QTimer::singleShot(1500, &w, [&w] { w.leaveInGame(); });
            else if (st == QLatin1String("in-game-fullscreen-hint")) {
                QTimer::singleShot(1200, &w, [&w] { w.toggleFullScreen(); });
                grabIn(2000);
            } else if (st == QLatin1String("in-game-fullscreen")) {
                QTimer::singleShot(800, &w, [&w] { w.toggleFullScreen(); });
                grabIn(5200);
            } else {
                grabIn(2500);
            }
        });
    }
    if (!path.isEmpty()) {
        QTimer::singleShot(30000, &w, [path] {
            QTextStream(stderr) << "timed out waiting for the state; nothing saved to " << path << Qt::endl;
            QApplication::exit(1);
        });
    }
    w.play(game);
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
        QStringLiteral("settings"), QStringLiteral("settings-shaders"), QStringLiteral("settings-folders"),
        QStringLiteral("settings-graphics"),
        QStringLiteral("first-launch"), QStringLiteral("first-launch-network"), QStringLiteral("first-launch-folders"),
        QStringLiteral("error-files-missing"), QStringLiteral("error-not-enough-ram"), QStringLiteral("error-crash"),
        QStringLiteral("error-device-removed"),
        QStringLiteral("in-game-booting"), QStringLiteral("in-game-playing"), QStringLiteral("in-game-overlay"),
        QStringLiteral("in-game-paused"), QStringLiteral("in-game-fitted"), QStringLiteral("in-game-fullscreen-hint"),
        QStringLiteral("in-game-fullscreen"), QStringLiteral("in-game-crashed"), QStringLiteral("in-game-device-removed"),
        QStringLiteral("in-game-boot-error"), QStringLiteral("in-game-back-to-library"),
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
            s.warmup.simulateForScreenshot(WarmupController::State::Failed, signal, p,
                                           QStringLiteral("the driver compile of a giant pipeline failed (exit code 3)"));
        } else if (st == QLatin1String("warmup-finished")) {
            WarmupController::Progress p = runningProgress();
            p.phase = QStringLiteral("rest");
            p.done = p.total;
            s.warmup.simulateForScreenshot(WarmupController::State::Finished, signal, p);
        } else if (st == QLatin1String("warmup-blocked"))
            s.emulator.simulateForScreenshot(EmulatorController::State::Paused, signal, pausedStats());
    } else if (st.startsWith(QLatin1String("settings"))) {
        w.showPage(Page::Settings);
        if (st == QLatin1String("settings-shaders"))
            w.settingsPage()->showSection(SettingsPage::Section::Shaders);
        else if (st == QLatin1String("settings-folders"))
            w.settingsPage()->showSection(SettingsPage::Section::GameFolders);
        else if (st == QLatin1String("settings-graphics"))
            w.settingsPage()->showSection(SettingsPage::Section::Graphics);
    } else if (st == QLatin1String("first-launch-network"))
        w.firstLaunchPage()->setStep(1);
    else if (st == QLatin1String("first-launch-folders"))
        w.firstLaunchPage()->setStep(2);

    w.resize(st == QLatin1String("in-game-fitted") && o.size == QSize(1280, 800) ? QSize(1280, 760) : o.size);
    w.show();
    if (st.startsWith(QLatin1String("in-game"))) {
        playForReal(o, s, w, signal);
        return;
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
            check.needMb = EmulatorController::kRamFloorMb;
            check.freeMb = 640;
            check.reason = QStringLiteral("Not enough free memory to start: %1 needs about 1.0 GB free and this PC has 0.6 GB free.").arg(coast.title());
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

    if (!o.grab.isEmpty())
        QTimer::singleShot(500, &w, [&w, path = o.grab] { grabAndQuit(w, path); });
}

#endif
