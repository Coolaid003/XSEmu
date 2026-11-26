#include "RunnerManager.h"

#include "Paths.h"
#include "RunnerInstall.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThreadPool>

namespace {

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QJsonObject installRecord(const QString& id) { return readJson(QDir(Paths::runnerDir(id)).filePath(QStringLiteral("runner.json"))); }

}

RunnerManager::RunnerManager(QObject* parent)
    : QObject(parent)
{
}

RunnerManager::~RunnerManager()
{
    if (m_task)
        m_task->cancel();
}

RunnerManager::Status RunnerManager::status(const QString& id) const
{
    if (m_simulated.contains(id))
        return m_simulated.value(id);
    Status s;
    const RunnerSpec* spec = Runners::find(id);
    if (!spec) {
        s.state = State::Unavailable;
        return s;
    }
    s.pinnedVersion = spec->pinnedVersion();
    if (spec->isBuiltIn()) {
        s.state = State::BuiltIn;
        return s;
    }
    const QJsonObject rec = installRecord(id);
    if (!rec.isEmpty()) {
        s.installedVersion = rec.value(QLatin1String("version")).toString();
        s.executable = QDir(spec->installDir()).filePath(rec.value(QLatin1String("executable")).toString());
    }
    if (m_taskId == id && m_task) {
        s.state = State::Installing;
        s.progress = m_task->progress();
        return s;
    }
    s.error = m_errors.value(id);
    if (s.error.isEmpty())
        s.error = readJson(RunnerInstall::failurePath(id)).value(QLatin1String("error")).toString();
    if (!rec.isEmpty())
        s.state = spec->isPinned() && s.installedVersion != s.pinnedVersion ? State::UpdateAvailable : State::Installed;
    else if (!s.error.isEmpty())
        s.state = State::Failed;
    else
        s.state = spec->isPinned() ? State::NotInstalled : State::Unavailable;
    return s;
}

bool RunnerManager::isRunnable(const QString& id) const
{
    const Status s = status(id);
    return s.state == State::BuiltIn || ((s.state == State::Installed || s.state == State::UpdateAvailable) && QFileInfo::exists(s.executable));
}

bool RunnerManager::isInstallable(const QString& id) const
{
    if (isRunnable(id))
        return true;
    const RunnerSpec* spec = Runners::find(id);
    return spec && spec->isPinned();
}

QString RunnerManager::busyId() const
{
    return m_task ? m_taskId : QString();
}

bool RunnerManager::install(const QString& id, QString* error)
{
    auto refuse = [error](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    const RunnerSpec* spec = Runners::find(id);
    if (!spec || spec->isBuiltIn())
        return refuse(QStringLiteral("XSEmu comes with the launcher and has nothing to install."));
    if (!spec->isPinned())
        return refuse(QStringLiteral("%1 is not available yet: no release is pinned for this launcher version.").arg(spec->name));
    if (m_task) {
        const RunnerSpec* busy = Runners::find(m_taskId);
        return refuse(QStringLiteral("%1 is installing; wait for it or cancel it first.").arg(busy ? busy->name : m_taskId));
    }
    m_errors.remove(id);
    m_taskId = id;
    m_task = new RunnerInstall(*spec, this);
    connect(m_task, &RunnerInstall::progressChanged, this, [this, id] { emit changed(id); });
    connect(m_task, &RunnerInstall::finished, this, [this, id](bool ok, const QString& why) {
        if (!ok)
            m_errors.insert(id, why);
        m_task->deleteLater();
        m_task.clear();
        m_taskId.clear();
        m_sizes.remove(id);
        emit changed(id);
        emit stateChanged(id);
        emit installFinished(id, ok, why);
    });
    m_task->start();
    emit changed(id);
    emit stateChanged(id);
    return true;
}

void RunnerManager::cancel()
{
    if (m_task)
        m_task->cancel();
}

bool RunnerManager::uninstall(const QString& id, QString* error)
{
    const RunnerSpec* spec = Runners::find(id);
    if (!spec || spec->isBuiltIn()) {
        if (error)
            *error = QStringLiteral("XSEmu comes with the launcher and cannot be uninstalled here.");
        return false;
    }
    if (m_taskId == id && m_task) {
        if (error)
            *error = QStringLiteral("%1 is installing; cancel it first.").arg(spec->name);
        return false;
    }
    m_errors.remove(id);
    m_sizes.remove(id);
    const bool ok = QDir(Paths::runnerDir(id)).removeRecursively();
    if (!ok && error)
        *error = QStringLiteral("Some files of %1 are in use; stop it and try again.").arg(spec->name);
    emit changed(id);
    emit stateChanged(id);
    return ok;
}

void RunnerManager::simulateForScreenshot(const QString& id, const Status& status)
{
    m_simulated.insert(id, status);
    emit changed(id);
    emit stateChanged(id);
}

qint64 RunnerManager::installedBytes(const QString& id)
{
    const RunnerSpec* spec = Runners::find(id);
    if (!spec || spec->isBuiltIn())
        return 0;
    if (const auto it = m_sizes.constFind(id); it != m_sizes.constEnd())
        return *it;
    m_sizes.insert(id, -1);
    const QString dir = spec->installDir();
    const QPointer<RunnerManager> self = this;
    QThreadPool::globalInstance()->start([self, id, dir] {
        qint64 total = 0;
        QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            total += it.fileInfo().size();
        }
        QMetaObject::invokeMethod(qApp, [self, id, total] {
            if (!self || self->m_sizes.value(id, 0) != -1)
                return;
            self->m_sizes.insert(id, total);
            emit self->changed(id);
        });
    });
    return -1;
}

QDateTime RunnerManager::installedAt(const QString& id) const
{
    return QDateTime::fromString(installRecord(id).value(QLatin1String("installedAt")).toString(), Qt::ISODate).toLocalTime();
}

QString RunnerManager::installLogPath(const QString& id) const { return RunnerInstall::logPath(id); }

QString RunnerManager::cachePath(const QString& id) const
{
    const RunnerSpec* spec = Runners::find(id);
    if (!spec)
        return {};
    if (spec->isBuiltIn())
        return Paths::dxilCacheDir();
    return spec->resolvedCacheDir();
}
