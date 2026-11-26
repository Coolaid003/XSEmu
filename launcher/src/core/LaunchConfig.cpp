#include "LaunchConfig.h"

#include "AppSettings.h"
#include "Paths.h"
#include "TitleConfig.h"

#include <QDir>

namespace {

QString& externalCacheDir()
{
    static QString dir = qEnvironmentVariable("XCL_DXIL_CACHE").trimmed();
    return dir;
}

void applyShaderCacheDir(const AppSettings& settings)
{
    if (!externalCacheDir().isEmpty())
        return;
    const QString dir = settings.shaderCacheDirOverride();
    if (dir.isEmpty())
        qunsetenv("XCL_DXIL_CACHE");
    else
        qputenv("XCL_DXIL_CACHE", QDir::toNativeSeparators(dir).toUtf8());
}

}

namespace LaunchConfig {

const QList<XclKey>& xclKeys()
{
    static const QList<XclKey> keys{
        {"device_type", Type::String},
        {"log_level", Type::String},
        {"keyboard_mouse", Type::Bool},
        {"mouse_sensitivity", Type::Float},
        {"display_mode", Type::String},
        {"raytracing", Type::String},
        {"present", Type::String},
        {"d3d12_adapter", Type::String},
    };
    return keys;
}

QVariant typed(const QString& tomlKey, const QVariant& value)
{
    if (!value.isValid())
        return {};
    for (const XclKey& k : xclKeys()) {
        if (tomlKey != QLatin1String(k.key))
            continue;
        switch (k.type) {
        case Type::String: return value.toString();
        case Type::Bool:
            if (value.typeId() == QMetaType::QString)
                return value.toString().compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
            return value.toBool();
        case Type::Float: return value.toDouble();
        }
    }
    return value;
}

QVariantMap titleValues(const AppSettings& settings, const QString& titleId)
{
    const QVariantMap overrides = settings.gameOverrides(titleId);
    QVariantMap out;
    for (const XclKey& k : xclKeys()) {
        const QString key = QLatin1String(k.key);
        QVariant v = overrides.value(key);
        if (!v.isValid())
            v = key == QLatin1String("device_type") ? QVariant(settings.defaultDeviceType()) : settings.xclDefault(key);
        if (v.isValid())
            out.insert(key, typed(key, v));
    }
    return out;
}

bool prepareTitle(const AppSettings& settings, const QString& titleId)
{
    if (settings.crashDumps())
        QDir().mkpath(crashDumpDir(titleId));
    TitleConfig cfg(Paths::xclToml(titleId));
    const QVariantMap current = cfg.values();
    QVariantMap changes;
    const QVariantMap wanted = titleValues(settings, titleId);
    for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
        if (typed(it.key(), current.value(it.key())) != it.value())
            changes.insert(it.key(), it.value());
    }
    return changes.isEmpty() || cfg.set(changes);
}

QProcessEnvironment runEnvironment(const QProcessEnvironment& base, const AppSettings& settings, const QString& titleId)
{
    QProcessEnvironment env = base;
    if (settings.offline())
        env.insert(QStringLiteral("XCL_NETWORK"), QStringLiteral("0"));
    else
        env.remove(QStringLiteral("XCL_NETWORK"));
    // the store answers with its network error: nothing grants a licence (hle/xgameruntime/store.rs)
    if (settings.storeOfflineError())
        env.insert(QStringLiteral("XCL_STORE"), QStringLiteral("offline-error"));
    else
        env.remove(QStringLiteral("XCL_STORE"));
    if (settings.crashDumps())
        env.insert(QStringLiteral("XCL_FULL_DUMP"), QDir::toNativeSeparators(crashDumpDir(titleId)));
    else
        env.remove(QStringLiteral("XCL_FULL_DUMP"));
    return env;
}

QString crashDumpDir(const QString& titleId)
{
    return QDir(Paths::logsDir(titleId)).filePath(QStringLiteral("dumps"));
}

void followShaderCacheDir(AppSettings& settings)
{
    externalCacheDir();
    applyShaderCacheDir(settings);
    QObject::connect(&settings, &AppSettings::changed, &settings, [&settings](const QString& key) {
        if (key == QLatin1String(AppSettings::Key::ShaderCacheDir))
            applyShaderCacheDir(settings);
    });
}

bool shaderCacheDirFromEnvironment()
{
    return !externalCacheDir().isEmpty();
}

}
