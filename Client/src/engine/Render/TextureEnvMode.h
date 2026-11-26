#pragma once

#include <cstdint>

// glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, ...): how the fixed function combines the texel with the fragment
// color. GL state that none of the original's blends (0x620980 to 0x620c90) touches: only whoever needs it changes it,
// and restores GL_MODULATE.
namespace Client::Render
{
enum class TextureEnvMode : std::uint8_t
{
    // GL_MODULATE, the usual one: texel · color, alpha too.
    Modulate,
    // GL_ADD (the 6ac/2 flame, 0x5489eb): color is texel + color, clamped to [0, 1], and alpha texel · color.
    Add,
};
} // namespace Client::Render
