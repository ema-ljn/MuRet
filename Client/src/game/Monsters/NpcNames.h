#pragma once

#include "Tables/NpcNames.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// 0x7681eb8: the monster and NPC names, 512 records of 0x36 bytes in .bss (up to 0x7688ab8): the type (a word) and the
// name. Filled by the data load (0x61ed05) with 0x54e7f0 from Data\Local\NpcName(%s).txt (Tables::LoadNpcNames):
// main.cpp copies those of GameData::npcNames at the start and leaves the rest at 0, like the original without the file
// (P-C174).
namespace Client::Monsters
{
using Tables::NpcNameBytes;
using Tables::NpcNameCount;
using NpcName = Tables::NpcNameRecord;

// NpcNameCount records on the heap (27 KB).
using NpcNameTable = std::vector<NpcName>;

// The 0x7681eb8 table after 0x54e7f0: `loaded` (the LoadNpcNames records, at most 512) from the first one and the rest
// at 0.
[[nodiscard]] NpcNameTable NpcNameTableOf(std::span<const NpcName> loaded);

// 0x54ebb0(type): the name of the first record whose type (the word, unsigned, against the dword passed in) is `type`,
// or nothing (the original's NULL) if none is. With the table at 0, type 0 gives the first record's empty name and any
// other, nothing. The original returns the pointer to +2 of the record: the name runs to its first '\0' and, without
// one in its 0x34 bytes (a long name from LoadNpcNames), on through the type and name of the following records. Without
// a
// '\0' up to the end of the table (LoadNpcNames does not leave that), whatever is there up to the end.
[[nodiscard]] std::optional<std::string> NpcNameOf(const NpcNameTable& table, std::int32_t type);

// The name at +2 of record `index`, as an original "%s" reads it: up to its first '\0' and, without one in its 0x34
// bytes, through the type and name of the following records, up to the end of the table. It is what NpcNameOf returns
// for the record it finds; the walkers that do not stop at the first one use it too (0x5b21a6).
[[nodiscard]] std::string NpcNameAt(const NpcNameTable& table, std::size_t index);
} // namespace Client::Monsters
