#include "Tables/FilterWords.h"

#include "Core/Expected.h"
#include "Crypto/Xor3.h"
#include "Data/DataError.h"
#include "Data/Tables/TableChecksum.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::Tables
{
Core::Expected<std::vector<std::string>, Data::DataError> LoadFilterWords(std::span<const std::byte> file,
                                                                          std::uint32_t checksumKey)
{
    const std::optional<std::uint32_t> stored = Data::Tables::StoredChecksum(file, FilterTableSize);
    if (!stored)
    {
        return Core::Unexpected(Data::DataError::FileTooShort);
    }
    const std::span<const std::byte> table = file.first(FilterTableSize);
    if (*stored != Data::Tables::TableChecksum(table, checksumKey, Data::Tables::ChecksumParity::XorOddDwords))
    {
        return Core::Unexpected(Data::DataError::ChecksumMismatch);
    }
    std::vector<std::string> words;
    for (std::size_t first = 0; first < FilterTableSize; first += FilterRecordSize)
    {
        std::array<std::byte, FilterRecordSize> record{};
        std::ranges::copy(table.subspan(first, FilterRecordSize), record.begin());
        Crypto::Xor3(record);
        if (record[0] == std::byte{0})
        {
            return words;
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
    // Without an empty record, 0x549fa0 exits at the cap (0x54a139) without storing the count, which stays 0: no words.
    return std::vector<std::string>{};
}
} // namespace Client::Tables
