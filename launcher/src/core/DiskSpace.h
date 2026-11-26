#pragma once

#include <QString>

#include <functional>

// Free space where runners are kept (Settings → Game folders → Runners folder, a runner's Change
// location). The free-bytes function is replaceable for tests and the screenshot lever.
//
//   const DiskSpace::Check c = DiskSpace::checkRunnerFolder(dir);
//   note->setText(c.text);
//   note->setTone(c.low ? Theme::Tone::Warning : Theme::Tone::Neutral);
namespace DiskSpace {

using FreeBytes = std::function<qint64(const QString& path)>;

// Source builds keep the Rust toolchain, the source checkout and the build output (CARGO_TARGET_DIR)
// in the runners folder; below this much free space a build can run out of room part way.
inline constexpr qint64 kRunnerLowBytes = 20LL * 1024 * 1024 * 1024;

// Bytes free to this user on the drive that holds path (its nearest existing folder); -1 unknown.
qint64 freeBytes(const QString& path);
void setFreeBytesFunction(FreeBytes f); // nullptr: the real drive

struct Check {
    qint64 freeBytes = -1;
    bool low = false;
    QString drive; // "D:\\", "/"
    QString text;  // "412 GB free on D:\\" or the low-space warning
};
Check checkRunnerFolder(const QString& dir);

}
