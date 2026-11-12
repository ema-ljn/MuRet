#include "Data/Text/SlideTable.h"

#include "Core/Expected.h"
#include "Core/Xor3.h"
#include "Data/DataError.h"

#include <algorithm>
#include <bit>
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
// 0x46b47b..0x46b4c9: el período y la velocidad, y desde +8 los tramos; en cada uno, el nivel y la cuenta.
constexpr std::size_t GroupsAt = 8;
constexpr std::size_t TextsInGroupAt = 8;

std::uint32_t DwordAt(const std::vector<std::byte>& bytes, std::size_t at)
{
    std::uint32_t value = 0;
    for (std::size_t i = 4; i-- > 0;)
        value = (value << 8U) | std::to_integer<std::uint32_t>(bytes[at + i]);
    return value;
}
} // namespace

Core::Expected<SlideTable, DataError> LoadSlideTable(std::span<const std::byte> file)
{
    if (file.size() < SlideTableSize)
        return Core::Unexpected(DataError::FileTooShort);
    std::vector<std::byte> bytes(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(SlideTableSize));
    Core::Xor3(bytes);
    SlideTable table;
    table.periodSeconds = static_cast<std::int32_t>(DwordAt(bytes, 0));
    table.speed = std::bit_cast<float>(DwordAt(bytes, 4));
    for (std::size_t group = 0; group < SlideGroupCount; ++group)
    {
        const std::size_t base = GroupsAt + group * SlideGroupSize;
        SlideGroup& slides = table.groups[group];
        slides.maxLevel = static_cast<std::int32_t>(DwordAt(bytes, base));
        slides.count = static_cast<std::int32_t>(DwordAt(bytes, base + 4));
        for (std::int32_t text = 0; text < slides.count; ++text)
        {
            const std::size_t at = base + TextsInGroupAt + static_cast<std::size_t>(text) * SlideTextSize;
            if (at >= bytes.size())
                return Core::Unexpected(DataError::InvalidData);
            const auto first = bytes.begin() + static_cast<std::ptrdiff_t>(at);
            const auto end = std::find(first, bytes.end(), std::byte{0});
            if (end == bytes.end())
                return Core::Unexpected(DataError::InvalidData);
            std::string read;
            read.reserve(static_cast<std::size_t>(end - first));
            for (auto byte = first; byte != end; ++byte)
                read.push_back(std::to_integer<char>(*byte));
            slides.texts.push_back(std::move(read));
        }
    }
    return table;
}
} // namespace Client::Data::Text
