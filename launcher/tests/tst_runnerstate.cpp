// A runner whose build output has VIctory (DexNom)'s real layout, installed the way the user did it:
// a first build that fails, then one that succeeds. Every surface that shows a runner's install
// state is read right after the install finishes and again after a restart (new Services).
#include "DevFixtures.h"
#include "Library.h"
#include "Paths.h"
#include "Runner.h"
#include "RunnerManager.h"
#include "app/Services.h"
#include "pages/GameDetailsPage.h"
#include "pages/RunsWithPicker.h"
#include "shell/MainWindow.h"
#include "theme/Theme.h"
#include "widgets/Badge.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

namespace {

const QString kId = QStringLiteral("victory-dexnom");

bool run(const QString& program, const QStringList& args, const QString& dir)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    p.start(program, args);
    return p.waitForFinished(30000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// Every label and badge text a person can see under root.
QStringList shown(const QWidget* root)
{
    QStringList out;
    for (const QLabel* l : root->findChildren<QLabel*>()) {
        if (l->isVisibleTo(root) && !l->text().isEmpty())
            out << l->text();
    }
    for (const Badge* b : root->findChildren<Badge*>()) {
        if (b->isVisibleTo(root) && !b->text().isEmpty())
            out << b->text();
    }
    return out;
}

bool anyContains(const QStringList& texts, const QString& needle)
{
    for (const QString& t : texts) {
        if (t.contains(needle, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

}

class TestRunnerState : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void dexnomReadsInstalledEverywhere();

private:
    struct Surfaces {
        QStringList card, details, picker, warmup;
    };
    Surfaces surfaces(Services& s, MainWindow& w);
    Game game() const;

    QTemporaryDir m_dir;
    QString m_commit;
};

void TestRunnerState::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    qputenv("XSEMU_RUNNERS_DIR", m_dir.filePath(QStringLiteral("runners")).toUtf8());
    qputenv("XSEMU_LAUNCHER_DATA", m_dir.filePath(QStringLiteral("launcher")).toUtf8());
    qputenv("XCL_DATA_ROOT", m_dir.filePath(QStringLiteral("xcl")).toUtf8());
    qputenv("XCL_DXIL_CACHE", m_dir.filePath(QStringLiteral("dxil")).toUtf8());
    if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty() || QStandardPaths::findExecutable(QStringLiteral("sh")).isEmpty())
        QSKIP("needs git and sh");

    const QString repo = m_dir.filePath(QStringLiteral("repo"));
    QVERIFY(QDir().mkpath(repo));
    QFile readme(QDir(repo).filePath(QStringLiteral("Cargo.toml")));
    QVERIFY(readme.open(QIODevice::WriteOnly));
    readme.write("[workspace]\n");
    readme.close();
    const QStringList id = {QStringLiteral("-c"), QStringLiteral("user.name=t"), QStringLiteral("-c"), QStringLiteral("user.email=t@t")};
    QVERIFY(run(QStringLiteral("git"), {QStringLiteral("init"), QStringLiteral("-q")}, repo));
    QVERIFY(run(QStringLiteral("git"), {QStringLiteral("add"), QStringLiteral("Cargo.toml")}, repo));
    QVERIFY(run(QStringLiteral("git"), id + QStringList{QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("one")}, repo));
    QVERIFY(run(QStringLiteral("git"), {QStringLiteral("config"), QStringLiteral("uploadpack.allowReachableSHA1InWant"), QStringLiteral("true")}, repo));
    QProcess p;
    p.setWorkingDirectory(repo);
    p.start(QStringLiteral("git"), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    QVERIFY(p.waitForFinished());
    m_commit = QString::fromLatin1(p.readAllStandardOutput()).trimmed();

    // cargo as DexNom's build runs it: --target puts the bins under <target>/<triple>/release and the
    // host build scripts under <target>/release. The first run fails like a broken build does.
    const QString cargo = m_dir.filePath(QStringLiteral("cargo"));
    QFile script(cargo);
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write(QStringLiteral(R"(#!/bin/sh
case " $* " in *" --target x86_64-pc-windows-msvc "*) ;; *) echo "error: no --target"; exit 2 ;; esac
t="$CARGO_TARGET_DIR"
mkdir -p "$t/release/build" "$t/release/deps" "$t/release/examples" "$t/release/incremental"
echo "   Compiling victory-formats v0.1.0"
if [ ! -e "%1" ]; then
  : > "%1"
  echo "error[E0425]: cannot find value \`BCPACK\` in this scope"
  echo "error: could not compile \`victory-formats\` (lib) due to 1 previous error"
  exit 101
fi
mkdir -p "$t/x86_64-pc-windows-msvc/release"
for b in victory victory-play victory-warm; do printf 'MZ %%s\n' "$b" > "$t/x86_64-pc-windows-msvc/release/$b.exe"; done
echo "    Finished \`release\` profile [optimized] target(s) in 61.02s"
)").arg(m_dir.filePath(QStringLiteral("attempted"))).toUtf8());
    script.close();
    script.setPermissions(script.permissions() | QFileDevice::ExeOwner);

    QJsonArray buildSteps;
    buildSteps.append(QJsonArray{cargo, QStringLiteral("build"), QStringLiteral("--locked"), QStringLiteral("--release"), QStringLiteral("-p"), QStringLiteral("victory"),
                                 QStringLiteral("--features"), QStringLiteral("diagnostics"), QStringLiteral("--bins"), QStringLiteral("--target"),
                                 QStringLiteral("x86_64-pc-windows-msvc")});
    QJsonObject feed;
    feed.insert(kId, QJsonObject{{QStringLiteral("commit"), m_commit},
                                 {QStringLiteral("repo"), QUrl::fromLocalFile(repo).toString()},
                                 {QStringLiteral("buildSteps"), buildSteps},
                                 {QStringLiteral("artifacts"), QJsonObject{{QStringLiteral("x86_64-pc-windows-msvc/release/victory.exe"), QStringLiteral("victory.exe")}}},
                                 {QStringLiteral("executable"), QStringLiteral("victory.exe")}});
    QFile f(m_dir.filePath(QStringLiteral("feed.json")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(feed).toJson());
    f.close();
    qputenv("XSEMU_RUNNER_FEED", f.fileName().toUtf8());

    QVERIFY(DevFixtures::materialize(QStringLiteral(XSEMU_FIXTURES_DIR "/games"), m_dir.filePath(QStringLiteral("games"))));
    Theme::instance().install(*qApp);
}

Game TestRunnerState::game() const
{
    for (const Game& g : Library::scan({m_dir.filePath(QStringLiteral("games"))}, 3, false)) {
        if (g.title() == QLatin1String("Signal Garden"))
            return g;
    }
    return {};
}

TestRunnerState::Surfaces TestRunnerState::surfaces(Services& s, MainWindow& w)
{
    Surfaces out;
    w.showPage(Page::Runners);
    QApplication::processEvents();
    if (QWidget* card = w.findChild<QWidget*>(QStringLiteral("runner-") + kId))
        out.card = shown(card);

    const Game g = game();
    w.showGameDetails(g);
    QApplication::processEvents();
    out.details = shown(w.gameDetailsPage());
    w.gameDetailsPage()->runsWith()->openPicker();
    QApplication::processEvents();
    if (QWidget* pop = w.findChild<QWidget*>(QStringLiteral("runsWithPicker"))) {
        if (QWidget* row = pop->findChild<QWidget*>(QStringLiteral("runsWith-") + kId))
            out.picker = shown(row);
        pop->close();
        QApplication::processEvents();
    }

    w.showPage(Page::Warmup);
    QApplication::processEvents();
    for (QWidget* page = w.gameDetailsPage()->parentWidget(); page; page = nullptr) {
        for (QWidget* child : page->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
            if (QByteArray(child->metaObject()->className()) == "WarmupPage")
                out.warmup = shown(child);
        }
    }
    Q_UNUSED(s);
    return out;
}

void TestRunnerState::dexnomReadsInstalledEverywhere()
{
    const Game g = game();
    QVERIFY(g.isValid());
    const RunnerSpec* spec = Runners::find(kId);
    QVERIFY(spec && spec->isPinned());

    {
        Services s;
        s.settings.setFirstLaunchDone(true);
        s.settings.setGameFolders({m_dir.filePath(QStringLiteral("games"))});
        s.settings.setGameOverride(g.titleId(), QStringLiteral("runner"), kId);
        QTRY_VERIFY_WITH_TIMEOUT(s.library.game(g.titleId()).isValid(), 10000);
        MainWindow w(s);
        w.resize(1280, 800);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.showGameDetails(g);
        QVERIFY(anyContains(shown(w.gameDetailsPage()), QStringLiteral("not installed")));

        // the first build fails: Failed with the reason everywhere, never "Not installed"
        QSignalSpy done(&s.runners, &RunnerManager::installFinished);
        QString error;
        QVERIFY2(s.runners.install(kId, &error), qPrintable(error));
        QVERIFY(done.wait(60000));
        QVERIFY(!done.first().at(1).toBool());
        QCOMPARE(s.runners.status(kId).state, RunnerManager::State::Failed);
        QVERIFY2(s.runners.status(kId).error.contains(QStringLiteral("101")), qPrintable(s.runners.status(kId).error));
        Surfaces failed = surfaces(s, w);
        QVERIFY2(anyContains(failed.card, QStringLiteral("Install failed")), qPrintable(failed.card.join(QLatin1Char('|'))));
        QVERIFY2(anyContains(failed.card, QStringLiteral("could not compile")), qPrintable(failed.card.join(QLatin1Char('|'))));
        QVERIFY2(!anyContains(failed.card, QStringLiteral("Not installed")), qPrintable(failed.card.join(QLatin1Char('|'))));
        QVERIFY2(anyContains(failed.details, QStringLiteral("install failed")), qPrintable(failed.details.join(QLatin1Char('|'))));
        QVERIFY2(anyContains(failed.picker, QStringLiteral("failed")), qPrintable(failed.picker.join(QLatin1Char('|'))));
        const QString log = QDir(spec->dataDir()).filePath(QStringLiteral("install.log"));
        QVERIFY(QFileInfo::exists(log));
        QWidget* card = w.findChild<QWidget*>(QStringLiteral("runner-") + kId);
        QVERIFY(card->findChild<QWidget*>(QStringLiteral("installLog-") + kId)->isVisibleTo(card));
        QFile logFile(log);
        QVERIFY(logFile.open(QIODevice::ReadOnly));
        const QByteArray logText = logFile.readAll();
        QVERIFY2(logText.contains("could not compile") && logText.contains("Install failed"), logText.constData());

        // details is on screen while the second build runs and finishes, as on the user's PC
        w.showGameDetails(g);
        done.clear();
        QVERIFY2(s.runners.install(kId, &error), qPrintable(error));
        QVERIFY(done.wait(60000));
        QVERIFY2(done.first().at(1).toBool(), qPrintable(done.first().at(2).toString()));
        QStringList details = shown(w.gameDetailsPage());
        QVERIFY2(!anyContains(details, QStringLiteral("not installed")), qPrintable(details.join(QLatin1Char('|'))));

        const RunnerManager::Status st = s.runners.status(kId);
        QCOMPARE(st.state, RunnerManager::State::Installed);
        QCOMPARE(st.installedVersion, m_commit.left(7));
        QCOMPARE(QDir::cleanPath(st.executable), QDir(spec->installDir()).filePath(QStringLiteral("victory.exe")));
        QVERIFY(QFileInfo::exists(st.executable));
        QVERIFY(s.runners.isRunnable(kId));
        QVERIFY(QFileInfo::exists(QDir(spec->targetDir()).filePath(QStringLiteral("x86_64-pc-windows-msvc/release/victory-warm.exe"))));
        QVERIFY(QFileInfo(QDir(spec->targetDir()).filePath(QStringLiteral("release/deps"))).isDir());
        QVERIFY(!QFileInfo::exists(QDir(spec->dataDir()).filePath(QStringLiteral("staging"))));
        QVERIFY(s.emulator.checkLaunch(g).problem != EmulatorController::LaunchCheck::Problem::RunnerNotInstalled);
        QVERIFY2(s.warmup.checkStart(g).isEmpty(), qPrintable(s.warmup.checkStart(g)));

        const Surfaces now = surfaces(s, w);
        QVERIFY2(anyContains(now.card, QStringLiteral("Installed")) && !anyContains(now.card, QStringLiteral("failed")), qPrintable(now.card.join(QLatin1Char('|'))));
        QVERIFY2(!anyContains(now.details, QStringLiteral("not installed")) && !anyContains(now.details, QStringLiteral("failed")), qPrintable(now.details.join(QLatin1Char('|'))));
        QVERIFY2(anyContains(now.picker, QStringLiteral("Installed")), qPrintable(now.picker.join(QLatin1Char('|'))));
        QVERIFY2(!anyContains(now.warmup, QStringLiteral("not installed")), qPrintable(now.warmup.join(QLatin1Char('|'))));
    }

    // a restart: the install record alone decides
    Services s;
    QTRY_VERIFY_WITH_TIMEOUT(s.library.game(g.titleId()).isValid(), 10000);
    MainWindow w(s);
    w.resize(1280, 800);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QCOMPARE(s.runners.status(kId).state, RunnerManager::State::Installed);
    QVERIFY(s.runners.isRunnable(kId));
    const Surfaces later = surfaces(s, w);
    QVERIFY2(anyContains(later.card, QStringLiteral("Installed")) && !anyContains(later.card, QStringLiteral("failed")), qPrintable(later.card.join(QLatin1Char('|'))));
    QVERIFY2(!anyContains(later.details, QStringLiteral("not installed")), qPrintable(later.details.join(QLatin1Char('|'))));
    QVERIFY2(anyContains(later.picker, QStringLiteral("Installed")), qPrintable(later.picker.join(QLatin1Char('|'))));
    QVERIFY2(!anyContains(later.warmup, QStringLiteral("not installed")), qPrintable(later.warmup.join(QLatin1Char('|'))));
}

QTEST_MAIN(TestRunnerState)
#include "tst_runnerstate.moc"
