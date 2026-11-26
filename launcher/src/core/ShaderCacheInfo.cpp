#include "ShaderCacheInfo.h"

#include "Paths.h"

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

ShaderCacheInfo ShaderCacheInfo::readDefault()
{
    return read(Paths::dxilCacheDir());
}

ShaderCacheInfo ShaderCacheInfo::read(const QString& dxilDir)
{
    ShaderCacheInfo info;
    info.dxilDir = dxilDir;
    const QDir root(dxilDir);
    info.exists = root.exists();
    if (!info.exists)
        return info;

    QFile manifest(root.filePath(QStringLiteral("pipelines/pipelines.bin")));
    if (manifest.open(QIODevice::ReadOnly)) {
        const QByteArray file = manifest.readAll();
        info.manifestBytes = file.size();
        QSet<quint64> keys;
        qint64 entries = 0;
        qsizetype at = 0;
        while (at + 4 <= file.size()) {
            const auto* p = reinterpret_cast<const unsigned char*>(file.constData() + at);
            const quint32 len = quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
            if (at + 4 + qsizetype(len) > file.size())
                break;
            keys.insert(fnv64(file.constData() + at + 4, len) | 1ULL);
            ++entries;
            at += 4 + len;
        }
        info.manifestEntries = entries;
        info.pipelines = keys.size();
    }

    QFile runs(root.filePath(QStringLiteral("pipelines/runs")));
    if (runs.open(QIODevice::ReadOnly)) {
        bool ok = false;
        const int n = QString::fromUtf8(runs.readAll()).trimmed().toInt(&ok);
        info.runs = ok ? n : 0;
    }

    const QDir driver(root.filePath(QStringLiteral("driver")));
    if (driver.exists()) {
        info.blobs = 0;
        info.blobBytes = 0;
        const QStringList adapters = driver.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        info.adapters = adapters.size();
        for (const QString& a : adapters) {
            QDirIterator it(driver.filePath(a), {QStringLiteral("*.pso")}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QFileInfo fi = it.fileInfo();
                ++info.blobs;
                info.blobBytes += fi.size();
                if (!info.newestBlob.isValid() || fi.lastModified() > info.newestBlob)
                    info.newestBlob = fi.lastModified();
            }
        }
    }
    return info;
}

TitleShaderState titleShaderState(const QString& titleId, const QList<WarmupRun>& recent, const ShaderCacheInfo& cache)
{
    // a finished warm-up counts only while its driver blobs are still there (Clear empties the cache)
    const bool empty = !cache.exists || cache.blobs <= 0;
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
