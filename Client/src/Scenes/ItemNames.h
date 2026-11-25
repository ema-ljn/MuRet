#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Scenes/NpcNames.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// 0x5b1c70(tipo, nivel, buffer): el nombre corto de un ítem por su tipo y su nivel, el que usan el aviso del 22
// (0x4d1019) y otros mensajes. La fila de la matriz la da al hito 11; la porta el hito 5, que la necesita primero
// (decisión 5 del plan de la CL9).
namespace Client::Tables
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

// Los premios del boleto (0x1c0c de nivel 2): la tabla de 0x6c9350, diez nombres de 100 bytes en coreano (EUC-KR) que
// leen con "%s" 0x5b1d60 (el índice es Item.bmd+0x2d) y 0x5b3835 (la durabilidad del ítem, D+0x1a). Los bytes son los
// de la .data del c32bbd5f, hasta su '\0'. Desde el 10, 0x6c9350 + i · 100 cae en la .data que sigue (strings de
// formato y globales que el juego cambia), que MuRet no tiene: nada.
[[nodiscard]] std::optional<std::string_view> EventTicketPrizeOf(std::uint32_t index);

// Lo que 0x5b1c70 escribe en el buffer. Los casos que no escriben nada (un nivel sin nombre de los tipos con un nombre
// por nivel, o el 0x1a0a sin su monstruo en la tabla) dan el texto vacío, como el buffer que 0x4d0fdd pone en cero
// antes. El 0x1a0a (0x5b21a6..0x5b21ef) recorre los 512 registros de `npcNames` (0x7681eb8) sin cortar en el primero:
// queda el último con el tipo de TransformationMonsterOf, "%s %s" con su nombre y el 103; sin `npcNames`, corta. El
// 0x1c0c con nivel 2 (0x5b1d52..0x5b1d6e) es el premio de EventTicketPrizeOf por el byte +0x2d de su registro; uno
// fuera de los diez corta. Un texto o un registro que no está corta.
[[nodiscard]] Core::Expected<std::string, std::string> ItemNameOf(const Tables::ItemTable* items,
                                                                  const Tables::TextTable* texts,
                                                                  const NpcNameTable* npcNames, std::uint16_t type,
                                                                  std::int32_t level);

// Falso en los casos en que 0x5b1c70 no escribe nada en el buffer: un nivel sin nombre de los tipos con un nombre por
// nivel y el 0x1a0a sin un registro de su monstruo en `npcNames` (los que ItemNameOf da vacíos; sin `npcNames`, sí,
// y ItemNameOf corta). Quien no pone el buffer en cero antes, como 0x41b810 (Q-C874), lee la pila.
[[nodiscard]] bool ItemNameIsWritten(std::uint16_t type, std::int32_t level, const NpcNameTable* npcNames);
} // namespace Client::Scenes
