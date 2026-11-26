#pragma once

#include "theme/Theme.h"

#include <QFrame>

// A surface with the design's ring border (Components: progress block, paused banner, settings
// cards). Panel uses --color-panel and no shadow. Put children in layout().
class Card : public QFrame {
    Q_OBJECT
public:
    enum class Kind { Surface, Panel, Sunken };
    explicit Card(Kind kind = Kind::Surface, int radius = Metrics::RadiusXl, QWidget* parent = nullptr);
    void setSelected(bool selected); // accent ring (radio cards)
    void setShadow(bool on) { m_shadow = on; update(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Kind m_kind;
    int m_radius;
    bool m_selected = false;
    bool m_shadow = true;
};
