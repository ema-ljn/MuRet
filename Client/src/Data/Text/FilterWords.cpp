#include "Data/Text/FilterWords.h"

#include "Core/Expected.h"
#include "Core/Xor3.h"
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

namespace Client::Data::Text
{
Core::Expected<std::vector<std::string>, DataError> LoadFilterWords(std::span<const std::byte> file,
                                                                    std::uint32_t checksumKey)
{
    const std::optional<std::uint32_t> stored = Tables::StoredChecksum(file, FilterTableSize);
    if (!stored)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }
    const std::span<const std::byte> table = file.first(FilterTableSize);
    if (*stored != Tables::TableChecksum(table, checksumKey, Tables::ChecksumParity::XorOddDwords))
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
    // Sin un registro vacío, 0x549fa0 sale por el tope (0x54a139) sin guardar la cuenta, que queda en
    // 0: ninguna palabra.
    return std::vector<std::string>{};
}
} // namespace Client::Data::Text
