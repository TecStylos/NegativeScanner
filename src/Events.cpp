#include "Events.h"

#include <cassert>

namespace ns
{
    MouseEvent::MouseEvent(float x, float y, Button btn, State st)
        : m_pos_x(x), m_pos_y(y),
          m_button(btn), m_state(st)
    {}

    EventType MouseEvent::get_type() const
    {
        return EventType::Mouse;
    }

    float MouseEvent::get_pos_x() const
    {
        return m_pos_x;
    }

    float MouseEvent::get_pos_y() const
    {
        return m_pos_y;
    }

    MouseEvent::Button MouseEvent::get_button() const
    {
        return m_button;
    }

    MouseEvent::State MouseEvent::get_state() const
    {
        return m_state;
    }

    KeyboardEvent::KeyboardEvent(float x, float y, unsigned char key, State st)
        : m_pos_x(x), m_pos_y(y),
          m_key(key), m_state(st)
    {}

    EventType KeyboardEvent::get_type() const
    {
        return EventType::Keyboard;
    }

    float KeyboardEvent::get_pos_x() const
    {
        return m_pos_x;
    }

    float KeyboardEvent::get_pos_y() const
    {
        return m_pos_y;
    }

    unsigned char KeyboardEvent::get_key() const
    {
        return m_key;
    }

    KeyboardEvent::State KeyboardEvent::get_state() const
    {
        return m_state;
    }

    void Events::push(const Event* pEvent)
    {
        std::unique_lock lock(m_queue_mtx);

        m_queue.push(pEvent);
        m_queue_cv.notify_one();
    }

    std::unique_ptr<const Event> Events::pop()
    {
        std::unique_lock lock(m_queue_mtx);

        m_queue_cv.wait(lock, [&]{ return !m_queue.empty(); });

        return pop_internal(lock);
    }

    std::optional<std::unique_ptr<const Event>> Events::try_pop()
    {
        std::unique_lock lock(m_queue_mtx);

        if (m_queue.empty())
            return {};

        return pop_internal(lock);
    }

    std::unique_ptr<const Event> Events::pop_internal(std::unique_lock<std::mutex>& lock)
    {
        (void)lock;
        assert(lock.owns_lock() && "Lock must be owned inside Events::pop_internal");

        std::unique_ptr<const Event> ptr;
        ptr.reset(m_queue.front());
        m_queue.pop();
        return ptr;
    }
}