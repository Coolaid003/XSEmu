#include "DevFixtures.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace DevFixtures {

bool materialize(const QString& src, const QString& dst)
{
    const QDir from(src);
    if (!from.exists() || !QDir().mkpath(dst))
        return false;
    const QString suffix = QLatin1String(kSuffix);
    QDirIterator it(src, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QString rel = from.relativeFilePath(it.filePath());
        if (rel.endsWith(suffix))
            rel.chop(suffix.size());
        const QString target = QDir(dst).filePath(rel);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile::remove(target);
        if (!QFile::copy(it.filePath(), target))
            return false;
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
    }
    return true;
}

}
