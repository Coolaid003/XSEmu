#include "ShaderCacheInfo.h"
#include "ShaderCacheMonitor.h"

#include <QSignalSpy>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestShaderCache : public QObject {
    Q_OBJECT
private slots:
    void fnvMatchesTheEmulator();
    void missingIsUnknown();
    void readsManifestRunsAndBlobs();
    void incrementalParseMatchesFullParse();
    void monitorKeepsASnapshot();
    void titleState();
    void clearKeepsWhatIsNotTheCaches();
};

static void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

static QByteArray entry(const QByteArray& body)
{
    QByteArray out;
    const quint32 n = static_cast<quint32>(body.size());
    out.append(char(n & 0xFF)).append(char((n >> 8) & 0xFF)).append(char((n >> 16) & 0xFF)).append(char(n >> 24));
    return out + body;
}

void TestShaderCache::fnvMatchesTheEmulator()
{
    QCOMPARE(ShaderCacheInfo::fnv64("", 0), Q_UINT64_C(0xcbf29ce484222325));
    QCOMPARE(ShaderCacheInfo::fnv64("a", 1), Q_UINT64_C(0xaf63dc4c8601ec8c));
    QCOMPARE(ShaderCacheInfo::fnv64("foobar", 6), Q_UINT64_C(0x85944171f73967e8));
}

void TestShaderCache::missingIsUnknown()
{
    QTemporaryDir dir;
    const ShaderCacheInfo none = ShaderCacheInfo::read(dir.filePath(QStringLiteral("nope")));
    QVERIFY(!none.exists);
    QCOMPARE(none.pipelines, -1);
    QCOMPARE(none.blobs, -1);
    const ShaderCacheInfo empty = ShaderCacheInfo::read(dir.path());
    QVERIFY(empty.exists);
    QCOMPARE(empty.pipelines, -1);
    QCOMPARE(empty.runs, -1);
    QCOMPARE(empty.blobs, -1);
}

void TestShaderCache::readsManifestRunsAndBlobs()
{
    QTemporaryDir dir;
    QByteArray manifest = entry("\x01\x00first") + entry("\x01\x01second") + entry("\x01\x00first");
    manifest.append("\x09\x00\x00\x00torn");
    writeFile(dir.filePath(QStringLiteral("pipelines/pipelines.bin")), manifest);
    writeFile(dir.filePath(QStringLiteral("pipelines/runs")), "7\n");
    writeFile(dir.filePath(QStringLiteral("driver/10de-2786-0000000000000001/g0000000000000001.pso")), QByteArray(100, 'x'));
    writeFile(dir.filePath(QStringLiteral("driver/10de-2786-0000000000000001/c0000000000000002.pso")), QByteArray(50, 'x'));
    writeFile(dir.filePath(QStringLiteral("driver/10de-2786-0000000000000001/abc.key")), "note");
    writeFile(dir.filePath(QStringLiteral("driver/unknown/x.pso")), QByteArray(10, 'x'));
    const ShaderCacheInfo info = ShaderCacheInfo::read(dir.path());
    QCOMPARE(info.manifestEntries, 3);
    QCOMPARE(info.pipelines, 2);
    QCOMPARE(info.runs, 7);
    QCOMPARE(info.blobs, 3);
    QCOMPARE(info.blobBytes, 160);
    QCOMPARE(info.adapters, 2);
    QVERIFY(info.newestBlob.isValid());
}

static QByteArray entries(int from, int to)
{
    QByteArray out;
    for (int i = from; i < to; ++i)
        out += entry(QByteArray("pipeline-") + QByteArray::number(i % 50) + QByteArray(i % 7 * 300, 'x'));
    return out;
}

