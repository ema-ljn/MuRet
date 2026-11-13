#pragma once

#include "Core/Expected.h"
#include "Protocol/JewelMix.h"

#include <string>

// Las respuestas del BC de la mezcla de joyas (CL9 hito 6, fase 4b; 04 § Las ventanas de los NPC, "La mezcla de
// joyas"): el caso BC del dispatcher (0x4e4547) con pkt[3] en 0 (juntar, en línea en 0x4e4570 con la tabla 0x4e60a8)
// o en 1 (deshacer, 0x4da540 con la tabla 0x4da620). 0x4da460 es el gemelo del 0 como función aparte, con los mismos
// textos y la tabla 0x4da524, y nadie la llama (*estático*: ni CALL ni puntero en el binario). El GameServer 0.97 no
// manda el BC (P-C341): el ASM es el oráculo. Ninguna saca rand(): los mensajes (0x555190), los diálogos (0x6236f0)
// y la vuelta (0x402e50) no tocan la tabla de protección, y el C1 03 31 de 0x402eb0 sale en claro.
namespace Client::Scenes
{
struct WorldEntryResources;

// Con pkt[4] fuera de la tabla (más de 5 con el 0, más de 8 con el 1) o pkt[3] distinto de 0 y 1, nada. Si no, según
// el resultado:
//   - BC 00 con 0, 2 o 3 y BC 01 con 0 o 5: "%s%s %s" (0x6c8490 y 0x6c8498) con los textos 1801 (juntar) o 1800
//     (deshacer), 1816 y 868 en un buffer de 0x100 bytes; el mensaje de tipo 1 y 0x402e50. Si no entra en el buffer,
//     corta (el sprintf del original pisaría la pila).
//   - 1: 0x6236f0(0) y 0x6236f0(0xa6), el diálogo del resultado.
//   - BC 00 con 4 (1817) o 5 (1811), y BC 01 con 2, 3, 4 o 6 (1812), 7 (1815) u 8 (1811): el texto como mensaje de
//     tipo 1 y 0x402e50.
// El nombre de los mensajes es una cadena vacía de la .bss (0x5767a54..0x5767a6c, que nadie escribe).
[[nodiscard]] Core::Expected<void, std::string> ApplyJewelMixAnswer(const Protocol::JewelMix::JewelMixAnswer& answer,
                                                                    const WorldEntryResources& resources);
} // namespace Client::Scenes
