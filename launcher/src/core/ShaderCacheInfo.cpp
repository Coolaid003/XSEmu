#include "ShaderCacheInfo.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

quint64 ShaderCacheInfo::fnv64(const char* data, qsizetype len)
{
    quint64 h = 0xcbf29ce484222325ULL;
    for (qsizetype i = 0; i < len; ++i) {
        h ^= static_cast<unsigned char>(data[i]);
        h *= 0x100000001b3ULL;
    }
    return h;
}

bool ShaderCacheInfo::readManifest(const QString& path, Manifest& m, const std::atomic<bool>* cancel)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m = Manifest{};
        return false;
    }
    const qint64 size = file.size();
    const QByteArray head = file.read(64);
    if (m.path != path || size < m.offset || !head.startsWith(m.head))
        m = Manifest{path, {}, 0, 0, {}};
    m.head = head;
    if (!file.seek(m.offset))
        return true;
    constexpr qint64 chunk = 4 << 20;
    QByteArray buf;
    qint64 at = m.offset;
    while (!file.atEnd()) {
        if (cancel && cancel->load())
            break;
        buf += file.read(chunk);
        qsizetype pos = 0;
        while (pos + 4 <= buf.size()) {
            const auto* p = reinterpret_cast<const unsigned char*>(buf.constData() + pos);
            const quint32 len = quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
            if (pos + 4 + qsizetype(len) > buf.size())
                break;
            m.keys.insert(fnv64(buf.constData() + pos + 4, len) | 1ULL);
            ++m.entries;
            pos += 4 + qsizetype(len);
        }
        buf.remove(0, pos);
        at += pos;
        m.offset = at;
    }
    return true;
}

ShaderCacheInfo ShaderCacheInfo::read(const QString& dxilDir)
{
    Manifest manifest;
    return read(dxilDir, manifest);
}

ShaderCacheInfo ShaderCacheInfo::read(const QString& dxilDir, Manifest& manifest, const std::atomic<bool>* cancel)
{
    ShaderCacheInfo info;
    info.dxilDir = dxilDir;
    info.scanned = true;
    const QDir root(dxilDir);
    info.exists = root.exists();
    if (!info.exists) {
        manifest = Manifest{};
        return info;
    }

    const QString manifestPath = root.filePath(QStringLiteral("pipelines/pipelines.bin"));
    if (readManifest(manifestPath, manifest, cancel)) {
        info.manifestBytes = QFileInfo(manifestPath).size();
        info.manifestEntries = manifest.entries;
        info.pipelines = manifest.keys.size();
    }

    QFile runs(root.filePath(QStringLiteral("pipelines/runs")));
    if (runs.open(QIODevice::ReadOnly)) {
        bool ok = false;
        const int n = QString::fromUtf8(runs.readAll()).trimmed().toInt(&ok);
        info.runs = ok ? n : 0;
    }

    // one walk for the folder size and the driver blobs (driver/<adapter>/**/*.pso)
    const QDir driver(root.filePath(QStringLiteral("driver")));
    const bool hasDriver = driver.exists();
    if (hasDriver) {
        info.blobs = 0;
        info.blobBytes = 0;
        info.adapters = int(driver.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size());
    }
    const QString driverPrefix = driver.absolutePath() + QLatin1Char('/');
    info.totalBytes = 0;
    QDirIterator it(root.absolutePath(), QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    for (int n = 0; it.hasNext(); ++n) {
        if ((n & 1023) == 0 && cancel && cancel->load())
            break;
        const QString path = it.next();
        const QFileInfo fi = it.fileInfo();
        info.totalBytes += fi.size();
        if (!hasDriver || !path.endsWith(QLatin1String(".pso")) || !path.startsWith(driverPrefix) || path.indexOf(QLatin1Char('/'), driverPrefix.size()) < 0)
            continue;
        ++info.blobs;
        info.blobBytes += fi.size();
        const QDateTime modified = fi.lastModified();
        if (!info.newestBlob.isValid() || modified > info.newestBlob)
            info.newestBlob = modified;
    }
    return info;
}

TitleShaderState titleShaderState(const QString& titleId, const QList<WarmupRun>& recent, const ShaderCacheInfo& cache)
{
    // a finished warm-up counts only while its driver blobs are still there (Clear empties the cache)
    const bool empty = cache.scanned && (!cache.exists || cache.blobs <= 0);
    for (const WarmupRun& r : recent) {
        if (r.titleId != titleId || !r.runnerId.isEmpty()) // a runner's pre-cache fills its own cache, not XSEmu's
            continue;
        switch (r.result) {
        case WarmupRun::Result::Finished: return empty ? TitleShaderState::NeverWarmed : TitleShaderState::Ready;
        case WarmupRun::Result::Failed: return TitleShaderState::LastRunFailed;
        case WarmupRun::Result::Cancelled:
        case WarmupRun::Result::StoppedPartWay: return TitleShaderState::LastRunIncomplete;
        }
    }
    if (empty)
        return TitleShaderState::NeverWarmed;
    return TitleShaderState::Unknown;
}

bool ShaderCacheInfo::isCacheEntry(const QFileInfo& entry)
{
    const QString name = entry.fileName();
    if (entry.isSymLink())
        return false;
    if (entry.isDir())
        return name == QLatin1String("pipelines") || name == QLatin1String("driver") || name == QLatin1String("translated");
    static const QRegularExpression dxil(QStringLiteral("^[0-9a-fA-F]{16}[.](dxil|tmp[0-9]+)$"));
    return name == QLatin1String("used-pipelines.txt") || name == QLatin1String("skipped-pipelines.txt") || dxil.match(name).hasMatch();
}

ShaderCacheInfo::Cleared ShaderCacheInfo::clear(const QString& dxilDir)
{
    Cleared r;
    const QDir dir(dxilDir);
    if (!dir.exists())
        return r;
    if (dir.isRoot() || QDir::cleanPath(dir.absolutePath()) == QDir::cleanPath(QDir::homePath())) {
        r.ok = false;
        r.error = QStringLiteral("%1 is not a shader cache folder, so nothing was deleted.").arg(QDir::toNativeSeparators(dxilDir));
        return r;
    }
    for (const QFileInfo& fi : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name)) {
        if (!isCacheEntry(fi)) {
            r.kept << fi.fileName();
            continue;
        }
        r.ok = (fi.isDir() ? QDir(fi.absoluteFilePath()).removeRecursively() : QFile::remove(fi.absoluteFilePath())) && r.ok;
    }
    if (!r.ok)
        r.error = QStringLiteral("Some cache files could not be deleted. Close XSEmu and try again.");
    return r;
}
