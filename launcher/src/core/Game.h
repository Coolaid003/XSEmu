#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

// One game folder found by Library: what MicrosoftGame.config says about it.
struct Game {
    QString folder;        // package root (the folder holding MicrosoftGame.config), canonical
    QString configPath;    // its MicrosoftGame.config
    QString identityName;  // <Identity Name="...">, may be empty
    QString version;       // <Identity Version="...">
    QString displayName;   // <ShellVisuals DefaultDisplayName="..."> unless ms-resource: or empty
    QString publisher;     // <ShellVisuals PublisherDisplayName="..."> unless ms-resource: or empty
    QStringList executables; // every <Executable Name="...">, in file order
    QString exe;           // the first executable that exists in the folder (the emulator's pick), else the first named
    bool exeExists = false;
    qint64 sizeBytes = -1; // -1: not measured

    // The name people see: DefaultDisplayName, else the identity, else the folder name.
    QString title() const;
    // The key everything per-title uses (xcl.toml folder, settings, covers): the emulator's data folder name.
    QString titleId() const;
    bool isValid() const { return !folder.isEmpty(); }
};

using GameList = QList<Game>;

Q_DECLARE_METATYPE(Game)
