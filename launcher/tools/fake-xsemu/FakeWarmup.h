#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

// "warm-up [--full] --progress-json": realistic per-phase progress, quickly, with cancel on stdin.
class FakeWarmup : public QObject {
    Q_OBJECT
public:
    FakeWarmup(bool full, bool json, QString scenario, QObject* parent = nullptr);
    void start();

private:
    void tick();
    void emitLine(const QJsonObject& o);
    void finish(bool cancelled);
    void onStdinLine(const QByteArray& line);
    void saveBuilt() const;

    struct Phase {
        const char* id;
        qint64 total;
    };
    bool m_full;
    bool m_json;
    QString m_scenario;
    QList<Phase> m_phases;
    int m_phase = 0;
    qint64 m_done = 0;
    qint64 m_built = 0;
    qint64 m_failed = 0;
    qint64 m_boundBefore = 0; // bound pipelines earlier runs built (kept like the real cache)
    qint64 m_boundBuilt = 0;
    QString m_stateFile;      // <XCL_DXIL_CACHE>/fake-warmup-built, only when XCL_DXIL_CACHE is set
    bool m_cancel = false;
    bool m_holdForRam = false;
    int m_ramTicks = 0;
    int m_sinceLine = 0;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QElapsedTimer m_phaseClock;
};
