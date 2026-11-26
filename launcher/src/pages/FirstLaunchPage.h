#pragma once

#include <QWidget>

class Button;
class Card;
class ChoiceCard;
class InfoNote;
class Label;
class QStackedWidget;
class QVBoxLayout;
class Services;
class StepIndicator;
class Toggle;

// First launch (First launch pages): three steps (Console, Network, Game folders) in a 640 px column
// with a footer bar. Game folders are written as they are picked, so the library scans them live;
// finishing writes the console default and the network choice and marks the first launch done.
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
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    QWidget* buildConsole();
    QWidget* buildNetwork();
    QWidget* buildFolders();
    void refreshNetwork();
    void refreshFolders();
    void finish();

    Services& m_s;
    int m_step = 0;
    StepIndicator* m_steps;
    QStackedWidget* m_stack;
    Label* m_version;
    Button* m_back;
    Button* m_skip;
    Button* m_next;
    ChoiceCard* m_seriesS = nullptr;
    Card* m_offlineCard = nullptr;
    Toggle* m_offline = nullptr;
    Label* m_signIn = nullptr;
    QWidget* m_dropZone = nullptr;
    Label* m_dropHint = nullptr;
    QWidget* m_found = nullptr;
    QVBoxLayout* m_folderRows = nullptr;
    Card* m_gamesCard = nullptr;
    InfoNote* m_warmNote = nullptr;
};
