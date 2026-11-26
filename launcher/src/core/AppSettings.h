#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>

#include <memory>

// One finished warm-up, as the Warm-up page's "Recent runs" table shows it.
struct WarmupRun {
    enum class Result { Finished, Cancelled, StoppedPartWay, Failed };

    QString titleId;    // the game the user started it for (the cache itself is machine-wide)
    QString titleName;
    qint64 done = 0;    // pipelines built (built + failed)
    qint64 total = 0;
    qint64 failed = 0;
    double tookS = 0;
    Result result = Result::Finished;
    int exitCode = 0;
    bool full = false;
    bool stoppedForRam = false; // ended part way while waiting at the RAM floor
    QString message;            // error text for Failed
    QDateTime finishedAt;

    QVariantMap toMap() const;
    static WarmupRun fromMap(const QVariantMap& m);
};

Q_DECLARE_METATYPE(WarmupRun)

// Launcher preferences (QSettings ini at Paths::settingsFile()). Every setter writes at once and
// emits changed(key) with one of the AppSettings::Key constants, so a page can follow one setting:
//
//   connect(&settings, &AppSettings::changed, this, [this](const QString& key) {
//       if (key == AppSettings::Key::Offline) refresh();
//   });
//
// Per-title emulator settings do not live here: they are the title's xcl.toml (TitleConfig).
class AppSettings : public QObject {
    Q_OBJECT
public:
    enum class ThemeMode { System, Light, Dark };

    struct Key {
        static constexpr const char* Theme = "ui/theme";
        static constexpr const char* GameFolders = "library/folders";
        static constexpr const char* FirstLaunchDone = "app/firstLaunchDone";
        static constexpr const char* DefaultDeviceType = "console/defaultDeviceType";
        static constexpr const char* Offline = "network/offline";
        static constexpr const char* OverlayDefault = "overlay/default";
        static constexpr const char* SteamGridDbKey = "covers/steamGridDbKey";
        static constexpr const char* ArtLookups = "covers/lookups";
        static constexpr const char* GiantCompileCap = "warmup/giantCompileCap";
        static constexpr const char* GameBarAutoHide = "ingame/gameBarAutoHide";
        static constexpr const char* EmulatorPath = "emulator/path";
        static constexpr const char* LaunchNeedMb = "memory/launchNeedMb";
        static constexpr const char* GameOverrides = "games/overrides";
        static constexpr const char* RecentWarmups = "warmup/recentRuns";
        static constexpr const char* PlayStats = "games/play";
        static constexpr const char* LibrarySort = "library/sort";
    };

    static constexpr int kRecentWarmupLimit = 20;
    static constexpr int kDefaultGiantCompileCap = 3;
    // No fixed run floor: a launch only needs the 1 GB floor free (EmulatorController::kRamFloorMb)
    // plus whatever the user configures here.
    static constexpr int kDefaultLaunchNeedMb = 0;

    // Opens Paths::settingsFile(), or the given ini file.
    explicit AppSettings(QObject* parent = nullptr);
    explicit AppSettings(const QString& iniPath, QObject* parent = nullptr);
    ~AppSettings() override;

    QString fileName() const;

    ThemeMode theme() const;
    void setTheme(ThemeMode mode);
    static QString toString(ThemeMode mode);
    static ThemeMode themeFromString(const QString& s);

    QStringList gameFolders() const;
    void setGameFolders(const QStringList& folders);

    bool firstLaunchDone() const;
    void setFirstLaunchDone(bool done);

    // "lockhart" (Series S, the default) or "anaconda" (Series X): written into a title's xcl.toml
    // before its first launch when the file has no device_type yet.
    QString defaultDeviceType() const;
    void setDefaultDeviceType(const QString& deviceType);

    // On by default: games start with XCL_NETWORK=0.
    bool offline() const;
    void setOffline(bool offline);

    bool overlayDefault() const;
    void setOverlayDefault(bool on);

    QString steamGridDbKey() const;
    void setSteamGridDbKey(const QString& key);
    bool artLookups() const; // the user's switch for cover lookups (on; nothing happens without a key)
    void setArtLookups(bool on);

    int giantCompileCap() const;
    void setGiantCompileCap(int cap);

    bool gameBarAutoHide() const;
    void setGameBarAutoHide(bool on);

    // The user's override of the emulator executable (empty: none).
    QString emulatorPathOverride() const;
    void setEmulatorPathOverride(const QString& path);
    // The executable used: env XSEMU_EMULATOR, else the override, else Paths::defaultEmulatorPath().
    QString emulatorPath() const;

    // Free RAM a game launch asks for on top of the 1 GB floor.
    int launchNeedMb() const;
    void setLaunchNeedMb(int mb);

    // Launcher-side per-game choices (not xcl.toml): an open map per title id.
    QVariantMap gameOverrides(const QString& titleId) const;
    void setGameOverride(const QString& titleId, const QString& key, const QVariant& value);
    void clearGameOverride(const QString& titleId, const QString& key);

    QList<WarmupRun> recentWarmups() const; // newest first
    void addRecentWarmup(const WarmupRun& run);
    void clearRecentWarmups();

    qint64 playSeconds(const QString& titleId) const;
    QDateTime lastPlayed(const QString& titleId) const;
    void addPlayTime(const QString& titleId, qint64 seconds, const QDateTime& endedAt);
    // The title's last run as the Game details page shows it: an open map (result, exitCode, at,
    // seconds, fpsAverage, stalls, worstMs, heldPeakBytes, freeMinMb), empty when it never ran.
    QVariantMap lastRun(const QString& titleId) const;
    void setLastRun(const QString& titleId, const QVariantMap& run);

    // The library's sort: "recent" (default), "name" or "size".
    QString librarySort() const;
    void setLibrarySort(const QString& sort);

signals:
    void changed(const QString& key);

private:
    void write(const char* key, const QVariant& value);
    QVariant read(const char* key, const QVariant& fallback = {}) const;
    QVariantMap readMap(const char* key) const;

    std::unique_ptr<QSettings> m_settings;
};
