#pragma once

#include <cstdint>

// Cómo se muestrea una textura: el tercer y el cuarto argumento de los cargadores del original
// (0x63b870 y 0x63bd00), que van a glTexParameteri. El tercero, el filtro, va a
// GL_TEXTURE_MAG_FILTER y GL_TEXTURE_MIN_FILTER; el cuarto, el wrap, a GL_TEXTURE_WRAP_S y _T
// (Client/docs/research/06-render-escenas-y-ui.md, "El login, al entrar"). Lo elige quien pide la
// textura, por textura: es parte del juego de texturas de una escena o de un mundo.
namespace Client::Data::Textures
{
enum class Filter : std::uint8_t
{
    Nearest, // 0x2600, GL_NEAREST
    Linear,  // 0x2601, GL_LINEAR
};

enum class Wrap : std::uint8_t
{
    Clamp,  // 0x2900, GL_CLAMP: en el perfil core no existe y va GL_CLAMP_TO_EDGE (Q-C73)
    Repeat, // 0x2901, GL_REPEAT
};

struct Sampling
{
    Filter filter = Filter::Nearest;
    Wrap wrap = Wrap::Clamp;
};
} // namespace Client::Data::Textures
