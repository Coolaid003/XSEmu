// Page switches and emulator stats ticks on a PC-shaped install: a large pipelines.bin, tens of
// thousands of driver blobs and DXIL files in the shader cache, and 40 games. With the background
// snapshots warm, a switch to Game details stays under a frame (16 ms), any other switch under
// 100 ms, and a stats tick under 8 ms, all on the UI thread and judged on the median of nine rounds.
// XSEMU_PERF_BUDGET_MS replaces every budget. XSEMU_PERF_SCALE=1 builds the full PC shape
// (about 650 MB of manifest, 30k blobs, 166k DXIL files); the default is a tenth of it.
// XSEMU_PERF_DIR keeps the generated install in that folder and reuses it on the next run.
#include "DevFixtures.h"
#include "EmulatorController.h"
#include "app/Services.h"
#include "pages/SettingsPage.h"
#include "shell/MainWindow.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <functional>

class TestPerf : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void pageSwitchesStayFast();
    void statsTicksStayCheap();

private:
    void warmUp(Services& s, MainWindow& w);

    QTemporaryDir m_temp;
    QString m_root;
};

static void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

void TestPerf::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    const double scale = qEnvironmentVariableIsSet("XSEMU_PERF_SCALE") ? qEnvironmentVariable("XSEMU_PERF_SCALE").toDouble() : 0.1;
    m_root = qEnvironmentVariableIsSet("XSEMU_PERF_DIR") ? qEnvironmentVariable("XSEMU_PERF_DIR") : m_temp.path();
    const QDir root(m_root);
    const QString dxil = root.filePath(QStringLiteral("dxil"));
    qputenv("XCL_DXIL_CACHE", dxil.toUtf8());
    qputenv("XCL_DATA_ROOT", root.filePath(QStringLiteral("xcl")).toUtf8());
    qputenv("XSEMU_LAUNCHER_DATA", root.filePath(QStringLiteral("launcher")).toUtf8());
    qputenv("XSEMU_RUNNERS_DIR", root.filePath(QStringLiteral("runners")).toUtf8());
    qputenv("XSEMU_EMULATOR", XSEMU_FAKE_EMULATOR_PATH);
    qputenv("FAKE_XSEMU_SCENARIO", "ok");
    qputenv("FAKE_XSEMU_TICK_MS", "500");
    Theme::instance().install(*qApp);
    const QString done = root.filePath(QStringLiteral("built-%1").arg(scale));
    if (QFileInfo::exists(done)) {
        qInfo("fixture: reusing %s", qPrintable(m_root));
        return;
    }
    QElapsedTimer build;
    build.start();

    QVERIFY(DevFixtures::materialize(QStringLiteral(XSEMU_FIXTURES_DIR "/games"), root.filePath(QStringLiteral("games"))));
    for (int i = 0; i < 36; ++i) {
        const QString dir = root.filePath(QStringLiteral("games/Generated %1").arg(i, 2, 10, QLatin1Char('0')));
        writeFile(dir + QStringLiteral("/MicrosoftGame.config"),
                  QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Game configVersion=\"1\">\n"
                                 "  <Identity Name=\"Perf.Generated%1\" Publisher=\"CN=Example\" Version=\"1.0.%1.0\" />\n"
                                 "  <ExecutableList><Executable Name=\"Game.exe\" Id=\"Game\" /></ExecutableList>\n"
                                 "  <ShellVisuals DefaultDisplayName=\"Generated Game %1\" PublisherDisplayName=\"Perf Studio\" Description=\"Fixture\" />\n"
                                 "</Game>\n").arg(i).toUtf8());
        writeFile(dir + QStringLiteral("/Game.exe"), "fixture: not a program\n");
    }

    // manifest entries of 1-6 KB in the emulator's format (u32 LE length, then the entry), every eighth a repeat
    QDir().mkpath(dxil + QStringLiteral("/pipelines"));
    QFile manifest(dxil + QStringLiteral("/pipelines/pipelines.bin"));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    const qint64 manifestBytes = qint64(650.0 * 1024 * 1024 * scale);
    QByteArray body(6 * 1024, 'p');
    qint64 written = 0;
    for (quint32 i = 0; written < manifestBytes; ++i) {
        const quint32 len = 1024 + (i * 2654435761u) % (5 * 1024);
        const QByteArray id = QByteArray::number(i % 8 == 7 ? i - 7 : i) + ':';
        memcpy(body.data(), id.constData(), size_t(id.size()));
        const char head[4] = {char(len & 0xff), char((len >> 8) & 0xff), char((len >> 16) & 0xff), char(len >> 24)};
        manifest.write(head, 4);
        manifest.write(body.constData(), len);
        written += 4 + len;
    }
    manifest.close();
    writeFile(dxil + QStringLiteral("/pipelines/runs"), "12\n");
    const int blobs = int(30000 * scale);
    for (int i = 0; i < blobs; ++i)
        writeFile(dxil + QStringLiteral("/driver/10de-2786-perf/%1.pso").arg(i), "b");
    const int files = int(166000 * scale);
    for (int i = 0; i < files; ++i)
        writeFile(dxil + QStringLiteral("/%1.dxil").arg(quint64(i) * 0x9e3779b97f4a7c15ULL, 16, 16, QLatin1Char('0')), "d");
    qInfo("fixture: %lld MB manifest, %d blobs, %d DXIL files, built in %lld ms", qint64(written >> 20), blobs, files, qint64(build.elapsed()));
    writeFile(done, "1\n");
}

