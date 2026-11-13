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

// Los slots del texto de MuRet (CL5.3), después de los del original. El original sube cada texto
// al slot 0 (Interface\FontInput.tga, la caja de 256 × 32) cada vez que lo dibuja, y su texto con
// caché (0x466000) usa los slots 10000 + índice que reparte 0x466560, que MuRet no reproduce
// (pregunta 11 del plan de la CL5). La caché de MuRet (Render/Text/TextCache.h) guarda cada caja en
// uno de estos: con 256, 8 MiB de texturas.
inline constexpr std::size_t TextTextureSlotCount = 256;
inline constexpr TextureSlot FirstTextTextureSlot{static_cast<std::uint16_t>(OriginalTextureSlotCount)};

// Las marcas de los gremios de MuRet (CL9 hito 8), después de las del texto. El original pinta cada marca en el slot
// 0x22 (Interface\guild.tga) con glTexImage2D justo antes de su quad (0x5f16a0); MuRet arma los lotes del frame y los
// dibuja después, así que cada marca tiene su slot: uno por entrada de la tabla de los gremios (2.000, 0x447b50) y por
// fondo (el segundo argumento de 0x5f16a0), Scenes::GuildMarkTextures.
inline constexpr std::size_t GuildMarkTextureSlotCount = 4000;
inline constexpr TextureSlot FirstGuildMarkTextureSlot{
    static_cast<std::uint16_t>(OriginalTextureSlotCount + TextTextureSlotCount)};
// Los cuadrados de color del editor de la marca (CL9 hito 8, fase 3), después de las marcas: 0x5f1bb0 pinta el 0x22
// entero de un color de la paleta (o el 0 tachado) antes de su quad; MuRet le da uno a cada color de los 16.
inline constexpr std::size_t GuildSwatchTextureSlotCount = 16;
inline constexpr TextureSlot FirstGuildSwatchTextureSlot{
    static_cast<std::uint16_t>(OriginalTextureSlotCount + TextTextureSlotCount + GuildMarkTextureSlotCount)};
// Todos los slots de una tabla de MuRet: los del original, los del texto, los de las marcas y los de sus colores.
inline constexpr std::size_t TextureSlotCount =
    OriginalTextureSlotCount + TextTextureSlotCount + GuildMarkTextureSlotCount + GuildSwatchTextureSlotCount;
} // namespace Client::Render
