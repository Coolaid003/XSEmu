#include "Services.h"

#include "core/LaunchConfig.h"
#include "theme/Theme.h"

Services::Services(QObject* parent)
    : QObject(parent)
    , emulator(settings, gate)
    , warmup(settings, gate)
    , covers(settings)
{
    emulator.setRunners(&runners);
    warmup.setRunners(&runners);
    Theme::instance().setMode(Theme::Mode(int(settings.theme())));
    LaunchConfig::followShaderCacheDir(settings);
    library.setFolders(settings.gameFolders());
    gate.setBusyMarker(settings.busyMarker());
    shaderCache.refresh();
    connect(&warmup, &WarmupController::finished, &shaderCache, &ShaderCacheMonitor::refresh);
    connect(&emulator, &EmulatorController::exited, &shaderCache, &ShaderCacheMonitor::refresh);
    connect(&covers, &CoverArtService::artChanged, this, [this](const QString& titleId) {
        if (titleId.isEmpty()) {
            m_art.clear();
            return;
        }
        m_art.remove(titleId + QStringLiteral("|grid"));
        m_art.remove(titleId + QStringLiteral("|hero"));
    });
    connect(&settings, &AppSettings::changed, this, [this](const QString& key) {
        if (key == QLatin1String(AppSettings::Key::Theme))
            Theme::instance().setMode(Theme::Mode(int(settings.theme())));
        else if (key == QLatin1String(AppSettings::Key::GameFolders))
            library.setFolders(settings.gameFolders());
        else if (key == QLatin1String(AppSettings::Key::BusyMarker))
            gate.setBusyMarker(settings.busyMarker());
        else if (key == QLatin1String(AppSettings::Key::ShaderCacheDir))
            shaderCache.refresh();
    });
}

QPixmap Services::art(const QString& titleId, CoverCache::Kind kind)
{
    const QString key = titleId + (kind == CoverCache::Kind::Grid ? QStringLiteral("|grid") : QStringLiteral("|hero"));
    if (const auto it = m_art.constFind(key); it != m_art.constEnd())
        return *it;
    const QString path = covers.artPath(titleId, kind);
    const QPixmap pm = path.isEmpty() ? QPixmap() : QPixmap(path);
    m_art.insert(key, pm);
    return pm;
}

QString Services::consoleName(const QString& deviceType)
{
    return deviceType == QLatin1String("anaconda") ? QStringLiteral("Series X") : QStringLiteral("Series S");
}
