#pragma once

#include <QString>

// Every file location the launcher reads or writes, mirroring the emulator (crates/xcl/src/launcher.rs,
// config.rs Boot::logs_dir/cache_dir, gpu/recomp/dxc.rs cache_dir). Pure functions of the environment.
namespace Paths {

// %XCL_DATA_ROOT%, else %LOCALAPPDATA%\XCL (Linux: $XCL_DATA_ROOT, else ~/.local/share/XCL).
QString dataRoot();

// The emulator's per-title folder name: the Identity Name of MicrosoftGame.config (else the game
// folder's name) with every character outside [A-Za-z0-9._-] (Unicode letters and digits allowed) as '_'.
QString titleDirName(const QString& identityOrFolderName);

QString titleDir(const QString& titleDirName);
QString xclToml(const QString& titleDirName);
QString logsDir(const QString& titleDirName);    // <title>\logs (xcl.log, xcl.prev.log, report.json)
QString cacheDir(const QString& titleDirName);   // <title>\cache (boot.json)
QString reportJson(const QString& titleDirName); // <title>\logs\report.json
QString xclLog(const QString& titleDirName);     // <title>\logs\xcl.log

// Machine-wide shader cache: %XCL_DXIL_CACHE%, else %LOCALAPPDATA%\xcl\dxil.
QString dxilCacheDir();
QString pipelineManifestDir(); // <dxil>\pipelines (pipelines.bin, shaders\, runs, created.bin)
QString driverBlobDir();       // <dxil>\driver\<vendor-device-umd>\*.pso

// Launcher-owned data: %XSEMU_LAUNCHER_DATA%, else QStandardPaths::AppLocalDataLocation (%LOCALAPPDATA%\XSEmu).
QString launcherDataDir();
QString settingsFile(); // <launcher data>\settings.ini
QString coverCacheDir(); // <launcher data>\covers

// The emulator executable's default location: XSEmu.exe next to the launcher (Linux: fake-xsemu).
QString defaultEmulatorPath();

}
