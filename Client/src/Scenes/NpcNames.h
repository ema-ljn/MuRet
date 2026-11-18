#pragma once

#include "Data/Text/NpcNames.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// 0x7681eb8: los nombres de los monstruos y los NPC, 512 registros de 0x36 bytes en la .bss (hasta 0x7688ab8): el tipo
// (un word) y el nombre. Los llena la carga de los datos (0x61ed05) con 0x54e7f0 desde Data\Local\NpcName(%s).txt
// (Data::Text::LoadNpcNames): main.cpp copia los de GameData::npcNames al principio y deja el resto en 0, como el
// original sin el archivo (CL9 hito 2, P-C174).
namespace Client::Scenes
{
using Data::Text::NpcNameBytes;
using Data::Text::NpcNameCount;
using NpcName = Data::Text::NpcNameRecord;

// NpcNameCount registros en el montón (27 KB).
using NpcNameTable = std::vector<NpcName>;

// La tabla de 0x7681eb8 después de 0x54e7f0: `loaded` (los registros de LoadNpcNames, que no pasan de 512) desde el
// primero y el resto en 0.
[[nodiscard]] NpcNameTable NpcNameTableOf(std::span<const NpcName> loaded);

// 0x54ebb0(tipo): el nombre del primer registro cuyo tipo (el word, sin signo, contra el dword que le pasan) es `type`,
// o nada (el NULL del original) si ninguno lo es. Con la tabla en 0, el tipo 0 da el nombre vacío del primer registro y
// cualquier otro, nada. El original devuelve el puntero a +2 del registro: el nombre va hasta su primer '\0' y, sin él
// en sus 0x34 bytes (un nombre largo de LoadNpcNames), sigue por el tipo y el nombre de los registros que siguen. Sin
// '\0' hasta el final de la tabla (LoadNpcNames no lo deja), lo que hay hasta ahí.
[[nodiscard]] std::optional<std::string> NpcNameOf(const NpcNameTable& table, std::int32_t type);
} // namespace Client::Scenes
