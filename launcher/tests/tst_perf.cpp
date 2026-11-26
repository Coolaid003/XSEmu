// Page switches on a PC-shaped install: a large pipelines.bin, tens of thousands of driver blobs and
// DXIL files in the shader cache, and 40 games. Every switch must stay under the budget on the UI
// thread once the background snapshots are warm. XSEMU_PERF_SCALE=1 builds the full PC shape
// (about 650 MB of manifest, 30k blobs, 166k DXIL files); the default is a tenth of it.
// XSEMU_PERF_DIR keeps the generated install in that folder and reuses it on the next run.
#include "DevFixtures.h"
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

#include <functional>

class TestPerf : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void pageSwitchesStayFast();

private:
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

void TestPerf::pageSwitchesStayFast()
{
    const qint64 budget = qEnvironmentVariableIsSet("XSEMU_PERF_BUDGET_MS") ? qEnvironmentVariable("XSEMU_PERF_BUDGET_MS").toLongLong() : 100;
    QElapsedTimer startup;
    startup.start();
    Services s;
    s.settings.setFirstLaunchDone(true);
    s.settings.setGameFolders({QDir(m_root).filePath(QStringLiteral("games"))});
    MainWindow w(s);
    w.resize(1280, 800);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QTRY_VERIFY_WITH_TIMEOUT(s.library.games().size() >= 40, 20000);
    const qint64 shown = startup.elapsed();
    // the budget is for a warm launcher: the background snapshots have landed
    QTRY_VERIFY_WITH_TIMEOUT(s.shaderCache.hasSnapshot(), 120000);
    QTRY_VERIFY_WITH_TIMEOUT(s.hardware.ready(), 20000);
    qInfo("startup: window shown and library listed in %lld ms, cache snapshot in %lld ms", shown, qint64(startup.elapsed()));
    const Game game = s.library.games().first();

    struct Step {
        const char* name;
        std::function<void()> go;
    };
    auto section = [&w](SettingsPage::Section sec) {
        return [&w, sec] {
            w.showPage(Page::Settings);
            w.settingsPage()->showSection(sec);
        };
    };
    const QList<Step> steps{
        {"Library", [&] { w.showPage(Page::Library); }},
        {"Game details", [&] { w.showGameDetails(game); }},
        {"Warm-up", [&] { w.showPage(Page::Warmup); }},
        {"Runners", [&] { w.showPage(Page::Runners); }},
        {"Library", [&] { w.showPage(Page::Library); }},
        {"Settings: Console", section(SettingsPage::Section::Console)},
        {"Settings: Network", section(SettingsPage::Section::Network)},
        {"Settings: Shaders", section(SettingsPage::Section::Shaders)},
        {"Settings: Overlay", section(SettingsPage::Section::Overlay)},
        {"Settings: Game folders", section(SettingsPage::Section::GameFolders)},
        {"Settings: Input", section(SettingsPage::Section::Input)},
        {"Settings: Graphics", section(SettingsPage::Section::Graphics)},
        {"Settings: Logs", section(SettingsPage::Section::Logs)},
        {"Settings: About", section(SettingsPage::Section::About)},
        {"Game details", [&] { w.showGameDetails(game); }},
    };
    QStringList slow;
    for (int pass = 0; pass < 2; ++pass) {
        for (const Step& step : steps) {
            QElapsedTimer t;
            t.start();
            step.go();
            QCoreApplication::processEvents();
            w.repaint();
            const qint64 ms = t.elapsed();
            qInfo("pass %d  %-24s %4lld ms", pass + 1, step.name, ms);
            if (pass == 1 && ms > budget)
                slow << QStringLiteral("%1 %2 ms").arg(QLatin1String(step.name)).arg(ms);
        }
    }
    QVERIFY2(slow.isEmpty(), qPrintable(slow.join(QStringLiteral(", "))));
}

QTEST_MAIN(TestPerf)
#include "tst_perf.moc"
