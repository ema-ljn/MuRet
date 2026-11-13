#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// Los renglones de los tooltips (06 § Los tooltips, CL8 hito 5): tres arreglos de la .bss que llenan los cuerpos
// (0x5b2aa0, 0x5b8150, 0x5b90b0), la ventana del set (0x40e850), su cartel (0x40eca0) y las ayudas de F1 (0x5b0550), y
// que dibuja 0x5af090. Nadie los vuelve a poner en 0 entre un frame y otro: cada cuerpo limpia lo suyo y lo que no
// escribe queda del anterior.
namespace Client::Scenes
{
// 0x78793c8: 30 renglones de 100 bytes, hasta 0x7879f80 (donde empieza el ítem del cursor).
inline constexpr std::size_t TooltipLineCount = 30;
// Lo que ocupa cada renglón, con su '\0'. Uno que sprintf escribe más largo pisa el que sigue (Q-C591).
inline constexpr std::size_t TooltipLineBytes = 100;
// Los cuerpos y el cartel limpian 20 colores (0x14 dwords con rep stosd) de los 30.
inline constexpr std::size_t TooltipClearedColors = 20;

// Los colores de 0x5af090 (el switch de 0x5af368): el glColor3f del renglón y, con 5, 7, 8 y 9, el fondo.
enum TooltipColor : std::int32_t
{
    TooltipWhite = 0,
    TooltipBlue = 1,
    TooltipRed = 2,
    TooltipGold = 3,
    TooltipGreen = 4,
    TooltipWhiteOnBlue = 5,
    TooltipPurple = 6,
    TooltipWhiteOnRed = 7,
    TooltipWhiteOnTeal = 8,
    TooltipGreenOnRed = 9,
    TooltipGray = 10,
};

struct TooltipLines
{
    // 0x78793c8 + 100 · i: el texto hasta su '\0'. Un renglón vacío corta la caja ahí (0x5af0c5).
    std::array<std::string, TooltipLineCount> text{};
    // 0x787a560 + 4 · i: el color de 0x5af090.
    std::array<std::int32_t, TooltipLineCount> colors{};
    // 0x78a9230 + 4 · i: con algo distinto de 0, la fuente negrita (0x5763558). Ningún cuerpo los limpia.
    std::array<std::int32_t, TooltipLineCount> bold{};
    // 0x78abb30: el renglón que sigue, y 0x78abb34: cuántos "\n" lleva (los medios renglones de la altura). Los ponen
    // en 0 el cuerpo del ítem (0x5b2aa0) y el de reparar (0x5b8150) al empezar; 0x5af590 los sigue. Las ayudas de F1
    // (0x5b057b) ponen en 0 solo el primero.
    std::int32_t next = 0;
    std::int32_t halves = 0;

    // memset de los colores que limpian los cuerpos (rep stosd de 0x14).
    void ClearColors()
    {
        for (std::size_t i = 0; i < TooltipClearedColors; ++i)
            colors[i] = 0;
    }
    // El primer byte de cada renglón en 0.
    void ClearTexts()
    {
        for (std::string& line : text)
            line.clear();
    }
};
} // namespace Client::Scenes
