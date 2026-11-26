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
// FAKE_XSEMU_SCENARIO = ok | crash | files_missing (exit 1 with "Error: ...") | slow_boot.
namespace FakeRunner {

// The runner this invocation stands in for, or empty when it is plain fake-xsemu.
QString kind(const QString& program, QStringList& args);
int run(const QString& kind, const QStringList& args, int argc, char** argv);

}
