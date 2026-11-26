#pragma once

#include "theme/Theme.h"

#include <QWidget>

class Label;

// A sunken note with a leading icon (first launch: "This PC has 32 GB of RAM ..."). The tone tints
// the icon and, for anything but Neutral, the background.
class InfoNote : public QWidget {
    Q_OBJECT
public:
    explicit InfoNote(const QString& text = {}, Theme::Tone tone = Theme::Tone::Neutral, const QString& icon = QStringLiteral("info"), QWidget* parent = nullptr);
    void setText(const QString& text);
    void setTone(Theme::Tone tone);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Theme::Tone m_tone;
    QString m_icon;
    Label* m_label;
};
