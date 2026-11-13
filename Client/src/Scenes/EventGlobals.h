#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Los globales que escriben los paquetes de los eventos (CL9 hito 11, 04 § Los eventos) y que no son del objeto de
// [0x723c60] (EventMapObject) ni de Crywolf (CrywolfGlobals). Arrancan como la .data y la .bss del original.
namespace Client::Scenes
{
// 0x6c9dd8, 0x6c9dd4, 0x6c9ddc y 0x6c9de0 (.data −1, −1, −1 y −1,0f; 0x5ff910 los vuelve a poner así): el derrumbe de
// objetos que pide 0x5ff930(mapa, tipo, cuenta, demora) y que hace 0x5ff960 con cada objeto vivo del paso (0x606560),
// solo con los tres enteros en 0 o más y el mapa en curso igual. 0x606560 le resta uno a la cuenta al final de cada
// paso, también en −1.
struct ObjectCollapse
{
    std::int32_t map = -1;
    std::int32_t type = -1;
    std::int32_t counter = -1;
    float delay = -1.0f;
};

// 0x57678d0..0x57678e2 y 0x57678b4, 0x57678b8, 0x576796f (.bss 0): lo que deja el F3 23 (0x4d9f00), el tanteador de
// Battle Soccer que leen 0x5a7640 (la guerra de gremios, hito 8) y 0x50ba30.
struct SoccerScoreGlobals
{
    // 0x57678d0: los 8 bytes del paquete y un '\0' en el noveno (0x4d9f34).
    std::array<char, 9> firstName{};
    // 0x57678d9: ídem (0x4d9f7e).
    std::array<char, 9> secondName{};
    // 0x57678b4 y 0x57678b8, de pkt[0xc] y pkt[0x15].
    std::int32_t firstScore = 0;
    std::int32_t secondScore = 0;
    // 0x576796f: pkt[0xc] distinto de 0xff.
    bool active = false;
};

struct EventGlobals
{
    ObjectCollapse objectCollapse{};
    SoccerScoreGlobals soccer{};
    // 0x5767814 y 0x5767816 (.bss 0): el F3 22 (0x4e31f8), que lee 0x5a7eb0.
    std::uint8_t timeKind = 0;
    std::uint16_t timeValue = 0;
    // 0x6c9328 (dword) y 0x6c932c (word), .data todo en 0xff: los tres números del Muto del 94 con el tipo 0 y del
    // 96 (0x4dd8f0, 0x4e4a22), que lee la ventana de 0x78abb00.
    std::array<std::uint16_t, 3> mutoNumbers{0xffff, 0xffff, 0xffff};
    // 0x78ab078 (.bss 0): los 0x40 bytes del 9D (0x4e4ab9), que 0x94 con el tipo 2 vacía (0x4dd8f5).
    std::array<std::byte, 0x40> serial{};
    // +0x154 del recorrido de la cámara ([0x6e5904], CCameraMove): la etapa de Crywolf que deja el BD 00 con 0x4033b0
    // después de 0x42fe80, y que leen 0x4033d0, 0x4033f0 y 0x403590 (el recorrido, CL9.5). Vacío hasta el primero:
    // el valor que le da 0x403300 es del recorrido, que MuRet no tiene.
    std::optional<std::int32_t> cameraMoveStage{};
    // 0x78ab9cc (.bss 0): los niveles de los seis cuadrados de Devil Square, {mínimo, máximo}, que escribe el dibujo de
    // su ventana (0x5f5a3f, con la tabla del Magic Gladiator y del Dark Lord por la clase del registro propio) y lee
    // su paso al elegir uno (0x5e5a8b, Q-C1336).
    std::array<std::array<std::int32_t, 2>, 6> devilSquareLevels{};
};
} // namespace Client::Scenes
