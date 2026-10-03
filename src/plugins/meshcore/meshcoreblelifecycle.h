#ifndef MESHCOREBLELIFECYCLE_H
#define MESHCOREBLELIFECYCLE_H

class MeshCoreBleLifecycle
{
public:
    enum class State
    {
        Idle,
        Opening,
        Connected,
        Closing
    };

    enum class OpenDisposition
    {
        Start,
        AlreadyActive,
        Deferred
    };

    State state() const
    {
        return m_state;
    }

    OpenDisposition requestOpen()
    {
        if (m_state == State::Closing) {
            m_pendingOpen = true;
            return OpenDisposition::Deferred;
        }
        if (m_state != State::Idle)
            return OpenDisposition::AlreadyActive;
        m_state = State::Opening;
        return OpenDisposition::Start;
    }

    void markConnected()
    {
        if (m_state == State::Opening)
            m_state = State::Connected;
    }

    void beginClose()
    {
        if (m_state != State::Idle)
            m_state = State::Closing;
    }

    bool controllerDestroyed()
    {
        if (m_state != State::Closing)
            return false;
        const bool reopen = m_pendingOpen;
        m_pendingOpen = false;
        m_state = State::Idle;
        return reopen;
    }

    void reset()
    {
        m_state = State::Idle;
        m_pendingOpen = false;
    }

    bool hasPendingOpen() const
    {
        return m_pendingOpen;
    }

    void cancelPendingOpen()
    {
        m_pendingOpen = false;
    }

    static bool controllerMayBeDestroyed(bool unconnected)
    {
        return unconnected;
    }

private:
    State m_state = State::Idle;
    bool m_pendingOpen = false;
};

#endif // MESHCOREBLELIFECYCLE_H