void TestShaderCache::incrementalParseMatchesFullParse()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("pipelines.bin"));
    const QByteArray all = entries(0, 400);
    const qsizetype cut = all.size() / 3 + 5; // ends inside an entry, as a manifest being written does
    writeFile(path, all.left(cut));
    ShaderCacheInfo::Manifest m;
    QVERIFY(ShaderCacheInfo::readManifest(path, m));
    const qint64 firstEntries = m.entries;
    QVERIFY(firstEntries > 0 && m.offset < cut);

    QFile f(path);
    QVERIFY(f.open(QIODevice::Append));
    f.write(all.mid(cut));
    f.close();
    QVERIFY(ShaderCacheInfo::readManifest(path, m));
    ShaderCacheInfo::Manifest full;
    QVERIFY(ShaderCacheInfo::readManifest(path, full));
    QCOMPARE(m.entries, qint64(400));
    QCOMPARE(m.entries, full.entries);
    QCOMPARE(m.keys, full.keys);
    QCOMPARE(m.offset, qint64(all.size()));

    // a shorter file, a rewritten start or another path is parsed from scratch
    writeFile(path, entries(0, 10));
    QVERIFY(ShaderCacheInfo::readManifest(path, m));
    QCOMPARE(m.entries, qint64(10));
    writeFile(path, entries(100, 110) + entries(0, 400));
    QVERIFY(ShaderCacheInfo::readManifest(path, m));
    QCOMPARE(m.entries, qint64(410));
    const QString other = dir.filePath(QStringLiteral("other.bin"));
    writeFile(other, entries(0, 3));
    QVERIFY(ShaderCacheInfo::readManifest(other, m));
    QCOMPARE(m.entries, qint64(3));
    QVERIFY(!ShaderCacheInfo::readManifest(dir.filePath(QStringLiteral("gone.bin")), m));
    QCOMPARE(m.entries, qint64(0));
}

void TestShaderCache::monitorKeepsASnapshot()
{
    QTemporaryDir dir;
    writeFile(dir.filePath(QStringLiteral("pipelines/pipelines.bin")), entries(0, 20));
    writeFile(dir.filePath(QStringLiteral("driver/10de-2786-1/a.pso")), QByteArray(30, 'x'));
    writeFile(dir.filePath(QStringLiteral("0123456789abcdef.dxil")), QByteArray(70, 'x'));
    qputenv("XCL_DXIL_CACHE", dir.path().toUtf8());
    ShaderCacheMonitor monitor;
    QVERIFY(!monitor.hasSnapshot());
    QSignalSpy changed(&monitor, &ShaderCacheMonitor::changed);
    monitor.refresh();
    monitor.refresh();
    monitor.refresh(); // coalesced into one more scan after the running one
    QTRY_VERIFY(monitor.hasSnapshot());
    QTRY_COMPARE(changed.count(), 2);
    QTest::qWait(50);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(monitor.snapshot().pipelines, 20);
    QCOMPARE(monitor.snapshot().blobs, 1);
    QCOMPARE(monitor.snapshot().totalBytes, qint64(QFileInfo(dir.filePath(QStringLiteral("pipelines/pipelines.bin"))).size() + 30 + 70));

    QFile f(dir.filePath(QStringLiteral("pipelines/pipelines.bin")));
    QVERIFY(f.open(QIODevice::Append));
    f.write(entries(50, 60));
    f.close();
    monitor.refreshIfStale(); // fresh: nothing to do
    QTest::qWait(50);
    QCOMPARE(changed.count(), 2);
    monitor.refresh();
    QTRY_COMPARE(changed.count(), 3);
    QCOMPARE(monitor.snapshot().manifestEntries, qint64(30));
    qunsetenv("XCL_DXIL_CACHE");
}

