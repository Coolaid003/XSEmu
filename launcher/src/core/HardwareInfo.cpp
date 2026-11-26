#include "HardwareInfo.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dxgi.h>
#endif

namespace {

#ifndef Q_OS_WIN
quint64 meminfoKb(const char* field)
{
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    const QByteArray want = QByteArray(field) + ':';
    while (!f.atEnd()) {
        const QByteArray line = f.readLine();
        if (line.startsWith(want)) {
            const QList<QByteArray> parts = line.mid(want.size()).simplified().split(' ');
            return parts.isEmpty() ? 0 : parts.first().toULongLong();
        }
    }
    return 0;
}

QString readTrimmed(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}
#endif

}

quint64 HardwareInfo::availableRam()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof(m);
    return GlobalMemoryStatusEx(&m) ? m.ullAvailPhys : 0;
#else
    return meminfoKb("MemAvailable") * 1024ULL;
#endif
}

HardwareInfo HardwareInfo::read()
{
    HardwareInfo hw;
#ifdef Q_OS_WIN
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m)) {
        hw.totalRamBytes = m.ullTotalPhys;
        hw.availableRamBytes = m.ullAvailPhys;
    }
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            const bool ok = SUCCEEDED(adapter->GetDesc1(&desc));
            adapter->Release();
            if (!ok || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                continue;
            // the adapter with the most dedicated memory: the one "auto" picks on a hybrid laptop
            if (desc.DedicatedVideoMemory > hw.dedicatedVramBytes || hw.gpuName.isEmpty()) {
                hw.gpuName = QString::fromWCharArray(desc.Description).trimmed();
                hw.dedicatedVramBytes = desc.DedicatedVideoMemory;
            }
        }
        factory->Release();
    }
#else
    hw.totalRamBytes = meminfoKb("MemTotal") * 1024ULL;
    hw.availableRamBytes = meminfoKb("MemAvailable") * 1024ULL;
    const QDir drm(QStringLiteral("/sys/class/drm"));
    for (const QString& card : drm.entryList({QStringLiteral("card?")}, QDir::Dirs | QDir::System)) {
        const QString dev = drm.filePath(card + QStringLiteral("/device"));
        const QString vram = readTrimmed(dev + QStringLiteral("/mem_info_vram_total"));
        const QString product = readTrimmed(dev + QStringLiteral("/product_name"));
        const QString vendor = readTrimmed(dev + QStringLiteral("/vendor"));
        const QString device = readTrimmed(dev + QStringLiteral("/device"));
        if (vendor.isEmpty())
            continue;
        if (!product.isEmpty())
            hw.gpuName = product;
        else
            hw.gpuName = QStringLiteral("GPU %1:%2").arg(vendor.mid(2), device.mid(2));
        hw.dedicatedVramBytes = vram.toULongLong();
        break;
    }
#endif
    return hw;
}
