// Runners: the curated list, the recommendation, progress parsing, and real installs (a release
// asset over file:// with its SHA-256, and a source build from a local Git repository).
#include "Paths.h"
#include "ProgressParsers.h"
#include "Runner.h"
#include "RunnerInstall.h"
#include "RunnerManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

namespace {

constexpr quint64 kGb = 1024ULL * 1024 * 1024;

bool run(const QString& program, const QStringList& args, const QString& dir)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    p.start(program, args);
    return p.waitForFinished(30000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

QString sha256(const QString& path)
{
    QFile f(path);
    QCryptographicHash h(QCryptographicHash::Sha256);
    f.open(QIODevice::ReadOnly);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

}

class TestRunners : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void curatedList();
    void recommendation();
    void parsers_data();
    void parsers();
    void installsReleaseAsset();
    void rejectsWrongHash();
    void buildsFromSource();

private:
    QTemporaryDir m_dir;
    QString m_zip;
    bool m_haveGit = false;
    QString m_commit;
};

void TestRunners::initTestCase()
{
    qputenv("XSEMU_RUNNERS_DIR", m_dir.filePath(QStringLiteral("runners")).toUtf8());

    // a release asset: one top-level folder holding the program and a file the spec copies
    const QString pkg = m_dir.filePath(QStringLiteral("pkg/fake-1.0"));
    QVERIFY(QDir().mkpath(pkg + QStringLiteral("/native")));
    for (const QString& name : {QStringLiteral("native/engine"), QStringLiteral("runtime.dll")}) {
        QFile f(QDir(pkg).filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(name.toUtf8());
    }
    m_zip = m_dir.filePath(QStringLiteral("fake-1.0.zip"));
#ifdef Q_OS_WIN
    QVERIFY(run(QStringLiteral("tar"), {QStringLiteral("-a"), QStringLiteral("-c"), QStringLiteral("-f"), m_zip, QStringLiteral("fake-1.0")}, m_dir.filePath(QStringLiteral("pkg"))));
#else
    if (!QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty())
        QVERIFY(run(QStringLiteral("python3"), {QStringLiteral("-c"), QStringLiteral("import shutil,sys; shutil.make_archive(sys.argv[1][:-4], 'zip', sys.argv[2], 'fake-1.0')"), m_zip, m_dir.filePath(QStringLiteral("pkg"))}, m_dir.path()));
#endif

    // a source repository with one commit
    const QString repo = m_dir.filePath(QStringLiteral("repo"));
    m_haveGit = !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty();
    if (m_haveGit) {
        QDir().mkpath(repo);
        QFile f(QDir(repo).filePath(QStringLiteral("README")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("runner source\n");
        f.close();
        const QStringList id = {QStringLiteral("-c"), QStringLiteral("user.name=t"), QStringLiteral("-c"), QStringLiteral("user.email=t@t")};
        QVERIFY(run(QStringLiteral("git"), {QStringLiteral("init"), QStringLiteral("-q")}, repo));
        QVERIFY(run(QStringLiteral("git"), {QStringLiteral("add"), QStringLiteral("README")}, repo));
        QVERIFY(run(QStringLiteral("git"), id + QStringList{QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("one")}, repo));
        QProcess p;
        p.setWorkingDirectory(repo);
        p.start(QStringLiteral("git"), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
        QVERIFY(p.waitForFinished());
        m_commit = QString::fromLatin1(p.readAllStandardOutput()).trimmed();
        // allow fetching an unadvertised commit by SHA from this repository
        QVERIFY(run(QStringLiteral("git"), {QStringLiteral("config"), QStringLiteral("uploadpack.allowReachableSHA1InWant"), QStringLiteral("true")}, repo));
    }

    // re-pin two runners at the local asset and repository before the list is first read
    // (append, not QJsonArray{QJsonArray{...}}: MSVC takes that as a copy of the inner array)
    QJsonArray buildSteps;
    buildSteps.append(QJsonArray{QStringLiteral("git"), QStringLiteral("archive"), QStringLiteral("-o"), QStringLiteral("{targetDir}/runner.tar"), QStringLiteral("HEAD")});
    QJsonObject feed;
    feed.insert(QStringLiteral("kelvaroq"), QJsonObject{{QStringLiteral("tag"), QStringLiteral("v1.0")},
                                                         {QStringLiteral("asset"), QStringLiteral("fake-1.0.zip")},
                                                         {QStringLiteral("sha256"), QFileInfo::exists(m_zip) ? sha256(m_zip) : QString(64, QLatin1Char('0'))},
                                                         {QStringLiteral("url"), QUrl::fromLocalFile(m_zip).toString()},
                                                         {QStringLiteral("executable"), QStringLiteral("native/engine")}});
    feed.insert(QStringLiteral("victory-7900xtx"), QJsonObject{{QStringLiteral("tag"), QStringLiteral("v1.0")},
                                                                {QStringLiteral("asset"), QStringLiteral("fake-1.0.zip")},
                                                                {QStringLiteral("sha256"), QString(64, QLatin1Char('a'))},
                                                                {QStringLiteral("url"), QUrl::fromLocalFile(m_zip).toString()},
                                                                {QStringLiteral("executable"), QStringLiteral("native/engine")}});
    feed.insert(QStringLiteral("victory-dexnom"), QJsonObject{{QStringLiteral("commit"), m_commit.isEmpty() ? QString(40, QLatin1Char('0')) : m_commit},
                                                               {QStringLiteral("repo"), QUrl::fromLocalFile(repo).toString()},
                                                               {QStringLiteral("buildSteps"), buildSteps},
                                                               {QStringLiteral("artifacts"), QJsonObject{{QStringLiteral("runner.tar"), QStringLiteral("runner.tar")}}},
                                                               {QStringLiteral("executable"), QStringLiteral("runner.tar")}});
    const QString feedPath = m_dir.filePath(QStringLiteral("feed.json"));
    QFile f(feedPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(feed).toJson());
    f.close();
    qputenv("XSEMU_RUNNER_FEED", feedPath.toUtf8());
}

void TestRunners::curatedList()
{
    const QList<RunnerSpec>& list = Runners::curated();
    QCOMPARE(list.size(), 4);
    QCOMPARE(list.first().id, QStringLiteral("xsemu"));
    QVERIFY(Runners::xsemu().isBuiltIn());
    for (const RunnerSpec& r : list) {
        QVERIFY2(Runners::find(r.id) == &r, qPrintable(r.id));
        QVERIFY2(r.isPinned(), qPrintable(r.id)); // the feed re-pins the Windows-only ones here
        if (!r.isBuiltIn()) {
            QVERIFY(!r.canPause && !r.canOverlay);
            QCOMPARE(r.embed, RunnerSpec::Embed::Docked);
        }
    }
    const RunnerSpec* k = Runners::find(QStringLiteral("kelvaroq"));
    QCOMPARE(k->precache.size(), 2);
    QVERIFY(k->precache.first().check);
    QCOMPARE(k->precache.last().cancel, RunnerSpec::PrecacheCancel::CloseStdin);
    QCOMPARE(k->stop, RunnerSpec::Stop::SysCommandClose);
    QVERIFY(!k->note.isEmpty());

    const QStringList args = RunnerSpec::expand(k->launchArgs, k->tokens(QStringLiteral("/g"), QStringLiteral("/g/Game.exe"), QStringLiteral("T"), QStringLiteral("lockhart"), 0));
    QCOMPARE(args, (QStringList{QStringLiteral("run"), QDir::toNativeSeparators(QStringLiteral("/g/Game.exe")), QStringLiteral("--console"), QStringLiteral("series-s"), QStringLiteral("--execute")}));
    QCOMPARE(QDir::fromNativeSeparators(k->resolvedCacheDir()), QDir(Paths::runnerDir(QStringLiteral("kelvaroq"))).filePath(QStringLiteral("cache")));
    QCOMPARE(RunnerSpec::expand(QStringLiteral("{rtFlag} {unknown}"), k->tokens({}, {}, {}, QStringLiteral("anaconda"), 0)), QStringLiteral("--no-rt {unknown}"));
}

void TestRunners::recommendation()
{
    auto all = [](const QString&) { return true; };
    auto none = [](const QString& id) { return id == QLatin1String(Runners::XSEmu); };
    QCOMPARE(Runners::recommend(Runners::kVendorAmd, QStringLiteral("Radeon RX 7900 XTX"), 64 * kGb, all).runnerId, QStringLiteral("victory-7900xtx"));
    QCOMPARE(Runners::recommend(Runners::kVendorAmd, QStringLiteral("Radeon RX 7800 XT"), 32 * kGb, all).runnerId, QStringLiteral("victory-dexnom"));
    const auto nv = Runners::recommend(Runners::kVendorNvidia, QStringLiteral("NVIDIA GPU (sample)"), 32 * kGb, all);
    QCOMPARE(nv.runnerId, QStringLiteral("kelvaroq"));
    QVERIFY(nv.reason.contains(QStringLiteral("RTX 5080")));
    QVERIFY(nv.reason.contains(QStringLiteral("NVIDIA GPU (sample)")));
    QCOMPARE(Runners::recommend(Runners::kVendorIntel, QStringLiteral("Arc A770"), 32 * kGb, all).runnerId, QStringLiteral("xsemu"));
    QCOMPARE(Runners::recommend(0, {}, 32 * kGb, all).runnerId, QStringLiteral("xsemu"));
    const auto blocked = Runners::recommend(Runners::kVendorAmd, QStringLiteral("Radeon"), 64 * kGb, none);
    QCOMPARE(blocked.runnerId, QStringLiteral("xsemu"));
    QVERIFY(blocked.reason.contains(QStringLiteral("AMD")));
}

void TestRunners::parsers_data()
{
    QTest::addColumn<QString>("parser");
    QTest::addColumn<QString>("line");
    QTest::addColumn<qint64>("done");
    QTest::addColumn<qint64>("total");
    QTest::newRow("git") << "git" << "Receiving objects:  45% (450/1000), 1.2 MiB" << qint64(450) << qint64(1000);
    QTest::newRow("cargo bar") << "cargo" << "    Building [=====>      ] 120/300: serde" << qint64(120) << qint64(300);
    QTest::newRow("cargo step") << "cargo" << "   Compiling serde v1.0.200" << qint64(-1) << qint64(-1);
    QTest::newRow("kelvaroq check") << "kelvaroq" << "precompile-check 812\r" << qint64(0) << qint64(812);
    QTest::newRow("kelvaroq step") << "kelvaroq" << "precompile 120 812 2" << qint64(120) << qint64(812);
    QTest::newRow("victory shaders") << "victory-shaders" << "4000 shaders in the cache, 12 s" << qint64(4000) << qint64(-1);
    QTest::newRow("victory shaders done") << "victory-shaders" << "41906 shaders in the cache, 3 do not translate, 95 s" << qint64(41906) << qint64(41906);
    QTest::newRow("victory warm header") << "victory-warm" << "9000 pipelines known, 7000 made before (last run), 4 bad; 1996 to make, 16 processes" << qint64(0) << qint64(1996);
    QTest::newRow("victory warm tick") << "victory-warm" << "500 of 1996, 40 s" << qint64(500) << qint64(1996);
    QTest::newRow("dexnom tick") << "victory-dexnom" << "   12 s  2000 shaders" << qint64(2000) << qint64(-1);
    QTest::newRow("generic") << "other" << "step 3/9" << qint64(3) << qint64(9);
}

void TestRunners::parsers()
{
    QFETCH(QString, parser);
    QFETCH(QString, line);
    QFETCH(qint64, done);
    QFETCH(qint64, total);
    const auto u = ProgressParsers::parse(parser, line);
    QVERIFY(u.has_value());
    QCOMPARE(u->done, done);
    QCOMPARE(u->total, total);
    QVERIFY(!ProgressParsers::parse(QStringLiteral("kelvaroq"), QStringLiteral("presenting to a window")).has_value());
}

void TestRunners::installsReleaseAsset()
{
    if (!QFileInfo::exists(m_zip))
        QSKIP("no zip tool on this machine");
    RunnerManager m;
    const QString id = QStringLiteral("kelvaroq");
    QCOMPARE(m.status(id).state, RunnerManager::State::NotInstalled);
    QSignalSpy done(&m, &RunnerManager::installFinished);
    QString error;
    QVERIFY2(m.install(id, &error), qPrintable(error));
    QCOMPARE(m.status(id).state, RunnerManager::State::Installing);
    QVERIFY(!m.install(QStringLiteral("victory-7900xtx"), &error)); // one install at a time
    QVERIFY(done.wait(30000));
    QVERIFY2(done.first().at(1).toBool(), qPrintable(done.first().at(2).toString()));
    const RunnerManager::Status s = m.status(id);
    QCOMPARE(s.state, RunnerManager::State::Installed);
    QCOMPARE(s.installedVersion, QStringLiteral("v1.0"));
    QVERIFY(QFileInfo::exists(s.executable));
    QVERIFY(m.isRunnable(id));
    QVERIFY(!QFileInfo::exists(QDir(Paths::runnerDir(id)).filePath(QStringLiteral("staging"))));

    // an older install record against the curated pin offers the update
    QFile rec(QDir(Paths::runnerDir(id)).filePath(QStringLiteral("runner.json")));
    QVERIFY(rec.open(QIODevice::ReadOnly));
    QJsonObject o = QJsonDocument::fromJson(rec.readAll()).object();
    rec.close();
    o.insert(QStringLiteral("version"), QStringLiteral("v0.9"));
    QVERIFY(rec.open(QIODevice::WriteOnly | QIODevice::Truncate));
    rec.write(QJsonDocument(o).toJson());
    rec.close();
    QCOMPARE(m.status(id).state, RunnerManager::State::UpdateAvailable);

    QVERIFY(m.uninstall(id, &error));
    QCOMPARE(m.status(id).state, RunnerManager::State::NotInstalled);
}

void TestRunners::rejectsWrongHash()
{
    if (!QFileInfo::exists(m_zip))
        QSKIP("no zip tool on this machine");
    RunnerManager m;
    const QString id = QStringLiteral("victory-7900xtx");
    QSignalSpy done(&m, &RunnerManager::installFinished);
    QVERIFY(m.install(id));
    QVERIFY(done.wait(30000));
    QVERIFY(!done.first().at(1).toBool());
    QVERIFY(done.first().at(2).toString().contains(QStringLiteral("SHA-256")));
    const RunnerManager::Status s = m.status(id);
    QCOMPARE(s.state, RunnerManager::State::Failed);
    QVERIFY(s.executable.isEmpty());
    QVERIFY(!QFileInfo::exists(Runners::find(id)->installDir()));
}

void TestRunners::buildsFromSource()
{
    if (!m_haveGit)
        QSKIP("git is not installed");
    RunnerManager m;
    const QString id = QStringLiteral("victory-dexnom");
    QSignalSpy done(&m, &RunnerManager::installFinished);
    QVERIFY(m.install(id));
    QVERIFY(done.wait(60000));
    QVERIFY2(done.first().at(1).toBool(), qPrintable(done.first().at(2).toString()));
    const RunnerManager::Status s = m.status(id);
    QCOMPARE(s.state, RunnerManager::State::Installed);
    QCOMPARE(s.installedVersion, m_commit.left(7));
    QCOMPARE(QDir::cleanPath(QFileInfo(s.executable).absolutePath()), QDir::cleanPath(Runners::find(id)->installDir()));
    QVERIFY(QFileInfo(s.executable).size() > 0);
}

QTEST_GUILESS_MAIN(TestRunners)
#include "tst_runners.moc"
