#include "DevFixtures.h"
#include "GameManifest.h"
#include "Library.h"
#include "Paths.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestLibrary : public QObject {
    Q_OBJECT
private slots:
    void scansFixtures();
    void depthLimitAndDedupe();
    void manifestDecoding();
    void attrsMirrorTheEmulator();
    void titleDirName();
    void watcherPicksUpNewGames();
};

static QString fixtures()
{
    static QTemporaryDir dir;
    static const bool ok = DevFixtures::materialize(QStringLiteral(XSEMU_FIXTURES_DIR "/games"), dir.filePath(QStringLiteral("games")));
    Q_ASSERT(ok);
    return dir.filePath(QStringLiteral("games"));
}

void TestLibrary::scansFixtures()
{
    const GameList games = Library::scan({fixtures()});
    QCOMPARE(games.size(), 4);
    QStringList titles;
    for (const Game& g : games)
        titles << g.title();
    QVERIFY(titles.contains(QStringLiteral("Signal Garden")));
    QVERIFY(titles.contains(QStringLiteral("Coastline Drive")));
    QVERIFY(titles.contains(QStringLiteral("The Long Quiet Between Lighthouses")));

    for (const Game& g : games) {
        if (g.title() == QLatin1String("Signal Garden")) {
            QCOMPARE(g.identityName, QStringLiteral("NorthwindInteractive.SignalGarden"));
            QCOMPARE(g.version, QStringLiteral("1.0.4.0"));
            QCOMPARE(g.publisher, QStringLiteral("Northwind Interactive"));
            QCOMPARE(g.exe, QStringLiteral("SignalGarden.exe"));
            QVERIFY(g.exeExists);
            QVERIFY(g.sizeBytes > 0);
            QCOMPARE(g.titleId(), QStringLiteral("NorthwindInteractive.SignalGarden"));
        } else if (g.title() == QLatin1String("Coastline Drive")) {
            QCOMPARE(g.executables, QStringList{QStringLiteral("Binaries\\CoastlineDrive.exe")});
        } else if (g.identityName == QLatin1String("PaleMothStudio.LongQuiet")) {
            QCOMPARE(g.publisher, QStringLiteral("Pale Moth & Co."));
        } else if (g.identityName == QLatin1String("NorthwindInteractive.TidePools")) {
            QVERIFY(!g.exeExists);
            QCOMPARE(g.exe, QStringLiteral("TidePools.exe"));
        }
    }
}

void TestLibrary::depthLimitAndDedupe()
{
    QCOMPARE(Library::scan({fixtures()}, 0).size(), 0);
    QCOMPARE(Library::scan({fixtures()}, 1).size(), 3);
    QCOMPARE(Library::scan({fixtures(), fixtures() + QStringLiteral("/SignalGarden"), fixtures() + QStringLiteral("/../games")}).size(), 4);
    QCOMPARE(Library::scan({fixtures() + QStringLiteral("/SignalGarden")}).size(), 1);
    QCOMPARE(Library::scan({QStringLiteral("/does/not/exist")}).size(), 0);
}

void TestLibrary::manifestDecoding()
{
    const QString xml = QStringLiteral("<Game><Identity Name=\"A.B\" /><!-- <Identity Name=\"Hidden\" /> --></Game>");
    QCOMPARE(GameManifest::decode(xml.toUtf8()), QStringLiteral("<Game><Identity Name=\"A.B\" /></Game>"));
    QByteArray le("\xFF\xFE");
    QByteArray be("\xFE\xFF");
    for (const QChar c : xml) {
        le.append(char(c.unicode() & 0xFF)).append(char(c.unicode() >> 8));
        be.append(char(c.unicode() >> 8)).append(char(c.unicode() & 0xFF));
    }
    QCOMPARE(GameManifest::decode(le), GameManifest::decode(xml.toUtf8()));
    QCOMPARE(GameManifest::decode(be), GameManifest::decode(xml.toUtf8()));
    QVERIFY(GameManifest::decode(QByteArray("\xFF\xFE\x41")).isNull());
    QVERIFY(GameManifest::decode(QByteArray("\xC3\x28")).isNull());
    QCOMPARE(GameManifest::decode("a<!-- unterminated"), QStringLiteral("a"));
}

void TestLibrary::attrsMirrorTheEmulator()
{
    const QString xml = QStringLiteral(
        "<ExecutableList><Executable Name=\"a.exe\"/><Executable\tName=\"b.exe\" Id=\"x\"></ExecutableList>"
        "<ShellVisuals DefaultDisplayName=\"Ratchet &amp; Co &#39;s &#x41;\" Name=\"n\"/>");
    QCOMPARE(GameManifest::attrs(xml, QStringLiteral("Executable"), QStringLiteral("Name")), (QStringList{QStringLiteral("a.exe"), QStringLiteral("b.exe")}));
    QCOMPARE(GameManifest::attr(xml, QStringLiteral("ShellVisuals"), QStringLiteral("DefaultDisplayName")), QStringLiteral("Ratchet & Co 's A"));
    QCOMPARE(GameManifest::attr(xml, QStringLiteral("ShellVisuals"), QStringLiteral("Name")), QStringLiteral("n"));
    QVERIFY(GameManifest::attrs(xml, QStringLiteral("ExecutableList"), QStringLiteral("Name")).isEmpty());
}

void TestLibrary::titleDirName()
{
    QCOMPARE(Paths::titleDirName(QStringLiteral("Rockstar.Games_1-2")), QStringLiteral("Rockstar.Games_1-2"));
    QCOMPARE(Paths::titleDirName(QStringLiteral("My Game: Deluxe")), QStringLiteral("My_Game__Deluxe"));
    QCOMPARE(Paths::titleDirName(QString()), QStringLiteral("title"));
    QCOMPARE(Paths::titleDirName(QString::fromUtf8("Caf\xC3\xA9")), QString::fromUtf8("Caf\xC3\xA9"));
}

void TestLibrary::watcherPicksUpNewGames()
{
    QTemporaryDir dir;
    Library lib;
    QSignalSpy spy(&lib, &Library::gamesChanged);
    lib.setFolders({dir.path()});
    QVERIFY(spy.wait(5000));
    QCOMPARE(lib.games().size(), 0);
    QDir(dir.path()).mkpath(QStringLiteral("New"));
    QFile f(dir.filePath(QStringLiteral("New/MicrosoftGame.config")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("<Game><Identity Name=\"Test.New\" Version=\"1.0\"/></Game>");
    f.close();
    QTRY_COMPARE_WITH_TIMEOUT(lib.games().size(), 1, 8000);
    QCOMPARE(lib.games().first().title(), QStringLiteral("Test.New"));
    QCOMPARE(lib.game(QStringLiteral("Test.New")).identityName, QStringLiteral("Test.New"));
}

QTEST_GUILESS_MAIN(TestLibrary)
#include "tst_library.moc"
