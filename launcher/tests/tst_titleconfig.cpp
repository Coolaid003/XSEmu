#include "TitleConfig.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

static QString readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

class TestTitleConfig : public QObject {
    Q_OBJECT
private slots:
    void readsTheEmulatorsFile();
    void setReplacesInPlaceAndKeepsEverythingElse();
    void setAddsMissingKeysBeforeTheFirstTable();
    void tablesAreNotTopLevel();
    void createsAMissingFileWithOnlyTheKeys();
    void formatsEveryScalar();
    void parsesQuotingAndComments();
    void roundTripsStrings();
    void unwritableLeavesFileAlone();
};

void TestTitleConfig::readsTheEmulatorsFile()
{
    TitleConfig cfg(QStringLiteral(XSEMU_FIXTURES_DIR "/xcl-data/NorthwindInteractive.SignalGarden/xcl.toml"));
    const QVariantMap v = cfg.values();
    QCOMPARE(v.value(QStringLiteral("device_type")).toString(), QStringLiteral("lockhart"));
    QCOMPARE(v.value(QStringLiteral("window_width")).toLongLong(), 1280);
    QCOMPARE(v.value(QStringLiteral("keyboard_mouse")).toBool(), true);
    QCOMPARE(v.value(QStringLiteral("mouse_sensitivity")).toDouble(), 1.0);
    QCOMPARE(v.value(QStringLiteral("user_id")).toLongLong(), Q_INT64_C(2533274790395905));
    QVERIFY(!v.contains(QStringLiteral("D:")));
    QVERIFY(!v.contains(QStringLiteral("mounts")));
}

void TestTitleConfig::setReplacesInPlaceAndKeepsEverythingElse()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("xcl.toml"));
    QVERIFY(QFile::copy(QStringLiteral(XSEMU_FIXTURES_DIR "/xcl-data/NorthwindInteractive.SignalGarden/xcl.toml"), path));
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    const QString before = readFile(path);

    TitleConfig cfg(path);
    QVERIFY(cfg.set(QVariantMap{{QStringLiteral("device_type"), QStringLiteral("anaconda")},
                                {QStringLiteral("window_width"), 1920},
                                {QStringLiteral("mouse_sensitivity"), 1.25},
                                {QStringLiteral("d3d12_debug"), true}}));
    const QString after = readFile(path);
    QStringList b = before.split(QLatin1Char('\n'));
    QStringList a = after.split(QLatin1Char('\n'));
    QCOMPARE(a.size(), b.size());
    int changed = 0;
    for (int i = 0; i < a.size(); ++i) {
        if (a[i] != b[i])
            ++changed;
    }
    QCOMPARE(changed, 4);
    QVERIFY(after.contains(QStringLiteral("device_type = \"anaconda\"\n")));
    QVERIFY(after.contains(QStringLiteral("window_width = 1920\n")));
    QVERIFY(after.contains(QStringLiteral("mouse_sensitivity = 1.25\n")));
    QVERIFY(after.contains(QStringLiteral("d3d12_debug = true\n")));
    QVERIFY(after.contains(QStringLiteral("[mounts]\n\"D:\" = \"{game}\"")));
    QVERIFY(after.startsWith(QStringLiteral("# XCL – title configuration.")));
    QCOMPARE(cfg.value(QStringLiteral("device_type")).toString(), QStringLiteral("anaconda"));
}

void TestTitleConfig::setAddsMissingKeysBeforeTheFirstTable()
{
    const QString text = QStringLiteral("# head\ndevice_type = \"lockhart\"\n\n[mounts]\n\"G:\" = \"{game}\"\n");
    const QString out = TitleConfig::apply(text, {{QStringLiteral("raytracing"), QStringLiteral("off")}});
    QCOMPARE(out, QStringLiteral("# head\ndevice_type = \"lockhart\"\nraytracing = \"off\"\n\n[mounts]\n\"G:\" = \"{game}\"\n"));
    const QString onlyTable = QStringLiteral("[mounts]\n\"G:\" = \"{game}\"\n");
    QCOMPARE(TitleConfig::apply(onlyTable, {{QStringLiteral("gamertag"), QStringLiteral("Me")}}),
             QStringLiteral("gamertag = \"Me\"\n\n[mounts]\n\"G:\" = \"{game}\"\n"));
}

