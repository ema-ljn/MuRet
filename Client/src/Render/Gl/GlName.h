#pragma once

#include "Platform/GlLoader.h"

// Un nombre de OpenGL con RAII (del lab de la CL5.0, labs/cl5.0-pipeline/gl/GlName.h): una
// textura, un buffer, un VAO, un framebuffer, un shader o un programa. GL no da objetos de C++ sino
// números (GLuint), y el 0 es "ninguno"; quien pide uno tiene que devolverlo con su glDelete*. El
// GlName lo devuelve en el destructor, se mueve (el de origen queda en 0) y no se copia, porque dos
// dueños lo borrarían dos veces. Guarda un puntero a la tabla de GL, que tiene que vivir más que
// él, y como cualquier llamada a GL, borrar necesita el contexto vivo y actual: quien tiene un
// GlName lo declara después del contexto, así se destruye antes.
namespace Client::Render::Gl
{
class GlName
{
public:
    // Cómo se borra cada clase de nombre: glDeleteTextures(1, &name), glDeleteProgram(name)...
    using Deleter = void (*)(const Platform::GlFunctions& gl, GLuint name);

    GlName() = default;
    GlName(const Platform::GlFunctions& gl, Deleter deleter, GLuint name);
    ~GlName();

    GlName(GlName&& other) noexcept;
    GlName& operator=(GlName&& other) noexcept;
    GlName(const GlName&) = delete;
    GlName& operator=(const GlName&) = delete;

    // El número de GL, o 0 si está vacío: por defecto, o después de moverlo.
    [[nodiscard]] GLuint Get() const;

private:
    void Release();

    const Platform::GlFunctions* m_gl = nullptr;
    Deleter m_deleter = nullptr;
    GLuint m_name = 0;
};

// glGen* de a uno, con su glDelete*.
[[nodiscard]] GlName GenTexture(const Platform::GlFunctions& gl);
[[nodiscard]] GlName GenBuffer(const Platform::GlFunctions& gl);
[[nodiscard]] GlName GenVertexArray(const Platform::GlFunctions& gl);
[[nodiscard]] GlName GenFramebuffer(const Platform::GlFunctions& gl);

// glCreateShader y glCreateProgram dan 0 si fallan; un GlName en 0 no borra nada.
[[nodiscard]] GlName CreateShader(const Platform::GlFunctions& gl, GLenum type);
[[nodiscard]] GlName CreateProgram(const Platform::GlFunctions& gl);
} // namespace Client::Render::Gl
