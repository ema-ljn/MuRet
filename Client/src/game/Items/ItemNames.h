#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Monsters/NpcNames.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// 0x5b1c70(type, level, buffer): an item's short name by its type and its level, the one the 22's notice (0x4d1019)
// and other messages use. The matrix row gives it to milestone 11; milestone 5 ports it, since it needs it first
// (decision 5 of the CL9 plan).
namespace Client::Tables
{
class TextTable;
}

namespace Client::Items
{
// The transformation ring's (0x1a0a) monster for each level: the dword at 0x6c9338 + level · 4, which 0x5b21bb,
// 0x5b41dc and 0x5bb2ed read. From 0 to 5, the six rings' (2, 7, 14, 8, 9 and 41); from 6 to 10, the bytes of the
// table that follows in the .data (0x6c9350, the names of the 0x1c0c of level 2): 6 to 9 do not fit in 16 bits and
// match no type, and 10's is 0xc7b1, which NpcName.txt does not have; from 11 to 15, 0. Outside 0..15, what MuRet
// does not have of the .data: nothing.
[[nodiscard]] std::optional<std::uint32_t> TransformationMonsterOf(std::int32_t level);

// The ticket's prizes (0x1c0c of level 2): the table at 0x6c9350, ten 100-byte names in Korean (EUC-KR) that 0x5b1d60
// (the index is Item.bmd+0x2d) and 0x5b3835 (the item's durability, D+0x1a) read with "%s". The bytes are those of the
// c32bbd5f's .data, up to their '\0'. From 10 on, 0x6c9350 + i · 100 falls in the .data that follows (format strings
// and globals the game changes), which MuRet does not have: nothing.
[[nodiscard]] std::optional<std::string_view> EventTicketPrizeOf(std::uint32_t index);

// What 0x5b1c70 writes in the buffer. The cases that write nothing (a level without a name for the types with one name
// per level, or the 0x1a0a without its monster in the table) give the empty text, like the buffer 0x4d0fdd zeroes
// before. The 0x1a0a (0x5b21a6..0x5b21ef) walks the 512 records of `npcNames` (0x7681eb8) without stopping at the
// first: the last one with TransformationMonsterOf's type stays, "%s %s" with its name and the 103; without
// `npcNames`, it stops. The 0x1c0c with level 2 (0x5b1d52..0x5b1d6e) is EventTicketPrizeOf's prize by its record's
// byte +0x2d; one outside the ten stops. A missing text or record stops.
[[nodiscard]] Core::Expected<std::string, std::string> ItemNameOf(const Tables::ItemTable* items,
                                                                  const Tables::TextTable* texts,
                                                                  const Monsters::NpcNameTable* npcNames,
                                                                  std::uint16_t type, std::int32_t level);

// False in the cases where 0x5b1c70 writes nothing in the buffer: a level without a name for the types with one name
// per level and the 0x1a0a without a record of its monster in `npcNames` (the ones ItemNameOf gives empty; without
// `npcNames`, true, and ItemNameOf stops). Whoever does not zero the buffer before, like 0x41b810 (Q-C874), reads the
// stack.
[[nodiscard]] bool ItemNameIsWritten(std::uint16_t type, std::int32_t level, const Monsters::NpcNameTable* npcNames);
} // namespace Client::Items
