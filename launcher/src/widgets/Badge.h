#pragma once

#include "theme/Theme.h"

#include <QWidget>

// Dot + text. Pill: the soft tone background (Components: Badges and game state). Plain: no
// background and text-2 text (the status line under a cover tile, Now playing).
//   new Badge(QStringLiteral("Shaders ready"), Theme::Tone::Success);
class Badge : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Pill, Plain, Chip }; // Chip: sunken 24 px, medium text-2 (sidebar footer)

    explicit Badge(const QString& text = {}, Theme::Tone tone = Theme::Tone::Neutral, Kind kind = Kind::Pill, QWidget* parent = nullptr);

    void setText(const QString& text);
    void setTone(Theme::Tone tone);
    void setDotVisible(bool on);
    QString text() const { return m_text; }
    Theme::Tone tone() const { return m_tone; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QFont textFont() const;

    QString m_text;
    Theme::Tone m_tone;
    Kind m_kind;
    bool m_dot = true;
};
