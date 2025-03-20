#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <functional>

#include "glIncludes.h"
#include "Color.h"

namespace ns
{
    class Image
    {
    public:
        Image();
        Image(int width, int height, bool make_viewable = true);
        Image(const Image& other);
        Image(Image&& other);
        virtual ~Image();
        Image& operator=(const Image& other);
        Image& operator=(Image&& other);
    public:
        void put_pixel(const Color& c, int x, int y);
        void push_changes() const;
    public:
        int get_width() const;
        int get_height() const;
    public:
        const Color& get_pixel(int x, int y) const;
    public:
        void bind() const;
    public:
        bool save_to_file(const std::string& filepath) const;
        static Image load_from_file(const std::string& filepath, bool make_viewable = true, std::function<Color (Color, float, float)> filter = [](Color c, float, float){ return c; });
    private:
        int pos_to_index(int x, int y) const;
    private:
        void gen_gl_data();
        void clear_gl_data();
    private:
        int m_width;
        int m_height;
        std::vector<Color> m_pixels;
        mutable std::mutex m_pixels_mtx;
        mutable bool m_has_unpushed_changes;
        bool m_make_viewable;
    private:
        GLuint m_textureObject;
    };
}