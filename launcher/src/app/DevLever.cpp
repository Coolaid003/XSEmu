#include "DevLever.h"

#ifdef XSEMU_DEV_TOOLS

#include "Services.h"
#include "core/DevFixtures.h"
#include "core/EmulatorController.h"
#include "core/Paths.h"
#include "core/TitleConfig.h"
#include "pages/FirstLaunchPage.h"
#include "pages/SettingsPage.h"
#include "shell/LaunchErrors.h"
#include "shell/MainWindow.h"
#include "widgets/SearchBox.h"
#include "pages/LibraryPage.h"

#include <QApplication>
#include <QDir>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <cmath>
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

// ---- Library, First launch, Game details and launch errors (fixture art and records) ----

bool isLibraryState(const QString& st)
{
    return st == QLatin1String("library") || st.startsWith(QLatin1String("library-")) || st.startsWith(QLatin1String("details"))
        || st == QLatin1String("cold-cache") || st == QLatin1String("error-files-missing") || st == QLatin1String("error-not-enough-ram");
}

// Invented, abstract art for the fixture titles (no real game art): a dusk sky over the sea for
// Coastline Drive, a night beam for The Long Quiet. Painted at runtime into the temporary cover cache.
QImage paintArt(const QString& kind, QSize size)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal w = size.width();
    const qreal h = size.height();
    if (kind == QLatin1String("coast")) {
        QLinearGradient sky(0, 0, 0, h);
        sky.setColorAt(0, QColor(0x2B, 0x2D, 0x6E));
        sky.setColorAt(0.45, QColor(0xE0, 0x6C, 0x7A));
        sky.setColorAt(0.62, QColor(0xF6, 0xB3, 0x6B));
        sky.setColorAt(0.63, QColor(0x1F, 0x4E, 0x79));
        sky.setColorAt(1, QColor(0x0E, 0x22, 0x3D));
        p.fillRect(img.rect(), sky);
        const qreal horizon = h * 0.625;
        const qreal r = qMin(w, h) * 0.16;
        QRadialGradient sun(w * 0.68, horizon - r * 0.35, r * 1.8);
        sun.setColorAt(0, QColor(0xFF, 0xE7, 0xA8));
        sun.setColorAt(0.5, QColor(0xFF, 0xC2, 0x7A, 160));
        sun.setColorAt(1, QColor(0xFF, 0xC2, 0x7A, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(sun);
        p.drawEllipse(QPointF(w * 0.68, horizon - r * 0.35), r * 1.8, r * 1.8);
        p.setBrush(QColor(0xFF, 0xE9, 0xB5));
        p.drawEllipse(QPointF(w * 0.68, horizon - r * 0.35), r, r);
        p.fillRect(QRectF(0, horizon, w, h - horizon), QColor(0x1F, 0x4E, 0x79));
        for (int i = 0; i < 14; ++i) {
            const qreal y = horizon + 6 + i * (h - horizon) / 14.0;
            p.fillRect(QRectF(w * 0.68 - r * (1.2 - i * 0.05), y, r * (2.4 - i * 0.1), 3), QColor(0xFF, 0xD3, 0x92, 150 - i * 9));
        }
        QPainterPath hills;
        hills.moveTo(0, horizon);
        hills.cubicTo(w * 0.15, horizon - h * 0.09, w * 0.3, horizon - h * 0.05, w * 0.42, horizon);
        hills.closeSubpath();
        p.setBrush(QColor(0x2A, 0x1F, 0x45));
        p.drawPath(hills);
        QPainterPath road;
        road.moveTo(w * 0.05, h);
        road.cubicTo(w * 0.25, horizon + (h - horizon) * 0.4, w * 0.35, horizon + 8, w * 0.42, horizon + 2);
        road.lineTo(w * 0.44, horizon + 2);
        road.cubicTo(w * 0.4, horizon + 12, w * 0.4, horizon + (h - horizon) * 0.5, w * 0.32, h);
        road.closeSubpath();
        p.setBrush(QColor(0x14, 0x14, 0x24));
        p.drawPath(road);
    } else {
        QLinearGradient sky(0, 0, 0, h);
        sky.setColorAt(0, QColor(0x07, 0x1B, 0x26));
        sky.setColorAt(1, QColor(0x12, 0x4A, 0x55));
        p.fillRect(img.rect(), sky);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 60; ++i)
            p.fillRect(QRectF(std::fmod(i * 97.3, w), std::fmod(i * 53.7, h * 0.55), 2, 2), QColor(255, 255, 255, 60 + (i * 37) % 120));
        const QPointF lamp(w * 0.62, h * 0.48);
        QPainterPath beam;
        beam.moveTo(lamp);
        beam.lineTo(0, h * 0.3);
        beam.lineTo(0, h * 0.42);
        beam.closeSubpath();
        p.setBrush(QColor(0xFF, 0xF1, 0xC4, 70));
        p.drawPath(beam);
        p.setBrush(QColor(0xE9, 0xEE, 0xF0));
        p.drawRect(QRectF(lamp.x() - w * 0.035, lamp.y(), w * 0.07, h * 0.3));
        p.setBrush(QColor(0xC2, 0x3B, 0x3B));
        p.drawRect(QRectF(lamp.x() - w * 0.035, lamp.y() + h * 0.1, w * 0.07, h * 0.05));
        p.setBrush(QColor(0xFF, 0xF1, 0xC4));
        p.drawEllipse(lamp, w * 0.03, w * 0.03);
        p.fillRect(QRectF(0, h * 0.78, w, h * 0.22), QColor(0x05, 0x12, 0x1A));
    }
    return img;
}

