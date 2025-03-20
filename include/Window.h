#pragma once

#include <cstddef>
#include <string>
#include <memory>
#include <thread>
#include <functional>

#include "glIncludes.h"

#include "Events.h"

namespace ns
{
    class Window
    {
    public:
        Window(const std::string &title, int width, int height, int x, int y, std::function<void (Window*, Events&, std::function<bool (void)>)> user_func);
        virtual ~Window();
    public:
        void set_render_func(std::function<void ()> render_func);
    private:
        void cb_render_scene();
        void cb_mouse_move_event(int x, int y);
        void cb_mouse_button_event(int button, int state, int x, int y);
        void cb_keyboard_down_event(unsigned char key, int x, int y);
        void cb_keyboard_up_event(unsigned char key, int x, int y);
        void cb_reshape(int width, int height);
    private:
        bool is_alive();
    private:
        int m_width;
        int m_height;
        bool m_is_alive;
        std::thread m_user_thread;
        std::function<void ()> m_render_func;
        Events m_events;
    };
}