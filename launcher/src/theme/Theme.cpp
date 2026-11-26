#include "Theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleHints>

namespace {

QColor rgba(int r, int g, int b, double a)
{
    QColor c(r, g, b);
    c.setAlphaF(static_cast<float>(a));
    return c;
}

QString css(const QColor& c)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alphaF(), 0, 'f', 3);
}

QString firstInstalled(const QStringList& wanted)
{
    const QStringList families = QFontDatabase::families();
    for (const QString& w : wanted) {
        for (const QString& f : families) {
            if (f.compare(w, Qt::CaseInsensitive) == 0)
                return f;
        }
    }
    return {};
}

}

Palette Palette::light()
{
    Palette p;
    p.bg = QColor(0xF7, 0xF7, 0xF8);
    p.surface = QColor(0xFF, 0xFF, 0xFF);
    p.sunken = QColor(0xEE, 0xEE, 0xF0);
    p.hover = QColor(0xF1, 0xF1, 0xF3);
    p.text = QColor(0x18, 0x18, 0x1B);
    p.text2 = QColor(0x5C, 0x5C, 0x63);
    p.text3 = QColor(0x8A, 0x8A, 0x91);
    p.border = rgba(0, 0, 0, 0.08);
    p.borderStrong = rgba(0, 0, 0, 0.14);
    p.accent = QColor(0x2F, 0x54, 0xEB);
    p.accentSoft = rgba(47, 84, 235, 0.10);
    p.accentText = QColor(0x2A, 0x4A, 0xD6);
    p.onAccent = QColor(0xFF, 0xFF, 0xFF);
    p.success = QColor(0x1E, 0x8E, 0x3E);
    p.successSoft = rgba(30, 142, 62, 0.10);
    p.warning = QColor(0x94, 0x68, 0x00);
    p.warningSoft = rgba(214, 160, 20, 0.14);
    p.danger = QColor(0xC7, 0x33, 0x2F);
    p.dangerSoft = rgba(199, 51, 47, 0.09);
    p.scrim = rgba(15, 15, 18, 0.42);
    p.panel = QColor(0xFB, 0xFB, 0xFC);
    p.hud = rgba(18, 18, 20, 0.84);
    p.hudText = QColor(0xF2, 0xF2, 0xF3);
    p.hudText2 = QColor(0xA8, 0xA8, 0xAE);
    return p;
}

Palette Palette::dark()
{
    Palette p = light(); // the HUD tokens have no dark variant
    p.bg = QColor(0x11, 0x11, 0x13);
    p.surface = QColor(0x1A, 0x1A, 0x1D);
    p.sunken = QColor(0x0B, 0x0B, 0x0C);
    p.hover = QColor(0x23, 0x23, 0x26);
    p.text = QColor(0xED, 0xED, 0xEF);
    p.text2 = QColor(0xA1, 0xA1, 0xA8);
    p.text3 = QColor(0x6E, 0x6E, 0x75);
    p.border = rgba(255, 255, 255, 0.08);
    p.borderStrong = rgba(255, 255, 255, 0.15);
    p.accent = QColor(0x52, 0x77, 0xFF);
    p.accentSoft = rgba(82, 119, 255, 0.16);
    p.accentText = QColor(0x8E, 0xA6, 0xFF);
    p.onAccent = QColor(0xFF, 0xFF, 0xFF);
    p.success = QColor(0x3F, 0xB9, 0x50);
    p.successSoft = rgba(63, 185, 80, 0.14);
    p.warning = QColor(0xE3, 0xB3, 0x41);
    p.warningSoft = rgba(227, 179, 65, 0.14);
    p.danger = QColor(0xF0, 0x60, 0x5A);
    p.dangerSoft = rgba(240, 96, 90, 0.14);
    p.scrim = rgba(0, 0, 0, 0.60);
    p.panel = QColor(0x15, 0x15, 0x17);
    return p;
}

Theme& Theme::instance()
{
    static Theme* t = new Theme;
    return *t;
}

Theme::Theme()
{
    m_palette = Palette::light();
}

