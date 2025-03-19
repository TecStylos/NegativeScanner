#include "Image.h"

#include <stdexcept>
#include <cassert>

#include "vendor/stb_image.h"

namespace ns
{
    Image::Image()
        : Image(1, 1)
    {}

    Image::Image(int width, int height)
        : m_width(width), m_height(height), m_pixels(width * height, Color()),
          m_pixels_mtx(), m_has_unpushed_changes(true),
          m_textureObject(0)
    {
        gen_gl_data();
    }

    Image::Image(const std::string& path, std::function<Color (Color, float, float)> filter)
    {
        int width, height, channels_in_file;
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels_in_file, 3);

        assert(data && "Unable to load image from file");

        m_width = width;
        m_height = height;
        m_pixels.resize(width * height, Color());
        m_has_unpushed_changes = true;

        for (int i = 0; i < width * height; ++i)
        {
            unsigned char* pixel = data + i * 3;
            Color c;
            c.r = pixel[0] / 255.0f;
            c.g = pixel[1] / 255.0f;
            c.b = pixel[2] / 255.0f;
            float u = (i % width) / (float)width;
            float v = (i / width) / (float)height;
            m_pixels[i] = filter(c, u, v);
        }

        stbi_image_free(data);

        gen_gl_data();
    }

    Image::Image(const Image& other)
        : Image(other.m_width, other.m_height)
    {
        m_pixels = other.m_pixels;
        m_has_unpushed_changes = true;
    }

    Image::Image(Image&& other)
        : m_width(other.m_width), m_height(other.m_height),
          m_pixels(std::move(other.m_pixels)),
          m_pixels_mtx(), m_has_unpushed_changes(true),
          m_holds_gl_texture(other.m_holds_gl_texture),
          m_textureObject(other.m_textureObject)
    {
        other.m_holds_gl_texture = false;
    }

    Image::~Image()
    {
        if (m_holds_gl_texture)
            glDeleteTextures(1, &m_textureObject);
    }

    void Image::put_pixel(const Color& c, int x, int y)
    {
        std::lock_guard lock(m_pixels_mtx);

        m_pixels[pos_to_index(x, y)] = c;
        m_has_unpushed_changes = true;
    }

    void Image::push_changes() const
    {
        if (!m_has_unpushed_changes)
            return;

        std::lock_guard lock(m_pixels_mtx);

        bind();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_width, m_height, 0, GL_RGB, GL_FLOAT, m_pixels.data());

        assert(glGetError() == GL_NO_ERROR);

        m_has_unpushed_changes = false;
    }

    void Image::bind() const
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_textureObject);
    }

    int Image::get_width() const
    {
        return m_width;
    }

    int Image::get_height() const
    {
        return m_height;
    }

    const Color& Image::get_pixel(int x, int y) const
    {
        return m_pixels[pos_to_index(x, y)];
    }

    int Image::pos_to_index(int x, int y) const
    {
        return x + y * m_width;
    }

    void Image::gen_gl_data()
    {
        glGenTextures(1, &m_textureObject);
        glBindTexture(GL_TEXTURE_2D, m_textureObject);

        GLint size;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &size);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_width, m_height, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        m_holds_gl_texture = true;
    }
}