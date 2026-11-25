#pragma once

#include <cstdint>

// glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, ...): cómo combina la función fija el téxel con el color del
// fragmento. Es estado de GL que ninguna mezcla del original (0x620980 a 0x620c90) toca: lo cambia solo quien
// lo necesita y lo devuelve a GL_MODULATE.
namespace Client::Render
{
enum class TextureEnvMode : std::uint8_t
{
    // GL_MODULATE, el de siempre: téxel · color, también el alfa.
    Modulate,
    // GL_ADD (la llama 6ac/2, 0x5489eb): el color es téxel + color, recortado a [0, 1], y el alfa, téxel · color.
    Add,
};
} // namespace Client::Render