void TestShaderCache::titleState()
{
    ShaderCacheInfo cache;
    cache.scanned = true;
    QCOMPARE(titleShaderState(QStringLiteral("A"), {}, cache), TitleShaderState::NeverWarmed);
    cache.exists = true;
    cache.blobs = 10;
    QCOMPARE(titleShaderState(QStringLiteral("A"), {}, cache), TitleShaderState::Unknown);
    WarmupRun ok;
    ok.titleId = QStringLiteral("A");
    ok.result = WarmupRun::Result::Finished;
    WarmupRun bad;
    bad.titleId = QStringLiteral("A");
    bad.result = WarmupRun::Result::Failed;
    QCOMPARE(titleShaderState(QStringLiteral("A"), {bad, ok}, cache), TitleShaderState::LastRunFailed);
    QCOMPARE(titleShaderState(QStringLiteral("A"), {ok, bad}, cache), TitleShaderState::Ready);
    QCOMPARE(titleShaderState(QStringLiteral("B"), {ok}, cache), TitleShaderState::Unknown);

    // after Clear the finished warm-up no longer makes the game ready
    ShaderCacheInfo cleared;
    cleared.scanned = true;
    cleared.exists = true;
    cleared.blobs = -1;
    QCOMPARE(titleShaderState(QStringLiteral("A"), {ok}, cleared), TitleShaderState::NeverWarmed);
    cleared.blobs = 0;
    QCOMPARE(titleShaderState(QStringLiteral("A"), {ok}, cleared), TitleShaderState::NeverWarmed);
    QCOMPARE(titleShaderState(QStringLiteral("A"), {bad, ok}, cleared), TitleShaderState::LastRunFailed);

    // before the first scan only the launcher's own records count
    const ShaderCacheInfo unscanned;
    QCOMPARE(titleShaderState(QStringLiteral("A"), {}, unscanned), TitleShaderState::Unknown);
    QCOMPARE(titleShaderState(QStringLiteral("A"), {ok}, unscanned), TitleShaderState::Ready);
}

void TestShaderCache::clearKeepsWhatIsNotTheCaches()
{
    QTemporaryDir dir;
    const QDir root(dir.path());
    writeFile(root.filePath(QStringLiteral("driver/10de-2786-1/a.pso")), "blob");
    writeFile(root.filePath(QStringLiteral("pipelines/pipelines.bin")), "x");
    writeFile(root.filePath(QStringLiteral("translated/v2/000000000000002a/b.bin")), "x");
    writeFile(root.filePath(QStringLiteral("00112233aabbccdd.dxil")), "dxil");
    writeFile(root.filePath(QStringLiteral("00112233aabbccdd.tmp4242")), "partial");
    writeFile(root.filePath(QStringLiteral("used-pipelines.txt")), "x");
    writeFile(root.filePath(QStringLiteral("skipped-pipelines.txt")), "x");
    writeFile(root.filePath(QStringLiteral("notes.txt")), "mine");
    writeFile(root.filePath(QStringLiteral("photos/holiday.jpg")), "mine");
    writeFile(root.filePath(QStringLiteral("cafe.dxil")), "mine");

    const ShaderCacheInfo::Cleared r = ShaderCacheInfo::clear(dir.path());
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.kept, (QStringList{QStringLiteral("cafe.dxil"), QStringLiteral("notes.txt"), QStringLiteral("photos")}));
    QVERIFY(QFile::exists(root.filePath(QStringLiteral("notes.txt"))));
    QVERIFY(QFile::exists(root.filePath(QStringLiteral("photos/holiday.jpg"))));
    QVERIFY(QFile::exists(root.filePath(QStringLiteral("cafe.dxil"))));
    for (const char* gone : {"driver", "pipelines", "translated", "00112233aabbccdd.dxil", "00112233aabbccdd.tmp4242", "used-pipelines.txt", "skipped-pipelines.txt"})
        QVERIFY2(!QFileInfo::exists(root.filePath(QLatin1String(gone))), gone);
    QVERIFY(root.exists());

    QVERIFY(!ShaderCacheInfo::clear(QDir::homePath()).ok);
}

QTEST_GUILESS_MAIN(TestShaderCache)
#include "tst_shadercache.moc"
