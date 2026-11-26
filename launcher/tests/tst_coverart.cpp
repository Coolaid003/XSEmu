#include "AppSettings.h"
#include "CoverArtService.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestCoverArt : public QObject {
    Q_OBJECT
private slots:
    void urls();
    void parsing();
    void cacheRoundTrip();
    void noNetworkWithoutKeyOrWhenOff();
};

void TestCoverArt::urls()
{
    QCOMPARE(CoverArtService::searchUrl(QStringLiteral("Signal Garden")).toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.steamgriddb.com/api/v2/search/autocomplete/Signal%20Garden"));
    QCOMPARE(CoverArtService::searchUrl(QStringLiteral("A/B & C?")).toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.steamgriddb.com/api/v2/search/autocomplete/A%2FB%20%26%20C%3F"));
    QCOMPARE(CoverArtService::gridsUrl(1234).toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.steamgriddb.com/api/v2/grids/game/1234?dimensions=600x900&types=static"));
    QCOMPARE(CoverArtService::heroesUrl(1234).toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.steamgriddb.com/api/v2/heroes/game/1234?types=static"));
}

void TestCoverArt::parsing()
{
    const QByteArray found = "{\"success\":true,\"data\":[{\"id\":38,\"name\":\"Signal Garden\",\"types\":[\"steam\"],\"verified\":true}]}";
    const QByteArray empty = "{\"success\":true,\"data\":[]}";
    const QByteArray failed = "{\"success\":false,\"errors\":[\"bad key\"]}";
    const QByteArray images = "{\"success\":true,\"data\":[{\"id\":1,\"url\":\"http://insecure/x.png\"},{\"id\":2,\"url\":\"https://cdn.example/grid/a.png\",\"mime\":\"image/png\"}]}";
    QString name;
    const qint64 id = CoverArtService::parseSearch(found, &name);
    QCOMPARE(id, 38);
    QCOMPARE(name, QStringLiteral("Signal Garden"));
    QCOMPARE(CoverArtService::parseSearch(empty), 0);
    QCOMPARE(CoverArtService::parseSearch(failed), 0);
    QCOMPARE(CoverArtService::parseSearch("garbage"), 0);
    QCOMPARE(CoverArtService::parseFirstImage(images), QUrl(QStringLiteral("https://cdn.example/grid/a.png")));
    QVERIFY(!CoverArtService::parseFirstImage(empty).isValid());
}

void TestCoverArt::cacheRoundTrip()
{
    QTemporaryDir dir;
    {
        CoverCache cache(dir.path());
        QVERIFY(!cache.contains(QStringLiteral("T.One")));
        QFile img(dir.filePath(CoverCache::fileName(QStringLiteral("T.One"), CoverCache::Kind::Grid, QStringLiteral("PNG"))));
        QVERIFY(img.open(QIODevice::WriteOnly));
        img.write("png");
        img.close();
        CoverCache::Entry e;
        e.gameId = 99;
        e.name = QStringLiteral("One");
        e.grid = QStringLiteral("T.One-grid.png");
        e.fetchedAt = QDateTime::currentDateTimeUtc();
        cache.put(QStringLiteral("T.One"), e);
        CoverCache::Entry missing;
        missing.notFound = true;
        cache.put(QStringLiteral("T.Two"), missing);
    }
    CoverCache again(dir.path());
    QVERIFY(again.contains(QStringLiteral("T.One")));
    QCOMPARE(again.entry(QStringLiteral("T.One")).gameId, 99);
    QCOMPARE(again.path(QStringLiteral("T.One"), CoverCache::Kind::Grid), dir.filePath(QStringLiteral("T.One-grid.png")));
    QVERIFY(again.path(QStringLiteral("T.One"), CoverCache::Kind::Hero).isEmpty());
    QVERIFY(again.entry(QStringLiteral("T.Two")).notFound);
    again.remove(QStringLiteral("T.One"));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("T.One-grid.png"))));
    QVERIFY(!CoverCache(dir.path()).contains(QStringLiteral("T.One")));
}

void TestCoverArt::noNetworkWithoutKeyOrWhenOff()
{
    QTemporaryDir dir;
    AppSettings settings(dir.filePath(QStringLiteral("settings.ini")));
    CoverArtService covers(settings, dir.filePath(QStringLiteral("covers")));
    Game g;
    g.folder = dir.path();
    g.identityName = QStringLiteral("T.Game");
    QVERIFY(!covers.canLookUp());
    QVERIFY(covers.lookupsUnavailableReason().contains(QStringLiteral("off")));
    covers.request(g);
    QVERIFY(!covers.isPending(g.titleId()));
    settings.setArtLookups(true);
    QVERIFY(covers.lookupsUnavailableReason().contains(QStringLiteral("API key")));
    covers.request(g);
    QVERIFY(!covers.isPending(g.titleId()));
    settings.setSteamGridDbKey(QStringLiteral("key"));
    settings.setArtLookups(false);
    QVERIFY(!covers.canLookUp());
    covers.request(g);
    QVERIFY(!covers.isPending(g.titleId()));
    settings.setOffline(true);
    settings.setArtLookups(true);
    QVERIFY(covers.canLookUp());
}

QTEST_GUILESS_MAIN(TestCoverArt)
#include "tst_coverart.moc"
