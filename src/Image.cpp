#include "Image.h"

#include <stdexcept>
#include <cassert>

#include "vendor/stb_image.h"
#include "vendor/stb_image_write.h"

namespace ns
{
    std::mutex Image::s_tex_objs_to_delete_mtx;
    std::queue<GLuint> Image::s_tex_objs_to_delete;

    Image::Image()
        : Image(1, 1, false)
    {}

    Image::Image(int width, int height, bool make_viewable)
        : m_width(width), m_height(height),
          m_pixels(width * height, Color()), m_pixels_mtx(),
          m_has_unpushed_changes(true),
          m_make_viewable(make_viewable),
          m_textureObject(0)
    {
        gen_gl_data();
    }

    Image::Image(const Image& other)
        : m_width(other.m_width), m_height(other.m_height),
          m_pixels(other.m_pixels), m_pixels_mtx(),
          m_has_unpushed_changes(true),
          m_make_viewable(other.m_make_viewable),
          m_textureObject(0)
    {
        gen_gl_data();
    }

    Image::Image(Image&& other)
        : Image()
    {
        *this = std::move(other);
    }

    Image::~Image()
    {
        clear_gl_data();
    }

    Image& Image::operator=(const Image& other)
    {
        clear_gl_data();
        
        m_width = other.m_width;
        m_height = other.m_height;
        m_pixels = other.m_pixels;
        //m_pixels_mtx = other.m_pixels_mtx;
        m_has_unpushed_changes = true;
        m_make_viewable = other.m_make_viewable;
        //m_textureObject = other.m_textureObject;

        gen_gl_data();

        return *this;
    }

    Image& Image::operator=(Image&& other)
    {
        std::swap(m_width, other.m_width);
        std::swap(m_height, other.m_height);
        std::swap(m_pixels, other.m_pixels);
        //std::swap(m_pixels_mtx, other.m_pixels_mtx);
        std::swap(m_has_unpushed_changes, other.m_has_unpushed_changes);
        std::swap(m_make_viewable, other.m_make_viewable);
        std::swap(m_textureObject, other.m_textureObject);

        return *this;
    }

    void Image::put_pixel(const Color& c, int x, int y)
    {
        std::lock_guard lock(m_pixels_mtx);

        m_pixels[pos_to_index(x, y)] = c;
        m_has_unpushed_changes = true;
    }

    void Image::push_changes() const
    {
        if (!m_has_unpushed_changes || !m_make_viewable)
            return;

        std::lock_guard lock(m_pixels_mtx);

        bind();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_FLOAT, m_pixels.data());

        assert(glGetError() == GL_NO_ERROR);

        m_has_unpushed_changes = false;
    }

    int Image::get_width() const
    {
        return m_width;
    }

    int Image::get_height() const
    {
        return m_height;
    }

    bool Image::is_viewable() const
    {
        return m_make_viewable;
    }

    const Color& Image::get_pixel(int x, int y) const
    {
        return m_pixels[pos_to_index(x, y)];
    }

    void Image::bind() const
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_textureObject);
    }

    Image Image::load_from_file(const std::string& filepath, bool make_viewable, std::function<Color (Color, float, float)> filter)
    {
        int width, height, channels_in_file;
        unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels_in_file, 4);

        assert(data && "Unable to load image from file");

        Image img(width, height, make_viewable);

        for (int i = 0; i < width * height; ++i)
        {
            unsigned char* pixel = data + i * 4;

            Color c;
            c.r = pixel[0] / 255.0f;
            c.g = pixel[1] / 255.0f;
            c.b = pixel[2] / 255.0f;
            c.a = pixel[3] / 255.0f;

            int x = (i % width);
            int y = (i / width);
            float u = x / (float)width;
            float v = y / (float)height;

            img.put_pixel(filter(c, u, v), x, y);
        }

        stbi_image_free(data);

        return img;
    }

    bool Image::save_to_file(const std::string& filepath) const
    {
        std::vector<unsigned char> data(m_width * m_height * 3);

        for (int i = 0; i < m_width * m_height; ++i)
        {
            auto& c = m_pixels[i];
            data[i * 3 + 0] = c.r * 255;
            data[i * 3 + 1] = c.g * 255;
            data[i * 3 + 2] = c.b * 255;
        }

        bool success = stbi_write_jpg(filepath.c_str(), m_width, m_height, 3, data.data(), 100);

        return success;
    }

    void Image::delete_pending_tex_objs()
    {
        std::lock_guard lock(s_tex_objs_to_delete_mtx);

        while (!s_tex_objs_to_delete.empty())
        {
            GLuint tex_obj = s_tex_objs_to_delete.front();
            s_tex_objs_to_delete.pop();
            glDeleteTextures(1, &tex_obj);
        }
    }

    int Image::pos_to_index(int x, int y) const
    {
        return x + y * m_width;
    }

    void Image::gen_gl_data()
    {
        if (!m_make_viewable)
            return;

        glGenTextures(1, &m_textureObject);
        glBindTexture(GL_TEXTURE_2D, m_textureObject);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    void Image::clear_gl_data()
    {
        if (!m_textureObject)
            return;
        m_textureObject = 0;
    }
}