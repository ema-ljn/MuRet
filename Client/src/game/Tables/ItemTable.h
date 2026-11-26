#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Tables/RecordTable.h"

#include <cstddef>
#include <span>

// Item.bmd (05-formatos-de-datos.md, "Tablas .bmd"; in the original 0x54b9e0's loader), from Data\Local\Item.bmd:
// 8192 XOR3 records of 0x54 bytes, then the checksum of the encrypted records (TableChecksum.h, key 0xE2F1). Q-C01:
// the original computes the checksum but does not check it (an EB 66 patch skips the comparison), and the lab's
// item.bmd fails its own. MuRet computes it, reports it and loads the table anyway.
namespace Client::Tables
{
// 16 sections of 512. An item's record is `section * 512 + index` (0054b9e0-item-bmd-loader.md §9).
inline constexpr std::size_t ItemCount = 8192;
// In the file and in the original's memory: name, damage, durability, requirements and the rest.
inline constexpr std::size_t ItemRecordSize = 0x54;
// In the file the checksum's 4 bytes follow.
inline constexpr std::size_t ItemTableSize = ItemCount * ItemRecordSize;
static_assert(ItemTableSize == 0xA8000, "el tamaño que mide el research (05, Tablas .bmd)");

using ItemTable = Data::Tables::RecordTable<ItemRecordSize>;

struct LoadedItemTable
{
    ItemTable table;
    // With false (no match, or the file does not reach it) the table still loads (Q-C01): the loader logs it.
    bool checksumMatches = false;
};

// FileTooShort if `file` lacks any record (Q-C24): the original would read anyway and leave whatever was in memory.
// With the records but not the checksum's 4 bytes it loads, with checksumMatches false: the original does not check
// the checksum either. A longer file's rest is ignored, like the original's fixed-size freads.
Core::Expected<LoadedItemTable, Data::DataError> LoadItemTable(std::span<const std::byte> file);
} // namespace Client::Tables
