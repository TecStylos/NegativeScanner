#include "ImageRenderer.h"

#include <cstring>

#include "Vertex.h"

const char* vertex_shader_src_str = R"(

#version 330

layout (location = 0) in vec2 Position;
layout (location = 1) in vec2 TexCoord;

uniform vec3 Camera;
uniform vec2 ImageDimensions;
uniform vec2 ImagePosition;

out vec2 TexCoord0;

void main()
{
    vec2 ScaledPosition = Position * ImageDimensions;
    vec2 MovedPosition = ScaledPosition + ImagePosition + Camera.xy;
    vec2 ZoomedPosition = MovedPosition * Camera.z;
    ZoomedPosition.y *= -1.0;
    gl_Position = vec4(ZoomedPosition, 0.0, 1.0);
    TexCoord0 = TexCoord;
};

)";

const char *fragment_shader_src_str = R"(

#version 330

in vec2 TexCoord0;

out vec4 FragColor;

uniform sampler2D gSampler;

uniform float Opacity;
uniform float HasBorder;

void main()
{
    if (HasBorder > 0.5 && 
        (TexCoord0.x < 0.01 || TexCoord0.x > 0.99 || TexCoord0.y < 0.01 || TexCoord0.y > 0.99))
    {
        FragColor = vec4(1.0, 0.0, 0.0, 0.2);
    }
    else
    {
        FragColor = vec4(texture2D(gSampler, TexCoord0.xy).rgb, Opacity);
    }
    
    //FragColor = vec4(TexCoord0.xy, 0.0, 0.0);
};

)";

namespace ns
{
    ImageRenderer::ImageRenderer()
    {
        generate_vertex_buffer();
        load_shader_program();
    }

    void ImageRenderer::render(const Image& img, int x, int y, const Camera& cam, float opacity, bool has_border)
    {
        glUseProgram(m_shader_prog);

        GLint u_camera = glGetUniformLocation(m_shader_prog, "Camera");
        glUniform3f(u_camera, cam.x, cam.y, cam.zoom);
        
        GLint u_img_dim = glGetUniformLocation(m_shader_prog, "ImageDimensions");
        glUniform2f(u_img_dim, (float)img.get_width(), (float)img.get_height());

        GLint u_img_pos = glGetUniformLocation(m_shader_prog, "ImagePosition");
        glUniform2f(u_img_pos, (float)x, (float)y);

        GLint u_opacity = glGetUniformLocation(m_shader_prog, "Opacity");
        glUniform1f(u_opacity, opacity);

        GLint u_has_border = glGetUniformLocation(m_shader_prog, "HasBorder");
        glUniform1f(u_has_border, has_border ? 1.0f : 0.0f);
    
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const GLvoid*)offsetof(Vertex, pos));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const GLvoid*)offsetof(Vertex, uvCoords));

        img.push_changes();
        img.bind();

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(0);

        glUseProgram(0);
    }

    void ImageRenderer::generate_vertex_buffer()
    {
        Vertex verts[6];
        verts[0] = { {  0.0f,  1.0f }, { 0.0f, 1.0f } };
        verts[1] = { {  1.0f,  1.0f }, { 1.0f, 1.0f } };
        verts[2] = { {  1.0f,  0.0f }, { 1.0f, 0.0f } };
        verts[3] = { {  0.0f,  1.0f }, { 0.0f, 1.0f } };
        verts[4] = { {  1.0f,  0.0f }, { 1.0f, 0.0f } };
        verts[5] = { {  0.0f,  0.0f }, { 0.0f, 0.0f } };

        glGenBuffers(1, &m_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    }

    void ImageRenderer::load_shader_program()
    {
        m_shader_prog = glCreateProgram();
        GLuint vertexShaderObject = load_shader_from_source(GL_VERTEX_SHADER, vertex_shader_src_str);
        GLuint fragmentShaderObject = load_shader_from_source(GL_FRAGMENT_SHADER, fragment_shader_src_str);

        glAttachShader(m_shader_prog, vertexShaderObject);
        glAttachShader(m_shader_prog, fragmentShaderObject);
        glLinkProgram(m_shader_prog);

        GLint success;
        glGetProgramiv(m_shader_prog, GL_LINK_STATUS, &success);
        if (!success)
        {
            GLchar infoLog[1024];
            glGetProgramInfoLog(m_shader_prog, sizeof(infoLog), NULL, infoLog);
            fprintf(stderr, "Error linking shader program: '%s'\n", infoLog);
        }

        glValidateProgram(m_shader_prog);
    }

    GLuint ImageRenderer::load_shader_from_source(GLuint shader_type, const char* src)
    {
        GLuint shaderObject = glCreateShader(shader_type);

        const GLchar *p[1];
        p[0] = src;
        GLint lengths[1];
        lengths[0] = strlen(src);
        glShaderSource(shaderObject, 1, p, lengths);

        glCompileShader(shaderObject);

        GLint success;
        glGetShaderiv(shaderObject, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            GLchar infoLog[1024];
            glGetShaderInfoLog(shaderObject, sizeof(infoLog), NULL, infoLog);
            fprintf(stderr, "Error compiling shader type %d: '%s'\n", shader_type, infoLog);
            exit(EXIT_FAILURE);
        }

        return shaderObject;
    }
}