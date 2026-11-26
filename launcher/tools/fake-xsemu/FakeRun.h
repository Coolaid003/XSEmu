#pragma once

#include <QElapsedTimer>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QTimer>

class Scene;
class QWindow;

// "run": the control pipe side of docs/launcher-protocol.md and a window with the test scene.
class FakeRun : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString folder;
        QString pipe;
        quintptr hostHwnd = 0;
        QString scenario; // ok, crash, device_removed, files_missing, slow_boot
    };

    explicit FakeRun(Options o, QObject* parent = nullptr);
    void start();

private:
    void send(const QJsonObject& o);
    void sendState(const char* state);
    void onCommand(const QByteArray& line);
    void enterRunning();
    void sendStats();
    void exitWith(const char* reason, int code, const QString& message);
    void followHost();

    Options m_o;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    QPointer<Scene> m_scene;
    QWindow* m_host = nullptr;
    QTimer m_stats;
    QTimer m_follow;
    QString m_state = QStringLiteral("booting");
    bool m_overlay = false;
    QElapsedTimer m_clock;
    qint64 m_pipelines = 0;
};
