#include "Render/Gl/GlScreenReader.h"

#include "App/WindowState.h"
#include "Platform/GlLoader.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Client::Render::Gl
{
GlScreenReader::GlScreenReader(const Platform::GlFunctions& gl) : m_gl(&gl) {}

std::vector<std::uint8_t> GlScreenReader::ReadTheScreen(App::PixelSize screen)
{
    if (screen.width <= 0 || screen.height <= 0)
        return {};
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(screen.width) * static_cast<std::size_t>(screen.height) *
                                     3);
    GLint previousAlignment = 0;
    m_gl->getIntegerv(GL_PACK_ALIGNMENT, &previousAlignment);
    m_gl->pixelStorei(GL_PACK_ALIGNMENT, 1);
    m_gl->readPixels(0, 0, screen.width, screen.height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    m_gl->pixelStorei(GL_PACK_ALIGNMENT, previousAlignment);
    return pixels;
}
} // namespace Client::Render::Gl
