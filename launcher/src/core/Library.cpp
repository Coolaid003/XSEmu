#include "Library.h"

#include "GameManifest.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSet>
#include <QThreadPool>

namespace {

constexpr int kSizeFileCap = 50000;

qint64 folderSize(const QString& folder)
{
    qint64 total = 0;
    int files = 0;
    QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
        if (++files > kSizeFileCap)
            return -1;
    }
    return total;
}

QString cleanName(const QString& v)
{
    const QString t = v.trimmed();
    if (t.isEmpty() || t.startsWith(QLatin1String("ms-resource:")))
        return {};
    return t;
}

void walk(const QString& dir, int depth, QSet<QString>& seen, GameList& out, bool measureSize)
{
    if (!GameManifest::findIn(dir).isEmpty()) {
        Game g = Library::readGame(dir);
        if (g.isValid() && !seen.contains(g.folder)) {
            seen.insert(g.folder);
            if (measureSize)
                g.sizeBytes = folderSize(g.folder);
            out.append(g);
        }
        return;
    }
    if (depth <= 0)
        return;
    QDir d(dir);
    const QStringList subs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    for (const QString& s : subs)
        walk(d.filePath(s), depth - 1, seen, out, measureSize);
}

}

Library::Library(QObject* parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(600);
    connect(&m_debounce, &QTimer::timeout, this, &Library::rescan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
}

void Library::setFolders(const QStringList& folders)
{
    m_folders = folders;
    rewatch();
    rescan();
}

Game Library::game(const QString& titleId) const
{
    for (const Game& g : m_games) {
        if (g.titleId() == titleId)
            return g;
    }
    return {};
}

void Library::rescan()
{
    const quint64 generation = ++m_generation;
    m_scanning = true;
    emit scanStarted();
    const QStringList folders = m_folders;
    QPointer<Library> self(this);
    QThreadPool::globalInstance()->start([self, folders, generation] {
        GameList games = Library::scan(folders);
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [self, games, generation] {
            if (self)
                self->apply(games, generation);
        }, Qt::QueuedConnection);
    });
}

void Library::rescanNow()
{
    apply(scan(m_folders), ++m_generation);
}

void Library::apply(const GameList& games, quint64 generation)
{
    if (generation != m_generation)
        return;
    m_scanning = false;
    m_games = games;
    rewatch();
    emit gamesChanged();
}

void Library::rewatch()
{
    const QStringList old = m_watcher.directories() + m_watcher.files();
    if (!old.isEmpty())
        m_watcher.removePaths(old);
    QStringList paths;
    for (const QString& f : m_folders) {
        if (!QFileInfo(f).isDir())
            continue;
        paths << f;
        const QDir d(f);
        for (const QString& s : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks))
            paths << d.filePath(s);
    }
    for (const Game& g : m_games)
        paths << g.configPath;
    paths.removeDuplicates();
    if (!paths.isEmpty())
        m_watcher.addPaths(paths);
}

GameList Library::scan(const QStringList& folders, int depth, bool measureSize)
{
    GameList out;
    QSet<QString> seen;
    for (const QString& f : folders) {
        const QString canonical = QFileInfo(f).canonicalFilePath();
        if (!canonical.isEmpty())
            walk(canonical, depth, seen, out, measureSize);
    }
    return out;
}

Game Library::readGame(const QString& folder)
{
    const QString canonical = QFileInfo(folder).canonicalFilePath();
    const QString configPath = GameManifest::findIn(canonical);
    if (canonical.isEmpty() || configPath.isEmpty())
        return {};
    QFile f(configPath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QString xml = GameManifest::decode(f.readAll());
    if (xml.isNull())
        return {};
    Game g;
    g.folder = canonical;
    g.configPath = configPath;
    g.identityName = GameManifest::attr(xml, QStringLiteral("Identity"), QStringLiteral("Name")).trimmed();
    g.version = GameManifest::attr(xml, QStringLiteral("Identity"), QStringLiteral("Version")).trimmed();
    g.displayName = cleanName(GameManifest::attr(xml, QStringLiteral("ShellVisuals"), QStringLiteral("DefaultDisplayName")));
    g.publisher = cleanName(GameManifest::attr(xml, QStringLiteral("ShellVisuals"), QStringLiteral("PublisherDisplayName")));
    g.executables = GameManifest::attrs(xml, QStringLiteral("Executable"), QStringLiteral("Name"));
    const QDir root(canonical);
    for (const QString& e : g.executables) {
        if (QFileInfo(root.filePath(QString(e).replace(QLatin1Char('\\'), QLatin1Char('/')))).isFile()) {
            g.exe = e;
            g.exeExists = true;
            break;
        }
    }
    if (g.exe.isEmpty() && !g.executables.isEmpty())
        g.exe = g.executables.first();
    return g;
}
