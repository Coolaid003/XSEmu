#pragma once

#include <QWidget>

class Button;
class ChoiceCard;
class Label;
class QStackedWidget;
class QVBoxLayout;
class Services;
class StepIndicator;

// First launch (First launch pages): three steps (Console, Network, Game folders) in a 640 px column
// with a footer bar. Finishing writes the choices and marks the first launch done.
class FirstLaunchPage : public QWidget {
    Q_OBJECT
public:
    explicit FirstLaunchPage(Services& services, QWidget* parent = nullptr);
    void setStep(int step);

signals:
    void addGamesRequested();
    void finished();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QWidget* buildConsole();
    QWidget* buildNetwork();
    QWidget* buildFolders();
    void refreshFolders();

    Services& m_s;
    int m_step = 0;
    StepIndicator* m_steps;
    QStackedWidget* m_stack;
    Button* m_back;
    Button* m_next;
    ChoiceCard* m_seriesS = nullptr;
    ChoiceCard* m_offline = nullptr;
    QVBoxLayout* m_folders = nullptr;
    Label* m_found = nullptr;
};
