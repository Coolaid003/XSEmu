#pragma once

#include <QString>

// The real machine: memory and the GPU XSEmu will use. Nothing here is invented: fields the platform
// does not report stay at their "unknown" values (0 / empty).
struct HardwareInfo {
    quint64 totalRamBytes = 0;
    quint64 availableRamBytes = 0;
    QString gpuName;            // empty: unknown
    quint64 dedicatedVramBytes = 0;

    static HardwareInfo read();
    static quint64 availableRam(); // cheap, for polling

    QString gpuNameOrUnknown() const { return gpuName.isEmpty() ? QStringLiteral("Unknown GPU") : gpuName; }
};
