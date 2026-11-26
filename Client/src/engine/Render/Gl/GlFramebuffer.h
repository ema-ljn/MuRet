#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "OpenGLApi.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlName.h"

#include <cstdint>
#include <string>
#include <vector>

// An offscreen framebuffer for the render_gl/ tests. Depth is GL_DEPTH_COMPONENT16: the 16 bits the original asks for
// (cDepthBits at 0x4b8e10), and the window's. Move-only; needs a live context.
namespace Client::Render::Gl
{
// A pixel as glReadPixels returns it with GL_RGBA and GL_UNSIGNED_BYTE.
struct Rgba8
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 0;

    friend bool operator==(const Rgba8&, const Rgba8&) = default;
};

class OPENGL_API GlFramebuffer
{
public:
    // Leaves it bound, cleared to (0, 0, 0, 0) with depth 1: what GL allocates without data has no fixed value. A side
    // of 0 or less never reaches GL.
    [[nodiscard]] static Core::Expected<GlFramebuffer, std::string> Create(const Platform::Sdl::GlFunctions& gl,
                                                                           int width, int height);

    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;
    [[nodiscard]] Core::PixelSize Size() const;

    // Rows start at the bottom, as GL returns them.
    [[nodiscard]] std::vector<Rgba8> ReadPixels() const;

private:
    GlFramebuffer(const Platform::Sdl::GlFunctions& gl, GlName color, GlName depth, GlName framebuffer, int width,
                  int height);

    const Platform::Sdl::GlFunctions* m_gl;
    GlName m_color;
    GlName m_depth;
    GlName m_framebuffer;
    int m_width;
    int m_height;
};
} // namespace Client::Render::Gl