static qint64 budgetMs(qint64 standard)
{
    return qEnvironmentVariableIsSet("XSEMU_PERF_BUDGET_MS") ? qEnvironmentVariable("XSEMU_PERF_BUDGET_MS").toLongLong() : standard;
}

static double median(QList<double> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// The launcher as the user meets it: shown, the library listed and the background snapshots in.
void TestPerf::warmUp(Services& s, MainWindow& w)
{
    QElapsedTimer startup;
    startup.start();
    w.resize(1280, 800);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QTRY_VERIFY_WITH_TIMEOUT(s.library.games().size() >= 40, 20000);
    const qint64 shown = startup.elapsed();
    QTRY_VERIFY_WITH_TIMEOUT(s.shaderCache.hasSnapshot(), 120000);
    QTRY_VERIFY_WITH_TIMEOUT(s.hardware.ready(), 20000);
    qInfo("startup: window shown and library listed in %lld ms, cache snapshot in %lld ms", shown, qint64(startup.elapsed()));
}

static Services* newServices(const QString& games)
{
    auto* s = new Services;
    s->settings.setFirstLaunchDone(true);
    s->settings.setLaunchNeedMb(0);
    s->settings.setGameFolders({games});
    return s;
}

void TestPerf::pageSwitchesStayFast()
{
    const std::unique_ptr<Services> services(newServices(QDir(m_root).filePath(QStringLiteral("games"))));
    Services& s = *services;
    MainWindow w(s);
    warmUp(s, w);
    if (QTest::currentTestFailed())
        return;
    const Game game = s.library.games().first();
    const Game other = s.library.games().last();

    struct Step {
        const char* name;
        std::function<void()> go;
        qint64 budget;
    };
    auto section = [&w](SettingsPage::Section sec) {
        return [&w, sec] {
            w.showPage(Page::Settings);
            w.settingsPage()->showSection(sec);
        };
    };
    const qint64 details = budgetMs(16);
    const qint64 page = budgetMs(100);
    const QList<Step> steps{
        {"Library", [&] { w.showPage(Page::Library); }, page},
        {"Game details", [&] { w.showGameDetails(game); }, details},
        {"Warm-up", [&] { w.showPage(Page::Warmup); }, page},
        {"Runners", [&] { w.showPage(Page::Runners); }, page},
        {"Library", [&] { w.showPage(Page::Library); }, page},
        {"Settings: Console", section(SettingsPage::Section::Console), page},
        {"Settings: Network", section(SettingsPage::Section::Network), page},
        {"Settings: Shaders", section(SettingsPage::Section::Shaders), page},
        {"Settings: Overlay", section(SettingsPage::Section::Overlay), page},
        {"Settings: Game folders", section(SettingsPage::Section::GameFolders), page},
        {"Settings: Input", section(SettingsPage::Section::Input), page},
        {"Settings: Graphics", section(SettingsPage::Section::Graphics), page},
        {"Settings: Logs", section(SettingsPage::Section::Logs), page},
        {"Settings: About", section(SettingsPage::Section::About), page},
        {"Game details (another game)", [&] { w.showGameDetails(other); }, details},
    };
    // a first round builds what is built once; then five measured rounds
    constexpr int rounds = 9;
    QList<QList<double>> took(steps.size());
    QList<QList<double>> painting(steps.size());
    for (int round = 0; round <= rounds; ++round) {
        for (int i = 0; i < steps.size(); ++i) {
            QElapsedTimer t;
            t.start();
            steps[i].go();
            QCoreApplication::processEvents();
            const double built = double(t.nsecsElapsed()) / 1e6;
            w.repaint();
            const double ms = double(t.nsecsElapsed()) / 1e6;
            if (round == 0)
                continue;
            took[i] << ms;
            painting[i] << ms - built;
        }
    }
    QStringList slow;
    for (int i = 0; i < steps.size(); ++i) {
        const double ms = median(took[i]);
        qInfo("%-28s %6.1f ms median (%.1f ms painting), slowest %.1f ms, budget %lld ms", steps[i].name, ms, median(painting[i]),
              *std::max_element(took[i].cbegin(), took[i].cend()), steps[i].budget);
        if (ms > double(steps[i].budget))
            slow << QStringLiteral("%1 %2 ms").arg(QLatin1String(steps[i].name)).arg(ms, 0, 'f', 1);
    }
    QVERIFY2(slow.isEmpty(), qPrintable(slow.join(QStringLiteral(", "))));
}

// A running game sends stats a few times a second, and every page that shows the game follows them:
// a tick must cost the UI thread well under a frame on each of them.
void TestPerf::statsTicksStayCheap()
{
    const std::unique_ptr<Services> services(newServices(QDir(m_root).filePath(QStringLiteral("games"))));
    Services& s = *services;
    MainWindow w(s);
    warmUp(s, w);
    if (QTest::currentTestFailed())
        return;
    Game game;
    for (const Game& g : s.library.games()) {
        if (g.title() == QLatin1String("Signal Garden"))
            game = g;
    }
    QVERIFY(game.isValid());
    w.play(game);
    QTRY_COMPARE_WITH_TIMEOUT(s.emulator.state(), EmulatorController::State::Running, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(s.emulator.stats().fps.has_value(), 10000);

    struct View {
        const char* name;
        std::function<void()> show;
    };
    const QList<View> views{
        {"In-game view", [&] { w.enterInGame(); }},
        {"Library", [&] { w.showPage(Page::Library); }},
        {"Warm-up", [&] { w.showPage(Page::Warmup); }},
    };
    const qint64 budget = budgetMs(8);
    QStringList slow;
    for (const View& v : views) {
        v.show();
        QCoreApplication::processEvents();
        w.repaint();
        QList<double> took;
        for (int i = 0; i <= 9; ++i) {
            QElapsedTimer t;
            t.start();
            emit s.emulator.statsChanged();
            QCoreApplication::processEvents();
            if (i > 0)
                took << double(t.nsecsElapsed()) / 1e6;
        }
        const double ms = median(took);
        qInfo("stats tick on %-14s %5.2f ms median, slowest %.2f ms, budget %lld ms", v.name, ms, *std::max_element(took.cbegin(), took.cend()), budget);
        if (ms > double(budget))
            slow << QStringLiteral("%1 %2 ms").arg(QLatin1String(v.name)).arg(ms, 0, 'f', 2);
    }
    s.emulator.stop();
    QTRY_COMPARE_WITH_TIMEOUT(s.emulator.state(), EmulatorController::State::Idle, 10000);
    QVERIFY2(slow.isEmpty(), qPrintable(slow.join(QStringLiteral(", "))));
}

QTEST_MAIN(TestPerf)
#include "tst_perf.moc"
