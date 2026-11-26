#include "ShaderCacheMonitor.h"

#include "Paths.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>

struct ShaderCacheMonitor::Worker {
    std::atomic<bool> cancel{false};
    ShaderCacheInfo::Manifest manifest; // touched only by the running scan
};

ShaderCacheMonitor::ShaderCacheMonitor(QObject* parent)
    : QObject(parent)
    , m_worker(std::make_shared<Worker>())
{
}

ShaderCacheMonitor::~ShaderCacheMonitor()
{
    m_worker->cancel = true;
}

void ShaderCacheMonitor::refresh()
{
    if (m_running) {
        m_pending = true;
        return;
    }
    start();
}

void ShaderCacheMonitor::refreshIfStale()
{
    if (!m_running && (!m_age.isValid() || m_age.elapsed() > 10000))
        start();
}

void ShaderCacheMonitor::start()
{
    m_running = true;
    m_pending = false;
    const QString dir = Paths::dxilCacheDir();
    const QPointer<ShaderCacheMonitor> self = this;
    QThreadPool::globalInstance()->start([dir, self, worker = m_worker] {
        ShaderCacheInfo info = ShaderCacheInfo::read(dir, worker->manifest, &worker->cancel);
        if (worker->cancel)
            return;
        QMetaObject::invokeMethod(qApp, [self, info] {
            if (self)
                self->finish(info);
        });
    });
}

void ShaderCacheMonitor::finish(const ShaderCacheInfo& info)
{
    m_running = false;
    // a scan of a folder the setting no longer points at is dropped, and the new one scanned
    if (info.dxilDir != Paths::dxilCacheDir()) {
        start();
        return;
    }
    m_snapshot = info;
    m_age.start();
    emit changed();
    if (m_pending)
        start();
}
