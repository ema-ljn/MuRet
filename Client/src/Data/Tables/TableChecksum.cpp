#include "Data/Tables/TableChecksum.h"

#include "Data/ByteReader.h"

#include <optional>

namespace
{

using Client::Data::Tables::ChecksumParity;

constexpr unsigned SeedShift = 9;
constexpr std::size_t MixPeriod = 4;    // cada cuántos dwords se mezcla
constexpr std::size_t MixShiftMask = 7; // corre (índice & 7) + 1 bits: 1 o 5, con MixPeriod 4

bool UsesXor(std::size_t index, ChecksumParity parity)
{
    const bool odd = index % 2 == 1;
    return parity == ChecksumParity::XorOddDwords ? odd : !odd;
}

} // namespace

namespace Client::Data::Tables
{

std::uint32_t TableChecksum(std::span<const std::byte> bytes, std::uint32_t key, ChecksumParity parity)
{
    std::uint32_t accumulator = key << SeedShift;
    ByteReader reader(bytes);
    std::size_t index = 0;
    while (const std::optional<std::uint32_t> word = reader.ReadU32Le())
    {
        accumulator = UsesXor(index, parity) ? accumulator ^ *word : accumulator + *word;
        if (index % MixPeriod == 0)
        {
            const std::size_t shift = (index & MixShiftMask) + 1;
            accumulator ^= (accumulator + key) >> shift;
        }
        ++index;
    }
    return accumulator;
}

} // namespace Client::Data::Tables
