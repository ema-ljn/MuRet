#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// 0x7681eb8: los nombres de los monstruos y los NPC, 512 registros de 0x36 bytes en la .bss (hasta 0x7688ab8): el tipo
// (un word) y el nombre. Los llena la carga de los datos (0x61ed05) con 0x54e7f0 desde Data\Local\NpcName(%s).txt, que
// MuRet no porta todavía (08, P-C174): la tabla queda en 0, como la del original sin el archivo (CL9 hito 2).
namespace Client::Scenes
{
inline constexpr std::size_t NpcNameCount = 512;
// 0x36 bytes menos el word del tipo.
inline constexpr std::size_t NpcNameBytes = 0x34;

struct NpcName
{
    // +0: el tipo del monstruo o del NPC.
    std::uint16_t type = 0;
    // +2: el nombre, tal cual.
    std::array<char, NpcNameBytes> name{};
};

// NpcNameCount registros en el montón (27 KB).
using NpcNameTable = std::vector<NpcName>;

// 0x54ebb0(tipo): el nombre del primer registro cuyo tipo (el word, sin signo, contra el dword que le pasan) es `type`,
// o nada (el NULL del original) si ninguno lo es. Con la tabla en 0, el tipo 0 da el nombre vacío del primer registro y
// cualquier otro, nada. El nombre va hasta su primer '\0'; sin él, MuRet corta en los 0x34 bytes (el original seguiría
// en el registro siguiente).
[[nodiscard]] std::optional<std::string_view> NpcNameOf(const NpcNameTable& table, std::int32_t type);
} // namespace Client::Scenes
