#include "Window.h"

#include <stdexcept>
#include <cstring>
#include <cassert>

#include "glIncludes.h"

#include "glInit.h"

#include "Vertex.h"

//#include "shaders/vertex.vs.h"
//#include "shaders/fragment.fs.h"

namespace ns
{
    class Window* g_window = nullptr;

    Window::Window(const std::string& title, int width, int height, int x, int y, std::function<void (Window*, Events&, std::function<bool (void)>)> user_func)
        : m_width(width), m_height(height),
          m_is_alive(true), m_user_thread(),
          m_events()
    {
        g_window = this;

        glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);

        glutInitWindowSize(width, height);
        glutInitWindowPosition(x, y);
        glutCreateWindow(title.c_str());

        GLenum res = glewInit();
        if (res != GLEW_OK)
        {
            fprintf(stderr, "Error: '%s'\n", glewGetErrorString(res));
            return;
        }

        auto display_callback = [](){
            if (!g_window) return;
            g_window->cb_render_scene();
        };

        glutDisplayFunc(display_callback);
        glutIdleFunc(display_callback);

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

        auto mouse_callback = [](int button, int state, int x, int y){ if (!g_window) return; g_window->cb_mouse_event(button, state, x, y); };
        glutMouseFunc(mouse_callback);

        auto keyboard_down_callback = [](unsigned char key, int x, int y){ if (!g_window) return; g_window->cb_keyboard_down_event(key, x, y); };
        glutKeyboardFunc(keyboard_down_callback);
        auto keyboard_up_callback = [](unsigned char key, int x, int y){ if (!g_window) return; g_window->cb_keyboard_up_event(key, x, y); };
        glutKeyboardUpFunc(keyboard_up_callback);

        auto reshape_callback = [](int width, int height){ if (!g_window) return; g_window->cb_reshape(width, height); };
        glutReshapeFunc(reshape_callback);

        if (user_func)
            m_user_thread = std::thread([&]{ user_func(this, m_events, std::bind(&Window::is_alive, this)); });

        glutMainLoop();
    }

    Window::~Window()
    {
        m_is_alive = false;
        m_user_thread.join();

        g_window = nullptr;
    }

    void Window::set_render_func(std::function<void ()> render_func)
    {
        m_render_func = render_func;
    }

    void Window::cb_render_scene()
    {
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        if (m_render_func)
            m_render_func();

        glutSwapBuffers();
    }

    void Window::cb_mouse_event(int button, int state, int x, int y)
    {
        MouseEvent::Button btn = MouseEvent::Button::None;
        switch (button)
        {
            case GLUT_LEFT_BUTTON:   btn = MouseEvent::Button::Left;   break;
            case GLUT_MIDDLE_BUTTON: btn = MouseEvent::Button::Middle; break;
            case GLUT_RIGHT_BUTTON:  btn = MouseEvent::Button::Right;  break;
            default: printf("Unhandled mouse button %d\n", button);
        }
        MouseEvent::State st = MouseEvent::State::None;
        switch (state)
        {
            case GLUT_UP:   st = MouseEvent::State::Up;   break;
            case GLUT_DOWN: st = MouseEvent::State::Down; break;
            default: assert(false && "Unhandled button state");
        }

        m_events.push(new MouseEvent((float)x / m_width, (float)y / m_height, btn, st));
    }

    void Window::cb_keyboard_down_event(unsigned char key, int x, int y)
    {
        m_events.push(new KeyboardEvent((float)x / m_width, (float)y / m_height, key, KeyboardEvent::State::Down));
    }

    void Window::cb_keyboard_up_event(unsigned char key, int x, int y)
    {
        m_events.push(new KeyboardEvent((float)x / m_width, (float)y / m_height, key, KeyboardEvent::State::Up));
    }

    void Window::cb_reshape(int width, int height)
    {
        m_width = width;
        m_height = height;

        glViewport(0, 0, m_width, m_height);
    }

    bool Window::is_alive()
    {
        return m_is_alive;
    }
}