#pragma once

#include "Core/Expected.h"
#include "Core/Log.h"
#include "OpenGLApi.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlRenderer.h"
#include "Render/Gl/TextureTable.h"
#include "Render/SpriteBatch.h"

#include <cstdint>
#include <set>
#include <string>

// Draws a frame's SpriteBatch ranges in order, each with 0x620a00's or 0x620a90's blend, its slot's texture (or none)
// and the original's glAlphaFunc(GL_GREATER, 0.25) as a shader discard (06 § Primitivas 2D). Leaves the blend on, like
// those two functions, and turns off the depth and culling a 3D pass leaves (0x6219b0 → 0x620870). Needs a live
// context.
namespace Client::Render::Gl
{
class OPENGL_API GlSpriteRenderer
{
public:
    // The error carries the GLSL compiler log or the missing uniform.
    [[nodiscard]] static Core::Expected<GlSpriteRenderer, std::string> Create(const Platform::Sdl::GlFunctions& gl);

    // Into the bound framebuffer. A range whose slot `textures` lacks is skipped and its slot is warned once in `log`:
    // the batch is drawn every frame, and warning in each one would flood the log.
    void Draw(const SpriteBatch& batch, const TextureTable& textures, Core::Log& log);

private:
    GlSpriteRenderer(const Platform::Sdl::GlFunctions& gl, GlRenderer renderer);

    const Platform::Sdl::GlFunctions* m_gl;
    GlRenderer m_renderer;
    std::set<std::uint16_t> m_warnedSlots;
};
} // namespace Client::Render::Gl
