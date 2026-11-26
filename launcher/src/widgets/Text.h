#pragma once

#include "theme/Theme.h"

#include <QLabel>

// Every piece of text in the launcher is a Label with a style from the design's type scale and a
// colour role; both follow the theme.
//   auto* t = new Label(QStringLiteral("Library"), Label::Style::PageTitle);
//   auto* s = new Label(QStringLiteral("3 games"), Label::Style::Small, Theme::Role::Text3);
class Label : public QLabel {
    Q_OBJECT
public:
    enum class Style {
        Display,       // 28 semibold, -0.02em (Components heading)
        PageTitle,     // 18 semibold, -0.01em (page header)
        SectionTitle,  // 20 semibold, -0.01em (settings section, dialog title)
        HeroTitle,     // 28 semibold, -0.02em, on art
        Title,         // 14 semibold
        Body,          // 14 regular
        BodyMedium,    // 14 medium
        Small,         // 13 regular
        SmallMedium,   // 13 medium
        SmallStrong,   // 13 semibold
        Caption,       // 12 regular
        CaptionMedium, // 12 medium
        Caps,          // 12 semibold, uppercase, 0.06em (section labels)
        CapsMedium,    // 12 medium, uppercase, 0.06em ("NOW PLAYING", "NO COVER ART")
        Mono,          // 11 Geist Mono (key hints)
        Number,        // 13 medium, tabular figures
    };

    explicit Label(const QString& text = {}, Style style = Style::Body, Theme::Role role = Theme::Role::Text, QWidget* parent = nullptr);

    void setStyle(Style style);
    void setRole(Theme::Role role);
    Style style() const { return m_style; }

    static QFont fontFor(Style style);

    void setText(const QString& text);

private:
    void applyTheme();

    Style m_style;
    Theme::Role m_role;
    QString m_raw;
};
