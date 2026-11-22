#pragma once

#include "App/WindowState.h"

#include <cstdint>
#include <vector>

namespace Client::Render::Frame
{
// 0x6202e0: the screenshot's glReadPixels, on the GL side (Render/Gl/GlScreenReader.h).
class IScreenReader
{
public:
    virtual ~IScreenReader() = default;
    // glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE) of what the frame drew: `screen.height` rows of
    // `screen.width` × 3 bytes, from the bottom one and without padding between rows (Q-C631).
    [[nodiscard]] virtual std::vector<std::uint8_t> ReadTheScreen(App::PixelSize screen) = 0;
};
} // namespace Client::Render::Frame
