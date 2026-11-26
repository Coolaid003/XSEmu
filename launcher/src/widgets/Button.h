#pragma once

#include "theme/Theme.h"

#include <QAbstractButton>

// The design's buttons (Components: Buttons). 36 px for page actions, 32 px in rows and bars,
// 26 px for inline ghost links. A checkable Ghost button shows the sunken fill while checked (the
// overlay toggle in the game bar).
//   auto* play = new Button(QStringLiteral("Play"), Button::Variant::Primary, Button::Size::Large, QStringLiteral("play"));
class Button : public QAbstractButton {
    Q_OBJECT
public:
    enum class Variant {
        Primary,         // accent fill, white semibold text
        Secondary,       // surface with a strong border
        Danger,          // danger fill
        DangerSecondary, // surface with a strong border, danger text and icon ("Stop game" in banners)
        Ghost,           // no fill, text-2; hover fill
        Link,            // accent text, no fill
        OnArt,           // translucent dark fill for buttons over cover/hero art ("Details")
        DangerGhost,     // no fill, danger text and icon ("Stop" in the game bar)
        Soft,            // accent-soft fill, accent text ("Resume" in the game bar while paused)
    };
    enum class Size { Large = 36, Small = 32, Tiny = 26 };

    explicit Button(const QString& text = {}, Variant variant = Variant::Secondary, Size size = Size::Large,
                    const QString& icon = {}, QWidget* parent = nullptr);

    void setVariant(Variant v);
    void setButtonSize(Size s);
    void setIconName(const QString& icon);
    Variant variant() const { return m_variant; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void changeEvent(QEvent*) override;
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;

private:
    QFont textFont() const;
    int iconSize() const { return 14; }
    int padLeft() const;
    int padRight() const;

    Variant m_variant;
    Size m_size;
    QString m_icon;
    bool m_hover = false;
    bool m_focusRing = false; // keyboard focus only; a click or the window's first focus shows no ring
};
