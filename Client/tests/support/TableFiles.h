#pragma once

#include "Data/Tables/RecordTable.h"
#include "Crypto/Xor3.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Helpers for the tests of the fixed-length-record .bmd tables (Gate, Item, Skill): build a synthetic file as the
// original writes it, and compare a loaded table with the original's memory.
namespace Client::Tests
{
// What the file would hold: each record encrypted with XOR3 from its start, like the original's writer. The bytes
// that do not reach a whole record stay as they are.
inline std::vector<std::byte> EncryptRecords(std::vector<std::byte> records, std::size_t recordSize)
{
    const std::span<std::byte> all(records);
    for (std::size_t offset = 0; recordSize <= all.size() - offset; offset += recordSize)
    {
        Crypto::Xor3(all.subspan(offset, recordSize));
    }
    return records;
}

// `encrypted` with a checksum after it, in 4 little-endian bytes, as the original's writer stores it after the
// records.
inline std::vector<std::byte> WithStoredChecksum(std::vector<std::byte> encrypted, std::uint32_t checksum)
{
    constexpr unsigned BitsPerByte = 8;
    for (unsigned byte = 0; byte < sizeof(checksum); ++byte)
    {
        encrypted.push_back(static_cast<std::byte>(checksum >> (BitsPerByte * byte)));
    }
    return encrypted;
}

// The first record where `table` and `memory` differ, or std::nullopt if they have the same records. If one lacks
// records the other has, the first missing one counts as different: an empty table is not equal to a full memory.
template <std::size_t RecordSize>
std::optional<std::size_t> FirstRecordThatDiffers(const Data::Tables::RecordTable<RecordSize>& table,
                                                  std::span<const std::byte> memory)
{
    for (std::size_t index = 0; index < table.Count(); ++index)
    {
        const auto record = table.Record(index);
        const std::size_t offset = index * RecordSize;
        if (!record || memory.size() < offset + RecordSize ||
            !std::ranges::equal(*record, memory.subspan(offset, RecordSize)))
        {
            return index;
        }
    }
    if (memory.size() != table.Count() * RecordSize)
    {
        return table.Count();
    }
    return std::nullopt;
}
} // namespace Client::Tests
