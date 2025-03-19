#pragma once

#include <condition_variable>

namespace ns
{
    // Own (shared) mutex implementation, starvation explicitly allowed
    class ReadWriteMutex
    {
    public:
        ReadWriteMutex() = default;
    public:
        void lock_read();
        void lock_write();
    public:
        void unlock_read();
        void unlock_write();
    private:
        std::mutex m_mtx;
        std::condition_variable m_con_var;
        int m_state = 0;
    };

    class ReadLock
    {
    public:
        ReadLock(ReadWriteMutex& mtx);
        ~ReadLock();
    private:
        ReadWriteMutex& m_mtx;
    };

    class WriteLock
    {
    public:
        WriteLock(ReadWriteMutex& mtx);
        ~WriteLock();
    private:
        ReadWriteMutex& m_mtx;
    };
}