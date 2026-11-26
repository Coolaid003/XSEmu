#pragma once

#include "widgets/Card.h"

class Label;
class QHBoxLayout;
class QVBoxLayout;

// The card the share dialog and the import preview list settings on: a CAPS title with a caption on
// the right, then label/value rows over hairlines, in the style of the Game details cards. A row can
// show the current value before the new one ("Series S → Series X"), mark itself not applied with the
// reason under it, and carry an action on the right ("Install Kelvaroq").
class ProfileCard : public Card {
    Q_OBJECT
public:
    explicit ProfileCard(const QString& title, QWidget* parent = nullptr);
    void setTrailing(const QString& text);
    void clear();
    void addRow(const QString& label, const QString& value, const QString& from = {}, const QString& notApplied = {}, QWidget* action = nullptr,
                const QString& caption = {});
    void addMessage(const QString& text);
    int rowCount() const { return m_count; }

private:
    QVBoxLayout* m_rows;
    Label* m_trailing;
    int m_count = 0;
};
