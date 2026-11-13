#include "Data/Tables/MonsterSkillTable.h"

#include "Core/Expected.h"
#include "Core/Xor3.h"
#include "Data/DataError.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Data::Tables
{
namespace
{
constexpr std::size_t CountSize = 4;
constexpr std::int32_t LastKind = 0xffff;

std::int32_t Int32At(std::span<const std::byte> bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + i])) << (8 * i);
    return static_cast<std::int32_t>(value);
}
} // namespace

MonsterSkills MonsterSkillTable::Of(std::uint16_t kind) const
{
    if (const auto found = records.find(kind); found != records.end())
        return found->second;
    MonsterSkills empty;
    // 0x54a44f..0x54a461: los seis dwords desde 0x7437670, el tipo 0.
    if (kind == 0)
    {
        empty.skills.fill(-1);
        empty.dword14 = -1;
    }
    return empty;
}

Core::Expected<MonsterSkillTable, DataError> LoadMonsterSkillTable(std::span<const std::byte> file)
{
    if (file.size() < CountSize)
        return Core::Unexpected(DataError::FileTooShort);
    // 0x54a490: la cuenta, en claro; 0x54a49b..0x54a4a0 la compara con signo.
    const std::int32_t count = Int32At(file, 0);
    MonsterSkillTable table;
    for (std::int32_t i = 0; i < count; ++i)
    {
        const std::size_t offset = CountSize + static_cast<std::size_t>(i) * MonsterSkillRecordSize;
        if (file.size() < offset + MonsterSkillRecordSize)
            return Core::Unexpected(DataError::FileTooShort);
        // 0x54a4a8..0x54a4b0: fread de 0x1c en el bloque y 0x5496c0 sobre él.
        std::array<std::byte, MonsterSkillRecordSize> block{};
        std::ranges::copy(file.subspan(offset, MonsterSkillRecordSize), block.begin());
        Core::Xor3(block);
        const std::span<const std::byte> bytes(block);
        const std::int32_t kind = Int32At(bytes, 0);
        if (kind < 0 || kind > LastKind)
            return Core::Unexpected(DataError::InvalidData);
        MonsterSkills& skills = table.records[static_cast<std::uint16_t>(kind)];
        for (std::size_t s = 0; s < MonsterSkillCount; ++s)
            skills.skills[s] = Int32At(bytes, 4 + 4 * s);
        skills.dword14 = Int32At(bytes, 4 + 4 * MonsterSkillCount);
    }
    return table;
}
} // namespace Client::Data::Tables
