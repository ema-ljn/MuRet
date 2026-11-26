#pragma once

#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <span>

// The CRC-32 0x683bd0 signs the executable with in MuError.dmp: the usual one (zlib's, PNG's), reflected polynomial
// 0xEDB88320 (the 0x683e00 table), starting at 0xFFFFFFFF (0x683cea) and negated at the end (0x683d72). Fed in chunks,
// as the original walks the mapped file 10 MB at a time, with no allocation: the table is constexpr. Own and not zlib:
// it is a few lines and the client does not link zlib (rule 10).
namespace Client::Diagnostics
{
class ENGINE_API Crc32
{
public:
    void Update(std::span<const std::byte> bytes);
    [[nodiscard]] std::uint32_t Value() const;

private:
    std::uint32_t m_state = 0xFFFFFFFF;
};
} // namespace Client::Diagnostics
