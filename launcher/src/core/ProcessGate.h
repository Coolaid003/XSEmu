#pragma once

#include <QObject>
#include <QString>

#include <functional>

// The one-process rule: at most one XSEmu process tree (a game or a warm-up) at a time. Both
// controllers acquire the gate before starting XSEmu.exe and release it when the tree is gone; every
// other UI asks it why an action is blocked and how to clear the block.
//
//   if (!gate.isFree()) {
//       note->setText(gate.blockedReason(ProcessGate::Action::StartWarmup));
//       stopButton->setText(gate.stopActionLabel());   // "Stop game" / "Cancel warm-up"
//       connect(stopButton, &QPushButton::clicked, &gate, &ProcessGate::stopOwner);
//   }
class ProcessGate : public QObject {
    Q_OBJECT
public:
    enum class Owner { None, Game, Warmup };
    enum class Action { LaunchGame, StartWarmup };

    explicit ProcessGate(QObject* parent = nullptr);

    Owner owner() const { return m_owner; }
    bool isFree() const { return m_owner == Owner::None; }
    QString ownerTitle() const { return m_title; } // the game playing or being warmed up for
    bool ownerPaused() const { return m_paused; }

    // Controllers only.
    bool acquire(Owner owner, const QString& title, std::function<void()> stop);
    void release(Owner owner);
    void setPaused(bool paused);

    // A human sentence for why the action is blocked now, or empty when it is allowed.
    QString blockedReason(Action action) const;
    // The label of the action that clears the block ("Stop game", "Cancel warm-up"), or empty.
    QString stopActionLabel() const;

public slots:
    // Ask the owner to stop (game: stop with grace then kill; warm-up: cancel).
    void stopOwner();

signals:
    void changed();

private:
    Owner m_owner = Owner::None;
    QString m_title;
    bool m_paused = false;
    std::function<void()> m_stop;
};
