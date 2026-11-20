#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Scenes/NpcNames.h"

#include <cstdint>
#include <optional>
#include <string>

// 0x5b1c70(tipo, nivel, buffer): el nombre corto de un ítem por su tipo y su nivel, el que usan el aviso del 22
// (0x4d1019) y otros mensajes. La fila de la matriz la da al hito 11; la porta el hito 5, que la necesita primero
// (decisión 5 del plan de la CL9).
namespace Client::Data::Text
{
class TextTable;
}

namespace Client::Scenes
{
// El monstruo del anillo de transformación (0x1a0a) de cada nivel: el dword de 0x6c9338 + nivel · 4, que leen
// 0x5b21bb, 0x5b41dc y 0x5bb2ed. Del 0 al 5, los de los seis anillos (2, 7, 14, 8, 9 y 41); del 6 al 10, los bytes de
// la tabla que sigue en la .data (0x6c9350, los nombres del 0x1c0c de nivel 2): del 6 al 9 no entran en 16 bits y no
// coinciden con ningún tipo, y el del 10 es 0xc7b1, que NpcName.txt no tiene; del 11 al 15, el 0. Fuera de 0..15, lo
// que MuRet no tiene de la .data: nada.
[[nodiscard]] std::optional<std::uint32_t> TransformationMonsterOf(std::int32_t level);

// Lo que 0x5b1c70 escribe en el buffer. Los casos que no escriben nada (un nivel sin nombre de los tipos con un nombre
// por nivel, o el 0x1a0a sin su monstruo en la tabla) dan el texto vacío, como el buffer que 0x4d0fdd pone en cero
// antes. El 0x1a0a (0x5b21a6..0x5b21ef) recorre los 512 registros de `npcNames` (0x7681eb8) sin cortar en el primero:
// queda el último con el tipo de TransformationMonsterOf, "%s %s" con su nombre y el 103; sin `npcNames`, corta. El
// 0x1c0c con nivel 2 (0x5b1d52, la tabla de 100 bytes de 0x6c9350 por +0x2d del registro) corta con su dirección. Un
// texto o un registro que no está corta.
[[nodiscard]] Core::Expected<std::string, std::string> ItemNameOf(const Data::Tables::ItemTable* items,
                                                                  const Data::Text::TextTable* texts,
                                                                  const NpcNameTable* npcNames, std::uint16_t type,
                                                                  std::int32_t level);

// Falso en los casos en que 0x5b1c70 no escribe nada en el buffer: un nivel sin nombre de los tipos con un nombre por
// nivel y el 0x1a0a sin un registro de su monstruo en `npcNames` (los que ItemNameOf da vacíos; sin `npcNames`, sí,
// y ItemNameOf corta). Quien no pone el buffer en cero antes, como 0x41b810 (Q-C874), lee la pila.
[[nodiscard]] bool ItemNameIsWritten(std::uint16_t type, std::int32_t level, const NpcNameTable* npcNames);
} // namespace Client::Scenes