void Theme::install(QApplication& app)
{
    m_app = &app;
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFont base = font(Metrics::TextBase);
    app.setFont(base);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(app.styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_mode == Mode::System)
            refresh();
    });
#endif
    refresh();
}

void Theme::setMode(Mode mode)
{
    m_mode = mode;
    refresh();
}

bool Theme::systemIsDark() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
#endif
#ifdef Q_OS_WIN
    QSettings personalize(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"), QSettings::NativeFormat);
    if (personalize.contains(QStringLiteral("AppsUseLightTheme")))
        return personalize.value(QStringLiteral("AppsUseLightTheme")).toInt() == 0;
#endif
    return false;
}

void Theme::refresh()
{
    m_dark = m_mode == Mode::Dark || (m_mode == Mode::System && systemIsDark());
    m_palette = m_dark ? Palette::dark() : Palette::light();
    if (m_app) {
        QPalette pal;
        pal.setColor(QPalette::Window, m_palette.bg);
        pal.setColor(QPalette::WindowText, m_palette.text);
        pal.setColor(QPalette::Base, m_palette.surface);
        pal.setColor(QPalette::AlternateBase, m_palette.hover);
        pal.setColor(QPalette::Text, m_palette.text);
        pal.setColor(QPalette::PlaceholderText, m_palette.text3);
        pal.setColor(QPalette::Button, m_palette.surface);
        pal.setColor(QPalette::ButtonText, m_palette.text);
        pal.setColor(QPalette::Highlight, m_palette.accent);
        pal.setColor(QPalette::HighlightedText, m_palette.onAccent);
        pal.setColor(QPalette::ToolTipBase, m_palette.surface);
        pal.setColor(QPalette::ToolTipText, m_palette.text);
        pal.setColor(QPalette::Link, m_palette.accentText);
        pal.setColor(QPalette::Mid, m_palette.borderStrong);
        pal.setColor(QPalette::Disabled, QPalette::Text, m_palette.text3);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, m_palette.text3);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, m_palette.text3);
        m_app->setPalette(pal);
        m_app->setStyleSheet(styleSheet());
    }
    emit changed();
}

QColor Theme::color(Role role)
{
    const Palette& p = instance().m_palette;
    switch (role) {
    case Role::Bg: return p.bg;
    case Role::Surface: return p.surface;
    case Role::Sunken: return p.sunken;
    case Role::Hover: return p.hover;
    case Role::Text: return p.text;
    case Role::Text2: return p.text2;
    case Role::Text3: return p.text3;
    case Role::Border: return p.border;
    case Role::BorderStrong: return p.borderStrong;
    case Role::Accent: return p.accent;
    case Role::AccentSoft: return p.accentSoft;
    case Role::AccentText: return p.accentText;
    case Role::OnAccent: return p.onAccent;
    case Role::Success: return p.success;
    case Role::SuccessSoft: return p.successSoft;
    case Role::Warning: return p.warning;
    case Role::WarningSoft: return p.warningSoft;
    case Role::Danger: return p.danger;
    case Role::DangerSoft: return p.dangerSoft;
    case Role::Scrim: return p.scrim;
    case Role::Panel: return p.panel;
    case Role::Hud: return p.hud;
    case Role::HudText: return p.hudText;
    case Role::HudText2: return p.hudText2;
    }
    return p.text;
}

QColor Theme::toneColor(Tone tone)
{
    switch (tone) {
    case Tone::Neutral: return color(Role::Text3);
    case Tone::Accent: return color(Role::AccentText);
    case Tone::Success: return color(Role::Success);
    case Tone::Warning: return color(Role::Warning);
    case Tone::Danger: return color(Role::Danger);
    }
    return color(Role::Text3);
}

QColor Theme::toneSoft(Tone tone)
{
    switch (tone) {
    case Tone::Neutral: return color(Role::Sunken);
    case Tone::Accent: return color(Role::AccentSoft);
    case Tone::Success: return color(Role::SuccessSoft);
    case Tone::Warning: return color(Role::WarningSoft);
    case Tone::Danger: return color(Role::DangerSoft);
    }
    return color(Role::Sunken);
}

