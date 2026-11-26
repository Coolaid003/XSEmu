#include "Services.h"

#include "core/LaunchConfig.h"
#include "theme/Theme.h"

Services::Services(QObject* parent)
    : QObject(parent)
    , emulator(settings, gate)
    , warmup(settings, gate)
    , covers(settings)
{
    Theme::instance().setMode(Theme::Mode(int(settings.theme())));
    LaunchConfig::followShaderCacheDir(settings);
    library.setFolders(settings.gameFolders());
    connect(&settings, &AppSettings::changed, this, [this](const QString& key) {
        if (key == QLatin1String(AppSettings::Key::Theme))
            Theme::instance().setMode(Theme::Mode(int(settings.theme())));
        else if (key == QLatin1String(AppSettings::Key::GameFolders))
            library.setFolders(settings.gameFolders());
    });
}

QString Services::consoleName(const QString& deviceType)
{
    return deviceType == QLatin1String("anaconda") ? QStringLiteral("Series X") : QStringLiteral("Series S");
}
