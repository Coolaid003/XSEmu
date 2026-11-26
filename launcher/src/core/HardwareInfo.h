#pragma once

#include <QObject>
#include <QString>

// The real machine: memory and the GPU XSEmu will use. Nothing here is invented: fields the platform
// does not report stay at their "unknown" values (0 / empty).
struct HardwareInfo {
    quint64 totalRamBytes = 0;
    quint64 availableRamBytes = 0;
    QString gpuName;            // empty: unknown
    quint64 dedicatedVramBytes = 0;
    quint32 gpuVendorId = 0;    // PCI vendor of that GPU (DXGI VendorId, /sys .../vendor); 0: unknown

    static HardwareInfo read();
    static quint64 availableRam(); // cheap, for polling

    QString gpuNameOrUnknown() const { return gpuName.isEmpty() ? QStringLiteral("Unknown GPU") : gpuName; }
};

// HardwareInfo::read() once, on a worker thread when the launcher starts; DXGI adapter enumeration
// can take a noticeable moment. Until it lands info() holds the unknown values.
class HardwareMonitor : public QObject {
    Q_OBJECT
public:
    explicit HardwareMonitor(QObject* parent = nullptr);
    bool ready() const { return m_ready; }
    const HardwareInfo& info() const { return m_info; }
    QString gpuLabel() const { return m_ready ? m_info.gpuNameOrUnknown() : QStringLiteral("Detecting..."); }

signals:
    void changed();

private:
    HardwareInfo m_info;
    bool m_ready = false;
};