void seedArt(Services& s, const Game& g, const QString& kind, bool hero)
{
    if (!g.isValid())
        return;
    const QString dir = s.covers.cache().dir();
    QDir().mkpath(dir);
    CoverCache::Entry e;
    e.name = g.title();
    e.fetchedAt = QDateTime::currentDateTime();
    e.grid = CoverCache::fileName(g.titleId(), CoverCache::Kind::Grid, QStringLiteral("png"));
    paintArt(kind, QSize(600, 900)).save(QDir(dir).filePath(e.grid));
    if (hero) {
        e.hero = CoverCache::fileName(g.titleId(), CoverCache::Kind::Hero, QStringLiteral("png"));
        paintArt(kind, QSize(1920, 620)).save(QDir(dir).filePath(e.hero));
    }
    s.covers.cache().put(g.titleId(), e);
}

// Three games as in the design: one played last with art and ready shaders, one with art and a
// warm-up that stopped part way, one without art that was never warmed up.
void seedLibrary(Services& s)
{
    const QDateTime now = QDateTime::currentDateTime();
    const Game coast = findGame(s, QStringLiteral("Coastline Drive"));
    const Game quiet = findGame(s, QStringLiteral("The Long Quiet Between Lighthouses"));
    seedArt(s, coast, QStringLiteral("coast"), true);
    seedArt(s, quiet, QStringLiteral("quiet"), false);
    s.settings.addPlayTime(coast.titleId(), 41 * 3600 + 20 * 60, now.addSecs(-3 * 3600));
    s.settings.addPlayTime(quiet.titleId(), 6 * 3600, now.addDays(-2));
    WarmupRun cancelled;
    cancelled.titleId = quiet.titleId();
    cancelled.titleName = quiet.title();
    cancelled.done = 1240;
    cancelled.total = 3120;
    cancelled.tookS = 10;
    cancelled.result = WarmupRun::Result::Cancelled;
    cancelled.finishedAt = now.addDays(-2);
    s.settings.addRecentWarmup(cancelled);
    WarmupRun ready;
    ready.titleId = coast.titleId();
    ready.titleName = coast.title();
    ready.done = 41906;
    ready.total = 41906;
    ready.tookS = 14 * 60;
    ready.result = WarmupRun::Result::Finished;
    ready.finishedAt = now.addSecs(-4 * 3600);
    s.settings.addRecentWarmup(ready);
    s.settings.setLastRun(coast.titleId(), {
        {QStringLiteral("result"), QStringLiteral("Stopped by you")},
        {QStringLiteral("at"), now.addSecs(-3 * 3600).toUTC().toString(Qt::ISODate)},
        {QStringLiteral("seconds"), 2 * 3600 + 14 * 60},
        {QStringLiteral("fpsAverage"), 58.2},
        {QStringLiteral("stalls"), 1},
        {QStringLiteral("heldPeakBytes"), qint64(8.6 * 1024 * 1024 * 1024)},
        {QStringLiteral("freeMinMb"), 9830},
    });
    TitleConfig(Paths::xclToml(coast.titleId())).set(QStringLiteral("device_type"), QStringLiteral("lockhart"));
    s.settings.setGameOverride(coast.titleId(), QStringLiteral("overlay"), true);
}

}

