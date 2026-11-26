#include "FakeRunner.h"

#include "Scene.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>

#include <cstdio>
#include <thread>

namespace FakeRunner {

namespace {

int tickMs()
{
    bool ok = false;
    const int v = qEnvironmentVariableIntValue("FAKE_XSEMU_TICK_MS", &ok);
    return ok && v > 0 ? v * 12 : 60;
}

void say(const QString& line)
{
    std::printf("%s\n", line.toUtf8().constData());
    std::fflush(stdout);
}

// Print lines(i) for i = 1..count, one per tick, then quit with code.
int printLines(int argc, char** argv, int count, std::function<QString(int)> line, std::function<QString()> last)
{
    QCoreApplication app(argc, argv);
    auto i = std::make_shared<int>(0);
    QTimer t;
    QObject::connect(&t, &QTimer::timeout, &app, [&, i] {
        if (++*i <= count) {
            say(line(*i));
            return;
        }
        if (last)
            say(last());
        app.exit(0);
    });
    t.start(tickMs());
    return app.exec();
}

// What the forks' Config::load refuses, read the way they read it.
QString victoryConfigError(const QString& kind)
{
    const QString path = qEnvironmentVariable("VICTORY_CONFIG");
    if (path.isEmpty())
        return QStringLiteral("fake: VICTORY_CONFIG is not set");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("VICTORY_CONFIG points at %1, which does not exist").arg(path);
    QStringList allowed = {QStringLiteral("paths.game_dir"), QStringLiteral("paths.cache_dir"), QStringLiteral("paths.data_dir"), QStringLiteral("paths.test_titles")};
    if (kind == QLatin1String("victory-dexnom"))
        allowed << QStringLiteral("paths.fidelityfx_dir") << QStringLiteral("graphics.ray_tracing");
    QString table;
    QStringList seen;
    for (const QString& raw : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            table = line.mid(1, line.size() - 2);
            continue;
        }
        const QString key = table + QLatin1Char('.') + line.section(QLatin1Char('='), 0, 0).trimmed();
        const QString value = line.section(QLatin1Char('='), 1).trimmed();
        if (!allowed.contains(key))
            return QStringLiteral("parsing %1: unknown field `%2`").arg(path, key);
        if (!value.startsWith(QLatin1Char('"')) || !value.endsWith(QLatin1Char('"')) || value.size() < 2)
            return QStringLiteral("parsing %1: %2 is not a string").arg(path, key);
        seen << key;
    }
    for (const QString& need : {QStringLiteral("paths.game_dir"), QStringLiteral("paths.cache_dir"), QStringLiteral("paths.data_dir")}) {
        if (!seen.contains(need))
            return QStringLiteral("fake: %1 has no %2").arg(path, need);
    }
    return {};
}

int runGame(const QString& kind, int argc, char** argv)
{
    const QString scenario = qEnvironmentVariable("FAKE_XSEMU_SCENARIO", QStringLiteral("ok"));
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(true);
    if (scenario == QLatin1String("files_missing")) {
        QTimer::singleShot(300, &app, [] {
            std::fprintf(stderr, "Error: the title executable could not be opened\n");
            std::fflush(stderr);
            QCoreApplication::exit(1);
        });
        return app.exec();
    }
    Scene scene;
    scene.setTitle(kind == QLatin1String("kelvaroq") ? QStringLiteral("Kelvaroq") : QStringLiteral("VIctory"));
    scene.resize(1280, 720);
    const int bootMs = scenario == QLatin1String("slow_boot") ? 6000 : 400;
    QTimer::singleShot(bootMs, &app, [&scene, kind] {
        scene.show();
        if (kind == QLatin1String("kelvaroq"))
            say(QStringLiteral("presenting to a window"));
    });
    if (scenario == QLatin1String("crash")) {
        QTimer::singleShot(bootMs + 2500, &app, [] {
            say(QStringLiteral("access violation in the title at 0x0000000140a1b2c3"));
            std::_Exit(3);
        });
    }
    return app.exec();
}

int precompile(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const int total = 812;
    auto done = std::make_shared<int>(0);
    say(QStringLiteral("precompile 0 %1 0").arg(total));
    QTimer t;
    auto finish = [&app, done, total] {
        say(QStringLiteral("precompile %1 %2 0").arg(*done).arg(total));
        app.exit(0);
    };
    QObject::connect(&t, &QTimer::timeout, &app, [&, done] {
        *done = qMin(total, *done + 9);
        if (*done >= total) {
            t.stop();
            finish();
            return;
        }
        say(QStringLiteral("precompile %1 %2 0").arg(*done).arg(total));
    });
    t.start(tickMs());
    // closing stdin asks it to stop: it saves what it has and exits 0
    std::thread([] {
        // read the descriptor, not stdio: exit() locks stdio streams and would wait forever on a getchar() blocked here
        QFile in;
        if (in.open(0, QIODevice::ReadOnly, QFileDevice::DontCloseHandle)) {
            char buf[256];
            while (in.read(buf, sizeof buf) > 0) {
            }
        }
        QMetaObject::invokeMethod(qApp, [] {
            QTimer::singleShot(100, qApp, [] { QCoreApplication::exit(0); });
        }, Qt::QueuedConnection);
    }).detach();
    const int code = app.exec();
    return code;
}

}

