#pragma once

#include "AppSettings.h"

#include <QDateTime>
#include <QSet>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <atomic>

// What the launcher can truthfully say about the shader cache, read from the files the emulator
// writes (crates/xcl/src/hle/d3d12x/pso_manifest.rs and real.rs). The cache is machine-wide, not
// per title: <dxil>\pipelines\pipelines.bin lists every pipeline any title made (u32 length + entry,
// keyed by FNV-1a 64 of the entry | 1), <dxil>\pipelines\runs counts the game runs, and the driver
// blobs the warm-up built are <dxil>\driver\<vendor-device-umd>\*.pso. A value of -1 means Unknown.
struct ShaderCacheInfo {
    QString dxilDir;
    bool scanned = false;          // read from disk; a default value says nothing about the cache
    bool exists = false;           // the DXIL cache folder exists
    qint64 pipelines = -1;         // distinct pipelines recorded (-1: no pipelines.bin)
    qint64 manifestEntries = -1;   // entries in pipelines.bin, repeats included
    qint64 manifestBytes = -1;
    int runs = -1;                 // game runs counted (-1: no runs file)
    qint64 blobs = -1;             // *.pso driver blobs, all adapters (-1: no driver folder)
    qint64 blobBytes = -1;
    int adapters = 0;              // driver blob folders (one per GPU + driver version)
    QDateTime newestBlob;          // when the newest blob was written (invalid: none)
    qint64 totalBytes = -1;        // every file in the cache folder

    // What has been parsed of pipelines.bin so far, so a later read parses only the appended tail.
    // It starts over when the file shrinks, its first bytes change or the path changes.
    struct Manifest {
        QString path;
        QByteArray head;
        qint64 offset = 0;
        qint64 entries = 0;
        QSet<quint64> keys;
    };
    // Parses the new part of the manifest at `path` in chunks; false when the file is missing.
    static bool readManifest(const QString& path, Manifest& m, const std::atomic<bool>* cancel = nullptr);

    // Walks the whole folder; slow on a real cache, so only ever called off the UI thread.
    static ShaderCacheInfo read(const QString& dxilDir);
    static ShaderCacheInfo read(const QString& dxilDir, Manifest& manifest, const std::atomic<bool>* cancel = nullptr);

    // FNV-1a 64 as the emulator computes it (gpu::shader::fnv64).
    static quint64 fnv64(const char* data, qsizetype len);

    // An entry at the top of the cache folder that the emulator writes (crates/xcl): the pipelines,
    // driver and translated folders (pso_manifest.rs, real.rs, gpu/recomp/pso.rs), <16 hex>.dxil
    // and its <16 hex>.tmp<pid> partial (gpu/recomp/dxc.rs), used-pipelines.txt and
    // skipped-pipelines.txt (pso_async.rs). Clear deletes these and nothing else.
    static bool isCacheEntry(const QFileInfo& entry);

    struct Cleared {
        bool ok = true;      // every cache entry was deleted
        QStringList kept;    // other entries in the folder, left alone
        QString error;       // why it refused or what could not be deleted
    };
    // Delete the emulator's entries in the cache folder and keep the folder and everything else.
    // Refuses a drive root and the home folder.
    static Cleared clear(const QString& dxilDir);
};

// A game's warm-up state as far as the launcher can tell: from its own warm-up records (the
// emulator keeps no per-title shader state, so a game nobody warmed up from this launcher is Unknown
// unless the machine-wide cache is empty). A finished warm-up is Ready only while the cache still
// holds driver blobs, so a cleared cache reads as NeverWarmed. Before the cache is scanned nothing is
// claimed about it: only the launcher's own records count.
enum class TitleShaderState { Unknown, NeverWarmed, Ready, LastRunIncomplete, LastRunFailed };

TitleShaderState titleShaderState(const QString& titleId, const QList<WarmupRun>& recent, const ShaderCacheInfo& cache);
