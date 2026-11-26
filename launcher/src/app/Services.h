#pragma once

#include "core/AppSettings.h"
#include "core/CoverArtService.h"
#include "core/EmulatorController.h"
#include "core/Library.h"
#include "core/ProcessGate.h"
#include "core/WarmupController.h"

// Every long-lived service of the launcher, created once in main() and handed to the shell by
// reference. The gate is shared so one XSEmu process tree runs at a time (a game or a warm-up).
class Services : public QObject {
    Q_OBJECT
public:
    explicit Services(QObject* parent = nullptr);

    AppSettings settings;
    ProcessGate gate;
    Library library;
    EmulatorController emulator;
    WarmupController warmup;
    CoverArtService covers;

    // Settings → Console: "lockhart" is Series S, "anaconda" Series X.
    static QString consoleName(const QString& deviceType);
};
