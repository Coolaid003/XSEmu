// fake-xsemu: the XSEmu.exe command line of docs/launcher-protocol.md for launcher development.
//   fake-xsemu run [--host-hwnd <hex>] [--control-pipe <name>] <game folder> [-- args]
//   fake-xsemu warm-up [--full] [--progress-json]
// FAKE_XSEMU_SCENARIO = ok | crash | device_removed | files_missing | slow_boot | warmup_fail |
//                       warmup_ram_floor | warmup_refuse
#include "FakeRun.h"
#include "FakeWarmup.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QStringList>

#include <cstdio>
#include <memory>

static int usage()
{
    std::fprintf(stderr, "usage: fake-xsemu run [--host-hwnd <hex>] [--control-pipe <name>] <game folder> [-- args]\n"
                         "       fake-xsemu warm-up [--full] [--progress-json]\n");
    return 2;
}

int main(int argc, char** argv)
{
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    const QString scenario = qEnvironmentVariable("FAKE_XSEMU_SCENARIO", QStringLiteral("ok"));
    if (args.isEmpty())
        return usage();

    if (args.first() == QLatin1String("warm-up")) {
        QCoreApplication app(argc, argv);
        FakeWarmup warmup(args.contains(QStringLiteral("--full")), args.contains(QStringLiteral("--progress-json")), scenario);
        QMetaObject::invokeMethod(&warmup, &FakeWarmup::start, Qt::QueuedConnection);
        return app.exec();
    }
    if (args.first() != QLatin1String("run"))
        return usage();

    FakeRun::Options o;
    o.scenario = scenario;
    int i = 1;
    for (; i < args.size(); ++i) {
        const QString& a = args[i];
        if (a == QLatin1String("--host-hwnd") && i + 1 < args.size()) {
            QString hex = args[++i];
            if (hex.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
                hex = hex.mid(2);
            o.hostHwnd = static_cast<quintptr>(hex.toULongLong(nullptr, 16));
        } else if (a == QLatin1String("--control-pipe") && i + 1 < args.size()) {
            o.pipe = args[++i];
        } else {
            break;
        }
    }
    if (i >= args.size())
        return usage();
    o.folder = args[i];

    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(true);
    FakeRun run(o);
    QMetaObject::invokeMethod(&run, &FakeRun::start, Qt::QueuedConnection);
    return app.exec();
}
