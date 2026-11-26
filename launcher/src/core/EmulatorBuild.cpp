#include "EmulatorBuild.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcess>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

QString keyFor(const QFileInfo& fi)
{
    return QStringLiteral("%1|%2|%3").arg(fi.absoluteFilePath()).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
}

QHash<QString, EmulatorBuild>& answers()
{
    static QHash<QString, EmulatorBuild> kept;
    return kept;
}

QString ask(const QString& path, const QString& option)
{
    QProcess p;
#ifdef Q_OS_WIN
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setStandardInputFile(QProcess::nullDevice());
    p.start(path, {option});
    if (!p.waitForStarted(3000))
        return {};
    if (!p.waitForFinished(3000)) {
        p.kill();
        p.waitForFinished(1000);
        return {};
    }
    return QString::fromLocal8Bit(p.readAll());
}

}

QString EmulatorBuild::problem() const
{
    if (!found)
        return QStringLiteral("XSEmu was not found at %1.").arg(QDir::toNativeSeparators(path));
    if (capable)
        return {};
    if (version.isEmpty())
        return QStringLiteral("%1 didn't say which version it is, so the launcher won't start it.").arg(QFileInfo(path).fileName());
    return QStringLiteral("This XSEmu build is too old for the launcher (%1 says \"%2\"). It needs a build that supports --control-pipe and --progress-json.")
        .arg(QFileInfo(path).fileName(), version);
}

bool EmulatorBuild::speaksProtocol(const QString& versionLine, const QString& help)
{
    return versionLine.startsWith(QLatin1String("XSEmu ")) && help.contains(QLatin1String("--control-pipe"))
        && help.contains(QLatin1String("--progress-json"));
}

EmulatorBuild EmulatorBuild::cached(const QString& path)
{
    const QFileInfo fi(path);
    EmulatorBuild b;
    b.path = path;
    if (!fi.isFile() || !fi.isExecutable())
        return b;
    return answers().value(keyFor(fi), b);
}

EmulatorBuild EmulatorBuild::probe(const QString& path)
{
    const QFileInfo fi(path);
    EmulatorBuild b;
    b.path = path;
    if (!fi.isFile() || !fi.isExecutable())
        return b;
    const QString key = keyFor(fi);
    if (const auto it = answers().constFind(key); it != answers().constEnd())
        return *it;
    b.found = true;
    b.version = ask(path, QStringLiteral("--version")).trimmed().section(QLatin1Char('\n'), 0, 0).trimmed();
    b.capable = b.version.startsWith(QLatin1String("XSEmu ")) && speaksProtocol(b.version, ask(path, QStringLiteral("--help")));
    answers().insert(key, b);
    return b;
}
