#include "FakeRun.h"

#include "Scene.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QWindow>

#include <cstdio>

#ifdef FAKE_XSEMU_XCB
#include <xcb/xcb.h>
#endif

namespace {

qint64 freeRamMb()
{
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly))
        return 9830;
    for (const QByteArray& line : f.readAll().split('\n')) {
        if (line.startsWith("MemAvailable:"))
            return line.mid(13).simplified().split(' ').value(0).toLongLong() / 1024;
    }
    return 9830;
}

QSize hostSize(quintptr host)
{
#ifdef FAKE_XSEMU_XCB
    static xcb_connection_t* conn = xcb_connect(nullptr, nullptr);
    if (!conn || xcb_connection_has_error(conn))
        return {};
    xcb_get_geometry_reply_t* g = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, static_cast<xcb_drawable_t>(host)), nullptr);
    if (!g)
        return {};
    const QSize s(g->width, g->height);
    free(g);
    return s;
#else
    Q_UNUSED(host);
    return {};
#endif
}

bool hasManifest(const QString& folder)
{
    const QDir d(folder);
    for (const QString& f : d.entryList(QDir::Files)) {
        if (f.compare(QLatin1String("MicrosoftGame.config"), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

}

FakeRun::FakeRun(Options o, QObject* parent)
    : QObject(parent)
    , m_o(std::move(o))
{
    m_stats.setInterval(1000);
    connect(&m_stats, &QTimer::timeout, this, &FakeRun::sendStats);
    connect(&m_socket, &QLocalSocket::readyRead, this, [this] {
        m_buffer += m_socket.readAll();
        qsizetype nl;
        while ((nl = m_buffer.indexOf('\n')) >= 0) {
            onCommand(m_buffer.left(nl));
            m_buffer.remove(0, nl + 1);
        }
    });
    connect(&m_socket, &QLocalSocket::disconnected, this, [] {
        // the launcher went away: end like a title whose host closed
        std::fprintf(stderr, "fake-xsemu: control pipe closed, exiting\n");
        QCoreApplication::exit(0);
    });
}

void FakeRun::send(const QJsonObject& o)
{
    if (m_socket.state() != QLocalSocket::ConnectedState)
        return;
    m_socket.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
    m_socket.flush();
}

void FakeRun::sendState(const char* state)
{
    m_state = QLatin1String(state);
    send({{QStringLiteral("ev"), QStringLiteral("state")}, {QStringLiteral("state"), m_state}});
}

void FakeRun::start()
{
    m_clock.start();
    m_scene = new Scene;
    if (m_o.hostHwnd != 0) {
        m_host = QWindow::fromWinId(static_cast<WId>(m_o.hostHwnd));
        m_scene->setParent(m_host);
        m_scene->setFlags(Qt::FramelessWindowHint);
        followHost();
        m_follow.setInterval(100);
        connect(&m_follow, &QTimer::timeout, this, &FakeRun::followHost);
        m_follow.start();
        m_scene->show();
    } else {
        m_scene->setTitle(QStringLiteral("fake-xsemu – %1").arg(QDir(m_o.folder).dirName()));
        m_scene->resize(1280, 720);
        m_scene->show();
    }
    m_scene->setForwardKeys(m_o.hostHwnd != 0); // keys go to the launcher only when hosted
    connect(m_scene, &Scene::launcherKey, this, [this](const QString& key) {
        send({{QStringLiteral("ev"), QStringLiteral("key")}, {QStringLiteral("key"), key}});
    });

    if (!m_o.pipe.isEmpty()) {
        m_socket.connectToServer(m_o.pipe);
        if (!m_socket.waitForConnected(3000))
            std::fprintf(stderr, "fake-xsemu: could not connect to control pipe %s\n", qPrintable(m_o.pipe));
    }
    QJsonObject hello{{QStringLiteral("ev"), QStringLiteral("hello")},
                      {QStringLiteral("proto"), 1},
                      {QStringLiteral("pid"), QCoreApplication::applicationPid()},
                      {QStringLiteral("version"), QStringLiteral("0.0.0-fake")}};
    if (m_o.scenario == QLatin1String("files_missing") || !hasManifest(m_o.folder)) {
        // a boot failure in the outer "XSEmu run": hello without a title, then exit reason error
        send(hello);
        QTimer::singleShot(400, this, [this] {
            exitWith("error", 2, QStringLiteral("game files missing: %1 has no MicrosoftGame.config or its executable")
                                     .arg(QDir::toNativeSeparators(m_o.folder)));
        });
        return;
    }
    hello.insert(QStringLiteral("title"), QDir(m_o.folder).dirName());
    hello.insert(QStringLiteral("device_type"), QStringLiteral("lockhart"));
    if (m_o.hostHwnd != 0)
        hello.insert(QStringLiteral("hwnd"), QString::number(static_cast<qulonglong>(m_scene->winId()), 16));
    send(hello);
    sendState("booting");
    m_stats.start();
    const int bootMs = m_o.scenario == QLatin1String("slow_boot") ? 8000 : 1500;
    QTimer::singleShot(bootMs, this, &FakeRun::enterRunning);
}

void FakeRun::enterRunning()
{
    if (m_state == QLatin1String("paused")) {
        // boot finishes while paused: the first frame waits for resume
        m_scene->setBooting(false);
        return;
    }
    if (m_state != QLatin1String("booting"))
        return;
    m_scene->setBooting(false);
    sendState("running");
    if (m_o.scenario == QLatin1String("crash")) {
        QTimer::singleShot(3000, this, [this] {
            send({{QStringLiteral("ev"), QStringLiteral("exit")}, {QStringLiteral("code"), -1073741819},
                  {QStringLiteral("reason"), QStringLiteral("crash")},
                  {QStringLiteral("message"), QStringLiteral("EXCEPTION_ACCESS_VIOLATION reading 0x0000000000000010 in fake_title.exe+0x1a2b30")}});
            std::fflush(nullptr);
            std::_Exit(static_cast<int>(0xC0000005u));
        });
    } else if (m_o.scenario == QLatin1String("device_removed")) {
        QTimer::singleShot(3000, this, [this] {
            exitWith("device_removed", static_cast<int>(0x887A0006u), QStringLiteral("DXGI_ERROR_DEVICE_HUNG (0x887A0006): the GPU stopped responding"));
        });
    }
}

void FakeRun::sendStats()
{
    // once a second while booting or running; nothing while paused
    if (m_state != QLatin1String("booting") && m_state != QLatin1String("running"))
        return;
    QJsonObject o{{QStringLiteral("ev"), QStringLiteral("stats")}, {QStringLiteral("ram_free_mb"), freeRamMb()}};
    const Scene::Window w = m_scene ? m_scene->takeWindow() : Scene::Window{};
    if (m_state == QLatin1String("running")) {
        m_pipelines += QRandomGenerator::global()->bounded(40);
        o.insert(QStringLiteral("fps"), w.frames);
        o.insert(QStringLiteral("frame_ms"), qRound(w.meanMs * 10) / 10.0);
        o.insert(QStringLiteral("worst_ms"), qRound(w.worstMs * 10) / 10.0);
        o.insert(QStringLiteral("stalls"), w.stalls);
        o.insert(QStringLiteral("pipelines"), m_pipelines);
        o.insert(QStringLiteral("compiling"), QRandomGenerator::global()->bounded(3));
    }
    // a fake has no adapter to query: vram_used_mb / vram_budget_mb are left out, as the protocol asks
    send(o);
}

void FakeRun::onCommand(const QByteArray& line)
{
    const QJsonObject o = QJsonDocument::fromJson(line).object();
    const QString cmd = o.value(QStringLiteral("cmd")).toString();
    if (cmd == QLatin1String("pause") && (m_state == QLatin1String("running") || m_state == QLatin1String("booting"))) {
        m_scene->setPaused(true);
        sendState("paused");
    } else if (cmd == QLatin1String("resume") && m_state == QLatin1String("paused")) {
        m_scene->setPaused(false);
        sendState(m_scene->framePresentedYet() ? "running" : "booting");
    } else if (cmd == QLatin1String("stop")) {
        sendState("stopping");
        QTimer::singleShot(300, this, [this] { exitWith("stopped", 0, QString()); });
    } else if (cmd == QLatin1String("overlay")) {
        const QJsonValue on = o.value(QStringLiteral("on"));
        m_overlay = on.isBool() ? on.toBool() : !m_overlay;
        m_scene->setOverlay(m_overlay);
    } else if (cmd == QLatin1String("focus")) {
        m_scene->requestActivate();
    }
}

void FakeRun::exitWith(const char* reason, int code, const QString& message)
{
    send({{QStringLiteral("ev"), QStringLiteral("exit")}, {QStringLiteral("code"), code},
          {QStringLiteral("reason"), QLatin1String(reason)}, {QStringLiteral("message"), message}});
    m_socket.waitForBytesWritten(500);
    QCoreApplication::exit(code);
}

void FakeRun::followHost()
{
    if (!m_host || !m_scene)
        return;
    // Qt does not track a foreign window's size, so ask the window system, the way the real emulator
    // polls GetClientRect on the host.
    const QSize s = hostSize(m_o.hostHwnd);
    if (s.isValid() && m_scene->geometry() != QRect(QPoint(0, 0), s))
        m_scene->setGeometry(QRect(QPoint(0, 0), s));
}
