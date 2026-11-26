#pragma once

#include <cstdint>

// The third and fourth arguments of the original's loaders (0x63b870 and 0x63bd00), which go to glTexParameteri: the
// filter to GL_TEXTURE_MAG_FILTER and GL_TEXTURE_MIN_FILTER, the wrap to GL_TEXTURE_WRAP_S and _T
// (Client/docs/research/06-render-escenas-y-ui.md, "El login, al entrar"). The caller picks them per texture.
namespace Client::Data::Textures
{
enum class Filter : std::uint8_t
{
    Nearest, // 0x2600, GL_NEAREST
    Linear,  // 0x2601, GL_LINEAR
};

enum class Wrap : std::uint8_t
{
    Clamp,  // 0x2900, GL_CLAMP: the core profile lacks it and uses GL_CLAMP_TO_EDGE (Q-C73)
    Repeat, // 0x2901, GL_REPEAT
};

struct Sampling
{
    Filter filter = Filter::Nearest;
    Wrap wrap = Wrap::Clamp;
};
} // namespace Client::Data::Textures
