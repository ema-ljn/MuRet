#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// FilterName.bmd and Filter.bmd, the words a new character's name cannot have (05-formatos-de-datos.md, "Tablas
// .bmd"; in the original 0x549fa0 and 0x549c60, from the game data load, 0x61ccb0). 20,000 bytes in 1,000 records of
// 20, each with Text.bmd's XOR3 from its first byte (0x5496c0 per record), and the tables' checksum of the 20,000
// encrypted bytes at the end. Reading stops at the first record that starts with zero. *Verified*: both lab checksums
// match, FilterName has six words and Filter none.
namespace Client::Tables
{
inline constexpr std::size_t FilterRecordSize = 20;
inline constexpr std::size_t FilterTableSize = 20000;

// Data::Tables::TableChecksum with Item.bmd's parity: the seed 0x578200 and 0x7cfa00 are the key << 9.
inline constexpr std::uint32_t FilterNameChecksumKey = 0x2bc1; // 0x549fa0
inline constexpr std::uint32_t FilterChecksumKey = 0x3e7d;     // 0x549c60

// Up to the first empty record; each word up to its first zero, or 20 bytes if it fills them. Without an empty
// record, none (the original does not store the count). FileTooShort below 20,004 bytes; ChecksumMismatch if the
// checksum is not `checksumKey`'s (the original shows "File corrupted" and closes). The excess is ignored.
[[nodiscard]] Core::Expected<std::vector<std::string>, Data::DataError> LoadFilterWords(std::span<const std::byte> file,
                                                                                        std::uint32_t checksumKey);
} // namespace Client::Tables
