#pragma once

#include "Core/Expected.h"
#include "OpenGLApi.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlName.h"

#include <string>
#include <string_view>

// A linked GLSL program, which replaces the original's fixed function. Move-only; needs a live context.
namespace Client::Render::Gl
{
class OPENGL_API GlShader
{
public:
    // The error names the shader that did not compile, or the failed link, with GL's log. The shaders are deleted at
    // the end: the program already has what it needs.
    [[nodiscard]] static Core::Expected<GlShader, std::string>
    Create(const Platform::Sdl::GlFunctions& gl, std::string_view vertexSource, std::string_view fragmentSource);

    [[nodiscard]] GLuint Program() const;

    // -1 if the program lacks that uniform (or the compiler removed it as unused).
    [[nodiscard]] GLint UniformLocation(const char* name) const;

private:
    GlShader(const Platform::Sdl::GlFunctions& gl, GlName program);

    const Platform::Sdl::GlFunctions* m_gl;
    GlName m_program;
};
} // namespace Client::Render::Gl
