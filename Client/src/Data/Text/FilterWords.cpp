#include "Data/Text/FilterWords.h"

#include "Core/Expected.h"
#include "Core/Xor3.h"
#include "Data/DataError.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::Data::Text
{
namespace
{
constexpr std::size_t SumSize = 4;

std::uint32_t WordAt(std::span<const std::byte> bytes, std::size_t offset)
{
    std::uint32_t word = 0;
    for (std::size_t k = 0; k < SumSize; ++k)
    {
        word |= std::to_integer<std::uint32_t>(bytes[offset + k]) << (8 * k);
    }
    return word;
}
} // namespace

std::uint32_t FilterChecksum(std::span<const std::byte, FilterTableSize> table, FilterChecksumKey key)
{
    std::uint32_t sum = key.seed;
    for (std::uint32_t offset = 0; offset < FilterTableSize; offset += SumSize)
    {
        const std::uint32_t index = offset >> 2;
        // ((k - 1) & 1): 1, suma, con k par; 0, XOR, con k impar (0x549ff0).
        if (((index - 1) & 1) == 0)
        {
            sum ^= WordAt(table, offset);
        }
        else
        {
            sum += WordAt(table, offset);
        }
        if ((offset & 0xf) == 0)
        {
            sum ^= (sum + key.step) >> ((index & 7) + 1);
        }
    }
    return sum;
}

Core::Expected<std::vector<std::string>, DataError> LoadFilterWords(std::span<const std::byte> file,
                                                                    FilterChecksumKey key)
{
    if (file.size() < FilterTableSize + SumSize)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }
    const std::span<const std::byte, FilterTableSize> table = file.first<FilterTableSize>();
    if (WordAt(file, FilterTableSize) != FilterChecksum(table, key))
    {
        return Core::Unexpected(DataError::ChecksumMismatch);
    }
    std::vector<std::string> words;
    for (std::size_t first = 0; first < FilterTableSize; first += FilterRecordSize)
    {
        std::array<std::byte, FilterRecordSize> record{};
        std::ranges::copy(table.subspan(first, FilterRecordSize), record.begin());
        Core::Xor3(record);
        if (record[0] == std::byte{0})
        {
            break;
        }
        std::string word;
        for (const std::byte letter : record)
        {
            if (letter == std::byte{0})
            {
                break;
            }
            word.push_back(static_cast<char>(letter));
        }
        words.push_back(std::move(word));
    }
    return words;
}
} // namespace Client::Data::Text
