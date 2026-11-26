#include "Text.h"

Label::Label(const QString& text, Style style, Theme::Role role, QWidget* parent)
    : QLabel(parent)
    , m_style(style)
    , m_role(role)
{
    setTextFormat(Qt::PlainText);
    setText(text);
    applyTheme();
    connect(&Theme::instance(), &Theme::changed, this, &Label::applyTheme);
}

QFont Label::fontFor(Style style)
{
    switch (style) {
    case Style::Display: return Theme::font(28, 600, -0.02);
    case Style::PageTitle: return Theme::font(18, 600, -0.01);
    case Style::SectionTitle: return Theme::font(20, 600, -0.01);
    case Style::HeroTitle: return Theme::font(28, 600, -0.02);
    case Style::Title: return Theme::font(14, 600);
    case Style::Body: return Theme::font(14, 400);
    case Style::BodyMedium: return Theme::font(14, 500);
    case Style::Small: return Theme::font(13, 400);
    case Style::SmallMedium: return Theme::font(13, 500);
    case Style::SmallStrong: return Theme::font(13, 600);
    case Style::Caption: return Theme::font(12, 400);
    case Style::CaptionMedium: return Theme::font(12, 500);
    case Style::Caps: return Theme::font(12, 600, 0.06);
    case Style::CapsMedium: return Theme::font(12, 500, 0.06);
    case Style::Mono: return Theme::mono(11);
    case Style::Number: {
        QFont f = Theme::font(13, 500);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        f.setFeature("tnum", 1);
#endif
        return f;
    }
    }
    return Theme::font(14);
}

void Label::setStyle(Style style)
{
    m_style = style;
    setText(m_raw);
    applyTheme();
}

void Label::setRole(Theme::Role role)
{
    m_role = role;
    applyTheme();
}

void Label::setText(const QString& text)
{
    m_raw = text;
    const bool caps = m_style == Style::Caps || m_style == Style::CapsMedium;
    QLabel::setText(caps ? text.toUpper() : text);
}

void Label::applyTheme()
{
    setFont(fontFor(m_style));
    // Colour through the widget's style sheet, not its palette: when the theme switches, the
    // application style sheet repolishes widgets and restores palettes captured at their first
    // polish, which would bring back the old theme's text colour.
    const QColor c = Theme::color(m_role);
    setStyleSheet(QStringLiteral("QLabel { color: rgba(%1, %2, %3, %4); }").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()));
}
