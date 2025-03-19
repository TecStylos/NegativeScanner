#pragma once

#include "Image.h"
#include "Camera.h"

namespace ns
{
    class ImageRenderer
    {
    public:
        ImageRenderer();
    public:
        void render(const Image& img, int x, int y, const Camera& cam, float opacity, bool has_border);
    private:
        void generate_vertex_buffer();
        void load_shader_program();
    private:
        static GLuint load_shader_from_source(GLuint shader_type, const char* src);
    private:
        GLuint m_vbo;
        GLuint m_shader_prog;
    };
}