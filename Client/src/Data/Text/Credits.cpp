#include "Data/Text/Credits.h"

#include "Core/Expected.h"
#include "Core/Xor3.h"
#include "Data/DataError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::Data::Text
{
Core::Expected<std::vector<CreditEntry>, DataError> LoadCredits(std::span<const std::byte> file)
{
    if (file.size() < CreditFileSize)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }
    std::vector<std::byte> table(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(CreditFileSize));
    Core::Xor3(table);
    std::vector<CreditEntry> credits;
    credits.reserve(CreditRecordCount);
    for (std::size_t first = 0; first < CreditFileSize; first += CreditRecordSize)
    {
        CreditEntry entry{.kind = std::to_integer<std::uint8_t>(table[first]), .text = {}};
        const std::span<const std::byte> text = std::span(table).subspan(first + 1, CreditRecordSize - 1);
        for (const std::byte letter : text)
        {
            if (letter == std::byte{0})
            {
                break;
            }
            entry.text.push_back(static_cast<char>(letter));
        }
        credits.push_back(std::move(entry));
    }
    return credits;
}

std::uint8_t CreditKindAt(std::span<const CreditEntry> credits, std::size_t index)
{
    return index < credits.size() ? credits[index].kind : static_cast<std::uint8_t>(CreditKind::End);
}
} // namespace Client::Data::Text