void TestTitleConfig::tablesAreNotTopLevel()
{
    const QString text = QStringLiteral("a = 1\n[stub_returns]\nb = 2\n");
    const QVariantMap v = TitleConfig::parse(text);
    QCOMPARE(v.size(), 1);
    const QString out = TitleConfig::apply(text, {{QStringLiteral("b"), 3}});
    QCOMPARE(out, QStringLiteral("a = 1\nb = 3\n[stub_returns]\nb = 2\n"));
}

void TestTitleConfig::createsAMissingFileWithOnlyTheKeys()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("Some.Title/xcl.toml"));
    TitleConfig cfg(path);
    QVERIFY(!cfg.exists());
    QVERIFY(cfg.set(QStringLiteral("device_type"), QStringLiteral("lockhart")));
    QCOMPARE(readFile(path), QStringLiteral("device_type = \"lockhart\"\n"));
    QVERIFY(cfg.set(QStringLiteral("keyboard_mouse"), false));
    QCOMPARE(readFile(path), QStringLiteral("device_type = \"lockhart\"\nkeyboard_mouse = false\n"));
}

void TestTitleConfig::formatsEveryScalar()
{
    QCOMPARE(TitleConfig::formatValue(true), QStringLiteral("true"));
    QCOMPARE(TitleConfig::formatValue(false), QStringLiteral("false"));
    QCOMPARE(TitleConfig::formatValue(42), QStringLiteral("42"));
    QCOMPARE(TitleConfig::formatValue(Q_INT64_C(-9000000000)), QStringLiteral("-9000000000"));
    QCOMPARE(TitleConfig::formatValue(1.0), QStringLiteral("1.0"));
    QCOMPARE(TitleConfig::formatValue(0.1), QStringLiteral("0.1"));
    QCOMPARE(TitleConfig::formatValue(QStringLiteral("a\"b\\c\n")), QStringLiteral("\"a\\\"b\\\\c\\n\""));
    QCOMPARE(TitleConfig::formatValue(QString(QChar(0x01))), QStringLiteral("\"\\u0001\""));
}

void TestTitleConfig::parsesQuotingAndComments()
{
    const QVariantMap v = TitleConfig::parse(QStringLiteral(
        "a = \"x # not a comment\" # comment\n"
        "b = 'literal \\ kept'\n"
        "c = 1_000\n"
        "d = -2.5e3\n"
        "\"quoted key\" = true\n"
        "  e   =   \"\\u00e9\"  \n"
        "f = [1, 2]\n"
        "g = { x = 1 }\n"));
    QCOMPARE(v.value(QStringLiteral("a")).toString(), QStringLiteral("x # not a comment"));
    QCOMPARE(v.value(QStringLiteral("b")).toString(), QStringLiteral("literal \\ kept"));
    QCOMPARE(v.value(QStringLiteral("c")).toLongLong(), 1000);
    QCOMPARE(v.value(QStringLiteral("d")).toDouble(), -2500.0);
    QCOMPARE(v.value(QStringLiteral("quoted key")).toBool(), true);
    QCOMPARE(v.value(QStringLiteral("e")).toString(), QString(QChar(0xE9)));
    QVERIFY(!v.contains(QStringLiteral("f")));
    QVERIFY(!v.contains(QStringLiteral("g")));
}

void TestTitleConfig::roundTripsStrings()
{
    const QString tricky = QStringLiteral("C:\\Games\\\"Quoted\"\tTab \u00e9");
    const QString out = TitleConfig::apply(QString(), {{QStringLiteral("exe"), tricky}});
    QCOMPARE(TitleConfig::parse(out).value(QStringLiteral("exe")).toString(), tricky);
}

void TestTitleConfig::unwritableLeavesFileAlone()
{
#ifdef Q_OS_UNIX
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("xcl.toml"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("a = 1\n");
    }
    QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::ExeOwner);
    QFile::setPermissions(path, QFile::ReadOwner);
    TitleConfig cfg(path);
    const bool ok = cfg.set(QStringLiteral("a"), 2);
    QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    if (ok)
        QSKIP("running with permission to write anyway (root)");
    QCOMPARE(readFile(path), QStringLiteral("a = 1\n"));
#endif
}

QTEST_GUILESS_MAIN(TestTitleConfig)
#include "tst_titleconfig.moc"
