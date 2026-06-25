#pragma once

#include <QtGlobal>

class LinuxAdminSession final
{
public:
    enum class State {
        Unavailable,
        Locked,
        Unlocking,
        Active,
        ExpiringSoon,
        Expired,
        Revoking,
        Error
    };

    State state() const;
    bool backendAvailable() const;
    int timeoutMinutes() const;
    void setTimeoutMinutes(int minutes);

    void setBackendAvailable(bool available);
    void beginUnlock();
    void activate(qint64 nowMs);
    void lock();
    void refreshAfterOperation(qint64 nowMs);
    void updateForNow(qint64 nowMs);
    int remainingSeconds(qint64 nowMs) const;

private:
    void setState(State state);
    qint64 timeoutMs() const;

    bool m_backendAvailable = false;
    State m_state = State::Unavailable;
    int m_timeoutMinutes = 10;
    qint64 m_expiresAtMs = 0;
};
