#pragma once

#include <cstddef>
#include <cstdint>

// El número de slot de una textura en la tabla del original: el que nombran las trazas y el
// research (0x15 es Logo\Login_Back01.jpg, 06 § El login). MuRet no tiene una tabla global, sino
// una por escena (Gl::TextureTable, pregunta 11 del plan de la CL5), pero la indexa con el mismo
// número, así lo que dibuja una escena se compara con la traza del original.
namespace Client::Render
{
struct TextureSlot
{
    std::uint16_t value = 0;

    friend bool operator==(TextureSlot, TextureSlot) = default;
};

// Los slots de la tabla del original: 0x622e40 la pone en cero con 0x64e6 dwords, 1.845 slots de
// 0x38 bytes desde 0x7d3a198.
inline constexpr std::size_t OriginalTextureSlotCount = 1845;
static_assert(OriginalTextureSlotCount * 0x38 == 0x64e6 * 4, "el memset de 0x622e40");
} // namespace Client::Render
