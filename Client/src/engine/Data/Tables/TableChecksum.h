#pragma once

#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// The checksum of Item.bmd and Skill.bmd (Client/docs/research/05-formatos-de-datos.md, "Tablas .bmd"; computed by
// the loaders at 0x54b9e0 and 0x54acf0). It follows the table in the file and is computed over the table as stored,
// encrypted. Unsigned 32-bit arithmetic that wraps like the original's; bytes after the last whole dword do not count.
namespace Client::Data::Tables
{
enum class ChecksumParity
{
    XorOddDwords,  // Item.bmd
    XorEvenDwords, // Skill.bmd
};

// (05 § Tablas .bmd) It seeds `key << 9` and is added again on every mix: another key gives another checksum for the
// same bytes.
inline constexpr std::uint32_t ItemChecksumKey = 0xE2F1;
inline constexpr std::uint32_t SkillChecksumKey = 0x5A18;

[[nodiscard]] ENGINE_API std::uint32_t TableChecksum(std::span<const std::byte> bytes, std::uint32_t key,
                                                     ChecksumParity parity);

// The little-endian dword after the `recordsSize` bytes of records, or std::nullopt if `file` is shorter. Anything
// after it does not count, as with the original's 4-byte fread.
[[nodiscard]] ENGINE_API std::optional<std::uint32_t> StoredChecksum(std::span<const std::byte> file,
                                                                     std::size_t recordsSize);
} // namespace Client::Data::Tables
