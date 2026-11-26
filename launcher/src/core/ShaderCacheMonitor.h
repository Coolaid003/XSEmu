#pragma once

#include "ShaderCacheInfo.h"

#include <QElapsedTimer>
#include <QObject>

#include <memory>

// The one place that reads the shader cache. Scans run on a worker thread, one at a time; a request
// during a scan runs once after it. Pages read the last snapshot instantly and follow changed().
// pipelines.bin is parsed incrementally across scans, so a refresh costs the appended tail plus a
// walk of the folder.
class ShaderCacheMonitor : public QObject {
    Q_OBJECT
public:
    explicit ShaderCacheMonitor(QObject* parent = nullptr);
    ~ShaderCacheMonitor() override;

    const ShaderCacheInfo& snapshot() const { return m_snapshot; }
    bool hasSnapshot() const { return m_snapshot.scanned; }

    void refresh();
    void refreshIfStale(); // when the snapshot is older than 10 s

signals:
    void changed();

private:
    struct Worker;
    void start();
    void finish(const ShaderCacheInfo& info);

    ShaderCacheInfo m_snapshot;
    QElapsedTimer m_age;
    bool m_running = false;
    bool m_pending = false;
    std::shared_ptr<Worker> m_worker;
};