QStringList DevLever::states()
{
    return {
        QStringLiteral("library"), QStringLiteral("library-empty"), QStringLiteral("library-search"),
        QStringLiteral("library-search-none"), QStringLiteral("library-paused"), QStringLiteral("library-stop-first"),
        QStringLiteral("cold-cache"), QStringLiteral("details"), QStringLiteral("details-no-art"),
        QStringLiteral("warmup-idle"), QStringLiteral("warmup-running"), QStringLiteral("warmup-paused-ram"),
        QStringLiteral("warmup-failed"), QStringLiteral("warmup-finished"), QStringLiteral("warmup-blocked"),
        QStringLiteral("settings"), QStringLiteral("settings-shaders"), QStringLiteral("settings-folders"),
        QStringLiteral("settings-graphics"),
        QStringLiteral("first-launch"), QStringLiteral("first-launch-network"), QStringLiteral("first-launch-folders"),
        QStringLiteral("first-launch-folders-found"),
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
    const bool libraryState = isLibraryState(o.state) && o.state != QLatin1String("library-empty");
    const QString games = root(QStringLiteral("games"));
    if (libraryState) {
        s.settings.setGameFolders({QDir(games).filePath(QStringLiteral("Coastline Drive")), QDir(games).filePath(QStringLiteral("Archive")),
                                   QDir(games).filePath(QStringLiteral("SignalGarden"))});
        s.library.rescanNow();
        seedLibrary(s);
    } else {
        s.settings.setGameFolders(o.state == QLatin1String("library-empty") || o.state == QLatin1String("first-launch")
                                      || o.state == QLatin1String("first-launch-network") || o.state == QLatin1String("first-launch-folders")
                                      ? QStringList()
                                      : QStringList{games});
        s.library.rescanNow();
    }
    if (o.state == QLatin1String("first-launch-folders-found")) {
        seedArt(s, findGame(s, QStringLiteral("Coastline Drive")), QStringLiteral("coast"), true);
        seedArt(s, findGame(s, QStringLiteral("The Long Quiet Between Lighthouses")), QStringLiteral("quiet"), false);
    }
    s.settings.setFirstLaunchDone(!firstLaunch);
    if (!firstLaunch && !isLibraryState(o.state))
        seedWarmups(s);

    const Game signal = findGame(s, QStringLiteral("Signal Garden"));
    const Game coast = findGame(s, QStringLiteral("Coastline Drive"));
    const QString& st = o.state;

    const Game quiet = findGame(s, QStringLiteral("The Long Quiet Between Lighthouses"));
    if (st == QLatin1String("library-search"))
        w.libraryPage()->searchBox()->setText(QStringLiteral("coast"));
    else if (st == QLatin1String("library-search-none"))
        w.libraryPage()->searchBox()->setText(QStringLiteral("racing"));
    else if (st == QLatin1String("library-paused") || st == QLatin1String("library-stop-first"))
        s.emulator.simulateForScreenshot(EmulatorController::State::Paused, coast, pausedStats());
    else if (st == QLatin1String("details"))
        w.showGameDetails(coast);
    else if (st == QLatin1String("details-no-art"))
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
    else if (st == QLatin1String("first-launch-folders") || st == QLatin1String("first-launch-folders-found"))
        w.firstLaunchPage()->setStep(2);
    else if (st == QLatin1String("in-game-booting") || st == QLatin1String("in-game-paused")) {
        s.emulator.simulateForScreenshot(st == QLatin1String("in-game-booting") ? EmulatorController::State::Booting : EmulatorController::State::Paused,
                                         signal, pausedStats());
        w.enterInGame();
    }

    w.resize(o.size);
    w.show();

    if (st == QLatin1String("library-stop-first"))
        w.play(quiet); // the real launch check: Busy while Coastline Drive is paused
    else if (st == QLatin1String("cold-cache"))
        w.libraryPage()->requestPlay(quiet); // the real prompt: its last warm-up stopped part way

    if (st.startsWith(QLatin1String("error"))) {
        if (st == QLatin1String("error-files-missing")) {
            // the game as the library knew it, on a drive that is no longer there; the check is real
            Game gone = quiet;
            gone.folder = root(QStringLiteral("unplugged/Archive/Quiet"));
            gone.configPath = QDir(gone.folder).filePath(QStringLiteral("MicrosoftGame.config"));
            LaunchErrors::showLaunchProblem(&w, s, gone, s.emulator.checkLaunch(gone), [] {});
        } else if (st == QLatin1String("error-not-enough-ram")) {
            // fixture numbers: the need is the real one (1 GB floor + the launch need setting), free RAM is invented
            EmulatorController::LaunchCheck check;
            check.problem = EmulatorController::LaunchCheck::Problem::NotEnoughRam;
            check.needMb = s.emulator.launchNeedMb();
            check.freeMb = 812;
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
