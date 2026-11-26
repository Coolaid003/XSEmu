#pragma once

#include <QProcess>
#include <QStringList>

// A QProcess whose whole process tree can be measured and killed: on Windows the child starts
// suspended, joins a job object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, then resumes, so every
// process it starts is in the job too (and dies with the launcher); elsewhere the child leads its
// own process group.
class ProcessTree : public QProcess {
    Q_OBJECT
public:
    explicit ProcessTree(QObject* parent = nullptr);
    ~ProcessTree() override;

    void startTree(const QString& program, const QStringList& arguments);

    // Kill every process in the tree at once.
    void killTree();

    // Private memory held by the tree's processes (Windows: sum of PrivateUsage over the job's
    // processes; Linux: RssAnon + VmSwap over the process group). -1 when it cannot be measured.
    qint64 heldBytes() const;

    // Every process in the tree now (Windows: the job's processes; Linux: the process group).
    QList<qint64> pids() const;

private:
    void onStarted();
#ifdef Q_OS_WIN
    void* m_job = nullptr;
#endif
};

// Other XSEmu trees on this machine (XSEmu.exe, xcl.exe, game_scarlett_master.exe; Windows ToolHelp,
// elsewhere argv[0] in /proc), as "name pid" strings. The launcher's own helpers never count.
QStringList runningEmulatorProcesses(qint64 excludePid = 0);

// A short-lived helper the launcher starts itself, such as the emulator asked --version. startHelper
// starts it and records its pid in one step, so runningEmulatorProcesses() on another thread never
// mistakes it for another emulator; forgetHelper drops the pid once the helper has exited. Any thread.
qint64 startHelper(QProcess& p, const QString& program, const QStringList& arguments);
void forgetHelper(qint64 pid);
