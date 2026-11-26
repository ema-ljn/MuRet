#pragma once

#include "Core/PixelSize.h"
#include "OpenGLApi.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Frame/IScreenReader.h"

#include <cstdint>
#include <vector>

// The screenshot read (0x6202e0): the window's back buffer, after the frame is drawn and before the swap. Needs a live
// context.
namespace Client::Render::Gl
{
class OPENGL_API GlScreenReader final : public Frame::IScreenReader
{
public:
    // The GL table outlives it.
    explicit GlScreenReader(const Platform::Sdl::GlFunctions& gl);

    // 0x62031f: glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE) into a width × height × 3 buffer. The
    // original leaves GL_PACK_ALIGNMENT at GL's 4, so a width not a multiple of 4 would pad rows and overrun the
    // buffer; MuRet sets it to 1 while reading and restores it. With the original's widths, multiples of 4, the bytes
    // match (Q-C631).
    [[nodiscard]] std::vector<std::uint8_t> ReadTheScreen(Core::PixelSize screen) override;

private:
    const Platform::Sdl::GlFunctions* m_gl;
};
} // namespace Client::Render::Gl
