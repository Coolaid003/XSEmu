#pragma once

#ifdef XSEMU_DEV_TOOLS

#include <QSize>
#include <QStringList>

class MainWindow;
class Services;

// The screenshot lever (developer builds only): puts the launcher into a design state on fixture
// data and saves a grab of the window, so a state can be compared with the Paper renders.
//   XSEmuLauncher --theme dark --size 1280x800 --state warmup-running --grab out.png
// Without --grab the window stays open in that state. --list-states prints the state names.
namespace DevLever {

struct Options {
    QString theme = QStringLiteral("light");
    QSize size{1280, 800};
    QString state;
    QString grab;
    bool listStates = false;
};

QStringList states();

// False when the arguments carry no lever option; error is set when they are malformed.
bool parse(const QStringList& args, Options* out, QString* error);

// Before Services exist: copy the fixtures to a temporary folder and point the launcher data, the
// emulator data root and the shader cache at it, so nothing on this machine is read or written.
bool prepare(QString* error);

// After the window exists: apply the state, show the window and, with --grab, save and quit.
void apply(const Options& options, Services& services, MainWindow& window);

}

#endif
