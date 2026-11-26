#include "ShaderCacheInfo.h"

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
    void titleState();
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

void TestShaderCache::titleState()
{
    ShaderCacheInfo cache;
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
}

QTEST_GUILESS_MAIN(TestShaderCache)
#include "tst_shadercache.moc"
