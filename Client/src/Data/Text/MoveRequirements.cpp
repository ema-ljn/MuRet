#include "Data/Text/MoveRequirements.h"

#include "Core/Expected.h"
#include "Core/Xor3.h"
#include "Data/DataError.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

namespace Client::Data::Text
{
namespace
{
constexpr std::size_t NameOffset = 4;
constexpr std::size_t SearchNameOffset = 0x24;
constexpr std::size_t LevelOffset = 0x44;
constexpr std::size_t ZenOffset = 0x48;
constexpr std::size_t GateOffset = 0x4c;

std::uint32_t ReadWord(std::span<const std::byte> bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4 && offset + i < bytes.size(); ++i)
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
    return value;
}
} // namespace

Core::Expected<std::vector<MoveRequirement>, DataError> LoadMoveRequirements(std::span<const std::byte> file)
{
    // 0x448467..0x448489: el int en 0 y fread(&cuenta, 4, 1).
    const auto count = static_cast<std::int32_t>(ReadWord(file, 0));
    std::vector<MoveRequirement> moves;
    if (count <= 0)
        return moves;
    const auto records = static_cast<std::size_t>(count);
    if ((file.size() - std::min(MoveCountSize, file.size())) / MoveRecordSize < records)
        return Core::Unexpected(DataError::FileTooShort);
    moves.reserve(records);
    for (std::size_t i = 0; i < records; ++i)
    {
        // 0x44849c..0x4484b3: operator new(0x50), fread de 0x50 y 0x449770 sobre ellos.
        std::array<std::byte, MoveRecordSize> record{};
        const std::span<const std::byte> source = file.subspan(MoveCountSize + i * MoveRecordSize, MoveRecordSize);
        std::ranges::copy(source, record.begin());
        Core::Xor3(record);
        const std::span<const std::byte> bytes(record);
        const auto end = std::ranges::find(bytes.subspan(NameOffset), std::byte{0});
        if (end == bytes.end())
            return Core::Unexpected(DataError::InvalidData);
        MoveRequirement move;
        move.index = static_cast<std::int32_t>(ReadWord(bytes, 0));
        std::ranges::transform(bytes.begin() + static_cast<std::ptrdiff_t>(NameOffset), end,
                               std::back_inserter(move.name),
                               [](std::byte letter) { return static_cast<char>(letter); });
        std::ranges::transform(bytes.subspan(SearchNameOffset, MoveNameSize), move.searchName.begin(),
                               [](std::byte letter) { return static_cast<char>(letter); });
        move.level = static_cast<std::int32_t>(ReadWord(bytes, LevelOffset));
        move.zen = ReadWord(bytes, ZenOffset);
        move.gate = static_cast<std::int32_t>(ReadWord(bytes, GateOffset));
        moves.push_back(std::move(move));
    }
    return moves;
}
} // namespace Client::Data::Text
