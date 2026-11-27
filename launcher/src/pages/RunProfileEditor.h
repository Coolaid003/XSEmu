#pragma once

#include <QStringList>
#include <QTimer>
#include <QWidget>

class Label;
class QPlainTextEdit;

// A run profile as text (Settings, Run profile and a game's details): KEY=VALUE lines in a monospaced
// box, checked as they are typed (LaunchConfig::parseProfile). A valid edit is reported through
// edited() after a short pause; an invalid one shows its errors and is not reported. Lines naming a
// variable Settings owns are kept and flagged as not used.
class RunProfileEditor : public QWidget {
    Q_OBJECT
public:
    explicit RunProfileEditor(int visibleLines, QWidget* parent = nullptr);
    // Shows these lines without emitting edited(); keeps the text as typed when it already says this.
    void setLines(const QStringList& lines);

signals:
    void edited(const QStringList& lines);

private:
    void check();

    QPlainTextEdit* m_text;
    Label* m_status;
    QTimer m_pause;
    QStringList m_lines; // what the text says when it is valid
};
