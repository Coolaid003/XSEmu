#include "CoverArtService.h"

#include "AppSettings.h"
#include "Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrlQuery>

#include <memory>

// ---- CoverCache ----

CoverCache::CoverCache(QString dir)
    : m_dir(std::move(dir))
{
    load();
}

void CoverCache::load()
{
    m_entries.clear();
    QFile f(QDir(m_dir).filePath(QStringLiteral("index.json")));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.gameId = static_cast<qint64>(o.value(QStringLiteral("gameId")).toDouble());
        e.name = o.value(QStringLiteral("name")).toString();
        e.grid = o.value(QStringLiteral("grid")).toString();
        e.hero = o.value(QStringLiteral("hero")).toString();
        e.fetchedAt = QDateTime::fromString(o.value(QStringLiteral("fetchedAt")).toString(), Qt::ISODate);
        e.notFound = o.value(QStringLiteral("notFound")).toBool();
        m_entries.insert(it.key(), e);
    }
}

void CoverCache::save() const
{
    QDir().mkpath(m_dir);
    QJsonObject root;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        const Entry& e = it.value();
        root.insert(it.key(), QJsonObject{
            {QStringLiteral("gameId"), static_cast<double>(e.gameId)},
            {QStringLiteral("name"), e.name},
            {QStringLiteral("grid"), e.grid},
            {QStringLiteral("hero"), e.hero},
            {QStringLiteral("fetchedAt"), e.fetchedAt.toUTC().toString(Qt::ISODate)},
            {QStringLiteral("notFound"), e.notFound},
        });
    }
    QSaveFile f(QDir(m_dir).filePath(QStringLiteral("index.json")));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void CoverCache::put(const QString& titleId, const Entry& entry)
{
    m_entries.insert(titleId, entry);
    save();
}

void CoverCache::remove(const QString& titleId)
{
    const Entry e = m_entries.take(titleId);
    const QDir d(m_dir);
    if (!e.grid.isEmpty()) QFile::remove(d.filePath(e.grid));
    if (!e.hero.isEmpty()) QFile::remove(d.filePath(e.hero));
    save();
}

void CoverCache::clear()
{
    const QDir d(m_dir);
    for (const Entry& e : std::as_const(m_entries)) {
        if (!e.grid.isEmpty()) QFile::remove(d.filePath(e.grid));
        if (!e.hero.isEmpty()) QFile::remove(d.filePath(e.hero));
    }
    m_entries.clear();
    save();
}

QString CoverCache::path(const QString& titleId, Kind kind) const
{
    const auto it = m_entries.constFind(titleId);
    if (it == m_entries.cend())
        return {};
    const QString file = kind == Kind::Grid ? it->grid : it->hero;
    if (file.isEmpty())
        return {};
    const QString p = QDir(m_dir).filePath(file);
    return QFileInfo(p).isFile() ? p : QString();
}

QString CoverCache::fileName(const QString& titleId, Kind kind, const QString& extension)
{
    const QString ext = extension.isEmpty() ? QStringLiteral("img") : extension.toLower();
    return QStringLiteral("%1-%2.%3").arg(titleId, kind == Kind::Grid ? QStringLiteral("grid") : QStringLiteral("hero"), ext);
}

// ---- CoverArtService ----

CoverArtService::CoverArtService(AppSettings& settings, QObject* parent)
    : CoverArtService(settings, Paths::coverCacheDir(), parent)
{
}

CoverArtService::CoverArtService(AppSettings& settings, const QString& cacheDir, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_cache(cacheDir)
{
}

CoverArtService::~CoverArtService() = default;

bool CoverArtService::canLookUp() const
{
    return lookupsUnavailableReason().isEmpty();
}

QString CoverArtService::lookupsUnavailableReason() const
{
    if (!m_settings.artLookups())
        return QStringLiteral("Cover art lookups are off.");
    if (m_settings.steamGridDbKey().isEmpty())
        return QStringLiteral("Add your SteamGridDB API key to look up cover art.");
    return {};
}

QString CoverArtService::artPath(const QString& titleId, CoverCache::Kind kind) const
{
    return m_cache.path(titleId, kind);
}

QString CoverArtService::searchTerm(const Game& game)
{
    return game.title().trimmed();
}

QUrl CoverArtService::searchUrl(const QString& term)
{
    QUrl url(QString::fromLatin1(kApiBase));
    url.setPath(url.path() + QStringLiteral("/search/autocomplete/") + QString::fromLatin1(QUrl::toPercentEncoding(term)), QUrl::StrictMode);
    return url;
}

QUrl CoverArtService::gridsUrl(qint64 gameId)
{
    QUrl url(QStringLiteral("%1/grids/game/%2").arg(QString::fromLatin1(kApiBase)).arg(gameId));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("dimensions"), QStringLiteral("600x900"));
    q.addQueryItem(QStringLiteral("types"), QStringLiteral("static"));
    url.setQuery(q);
    return url;
}