QString kind(const QString& program, QStringList& args)
{
    if (args.size() >= 2 && args.first() == QLatin1String("--as")) {
        const QString k = args.at(1);
        args = args.mid(2);
        return k;
    }
    const QString base = QFileInfo(program).completeBaseName();
    if (base == QLatin1String("kelvaroq-engine"))
        return QStringLiteral("kelvaroq");
    if (base == QLatin1String("victory"))
        return program.contains(QLatin1String("dexnom")) ? QStringLiteral("victory-dexnom") : QStringLiteral("victory");
    return {};
}

int run(const QString& kind, const QStringList& args, int argc, char** argv)
{
    const QString cmd = args.value(0);
    if (kind.startsWith(QLatin1String("victory"))) {
        if (const QString why = victoryConfigError(kind); !why.isEmpty()) {
            std::fprintf(stderr, "Error: %s\n", why.toUtf8().constData());
            return 1;
        }
    }
    if (cmd == QLatin1String("run"))
        return runGame(kind, argc, argv);
    if (qEnvironmentVariable("FAKE_XSEMU_SCENARIO") == QLatin1String("silent"))
        return 0;
    if (kind == QLatin1String("kelvaroq") && cmd == QLatin1String("precompile")) {
        if (args.contains(QStringLiteral("--check"))) {
            say(QStringLiteral("precompile-check %1").arg(qEnvironmentVariable("FAKE_XSEMU_WAITING", QStringLiteral("812"))));
            return 0;
        }
        return precompile(argc, argv);
    }
    if (kind == QLatin1String("victory") && cmd == QLatin1String("shaders"))
        return printLines(argc, argv, 4, [](int i) { return QStringLiteral("%1 shaders in the cache, %2 s").arg(i * 1000).arg(i); },
                          [] { return QStringLiteral("4210 shaders in the cache, 3 do not translate, 5 s"); });
    if (kind == QLatin1String("victory") && cmd == QLatin1String("warm")) {
        QFile book(qEnvironmentVariable("VICTORY_CACHE_DIR") + QStringLiteral("/pipelines/known-pipelines.bin"));
        const int known = book.open(QIODevice::ReadOnly) ? int(book.readAll().count('\n')) : 0;
        if (known < 1996) {
            say(QStringLiteral("%1 pipelines known, 0 made before (last run), 0 bad; 0 to make, 16 processes").arg(known));
            say(QStringLiteral("0 made in 0 s (0 have no host pipeline, 0 ended their process)"));
            return 0;
        }
        say(QStringLiteral("%1 pipelines known, 7004 made before (last run), 4 bad; 1996 to make, 16 processes").arg(known));
        return printLines(argc, argv, 20, [](int i) { return QStringLiteral("%1 of 1996, %2 s").arg(qMin(1996, i * 100)).arg(i); }, {});
    }
    if (kind == QLatin1String("victory-dexnom") && cmd == QLatin1String("warm"))
        return printLines(argc, argv, 3, [](int i) { return QStringLiteral("%1 s  %2 shaders").arg(i * 2, 5).arg(i * 2000); },
                          [] { return QStringLiteral("warm         4210 translated, 3 refused; 812 compute pipelines, 0 refused; 0 omitted by ray-tracing mode; 7 s"); });
    std::fprintf(stderr, "fake %s: unknown command\n", kind.toUtf8().constData());
    return 2;
}

}
