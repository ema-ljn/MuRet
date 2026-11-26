#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "OpenGLApi.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlName.h"
#include "Render/Gl/GlShader.h"
#include "Render/Gl/GlTexture.h"
#include "Render/SpriteVertex.h"

#include <cstddef>
#include <span>
#include <string>

// The 2D pipeline, replacing the original's glBegin(GL_TRIANGLE_FAN), glTexCoord2f and glVertex2f per vertex, glEnd
// and the gluOrtho2D matrix (06 § Primitivas 2D). Move-only; needs a live context.
namespace Client::Render::Gl
{
class OPENGL_API GlRenderer
{
public:
    // The error carries the GLSL compiler log.
    [[nodiscard]] static Core::Expected<GlRenderer, std::string> Create(const Platform::Sdl::GlFunctions& gl);

    // Three vertices per triangle, without the alpha test: Upload plus one DrawRange of all. Does not touch the blend.
    void Draw(std::span<const Render::SpriteVertex> vertices, const GlTexture& texture, Core::PixelSize screen) const;

    // Y up, like the original's 2D (0x6219b0: the full viewport and gluOrtho2D(0, W, 0, H)). Leaves the program and the
    // VAO bound for the DrawRange calls that follow.
    void Upload(std::span<const Render::SpriteVertex> vertices, Core::PixelSize screen) const;

    // A null `texture` samples none. Does not touch the blend.
    void DrawRange(std::size_t firstVertex, std::size_t vertexCount, const GlTexture* texture, bool alphaTest) const;

private:
    struct Uniforms
    {
        GLint projection = -1;
        GLint textured = -1;
        GLint alphaTest = -1;
    };

    GlRenderer(const Platform::Sdl::GlFunctions& gl, GlShader shader, GlName vertexArray, GlName vertexBuffer,
               Uniforms uniforms);

    const Platform::Sdl::GlFunctions* m_gl;
    GlShader m_shader;
    GlName m_vertexArray;
    GlName m_vertexBuffer;
    Uniforms m_uniforms;
};
} // namespace Client::Render::Gl
