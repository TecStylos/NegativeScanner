#pragma once

#include <queue>
#include <optional>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <cassert>

namespace ns
{
    enum class EventType
    {
        Mouse,
        Keyboard
    };

    class Event
    {
    public:
        Event() = default;
    public:
        template <typename T>
        const T& as_type() const;
    public:
        virtual EventType get_type() const = 0;
    };

    class MouseEvent : public Event
    {
    public:
        enum class Button
        { None, Left, Middle, Right };
        enum class State
        { None, Down, Up };
    public:
        MouseEvent(float x, float y, Button btn, State st);
    public:
        virtual EventType get_type() const override;
    public:
        float get_pos_x() const;
        float get_pos_y() const;
        Button get_button() const;
        State get_state() const;
    private:
        float m_pos_x;
        float m_pos_y;
        Button m_button;
        State m_state;
    };

    class KeyboardEvent : public Event
    {
    public:
        enum class State
        { None, Down, Up };
    public:
        KeyboardEvent(float x, float y, unsigned char key, State st);
    public:
        virtual EventType get_type() const override;
    public:
        float get_pos_x() const;
        float get_pos_y() const;
        unsigned char get_key() const;
        State get_state() const;
    private:
        float m_pos_x;
        float m_pos_y;
        unsigned char m_key;
        State m_state;
    };

    class Events
    {
    public:
        void push(const Event* pEvent);
        std::unique_ptr<const Event> pop();
        std::optional<std::unique_ptr<const Event>> try_pop();
    private:
        std::unique_ptr<const Event> pop_internal(std::unique_lock<std::mutex>& lock);
    private:
        std::queue<const Event*> m_queue;
        std::mutex m_queue_mtx;
        std::condition_variable m_queue_cv;
    };

    template <typename T>
    const T& Event::as_type() const
    {
        const T* p = dynamic_cast<const T*>(this);
        assert(p && "Unable to convert event");
        return *p;
    }
}