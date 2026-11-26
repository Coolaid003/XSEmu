#pragma once

#include "Runner.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>

class RunnerInstall;

// What is installed under %LOCALAPPDATA%\XSEmu\runners\<id>\ and the actions on it. One install or
// update runs at a time; each is a RunnerInstall task whose progress the UI shows and can cancel.
//
//   RunnerManager& m = services.runners;
//   if (m.status(id).state == RunnerManager::State::NotInstalled) m.install(id);
//   connect(&m, &RunnerManager::changed, this, [&](const QString& id) { refresh(m.status(id)); });
class RunnerManager : public QObject {
    Q_OBJECT
public:
    enum class State { BuiltIn, Unavailable, NotInstalled, Installing, Installed, UpdateAvailable, Failed };

    struct Progress {
        QString step;      // "Downloading", "Checking the download", "Unpacking", "Building"...
        int stepIndex = 0; // 1-based
        int stepCount = 0;
        qint64 done = -1;  // within the step; -1: unknown (indeterminate)
        qint64 total = -1;
        bool bytes = false; // done/total are bytes
        QString detail;    // the last line or item the step reported
        int phase = 0;     // 0 download (and toolchains), 1 unpack or build, 2 install
        QDateTime startedAt;
    };

    struct Status {
        State state = State::NotInstalled;
        QString installedVersion; // what runner.json says is installed
        QString pinnedVersion;    // what the curated list pins
        QString executable;       // absolute path of the installed program; empty when not installed
        Progress progress;        // Installing
        QString error;            // Failed: why the last install stopped
    };

    explicit RunnerManager(QObject* parent = nullptr);
    ~RunnerManager() override;

    Status status(const QString& id) const;
    bool isRunnable(const QString& id) const; // built in, or installed (an update may be pending)
    // Installed or installable now: what the recommendation may pick.
    bool isInstallable(const QString& id) const;
    QString busyId() const; // the runner being installed, or empty

    // Install, or update to the curated pin. Returns false with *error when it cannot start now.
    bool install(const QString& id, QString* error = nullptr);
    void cancel();
    bool uninstall(const QString& id, QString* error = nullptr);

    // Bytes in the runner's cache directory; -1 when the runner has no known cache dir.
    qint64 cacheBytes(const QString& id) const;
    QString cachePath(const QString& id) const;
    // Bytes the installed copy takes on disk (its install dir); 0 when not installed.
    qint64 installedBytes(const QString& id) const;
    QDateTime installedAt(const QString& id) const;

    // Screenshot fixtures only: report this status for the runner instead of the real one.
    void simulateForScreenshot(const QString& id, const Status& status);

signals:
    void changed(const QString& id);
    void installFinished(const QString& id, bool ok, const QString& error);

private:
    QPointer<RunnerInstall> m_task;
    QString m_taskId;
    QHash<QString, QString> m_errors;
    QHash<QString, Status> m_simulated;
};
