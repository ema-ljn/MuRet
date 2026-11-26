#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Tables/RecordTable.h"

#include <cstddef>
#include <span>

// Skill.bmd (05-formatos-de-datos.md, "Tablas .bmd"; in the original 0x54acf0's loader), from Data\Local\Skill.bmd:
// 256 XOR3 records of 0x3C bytes, then the checksum of the encrypted records (TableChecksum.h, key 0x5A18). Unlike
// Item's, the original does check it. Q-C08: the original keeps the records in a "guard" container that, while
// nobody uses them, leaves rand() bytes in their place. An anti-cheat measure, not ported: MuRet keeps the table plain.
namespace Client::Tables
{
// The original always reads 256.
inline constexpr std::size_t SkillCount = 256;
// In the file and in the original's memory.
inline constexpr std::size_t SkillRecordSize = 0x3C;
// In the file the checksum's 4 bytes follow.
inline constexpr std::size_t SkillTableSize = SkillCount * SkillRecordSize;
static_assert(SkillTableSize == 0x3C00, "el tamaño que mide el research (05, Tablas .bmd)");

using SkillTable = Data::Tables::RecordTable<SkillRecordSize>;

// ChecksumMismatch and no table if the checksum does not match, as in the original ("File corrupted."); FileTooShort if
// `file` lacks the records and the checksum's 4 bytes (Q-C24); a longer file's rest is ignored, like the original's
// fixed-size freads.
Core::Expected<SkillTable, Data::DataError> LoadSkillTable(std::span<const std::byte> file);
} // namespace Client::Tables
