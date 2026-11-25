#pragma once

#include "Render/Text/TextStyle.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Los cambios de color dentro de un texto: un 0x02 seguido de un código. Los separa 0x4667a0, que
// solo corre con el texto con caché (0x466000, TextOut en 1); el del login, 0x467060, los dibuja
// como letras (06 § Texto). Lo usa el chat de la CL8.
//
// 0x4667a0 recorre el texto hasta el primer 0 (strlen) y, por cada 0x02, anota un tramo: dónde
// empieza en el texto limpio (la posición menos dos por cada par anterior) y su color, que da
// 0x4665d0 por el byte que sigue. El byte que sigue es el código aunque sea otro 0x02 o el 0 del
// final. Anota hasta nueve tramos (0x4667e9: corta cuando lleva 18 bytes de pares) y después
// reescribe el texto sin ningún par, también los que pasan de nueve (0x46687b..0x466894), así que
// el texto del décimo en adelante sigue con el color del noveno. *Estático*.
namespace Client::Render::Text
{
inline constexpr char InlineColorMarker = '\x02';
inline constexpr std::size_t MaxInlineColors = 9;

// Lo que 0x4665d0 lee de las globales: los colores del texto (DAT_006c8e5c y DAT_006c8e64), que
// 0xf0 usa como están y 0xf1 cruza, y DAT_006c7a88, que decide el fondo de 0xf8. Esa bandera es 1
// en la .data y la escriben ventanas de la UI (0x45b76e, 0x45d900, 0x45d922 y 0x45d93a); qué dice
// no se leyó.
struct InlineColorContext
{
    TextColors scene;
    bool f8WithoutBackground = true;
};

struct ColorRun
{
    // En el texto limpio: el primer byte que va con este color.
    std::size_t start = 0;
    // El byte que sigue al 0x02, o 0 si el 0x02 es el último.
    std::uint8_t code = 0;
    TextColors colors;

    friend bool operator==(const ColorRun&, const ColorRun&) = default;
};

struct InlineText
{
    // Sin los pares.
    std::string text;
    // Hasta MaxInlineColors, en el orden del texto. Lo que va antes del primero, con los colores de
    // la escena.
    std::vector<ColorRun> runs;
};

// 0x4665d0: el color de un código. 0x01 y 0xf4, rojo sin fondo; 0xf0, los de la escena; 0xf1, los
// de la escena cruzados; de 0xf2 a 0xf8, uno de la tabla (0xf8 con fondo 0x96000000 si la bandera
// de `context` está apagada); cualquier otro, blanco sin fondo.
[[nodiscard]] TextColors InlineColor(std::uint8_t code, const InlineColorContext& context);

// 0x4667a0: el texto limpio y sus tramos.
[[nodiscard]] InlineText ParseInlineColors(std::string_view text, const InlineColorContext& context);
} // namespace Client::Render::Text
