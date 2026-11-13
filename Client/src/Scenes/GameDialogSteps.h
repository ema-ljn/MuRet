#pragma once

#include "Core/Expected.h"
#include "Protocol/OptionData.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Los diálogos del juego en el paso (06 § HUD y ventanas, "Los diálogos del juego", CL8 hito 6): lo que 0x623e70 hace
// con el diálogo abierto (0x7d3a10c) después de Esc, desde 0x623f78. Cada id va por su tramo de la tabla de
// despacho: el menú de Esc, las opciones, las cajas con botones, el OK genérico, o el corte de su ola con la dirección
// del tramo. Una ola que porta un tramo cambia su fila.
namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Scenes
{
class WorldState;
struct GameUiContext;

enum class DialogStep : std::uint8_t
{
    // 0x623f90: el menú de Esc (0x6e).
    EscapeMenu,
    // 0x62556f: las opciones (0x93).
    Options,
    // 0x62d756: los dos botones de 0x7d368b8 (y las flechas del 0x8c, y las respuestas del 0x8d).
    Buttons,
    // 0x6263df: Enter o el botón (0x11c, 0x62, 0x46, 0x15) cierran.
    GenericOk,
    // 0x6282dc: los dos botones de sí y no (y Esc, el no) de 0x94, 0x96, 0x98, 0x99, 0xa3 y 0xa9.
    YesNo,
    // 0x625910: el modo de la caja del caos (0x8e).
    ChaosMixMode,
    // 0x6259c6, 0x625b23 y 0x625f98: la mezcla de joyas (0xa1, 0xa2 y 0xa6, Scenes/JewelMix.h).
    JewelMenu,
    JewelChoice,
    JewelResult,
    // Un tramo de otra ola: corta con `address` y `wave`.
    Cut,
};

struct DialogRoute
{
    std::int32_t id = 0;
    DialogStep step = DialogStep::GenericOk;
    // El tramo del original y la ola que lo porta, para el corte.
    std::string_view address{};
    std::string_view wave{};
};

// La tabla de despacho de 0x623f78..0x6263d9 en el orden del original. Un id que no está va al OK genérico, como en
// el original, que lo alcanza después de todas las comparaciones.
[[nodiscard]] std::span<const DialogRoute> DialogRoutes();

// El tramo de `id`.
[[nodiscard]] DialogRoute DialogRouteOf(std::int32_t id);

// 0x623f78: con un diálogo abierto, su tramo. Sin ninguno, nada. `escape` es [ebp − 0x7d], el flanco de Esc de
// 0x623eb5 (falso con la ventana de amigos abierta), que leen los de sí y no (0x628409).
[[nodiscard]] Core::Expected<void, std::string> StepTheOpenDialog(const GameUiContext& context, bool escape);

// 0x61c210 hasta el paquete: los 24 bytes de su pila. Abre y cierra el registro propio (0xf08 rand() de la tabla de
// protección, 0x61c26a..0x61c34f) y, para cada lugar `at` de 0 a 0x71 con tecla (+0x26c + elegido · 0x100 + at no es
// 0xff), escribe el byte de la skill (+0x6c + at) en [tecla]. Después, en [10], OR de 1 o 2 (el ataque automático),
// de 4 u 8 (el sonido de los avisos) y de 0x10 sin carteles; en [11..13], el byte bajo de Q, W y E (0x6c8e40..48), y
// en [14] los renglones del registro / 3 (0 con su lugar en 0) en el nibble alto y, en el bajo, la transparencia · 10
// truncada (0x69fd3c). Sin elegido, o con una tecla de 24 o más (pisaría la pila), corta con error.
[[nodiscard]] Core::Expected<Protocol::OptionData::OptionBody, std::string>
OptionBodyOf(WorldState& world, Render::Models::CrtRandom& random, std::optional<std::uint8_t> chosenSlot);
} // namespace Client::Scenes
