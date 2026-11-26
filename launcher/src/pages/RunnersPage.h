#pragma once

#include <QHash>
#include <QWidget>

class Label;
class QVBoxLayout;
class RunnerCard;
class Services;

// Runners page: every curated runner as a card with its badges, pin and licence, install state and
// the action that fits it (Install, Update to the curated pin, Uninstall, Cancel); an install shows
// its steps inside its card. The header names the detected GPU the recommendation is based on.
class RunnersPage : public QWidget {
    Q_OBJECT
public:
    explicit RunnersPage(Services& services, QWidget* parent = nullptr);

private:
    void reorder();

    Services& m_s;
    QVBoxLayout* m_list;
    QHash<QString, RunnerCard*> m_cards;
    QString m_busy;
};
