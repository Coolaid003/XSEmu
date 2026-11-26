#pragma once

#include <QStringList>

// fake-xsemu standing in for a third-party runner, so the runner flow can be shown and tested
// without Windows or the real programs. It prints each runner's real output formats (as read from
// their sources) and opens its own top-level game window, like they do.
//   fake-xsemu --as kelvaroq run <title exe> --console series-s --execute
//   fake-xsemu --as kelvaroq precompile [--check]
//   fake-xsemu --as victory shaders --jobs N <game dir> | warm --used --jobs N
//   fake-xsemu --as victory-dexnom warm --no-rt --jobs N <game dir>
// The kind also follows the program's file name: a copy named kelvaroq-engine or victory acts as one.
// FAKE_XSEMU_SCENARIO = ok | crash | files_missing (exit 1 with "Error: ...") | slow_boot | silent (a
// pre-cache command exits 0 at once and prints nothing). FAKE_XSEMU_WAITING: what Kelvaroq's check
// finds waiting (812).
// Both VIctory kinds load their config first, as the forks do: with VICTORY_CONFIG unset, naming a file
// that is not there, or holding anything but [paths] game_dir, cache_dir and data_dir (DexNom also
// [graphics] ray_tracing), they print "Error: ..." and exit 1. The 7900 XTX "warm" counts the pipelines
// in <VICTORY_CACHE_DIR>/pipelines/known-pipelines.bin (one per line here).
namespace FakeRunner {

// The runner this invocation stands in for, or empty when it is plain fake-xsemu.
QString kind(const QString& program, QStringList& args);
int run(const QString& kind, const QStringList& args, int argc, char** argv);

}
