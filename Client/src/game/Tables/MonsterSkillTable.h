#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>

// Data\Local\MonsterSkill.bmd, [0x7437670]'s monster skills (05-formatos-de-datos.md, "MonsterSkill.bmd"; in the
// original 0x54a440, which the game data load, 0x61ccb0, calls at 0x61ecdf with "Data\Local\MonsterSkill.bmd",
// 0x6d29fc). 0x54a440 sets only type 0's six dwords to −1 (0x54a44f..0x54a461); the rest of the table is .bss, at 0.
// It reads the count in the clear (an int32) and count records of 0x1c with fread into an operator new block, runs
// XOR3 from its first byte (0x5496c0, 0x6c8c74's key) and copies the five skills to 0x7437670 + type · 0x18 and the
// sixth int32 to 0x7437684 + type · 0x18 (0x54a4b5..0x54a4d6). It checks neither how much it read nor the type: a
// short file leaves the previous record in the block (FileTooShort in MuRet) and a type outside the table writes
// outside it (InvalidData in MuRet, outside [0, 0xffff], what fits in +0x376). Without the file it shows a MessageBox
// and closes the window (0x54a500..0x54a549). *Verified* with the repo's: 228 bytes, 8 records, types 0x130, 0x131,
// 0x135, 0x154, 0x155, 0x158, 0x159 and 0x15d. Read by 0x4f92f0.
namespace Client::Tables
{
inline constexpr std::size_t MonsterSkillRecordSize = 0x1c;
inline constexpr std::size_t MonsterSkillCount = 5;

// At 0x7437670 + type · 0x18.
struct MonsterSkills
{
    std::array<std::int32_t, MonsterSkillCount> skills{};
    // +0x14: the record's last int32; who reads it is not known yet.
    std::int32_t dword14 = 0;
};

struct MonsterSkillTable
{
    // With each type's last record (0x54a440 copies them in order).
    std::map<std::uint16_t, MonsterSkills> records;

    // The file's; without it the .bss (0), except type 0, at −1.
    [[nodiscard]] MonsterSkills Of(std::uint16_t kind) const;
};

// Like 0x54a440: the count and count records; none with a count of 0 or less. FileTooShort if bytes are missing for
// the count or a record; InvalidData for a type outside [0, 0xffff]. The excess is ignored.
Core::Expected<MonsterSkillTable, Data::DataError> LoadMonsterSkillTable(std::span<const std::byte> file);
} // namespace Client::Tables
