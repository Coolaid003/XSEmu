#include "DiskSpace.h"

#include "Format.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

namespace DiskSpace {

namespace {

FreeBytes& override()
{
    static FreeBytes f;
    return f;
}

QString existingAncestor(const QString& path)
{
    QString p = QDir::cleanPath(QDir::fromNativeSeparators(path));
    while (!p.isEmpty() && !QFileInfo(p).isDir()) {
        const QString up = QFileInfo(p).path();
        if (up == p)
            break;
        p = up;
    }
    return p;
}

}

qint64 freeBytes(const QString& path)
{
    if (override())
        return override()(path);
    const QStorageInfo info(existingAncestor(path));
    return info.isValid() ? info.bytesAvailable() : -1;
}

void setFreeBytesFunction(FreeBytes f) { override() = std::move(f); }

Check checkRunnerFolder(const QString& dir)
{
    Check c;
    c.freeBytes = freeBytes(dir);
    const QStorageInfo info(existingAncestor(dir));
    c.drive = QDir::toNativeSeparators(info.isValid() ? info.rootPath() : QDir::rootPath());
    if (c.freeBytes < 0) {
        c.text = QStringLiteral("Free space on %1 is unknown.").arg(c.drive);
        return c;
    }
    c.low = c.freeBytes < kRunnerLowBytes;
    c.text = c.low ? QStringLiteral("Only %1 free on %2. Source builds keep Rust, the source and the build output here; with less than %3 free a build can run out of room part way.")
                         .arg(Format::bytes(c.freeBytes), c.drive, Format::bytes(kRunnerLowBytes))
                   : QStringLiteral("%1 free on %2").arg(Format::bytes(c.freeBytes), c.drive);
    return c;
}

}