QColor Theme::withAlpha(QColor c, double alpha)
{
    c.setAlphaF(static_cast<float>(c.alphaF() * alpha));
    return c;
}

QString Theme::sansFamily()
{
    static const QString family = [] {
        const QString f = firstInstalled({QStringLiteral("Inter"), QStringLiteral("Inter Variable"), QStringLiteral("Segoe UI Variable Text"),
                                          QStringLiteral("Segoe UI Variable"), QStringLiteral("Segoe UI")});
        return f.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::GeneralFont).family() : f;
    }();
    return family;
}

QString Theme::monoFamily()
{
    static const QString family = [] {
        const QString f = firstInstalled({QStringLiteral("Geist Mono"), QStringLiteral("GeistMono Nerd Font Mono"), QStringLiteral("GeistMono Nerd Font"),
                                          QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono")});
        return f.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::FixedFont).family() : f;
    }();
    return family;
}

QFont Theme::font(int px, int weight, double letterSpacingEm)
{
    QFont f(sansFamily());
    f.setPixelSize(px);
    f.setWeight(static_cast<QFont::Weight>(weight));
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setStyleStrategy(QFont::PreferAntialias);
    if (letterSpacingEm != 0.0)
        f.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacingEm * px);
    return f;
}

QFont Theme::mono(int px, int weight)
{
    QFont f(monoFamily());
    f.setStyleHint(QFont::Monospace);
    f.setPixelSize(px);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

QString Theme::styleSheet() const
{
    const Palette& p = m_palette;
    QString s;
    s += QStringLiteral("QToolTip { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 4px 8px; }\n")
             .arg(css(p.surface), css(p.text), css(p.borderStrong));
    s += QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget#scrollContents { background: transparent; border: none; }\n");
    s += QStringLiteral("QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }\n"
                        "QScrollBar::handle:vertical { background: %1; border-radius: 3px; min-height: 32px; }\n"
                        "QScrollBar::handle:vertical:hover { background: %2; }\n"
                        "QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }\n"
                        "QScrollBar::handle:horizontal { background: %1; border-radius: 3px; min-width: 32px; }\n"
                        "QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; border: none; width: 0; height: 0; }\n")
             .arg(css(p.borderStrong), css(p.text3));
    s += QStringLiteral("QLineEdit { background: %1; color: %2; border: none; border-radius: 7px; padding: 0 10px; min-height: 32px; selection-background-color: %3; }\n"
                        "QLineEdit[framed=\"true\"] { border: 1px solid %4; }\n"
                        "QLineEdit[framed=\"true\"]:focus { border: 1px solid %5; }\n")
             .arg(css(p.surface), css(p.text), css(p.accentSoft), css(p.borderStrong), css(p.accent));
    s += QStringLiteral("QMenu { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 4px; }\n"
                        "QMenu::item { padding: 6px 12px; border-radius: 5px; }\n"
                        "QMenu::item:selected { background: %4; }\n")
             .arg(css(p.surface), css(p.text), css(p.borderStrong), css(p.hover));
    s += QStringLiteral("QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: 7px; padding: 0 10px; min-height: 30px; }\n"
                        "QComboBox QAbstractItemView { background: %1; color: %2; border: 1px solid %3; selection-background-color: %4; selection-color: %2; outline: none; }\n")
             .arg(css(p.surface), css(p.text), css(p.borderStrong), css(p.hover));
    s += QStringLiteral("QTableView, QListView, QTreeView { background: transparent; color: %1; border: none; gridline-color: %2; selection-background-color: %3; selection-color: %1; }\n"
                        "QHeaderView::section { background: transparent; color: %4; border: none; border-bottom: 1px solid %2; padding: 6px 0; }\n")
             .arg(css(p.text), css(p.border), css(p.hover), css(p.text3));
    s += QStringLiteral("QPlainTextEdit, QTextEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; selection-background-color: %4; }\n")
             .arg(css(p.surface), css(p.text), css(p.border), css(p.accentSoft));
    return s;
}