QUrl CoverArtService::heroesUrl(qint64 gameId)
{
    QUrl url(QStringLiteral("%1/heroes/game/%2").arg(QString::fromLatin1(kApiBase)).arg(gameId));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("types"), QStringLiteral("static"));
    url.setQuery(q);
    return url;
}

qint64 CoverArtService::parseSearch(const QByteArray& body, QString* name)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    if (!root.value(QStringLiteral("success")).toBool())
        return 0;
    for (const QJsonValue& v : root.value(QStringLiteral("data")).toArray()) {
        const QJsonObject o = v.toObject();
        const qint64 id = static_cast<qint64>(o.value(QStringLiteral("id")).toDouble());
        if (id > 0) {
            if (name)
                *name = o.value(QStringLiteral("name")).toString();
            return id;
        }
    }
    return 0;
}

QUrl CoverArtService::parseFirstImage(const QByteArray& body)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    if (!root.value(QStringLiteral("success")).toBool())
        return {};
    for (const QJsonValue& v : root.value(QStringLiteral("data")).toArray()) {
        const QUrl url(v.toObject().value(QStringLiteral("url")).toString());
        if (url.isValid() && url.scheme() == QLatin1String("https"))
            return url;
    }
    return {};
}

void CoverArtService::request(const Game& game)
{
    if (!game.isValid() || m_cache.contains(game.titleId()))
        return;
    fetch(game);
}

void CoverArtService::refresh(const Game& game)
{
    if (game.isValid())
        fetch(game);
}

void CoverArtService::clearCache()
{
    m_cache.clear();
    emit artChanged(QString());
}

QNetworkReply* CoverArtService::apiGet(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", "Bearer " + m_settings.steamGridDbKey().toUtf8());
    req.setRawHeader("Accept", "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return m_net->get(req);
}

void CoverArtService::fetch(const Game& game)
{
    const QString titleId = game.titleId();
    if (!canLookUp() || m_pending.contains(titleId))
        return;
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    m_pending.insert(titleId);

    QNetworkReply* search = apiGet(searchUrl(searchTerm(game)));
    connect(search, &QNetworkReply::finished, this, [this, search, titleId] {
        search->deleteLater();
        if (search->error() != QNetworkReply::NoError) {
            m_pending.remove(titleId);
            emit lookupFailed(titleId, search->errorString());
            return;
        }
        auto entry = std::make_shared<CoverCache::Entry>();
        entry->fetchedAt = QDateTime::currentDateTimeUtc();
        entry->gameId = parseSearch(search->readAll(), &entry->name);
        if (entry->gameId == 0) {
            entry->notFound = true;
            finish(titleId, *entry);
            return;
        }
        QNetworkReply* grids = apiGet(gridsUrl(entry->gameId));
        connect(grids, &QNetworkReply::finished, this, [this, grids, titleId, entry] {
            grids->deleteLater();
            const QUrl gridUrl = grids->error() == QNetworkReply::NoError ? parseFirstImage(grids->readAll()) : QUrl();
            QNetworkReply* heroes = apiGet(heroesUrl(entry->gameId));
            connect(heroes, &QNetworkReply::finished, this, [this, heroes, titleId, entry, gridUrl] {
                heroes->deleteLater();
                const QUrl heroUrl = heroes->error() == QNetworkReply::NoError ? parseFirstImage(heroes->readAll()) : QUrl();
                const auto done = [this, titleId, entry] { finish(titleId, *entry); };
                const auto hero = [this, titleId, heroUrl, done] { downloadImage(titleId, CoverCache::Kind::Hero, heroUrl, done); };
                downloadImage(titleId, CoverCache::Kind::Grid, gridUrl, hero);
            });
        });
    });
}

void CoverArtService::downloadImage(const QString& titleId, CoverCache::Kind kind, const QUrl& url, std::function<void()> next)
{
    if (!url.isValid()) {
        next();
        return;
    }
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, titleId, kind, url, next] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            const QString ext = QFileInfo(url.path()).suffix();
            const QString name = CoverCache::fileName(titleId, kind, ext);
            QDir().mkpath(m_cache.dir());
            QSaveFile f(QDir(m_cache.dir()).filePath(name));
            if (f.open(QIODevice::WriteOnly)) {
                f.write(reply->readAll());
                if (f.commit()) {
                    CoverCache::Entry e = m_cache.entry(titleId);
                    (kind == CoverCache::Kind::Grid ? e.grid : e.hero) = name;
                    m_cache.put(titleId, e);
                }
            }
        }
        next();
    });
}

void CoverArtService::finish(const QString& titleId, const CoverCache::Entry& found)
{
    CoverCache::Entry e = m_cache.entry(titleId);
    e.gameId = found.gameId;
    e.name = found.name;
    e.fetchedAt = found.fetchedAt;
    e.notFound = found.notFound;
    m_cache.put(titleId, e);
    m_pending.remove(titleId);
    emit artChanged(titleId);
}
