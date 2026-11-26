#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>

class QApplication;

// Design tokens of the Paper file "XSEmu Launcher · Direction C, Cover grid" (--color-* light,
// --color-dk-* dark, type scale, radii, spacing, control heights), the global style sheet built from
// them, and the light/dark/system switch.
//
// Widgets never hard-code colours: they ask for a role and repaint on Theme::changed().
//   QColor c = Theme::color(Theme::Role::Text2);
//   connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));

namespace Metrics {
// control heights
constexpr int PageAction = 36;  // page-level buttons
constexpr int Row = 32;         // buttons in rows and bars, search box
constexpr int GameBar = 40;     // in-game bar
constexpr int TitleBar = 32;
constexpr int NavItem = 34;
constexpr int Sidebar = 224;    // --container-sidebar
constexpr int PageHeader = 64;
constexpr int BadgeHeight = 22;
// radii (--radius-*)
constexpr int RadiusSm = 4;
constexpr int RadiusMd = 6;
constexpr int RadiusLg = 8;
constexpr int RadiusXl = 12;
// spacing (--spacing-*)
constexpr int S1 = 4;
constexpr int S2 = 8;
constexpr int S3 = 12;
constexpr int S4 = 16;
constexpr int S6 = 24;
constexpr int S8 = 32;
constexpr int PagePadding = 28; // page header and content inset
// type scale (--text-*), px
constexpr int TextXs = 12;
constexpr int TextSm = 13;
constexpr int TextBase = 14;
constexpr int TextLg = 16;
constexpr int TextXl = 20;
constexpr int Text2xl = 28;
constexpr int TextDisplay = 36;
constexpr double DisabledOpacity = 0.45; // --opacity-disabled
constexpr int WindowWidth = 1280;        // --breakpoint-window
}

struct Palette {
    QColor bg, surface, sunken, hover;
    QColor text, text2, text3;
    QColor border, borderStrong;
    QColor accent, accentSoft, accentText, onAccent;
    QColor success, successSoft;
    QColor warning, warningSoft;
    QColor danger, dangerSoft;
    QColor scrim, panel;
    QColor hud, hudText, hudText2;

    static Palette light();
    static Palette dark();
};

class Theme : public QObject {
    Q_OBJECT
public:
    enum class Mode { System, Light, Dark };
    enum class Role {
        Bg, Surface, Sunken, Hover, Text, Text2, Text3, Border, BorderStrong,
        Accent, AccentSoft, AccentText, OnAccent, Success, SuccessSoft, Warning, WarningSoft,
        Danger, DangerSoft, Scrim, Panel, Hud, HudText, HudText2,
    };
    // Semantic tones shared by badges, notes, buttons and status dots.
    enum class Tone { Neutral, Accent, Success, Warning, Danger };

    static Theme& instance();

    // Install fonts, palette and style sheet on the application; call once after QApplication exists.
    void install(QApplication& app);

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    bool isDark() const { return m_dark; }
    const Palette& palette() const { return m_palette; }

    static QColor color(Role role);
    static QColor toneColor(Tone tone);     // the strong colour (dot, text)
    static QColor toneSoft(Tone tone);      // the soft background
    static QColor withAlpha(QColor c, double alpha);

    // Inter if installed, else Segoe UI Variable / Segoe UI, else the system font.
    static QFont font(int px, int weight = 400, double letterSpacingEm = 0.0);
    // Geist Mono if installed, else Cascadia Mono / Consolas / the system fixed font.
    static QFont mono(int px, int weight = 400);
    static QString sansFamily();
    static QString monoFamily();

    QString styleSheet() const;

signals:
    void changed();

private:
    Theme();
    void refresh();
    bool systemIsDark() const;

    Mode m_mode = Mode::System;
    bool m_dark = false;
    Palette m_palette;
    QApplication* m_app = nullptr;
};
