#pragma once

#include "Game.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QUrl>

#include <functional>

class AppSettings;
class QNetworkAccessManager;
class QNetworkReply;

// Cover art cache: <cover cache>\index.json plus one image file per title and kind.
// index.json: { "<titleId>": { "gameId": 123, "name": "...", "grid": "<file>", "hero": "<file>",
//               "fetchedAt": "<ISO>", "notFound": false } }
class CoverCache {
public:
    enum class Kind { Grid, Hero };

    struct Entry {
        qint64 gameId = 0;
        QString name;      // the SteamGridDB game name matched
        QString grid;      // file name in the cache folder, empty when none
        QString hero;
        QDateTime fetchedAt;
        bool notFound = false;
    };

    explicit CoverCache(QString dir);

    QString dir() const { return m_dir; }
    bool contains(const QString& titleId) const { return m_entries.contains(titleId); }
    Entry entry(const QString& titleId) const { return m_entries.value(titleId); }
    void put(const QString& titleId, const Entry& entry); // saves the index
    void remove(const QString& titleId);
    void clear();

    // Absolute path of a cached image that exists, or empty.
    QString path(const QString& titleId, Kind kind) const;
    // The file name an image of that kind gets (titleId is already a safe file name).
    static QString fileName(const QString& titleId, Kind kind, const QString& extension);

private:
    void load();
    void save() const;

    QString m_dir;
    QHash<QString, Entry> m_entries;
};

// Optional cover art from SteamGridDB (API v2) with the user's own key. Never touches the network
// without a key or with art lookups switched off; the offline setting is for the title, not for the
// launcher, so it does not block lookups (the UI shows a note instead).
//
//   covers.request(game);                       // no-op when cached or lookups are off
//   connect(&covers, &CoverArtService::artChanged, this, [&](const QString& titleId) { ... });
//   QString file = covers.artPath(game.titleId(), CoverCache::Kind::Grid);   // empty: no art
class CoverArtService : public QObject {
    Q_OBJECT
public:
    static constexpr const char* kApiBase = "https://www.steamgriddb.com/api/v2";

    explicit CoverArtService(AppSettings& settings, QObject* parent = nullptr);
    CoverArtService(AppSettings& settings, const QString& cacheDir, QObject* parent = nullptr);
    ~CoverArtService() override;

    bool canLookUp() const;
    QString lookupsUnavailableReason() const; // empty when lookups can run

    QString artPath(const QString& titleId, CoverCache::Kind kind) const;
    bool isPending(const QString& titleId) const { return m_pending.contains(titleId); }

    void request(const Game& game);  // fetch unless cached (a "not found" is cached too)
    void refresh(const Game& game);  // fetch again
    void clearCache();

    static QString searchTerm(const Game& game);
    static QUrl searchUrl(const QString& term);
    static QUrl gridsUrl(qint64 gameId);
    static QUrl heroesUrl(qint64 gameId);
    // Pick the first usable result from an API v2 response body (tested).
    static qint64 parseSearch(const QByteArray& body, QString* name = nullptr);
    static QUrl parseFirstImage(const QByteArray& body);

    CoverCache& cache() { return m_cache; }

signals:
    void artChanged(const QString& titleId);
    void lookupFailed(const QString& titleId, const QString& message);

private:
    void fetch(const Game& game);
    QNetworkReply* apiGet(const QUrl& url);
    void downloadImage(const QString& titleId, CoverCache::Kind kind, const QUrl& url, std::function<void()> next);
    void finish(const QString& titleId, const CoverCache::Entry& entry);

    AppSettings& m_settings;
    CoverCache m_cache;
    QNetworkAccessManager* m_net = nullptr;
    QSet<QString> m_pending;
};
