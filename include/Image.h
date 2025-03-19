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
        Image(int width, int height);
        Image(const std::string& file_path, std::function<Color (Color, float, float)> filter = [](Color c, float, float){ return c; });
        Image(const Image& other);
        Image(Image&& other);
        virtual ~Image();
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
    private:
        int pos_to_index(int x, int y) const;
    private:
        void gen_gl_data();
    private:
        int m_width;
        int m_height;
        std::vector<Color> m_pixels;
        mutable std::mutex m_pixels_mtx;
        mutable bool m_has_unpushed_changes;
    private:
        bool m_holds_gl_texture;
        GLuint m_textureObject;
    };
}