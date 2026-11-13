#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"

#include <cstdint>
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
// Lo que 0x5b1c70 escribe en el buffer. Los casos que no escriben nada (un nivel sin nombre de los tipos con un nombre
// por nivel) dan el texto vacío, como el buffer que 0x4d0fdd pone en cero antes. El 0x1c0c con nivel 2 (0x5b1d52, la
// tabla de 100 bytes de 0x6c9350 por +0x2d del registro) y el 0x1a0a (0x5b21a6, la tabla de los monstruos de
// 0x7681eb8) cortan con su dirección. Un texto o un registro que no está corta.
[[nodiscard]] Core::Expected<std::string, std::string> ItemNameOf(const Data::Tables::ItemTable* items,
                                                                  const Data::Text::TextTable* texts,
                                                                  std::uint16_t type, std::int32_t level);
} // namespace Client::Scenes
