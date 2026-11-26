#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QVariantMap>

class AppSettings;

// What the launcher hands XSEmu at launch besides the command line: the xcl.toml keys Settings
// owns, the XCL_* environment, and the folders those name. Precedence for an xcl.toml key: the
// game's own launcher override (AppSettings::gameOverrides(titleId), keyed by the xcl.toml key name),
// else the launcher default from Settings, else whatever the title's xcl.toml already says.
// device_type always has a launcher default (lockhart, Series S).
//
//   LaunchConfig::prepareTitle(settings, game.titleId());   // before XSEmu.exe run
//   env = LaunchConfig::runEnvironment(QProcessEnvironment::systemEnvironment(), settings, game.titleId());
namespace LaunchConfig {

// The xcl.toml keys Settings writes (crates/xcl/src/config.rs), with their TOML type.
enum class Type { String, Bool, Float };
struct XclKey {
    const char* key;
    Type type;
};
const QList<XclKey>& xclKeys(); // device_type first

// Converts a stored value (QSettings returns strings from the ini) to the key's TOML type.
QVariant typed(const QString& tomlKey, const QVariant& value);

// The values to write into the title's xcl.toml before it starts (tested).
QVariantMap titleValues(const AppSettings& settings, const QString& titleId);

// The console a game runs as: its per-game override, else the launcher default ("lockhart" or
// "anaconda"). Third-party runners get it as a token; xcl.toml is XSEmu's and is not read for it.
QString deviceType(const AppSettings& settings, const QString& titleId);

// Writes titleValues() into the title's xcl.toml where they differ, and creates the crash dump folder
// when crash dumps are on. False when xcl.toml cannot be written.
bool prepareTitle(const AppSettings& settings, const QString& titleId);

// XCL_NETWORK, XCL_STORE and XCL_FULL_DUMP for a game run (tested).
QProcessEnvironment runEnvironment(const QProcessEnvironment& base, const AppSettings& settings, const QString& titleId);

// <title>\logs\dumps: where XCL_FULL_DUMP points (crates/xcl/src/eh.rs writes xcl-{full|small}-<pid>.dmp).
QString crashDumpDir(const QString& titleId);

// The shader cache folder: XCL_DXIL_CACHE from the launcher's own environment wins; otherwise the
// user's Settings choice is put into this process's environment (XCL_DXIL_CACHE), so Paths, the warm-up
// and game runs all use it. Call once at startup; it follows later changes of the setting.
void followShaderCacheDir(AppSettings& settings);
bool shaderCacheDirFromEnvironment(); // XCL_DXIL_CACHE was set before the launcher started

}
