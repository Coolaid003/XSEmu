#include "ProcessTree.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSet>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <vector>
#else
#include <csignal>
#include <unistd.h>
#endif

ProcessTree::ProcessTree(QObject* parent)
    : QProcess(parent)
{
#ifdef Q_OS_WIN
    m_job = CreateJobObjectW(nullptr, nullptr);
    if (m_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        // no console window for console programs (third-party runners, cargo, git): output is piped
        args->flags |= CREATE_SUSPENDED | CREATE_NO_WINDOW;
    });
#else
    setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
    connect(this, &QProcess::started, this, &ProcessTree::onStarted);
}

ProcessTree::~ProcessTree()
{
    if (state() != QProcess::NotRunning) {
        killTree();
        waitForFinished(2000);
    }
#ifdef Q_OS_WIN
    if (m_job)
        CloseHandle(m_job);
#endif
}

void ProcessTree::startTree(const QString& program, const QStringList& arguments)
{
    start(program, arguments);
}

void ProcessTree::onStarted()
{
#ifdef Q_OS_WIN
    const DWORD pid = static_cast<DWORD>(processId());
    HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, pid);
    if (process && m_job)
        AssignProcessToJobObject(m_job, process);
    if (process)
        CloseHandle(process);
    // the child was created suspended so it joins the job before it can start anything: resume it
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid)
                continue;
            HANDLE t = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (t) {
                ResumeThread(t);
                CloseHandle(t);
            }
        }
        CloseHandle(snap);
    }
#endif
}

void ProcessTree::killTree()
{
#ifdef Q_OS_WIN
    if (m_job)
        TerminateJobObject(m_job, 1);
    kill();
#else
    const qint64 pid = processId();
    if (pid > 0)
        ::kill(-static_cast<pid_t>(pid), SIGKILL);
    kill();
#endif
}

QList<qint64> ProcessTree::pids() const
{
    QList<qint64> out;
    if (state() == QProcess::NotRunning)
        return out;
#ifdef Q_OS_WIN
    if (!m_job)
        return out;
    std::vector<char> buf(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + 1024 * sizeof(ULONG_PTR));
    auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buf.data());
    if (!QueryInformationJobObject(m_job, JobObjectBasicProcessIdList, list, static_cast<DWORD>(buf.size()), nullptr))
        return out;
    for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i)
        out << static_cast<qint64>(list->ProcessIdList[i]);
#else
    const qint64 group = processId();
    if (group <= 0)
        return out;
    const QDir proc(QStringLiteral("/proc"));
    for (const QString& entry : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isPid = false;
        const int pid = entry.toInt(&isPid);
        if (isPid && ::getpgid(pid) == group)
            out << pid;
    }
#endif
    return out;
}

qint64 ProcessTree::heldBytes() const
{
    if (state() == QProcess::NotRunning)
        return -1;
    qint64 total = 0;
    for (const qint64 pid : pids()) {
#ifdef Q_OS_WIN
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
        if (!h)
            continue;
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
            total += static_cast<qint64>(pmc.PrivateUsage);
        CloseHandle(h);
#else
        QFile status(QStringLiteral("/proc/%1/status").arg(pid));
        if (!status.open(QIODevice::ReadOnly))
            continue;
        for (const QByteArray& line : status.readAll().split('\n')) {
            if (line.startsWith("RssAnon:") || line.startsWith("VmSwap:")) {
                const QList<QByteArray> parts = line.simplified().split(' ');
                if (parts.size() >= 2)
                    total += parts.at(1).toLongLong() * 1024;
            }
        }
#endif
    }
    return total;
}

namespace {

QMutex helpersLock;

QSet<qint64>& helpers()
{
    static QSet<qint64> pids;
    return pids;
}

QSet<qint64> helperPids()
{
    const QMutexLocker lock(&helpersLock);
    return helpers();
}

}

qint64 startHelper(QProcess& p, const QString& program, const QStringList& arguments)
{
    const QMutexLocker lock(&helpersLock);
    p.start(program, arguments);
    const qint64 pid = p.processId();
    if (pid > 0)
        helpers().insert(pid);
    return pid;
}

void forgetHelper(qint64 pid)
{
    const QMutexLocker lock(&helpersLock);
    helpers().remove(pid);
}

QStringList runningEmulatorProcesses(qint64 excludePid)
{
    // A helper seen alive during the scan was recorded before the scan began or, since startHelper
    // records under the lock it starts under, by the time the scan ends.
    const QSet<qint64> ownBefore = helperPids();
    QList<QPair<QString, qint64>> found;
#ifdef Q_OS_WIN
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return {};
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        const QString name = QString::fromWCharArray(pe.szExeFile);
        if (static_cast<qint64>(pe.th32ProcessID) == excludePid)
            continue;
        if (name.compare(QLatin1String("XSEmu.exe"), Qt::CaseInsensitive) == 0
            || name.compare(QLatin1String("xcl.exe"), Qt::CaseInsensitive) == 0
            || name.compare(QLatin1String("game_scarlett_master.exe"), Qt::CaseInsensitive) == 0)
            found.append({name, static_cast<qint64>(pe.th32ProcessID)});
    }
    CloseHandle(snap);
#else
    const QDir proc(QStringLiteral("/proc"));
    for (const QString& entry : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isPid = false;
        const qint64 pid = entry.toLongLong(&isPid);
        if (!isPid || pid == excludePid)
            continue;
        QFile cmdline(proc.filePath(entry + QStringLiteral("/cmdline")));
        if (!cmdline.open(QIODevice::ReadOnly))
            continue;
        const QString name = QFileInfo(QString::fromLocal8Bit(cmdline.readAll().split('\0').value(0))).fileName();
        for (const char* known : {"XSEmu.exe", "xcl.exe", "game_scarlett_master.exe"})
            if (name.compare(QLatin1String(known), Qt::CaseInsensitive) == 0)
                found.append({name, pid});
    }
#endif
    const QSet<qint64> own = ownBefore | helperPids();
    QStringList out;
    for (const auto& [name, pid] : std::as_const(found))
        if (!own.contains(pid))
            out << QStringLiteral("%1 %2").arg(name).arg(pid);
    return out;
}
