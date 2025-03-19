#include "ReadWriteMutex.h"

namespace ns
{
    void ReadWriteMutex::lock_read()
    {
        std::unique_lock lock(m_mtx);

        while (m_state < 0)
            m_con_var.wait(lock);

        ++m_state;
    }

    void ReadWriteMutex::lock_write()
    {
        std::unique_lock lock(m_mtx);

        while (m_state != 0)
            m_con_var.wait(lock);

        --m_state;
    }

    void ReadWriteMutex::unlock_read()
    {
        std::unique_lock lock(m_mtx);
        --m_state;
        m_con_var.notify_one();
    }

    void ReadWriteMutex::unlock_write()
    {
        std::unique_lock lock(m_mtx);
        ++m_state;
        m_con_var.notify_all();
    }

    ReadLock::ReadLock(ReadWriteMutex& mtx)
        : m_mtx(mtx)
    {
        m_mtx.lock_read();
    }

    ReadLock::~ReadLock()
    {
        m_mtx.unlock_read();
    }

    WriteLock::WriteLock(ReadWriteMutex& mtx)
        : m_mtx(mtx)
    {
        m_mtx.lock_write();
    }

    WriteLock::~WriteLock()
    {
        m_mtx.unlock_write();
    }
}