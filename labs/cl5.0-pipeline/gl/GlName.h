#pragma once

#include "LabGl.h"

// Un nombre de OpenGL con RAII (CL5.0, "El C++ que entra" de la guía): una textura, un buffer, un
// VAO, un framebuffer, un shader o un programa. GL no da objetos de C++ sino números (GLuint), y el
// 0 es "ninguno"; quien pide uno tiene que devolverlo con su glDelete*. El GlName lo devuelve en el
// destructor, se mueve (el de origen queda en 0) y no se copia, porque dos dueños lo borrarían dos
// veces. Como cualquier llamada a GL, borrar necesita el contexto vivo y actual: quien tiene un
// GlName lo declara después de la ventana, así se destruye antes.
namespace Labs
{
class GlName
{
public:
    // Cómo se borra cada clase de nombre: glDeleteTextures(1, &name), glDeleteProgram(name)...
    using Deleter = void (*)(const LabGl& gl, GLuint name);

    GlName() = default;
    GlName(const LabGl& gl, Deleter deleter, GLuint name);
    ~GlName();

    GlName(GlName&& other) noexcept;
    GlName& operator=(GlName&& other) noexcept;
    GlName(const GlName&) = delete;
    GlName& operator=(const GlName&) = delete;

    // El número de GL, o 0 si está vacío: por defecto, o después de moverlo.
    [[nodiscard]] GLuint Get() const;

private:
    void Release();

    const LabGl* m_gl = nullptr;
    Deleter m_deleter = nullptr;
    GLuint m_name = 0;
};

// glGen* de a uno, con su glDelete*.
[[nodiscard]] GlName GenTexture(const LabGl& gl);
[[nodiscard]] GlName GenBuffer(const LabGl& gl);
[[nodiscard]] GlName GenVertexArray(const LabGl& gl);
[[nodiscard]] GlName GenFramebuffer(const LabGl& gl);

// glCreateShader y glCreateProgram dan 0 si fallan; un GlName en 0 no borra nada.
[[nodiscard]] GlName CreateShader(const LabGl& gl, GLenum type);
[[nodiscard]] GlName CreateProgram(const LabGl& gl);
} // namespace Labs
