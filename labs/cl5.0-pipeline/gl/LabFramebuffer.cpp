#include "LabFramebuffer.h"

#include <cstddef>
#include <string>
#include <utility>

namespace Labs
{
// glReadPixels escribe 4 bytes por píxel, uno detrás del otro: un Rgba tiene que medir eso.
static_assert(sizeof(Rgba) == 4);

LabFramebuffer::LabFramebuffer(const LabGl& gl, GlName color, GlName framebuffer, int width, int height)
    : m_gl(&gl), m_color(std::move(color)), m_framebuffer(std::move(framebuffer)), m_width(width), m_height(height)
{
}

Client::Core::Expected<LabFramebuffer, std::string> LabFramebuffer::Create(const LabGl& gl, int width, int height)
{
    GlName color = GenTexture(gl);
    gl.bindTexture(GL_TEXTURE_2D, color.Get());
    // Sin datos (nullptr): GL reserva la textura, y lo que se dibuje la llena.
    gl.texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    GlName framebuffer = GenFramebuffer(gl);
    gl.bindFramebuffer(GL_FRAMEBUFFER, framebuffer.Get());
    gl.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color.Get(), 0);
    const GLenum status = gl.checkFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        return Client::Core::Unexpected("el framebuffer no quedó completo: glCheckFramebufferStatus dio " +
                                        std::to_string(status));
    }
    gl.viewport(0, 0, width, height);
    return LabFramebuffer(gl, std::move(color), std::move(framebuffer), width, height);
}

int LabFramebuffer::Width() const
{
    return m_width;
}

int LabFramebuffer::Height() const
{
    return m_height;
}

// GL_PACK_ALIGNMENT queda en 4: una fila RGBA mide un múltiplo de 4 y no lleva relleno.
std::vector<Rgba> LabFramebuffer::ReadPixels() const
{
    std::vector<Rgba> pixels(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height));
    m_gl->bindFramebuffer(GL_FRAMEBUFFER, m_framebuffer.Get());
    m_gl->readPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}
} // namespace Labs
