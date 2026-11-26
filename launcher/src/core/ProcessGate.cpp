#include "ProcessGate.h"

ProcessGate::ProcessGate(QObject* parent)
    : QObject(parent)
{
}

bool ProcessGate::acquire(Owner owner, const QString& title, std::function<void()> stop)
{
    if (m_owner != Owner::None || owner == Owner::None)
        return false;
    m_owner = owner;
    m_title = title;
    m_paused = false;
    m_stop = std::move(stop);
    emit changed();
    return true;
}

void ProcessGate::release(Owner owner)
{
    if (m_owner != owner)
        return;
    m_owner = Owner::None;
    m_title.clear();
    m_paused = false;
    m_stop = nullptr;
    emit changed();
}

void ProcessGate::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    emit changed();
}

QString ProcessGate::blockedReason(Action action) const
{
    const QString what = m_title.isEmpty() ? QStringLiteral("A game") : m_title;
    switch (m_owner) {
    case Owner::None:
        return {};
    case Owner::Game: {
        const QString state = m_paused ? QStringLiteral("is paused") : QStringLiteral("is running");
        if (action == Action::StartWarmup)
            return QStringLiteral("%1 %2. Stop it to warm up shaders, because only one XSEmu process runs at a time.").arg(what, state);
        return QStringLiteral("%1 %2. Stop it to play another game, because only one XSEmu process runs at a time.").arg(what, state);
    }
    case Owner::Warmup:
        if (action == Action::StartWarmup)
            return QStringLiteral("A warm-up is already running.");
        return QStringLiteral("A shader warm-up is running. Cancel it or wait for it to finish, because only one XSEmu process runs at a time.");
    }
    return {};
}

QString ProcessGate::stopActionLabel() const
{
    switch (m_owner) {
    case Owner::Game: return QStringLiteral("Stop game");
    case Owner::Warmup: return QStringLiteral("Cancel warm-up");
    case Owner::None: break;
    }
    return {};
}

void ProcessGate::stopOwner()
{
    if (m_stop)
        m_stop();
}
