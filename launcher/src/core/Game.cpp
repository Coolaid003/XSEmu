#include "Game.h"

#include "Paths.h"

#include <QDir>
#include <QFileInfo>

QString Game::title() const
{
    if (!displayName.isEmpty())
        return displayName;
    if (!identityName.isEmpty())
        return identityName;
    return QFileInfo(folder).fileName();
}

QString Game::titleId() const
{
    return Paths::titleDirName(identityName.isEmpty() ? QFileInfo(folder).fileName() : identityName);
}
