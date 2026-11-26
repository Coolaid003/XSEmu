# XSEmu launcher

`XSEmuLauncher` is the desktop front end for XSEmu: a game library, the shader warm-up, settings,
and the in-game window. It is a Qt 6 Widgets app in C++17. `XSEmu.exe` stays a separate process.
The launcher starts it, talks to it over the pipe described in
[docs/launcher-protocol.md](../docs/launcher-protocol.md), and runs only one XSEmu process tree at
a time: one game or one warm-up.

The design is the Paper file "XSEmu Launcher · Direction C, Cover grid". Its tokens (colours, type
scale, radii, spacing, control heights) are in `src/theme/Theme.h` and `Theme.cpp`.

## Build

You need CMake 3.21 or later, Ninja, and Qt 6.4 or later with the Core, Gui, Widgets, Network,
Svg and Test modules.

Windows (Qt 6.8, MSVC 2022), from a Developer PowerShell:

```powershell
cmake -S launcher -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build
cmake --install build --prefix dist   # runs windeployqt into dist/bin
```

Put `XSEmu.exe` next to `XSEmuLauncher.exe`, or set its path in Settings.

Linux (Ubuntu 24.04, development only):

```sh
sudo apt install cmake ninja-build g++ qt6-base-dev qt6-svg-dev
cmake -S launcher -B build -G Ninja
cmake --build build
cd build && ctest --output-on-failure
```

### Options

| Option | Default | What it does |
| --- | --- | --- |
| `XSEMU_LAUNCHER_TESTS` | on (off on Windows) | Builds the unit and controller tests. |
| `XSEMU_FAKE_EMULATOR` | on (off on Windows) | Builds `fake-xsemu`, a stand-in for `XSEmu.exe`. |
| `XSEMU_DEV_TOOLS` | on (off on Windows) | Builds the screenshot lever (`--state`, `--grab`). |
| `XSEMU_DEPLOY_QT` | on | Runs `windeployqt` after the build and at install (Windows). |

## Run

```sh
./XSEmuLauncher
```

The settings file is `settings.ini` in the app's local data folder. Set `XSEMU_LAUNCHER_DATA` to
use another folder. The launcher reads emulator data from `%LOCALAPPDATA%\XCL` (`XCL_DATA_ROOT`
overrides it) and the shader cache from `%LOCALAPPDATA%\xcl\dxil` (`XCL_DXIL_CACHE`).

On Linux the launcher looks for `fake-xsemu` next to itself instead of `XSEmu.exe`. Choose its
behaviour with `FAKE_XSEMU_SCENARIO`: `ok`, `crash`, `device_removed`, `files_missing`,
`slow_boot`, `warmup_fail`, `warmup_ram_floor` or `warmup_refuse`. `FAKE_XSEMU_TICK_MS` sets
its speed.

## Screenshot lever

Developer builds can put the window into a design state on fixture data and save a grab, so a state
can be compared with the Paper renders:

```sh
QT_QPA_PLATFORM=offscreen ./XSEmuLauncher --theme dark --size 1280x800 --state warmup-running --grab out.png
./XSEmuLauncher --list-states
```

The lever copies `tests/fixtures` to a temporary folder and points the launcher data, the emulator
data root and the shader cache there. It reads and writes nothing else on the machine. Without
`--grab` the window stays open in that state. Keep grabs out of the repository.

## Layout

| Path | Contents |
| --- | --- |
| `src/core` | Services without UI, built as the `xsemu_core` library and unit tested: paths, the game library, `MicrosoftGame.config` and `xcl.toml` handling, the protocol, settings, the process gate and tree, the emulator and warm-up controllers, cover art, the shader cache. |
| `src/theme` | Design tokens, light and dark palettes, the global style sheet. |
| `src/widgets` | The component kit: Label, Button, Badge, Toggle, SegmentedControl, SearchBox, CoverTile, ProgressBlock, Card, Dialog, InfoNote, ChoiceCard, NavItem, PageHeader, Spinner, icons and the app mark. |
| `src/shell` | The frameless main window, title bar, sidebar, in-game view and error dialogs. |
| `src/pages` | Library, Warm-up, Settings, Game details and First launch. |
| `src/app` | Service wiring and the screenshot lever. |
| `resources` | Line icons as SVG, the Qt resource file and the Windows `.rc`. The app logo comes from `assets/logo` at the repository root. |
| `tools/fake-xsemu` | The protocol stand-in used by the tests and on Linux. |
| `tests` | Unit tests, controller tests against `fake-xsemu`, and text fixtures. |

The repository's PR check refuses binary files and game file names, so fixtures are text files
with a `.fixture` suffix. `DevFixtures::materialize` copies them to their real names at test time.

## What works today

- Library scan and watch, cover tiles, locked tiles while a game or warm-up runs, search, the paused
  banner and NOW PLAYING.
- Launch checks (emulator missing, game files missing, busy, another XSEmu running, not enough RAM)
  with their dialogs, and the crash, GPU-removed and boot-error dialogs.
- Warm-up start, cancel and per-phase progress from `--progress-json`, plus the recent runs.
- Settings for console model, network, giant-compile cap, overlay, game folders and theme
  (light, dark or system, switched live).
- First launch in three steps.
- Cover art from SteamGridDB once a key is set in `settings.ini` (`covers/steamGridDbKey`).

Not done yet: the game window is not docked into the in-game view (the launcher starts XSEmu
without `--host-hwnd`, so the game opens its own window), and there is no in-game overlay control,
key handling or fullscreen. Those come with the in-game view.
