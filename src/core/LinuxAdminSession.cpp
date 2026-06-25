#include "LinuxAdminSession.h"

#include <algorithm>

LinuxAdminSession::State LinuxAdminSession::state() const
{
    return m_state;
}

bool LinuxAdminSession::backendAvailable() const
{
    return m_backendAvailable;
}

int LinuxAdminSession::timeoutMinutes() const
{
    return m_timeoutMinutes;
}

void LinuxAdminSession::setTimeoutMinutes(int minutes)
{
    m_timeoutMinutes = std::clamp(minutes, 1, 120);
}

void LinuxAdminSession::setBackendAvailable(bool available)
{
    if (m_backendAvailable == available) {
        return;
    }
    m_backendAvailable = available;
    m_expiresAtMs = 0;
    setState(available ? State::Locked : State::Unavailable);
}

void LinuxAdminSession::beginUnlock()
{
    if (!m_backendAvailable) {
        setState(State::Unavailable);
        return;
    }
    setState(State::Unlocking);
}

void LinuxAdminSession::activate(qint64 nowMs)
{
    if (!m_backendAvailable) {
        setState(State::Unavailable);
        return;
    }
    m_expiresAtMs = nowMs + timeoutMs();
    setState(State::Active);
}

void LinuxAdminSession::lock()
{
    m_expiresAtMs = 0;
    setState(m_backendAvailable ? State::Locked : State::Unavailable);
}

void LinuxAdminSession::refreshAfterOperation(qint64 nowMs)
{
    if (m_state != State::Active && m_state != State::ExpiringSoon) {
        return;
    }
    m_expiresAtMs = nowMs + timeoutMs();
    setState(State::Active);
}

void LinuxAdminSession::updateForNow(qint64 nowMs)
{
    if (m_state != State::Active && m_state != State::ExpiringSoon) {
        return;
    }

    const int remaining = remainingSeconds(nowMs);
    if (remaining <= 0) {
        m_expiresAtMs = 0;
        setState(State::Expired);
        return;
    }
    setState(remaining <= 60 ? State::ExpiringSoon : State::Active);
}

int LinuxAdminSession::remainingSeconds(qint64 nowMs) const
{
    if (m_expiresAtMs <= 0 || (m_state != State::Active && m_state != State::ExpiringSoon)) {
        return 0;
    }
    const qint64 remainingMs = m_expiresAtMs - nowMs;
    return static_cast<int>(std::max<qint64>(0, (remainingMs + 999) / 1000));
}

void LinuxAdminSession::setState(State state)
{
    m_state = state;
}

qint64 LinuxAdminSession::timeoutMs() const
{
    return static_cast<qint64>(m_timeoutMinutes) * 60 * 1000;
}
