#include "Render/Gl/GlFramebuffer.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlName.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Client::Render::Gl
{
// glReadPixels writes 4 contiguous bytes per pixel: an Rgba8 must be that size.
static_assert(sizeof(Rgba8) == 4);

GlFramebuffer::GlFramebuffer(const Platform::Sdl::GlFunctions& gl, GlName color, GlName depth, GlName framebuffer,
                             int width, int height)
    : m_gl(&gl), m_color(std::move(color)), m_depth(std::move(depth)), m_framebuffer(std::move(framebuffer)),
      m_width(width), m_height(height)
{
}

Core::Expected<GlFramebuffer, std::string> GlFramebuffer::Create(const Platform::Sdl::GlFunctions& gl, int width,
                                                                 int height)
{
    if (width <= 0 || height <= 0)
    {
        return Core::Unexpected("un framebuffer de " + std::to_string(width) + " × " + std::to_string(height) +
                                " no se puede crear");
    }
    GlName color = GenTexture(gl);
    gl.bindTexture(GL_TEXTURE_2D, color.Get());
    // No data: GL allocates the texture and drawing fills it.
    gl.texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Depth is also a texture, not a renderbuffer, so the functions the client already loads suffice
    // (GlFunctionList.h).
    GlName depth = GenTexture(gl);
    gl.bindTexture(GL_TEXTURE_2D, depth.Get());
    gl.texImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT,
                  nullptr);
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    GlName framebuffer = GenFramebuffer(gl);
    gl.bindFramebuffer(GL_FRAMEBUFFER, framebuffer.Get());
    gl.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color.Get(), 0);
    gl.framebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth.Get(), 0);
    const GLenum status = gl.checkFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        return Core::Unexpected("el framebuffer no quedó completo: glCheckFramebufferStatus dio " +
                                std::to_string(status));
    }
    gl.viewport(0, 0, width, height);
    gl.clearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl.clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return GlFramebuffer(gl, std::move(color), std::move(depth), std::move(framebuffer), width, height);
}

int GlFramebuffer::Width() const
{
    return m_width;
}

int GlFramebuffer::Height() const
{
    return m_height;
}

Core::PixelSize GlFramebuffer::Size() const
{
    return Core::PixelSize{m_width, m_height};
}

// GL_PACK_ALIGNMENT stays at 4: an RGBA row is a multiple of 4 bytes and has no padding.
std::vector<Rgba8> GlFramebuffer::ReadPixels() const
{
    std::vector<Rgba8> pixels(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height));
    m_gl->bindFramebuffer(GL_FRAMEBUFFER, m_framebuffer.Get());
    m_gl->readPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}
} // namespace Client::Render::Gl
