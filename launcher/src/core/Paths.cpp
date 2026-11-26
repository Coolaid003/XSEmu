#include "Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace {

QString env(const char* name)
{
    return qEnvironmentVariable(name).trimmed();
}

// %LOCALAPPDATA% on Windows; ~/.local/share elsewhere so the same layout can be tested on Linux.
QString localAppData()
{
#ifdef Q_OS_WIN
    const QString v = env("LOCALAPPDATA");
    if (!v.isEmpty())
        return QDir::fromNativeSeparators(v);
    return QDir::tempPath();
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#endif
}

QString join(const QString& a, const QString& b)
{
    return QDir::cleanPath(a + QLatin1Char('/') + b);
}

}

namespace Paths {

QString dataRoot()
{
    const QString v = env("XCL_DATA_ROOT");
    if (!v.isEmpty())
        return QDir::cleanPath(QDir::fromNativeSeparators(v));
    return join(localAppData(), QStringLiteral("XCL"));
}

QString titleDirName(const QString& identityOrFolderName)
{
    QString out;
    out.reserve(identityOrFolderName.size());
    for (const QChar c : identityOrFolderName) {
        const bool keep = c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('_') || c == QLatin1Char('-');
        out.append(keep ? c : QLatin1Char('_'));
    }
    return out.isEmpty() ? QStringLiteral("title") : out;
}

QString titleDir(const QString& name) { return join(dataRoot(), name); }
QString xclToml(const QString& name) { return join(titleDir(name), QStringLiteral("xcl.toml")); }
QString logsDir(const QString& name) { return join(titleDir(name), QStringLiteral("logs")); }
QString cacheDir(const QString& name) { return join(titleDir(name), QStringLiteral("cache")); }
QString reportJson(const QString& name) { return join(logsDir(name), QStringLiteral("report.json")); }
QString xclLog(const QString& name) { return join(logsDir(name), QStringLiteral("xcl.log")); }

QString dxilCacheDir()
{
    const QString v = env("XCL_DXIL_CACHE");
    if (!v.isEmpty())
        return QDir::cleanPath(QDir::fromNativeSeparators(v));
    return join(localAppData(), QStringLiteral("xcl/dxil"));
}

QString pipelineManifestDir() { return join(dxilCacheDir(), QStringLiteral("pipelines")); }
QString driverBlobDir() { return join(dxilCacheDir(), QStringLiteral("driver")); }

QString launcherDataDir()
{
    const QString v = env("XSEMU_LAUNCHER_DATA");
    if (!v.isEmpty())
        return QDir::cleanPath(QDir::fromNativeSeparators(v));
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString settingsFile() { return join(launcherDataDir(), QStringLiteral("settings.ini")); }
QString coverCacheDir() { return join(launcherDataDir(), QStringLiteral("covers")); }

QString defaultEmulatorPath()
{
#ifdef Q_OS_WIN
    return join(QCoreApplication::applicationDirPath(), QStringLiteral("XSEmu.exe"));
#else
    return join(QCoreApplication::applicationDirPath(), QStringLiteral("fake-xsemu"));
#endif
}

}
