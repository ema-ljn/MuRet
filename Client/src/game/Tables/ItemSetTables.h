#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Tables/ItemTable.h"
#include "Data/Tables/RecordTable.h"

#include <cstddef>
#include <cstdint>
#include <span>

// ItemSetType.bmd and ItemSetOption.bmd (05-formatos-de-datos.md, "Las tablas de los conjuntos"). In the original
// 0x40c600 (0x40c660 and 0x40c850) loads them into the static object at 0x6e5930, which [0x6eed3c] points to: the
// types from +4 and the sets from +0x8004. The *Test.bmd variants are 0x40c600's with a non-zero argument, which the
// original does not use (0x61ecd5 calls it with 0). Each file brings its XOR3 records and then the checksum of the
// encrypted records (TableChecksum.h, with Item.bmd's parity). The original does check it: on a mismatch it shows
// "File corrupted." and leaves the table alone.
namespace Client::Tables
{
// One 4-byte record per item type, the same index as Item.bmd. Bytes 0 to 2 are the item's set with byte +0x1c & 3 at
// 1, 2 or 3 (0x40cfa0); 0 or 0xff, none.
inline constexpr std::size_t ItemSetTypeCount = ItemCount;
inline constexpr std::size_t ItemSetTypeRecordSize = 4;
inline constexpr std::size_t ItemSetTypeTableSize = ItemSetTypeCount * ItemSetTypeRecordSize;
static_assert(ItemSetTypeTableSize == 0x8000, "lo que lee 0x40c660 (05, Las tablas de los conjuntos)");
// 0x40c6bb and 0x40c6ea.
inline constexpr std::uint32_t ItemSetTypeChecksumKey = 0xE5F1;

// 64 sets of 0x4c bytes: the name, the options by piece count, the full set's and the class marks (06, "Los stats del
// registro propio").
inline constexpr std::size_t ItemSetOptionCount = 0x40;
inline constexpr std::size_t ItemSetOptionRecordSize = 0x4c;
inline constexpr std::size_t ItemSetOptionTableSize = ItemSetOptionCount * ItemSetOptionRecordSize;
static_assert(ItemSetOptionTableSize == 0x1300, "lo que lee 0x40c850 (05, Las tablas de los conjuntos)");
// 0x40c8ae and 0x40c8dd.
inline constexpr std::uint32_t ItemSetOptionChecksumKey = 0xA2F1;

using ItemSetTypeTable = Data::Tables::RecordTable<ItemSetTypeRecordSize>;
using ItemSetOptionTable = Data::Tables::RecordTable<ItemSetOptionRecordSize>;

// [0x6eed3c]'s two tables, decrypted.
struct ItemSetTables
{
    ItemSetTypeTable types;
    ItemSetOptionTable options;
};

// Like Skill.bmd: ChecksumMismatch and no table if the checksum does not match ("File corrupted."); FileTooShort if
// `file` lacks the records and the checksum's 4 bytes (Q-C24: the original would read anyway, with uninitialized heap
// in what is missing); a longer file's rest is ignored, like the original's fixed-size freads.
Core::Expected<ItemSetTypeTable, Data::DataError> LoadItemSetTypeTable(std::span<const std::byte> file);
Core::Expected<ItemSetOptionTable, Data::DataError> LoadItemSetOptionTable(std::span<const std::byte> file);
} // namespace Client::Tables
