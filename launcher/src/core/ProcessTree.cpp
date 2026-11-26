#include "ProcessTree.h"

#include <QDir>
#include <QFile>

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

qint64 ProcessTree::heldBytes() const
{
    if (state() == QProcess::NotRunning)
        return -1;
#ifdef Q_OS_WIN
    if (!m_job)
        return -1;
    std::vector<char> buf(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + 1024 * sizeof(ULONG_PTR));
    auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buf.data());
    if (!QueryInformationJobObject(m_job, JobObjectBasicProcessIdList, list, static_cast<DWORD>(buf.size()), nullptr))
        return -1;
    qint64 total = 0;
    for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(list->ProcessIdList[i]));
        if (!h)
            continue;
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
            total += static_cast<qint64>(pmc.PrivateUsage);
        CloseHandle(h);
    }
    return total;
#else
    const qint64 group = processId();
    if (group <= 0)
        return -1;
    qint64 total = 0;
    const QDir proc(QStringLiteral("/proc"));
    for (const QString& entry : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isPid = false;
        const int pid = entry.toInt(&isPid);
        if (!isPid || ::getpgid(pid) != group)
            continue;
        QFile status(proc.filePath(entry + QStringLiteral("/status")));
        if (!status.open(QIODevice::ReadOnly))
            continue;
        for (const QByteArray& line : status.readAll().split('\n')) {
            if (line.startsWith("RssAnon:") || line.startsWith("VmSwap:")) {
                const QList<QByteArray> parts = line.simplified().split(' ');
                if (parts.size() >= 2)
                    total += parts.at(1).toLongLong() * 1024;
            }
        }
    }
    return total;
#endif
}

QStringList runningEmulatorProcesses(qint64 excludePid)
{
    QStringList out;
#ifdef Q_OS_WIN
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return out;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        const QString name = QString::fromWCharArray(pe.szExeFile);
        if (static_cast<qint64>(pe.th32ProcessID) == excludePid)
            continue;
        if (name.compare(QLatin1String("XSEmu.exe"), Qt::CaseInsensitive) == 0
            || name.compare(QLatin1String("xcl.exe"), Qt::CaseInsensitive) == 0
            || name.compare(QLatin1String("game_scarlett_master.exe"), Qt::CaseInsensitive) == 0)
            out << QStringLiteral("%1 %2").arg(name).arg(pe.th32ProcessID);
    }
    CloseHandle(snap);
#else
    Q_UNUSED(excludePid);
#endif
    return out;
}
