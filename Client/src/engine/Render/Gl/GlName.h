#pragma once

#include "OpenGLApi.h"
#include "Platform/Sdl/GlLoader.h"

// An RAII OpenGL name (a GLuint, 0 is none) released with its glDelete* in the destructor. Move-only: two owners would
// delete it twice. The GL table must outlive it, and deleting needs a live, current context: declare a GlName after
// the context so it is destroyed first.
namespace Client::Render::Gl
{
class OPENGL_API GlName
{
public:
    using Deleter = void (*)(const Platform::Sdl::GlFunctions& gl, GLuint name);

    GlName() = default;
    GlName(const Platform::Sdl::GlFunctions& gl, Deleter deleter, GLuint name);
    ~GlName();

    GlName(GlName&& other) noexcept;
    GlName& operator=(GlName&& other) noexcept;
    GlName(const GlName&) = delete;
    GlName& operator=(const GlName&) = delete;

    // 0 when empty: default-constructed or moved from.
    [[nodiscard]] GLuint Get() const;

private:
    void Release();

    const Platform::Sdl::GlFunctions* m_gl = nullptr;
    Deleter m_deleter = nullptr;
    GLuint m_name = 0;
};

[[nodiscard]] GlName GenTexture(const Platform::Sdl::GlFunctions& gl);
[[nodiscard]] GlName GenBuffer(const Platform::Sdl::GlFunctions& gl);
[[nodiscard]] GlName GenVertexArray(const Platform::Sdl::GlFunctions& gl);
[[nodiscard]] GlName GenFramebuffer(const Platform::Sdl::GlFunctions& gl);

// glCreateShader and glCreateProgram return 0 on failure; a GlName of 0 deletes nothing.
[[nodiscard]] GlName CreateShader(const Platform::Sdl::GlFunctions& gl, GLenum type);
[[nodiscard]] GlName CreateProgram(const Platform::Sdl::GlFunctions& gl);
} // namespace Client::Render::Gl
