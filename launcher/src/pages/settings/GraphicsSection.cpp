#include "Sections.h"
#include "SettingsKit.h"

#include "app/Services.h"
#include "core/Format.h"
#include "core/HardwareInfo.h"
#include "core/LaunchConfig.h"
#include "widgets/SegmentedControl.h"
#include "widgets/Text.h"
#include "widgets/Toggle.h"

namespace {

// A drop-down bound to a launcher default for an xcl.toml key ("Default" leaves the title's value).
QComboBox* xclDropdown(Services& s, const QString& key, const QList<QPair<QString, QVariant>>& items, QWidget* owner, int width = 220)
{
    const QVariant stored = s.settings.xclDefault(key);
    auto* box = SettingsKit::dropdown(items, stored.isValid() ? stored : items.first().second, width);
    QObject::connect(box, &QComboBox::activated, owner, [&s, key, box] { s.settings.setXclDefault(key, box->currentData()); });
    return box;
}

QString envOverride(const char* name)
{
    const QString v = qEnvironmentVariable(name).trimmed();
    return v.isEmpty() ? QString() : QStringLiteral("%1=%2 in the environment overrides this.").arg(QLatin1String(name), v);
}

}

// Theme and the in-game bar are launcher settings. display_mode, raytracing, present and
// d3d12_adapter are launcher defaults written into each title's xcl.toml at launch (LaunchConfig).
QWidget* SettingsSections::graphics(Services& s)
{
    const SettingsKit::Section sec = SettingsKit::makeSection(
        QStringLiteral("Graphics"), QStringLiteral("How the launcher looks and how the game image reaches the screen. XSEmu renders with Direct3D 12."));
    sec.caps(QStringLiteral("Launcher"));
    auto* theme = new SegmentedControl({QStringLiteral("System"), QStringLiteral("Light"), QStringLiteral("Dark")});
    theme->setCurrentIndex(int(s.settings.theme()));
    QObject::connect(theme, &SegmentedControl::currentChanged, sec.page, [&s](int i) { s.settings.setTheme(AppSettings::ThemeMode(i)); });
    sec.row(QStringLiteral("Theme"), QStringLiteral("System follows the Windows app mode."), theme);

    sec.caps(QStringLiteral("Window"));
    auto* start = new SegmentedControl({QStringLiteral("Window"), QStringLiteral("Fullscreen")});
    sec.row(QStringLiteral("Start games in"), {}, start)->setUnavailable(QStringLiteral("Games start in a window; F11 switches. Choosing is not in this launcher build yet."));
    auto* bar = SettingsKit::dropdown({{QStringLiteral("Always shown"), false}, {QStringLiteral("Auto-hide"), true}}, s.settings.windowGameBarAutoHide(), 160);
    QObject::connect(bar, &QComboBox::activated, sec.page, [&s, bar] { s.settings.setWindowGameBarAutoHide(bar->currentData().toBool()); });
    sec.row(QStringLiteral("Game bar in a window"), QStringLiteral("The bar is also the window's title bar, so it stays by default."), bar);

    sec.caps(QStringLiteral("Image"));
    auto* scaling = new SegmentedControl({QStringLiteral("Letterbox"), QStringLiteral("Stretch")});
    sec.row(QStringLiteral("Scaling"), QStringLiteral("Keep the 16:9 image and fill the rest with black, or stretch it to the window."), scaling)
        ->setUnavailable(QStringLiteral("XSEmu always keeps 16:9 with black bars. Stretching is not in this XSEmu build yet."));
    auto* mode = xclDropdown(s, QStringLiteral("display_mode"),
                             {{QStringLiteral("Auto"), QStringLiteral("auto")},
                              {QStringLiteral("This display"), QStringLiteral("host")},
                              {QStringLiteral("1920×1080 · 60 Hz"), QStringLiteral("1920x1080@60")},
                              {QStringLiteral("2560×1440 · 60 Hz"), QStringLiteral("2560x1440@60")},
                              {QStringLiteral("3840×2160 · 60 Hz"), QStringLiteral("3840x2160@60")},
                              {QStringLiteral("1920×1080 · 120 Hz"), QStringLiteral("1920x1080@120")},
                              {QStringLiteral("2560×1440 · 120 Hz"), QStringLiteral("2560x1440@120")}},
                             sec.page);
    SettingsKit::Row* modeRow = sec.row(QStringLiteral("Display mode"),
                                        QStringLiteral("The output mode the game is told about. Auto: 1280×720 at 60 Hz on Series S, this display on Series X."), mode);
    if (const QString env = envOverride("XCL_DISPLAY_MODE"); !env.isEmpty())
        modeRow->setUnavailable(env);

    sec.caps(QStringLiteral("Presentation"));
    auto* lowLatency = new Toggle;
    sec.row(QStringLiteral("Low-latency present"), QStringLiteral("One frame in flight. Lowers input lag, costs a little frame rate."), lowLatency)->setUnavailable();
    auto* present = new Toggle;
    const QVariant presentValue = s.settings.xclDefault(QStringLiteral("present"));
    present->setChecked(!presentValue.isValid() || presentValue.toString() != QLatin1String("off"));
    QObject::connect(present, &Toggle::toggled, sec.page, [&s](bool on) { s.settings.setXclDefault(QStringLiteral("present"), on ? QStringLiteral("auto") : QStringLiteral("off")); });
    sec.row(QStringLiteral("Show the game image"), QStringLiteral("Off runs the game without showing its frames, for testing."), present);

    sec.caps(QStringLiteral("GPU"));
    const HardwareInfo hw = HardwareInfo::read();
    const QString vram = hw.dedicatedVramBytes > 0 ? QStringLiteral(" · %1 VRAM").arg(Format::gigabytes(qint64(hw.dedicatedVramBytes), 0)) : QString();
    auto* gpu = SettingsKit::status(hw.gpuNameOrUnknown() + vram, Theme::Role::Text);
    gpu->setWordWrap(true);
    sec.row(QStringLiteral("Graphics card"), QStringLiteral("What Windows reports for this PC. RAM: %1.").arg(Format::gigabytes(qint64(hw.totalRamBytes), 0)), gpu);
    QList<QPair<QString, QVariant>> adapters{{QStringLiteral("Auto (high-performance GPU)"), QStringLiteral("auto")}};
    if (!hw.gpuName.isEmpty())
        adapters.append({hw.gpuName, hw.gpuName});
    adapters.append({QStringLiteral("WARP (software, very slow)"), QStringLiteral("warp")});
    const QVariant storedAdapter = s.settings.xclDefault(QStringLiteral("d3d12_adapter"));
    if (storedAdapter.isValid() && std::none_of(adapters.cbegin(), adapters.cend(), [&](const auto& a) { return a.second == storedAdapter; }))
        adapters.append({storedAdapter.toString(), storedAdapter});
    auto* adapter = xclDropdown(s, QStringLiteral("d3d12_adapter"), adapters, sec.page, 260);
    SettingsKit::Row* adapterRow = sec.row(QStringLiteral("D3D12 adapter"), QStringLiteral("The GPU XSEmu renders on."), adapter);
    if (const QString env = envOverride("XCL_ADAPTER"); !env.isEmpty())
        adapterRow->setUnavailable(env);
    auto* rt = xclDropdown(s, QStringLiteral("raytracing"),
                           {{QStringLiteral("Auto (this GPU)"), QStringLiteral("auto")},
                            {QStringLiteral("Tier 1.1"), QStringLiteral("tier1_1")},
                            {QStringLiteral("Tier 1.0"), QStringLiteral("tier1_0")},
                            {QStringLiteral("Off"), QStringLiteral("off")}},
                           sec.page, 180);
    SettingsKit::Row* rtRow = sec.row(QStringLiteral("Raytracing"), QStringLiteral("The raytracing tier the game is told the GPU has."), rt);
    if (const QString env = envOverride("XCL_RAYTRACING"); !env.isEmpty())
        rtRow->setUnavailable(env);

    QObject::connect(&s.settings, &AppSettings::changed, sec.page, [&s, theme, bar](const QString& key) {
        if (key == QLatin1String(AppSettings::Key::Theme))
            theme->setCurrentIndex(int(s.settings.theme()));
        else if (key == QLatin1String(AppSettings::Key::WindowGameBarAutoHide))
            bar->setCurrentIndex(s.settings.windowGameBarAutoHide() ? 1 : 0);
    });
    sec.finish();
    return sec.page;
}
