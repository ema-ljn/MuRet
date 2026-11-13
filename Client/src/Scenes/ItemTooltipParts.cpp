#include "Scenes/ItemTooltipParts.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/SkillTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameHud.h"
#include "Scenes/TooltipWriter.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// D+0x1b: los bits de las opciones excelentes.
constexpr std::uint8_t ExcellentBits = 0x3f;
// D+0x1c & 3: la clase de pieza de un conjunto; 0, ninguna.
constexpr std::uint8_t SetKindMask = 3;
// En ItemSetType.bmd, ningún conjunto (0x40cf51 y 0x40cf55).
constexpr std::uint8_t NoSet = 0xff;
// 0x40cf72: el nombre se copia con 8 dwords.
constexpr std::size_t SetNameBytes = 0x20;
// Skill.bmd: el byte +0x21, el word +0x22 (el maná), el word +0x24 y el byte +0x26 (0x551e25..0x551e51).
constexpr std::size_t SkillByte21 = 0x21;
constexpr std::size_t SkillMana = 0x22;
constexpr std::size_t SkillStamina = 0x24;
constexpr std::size_t SkillRange = 0x26;

std::uint16_t WordAt(std::span<const std::byte, Data::Tables::SkillRecordSize> entry, std::size_t offset)
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint32_t>(entry[offset]) |
                                      (std::to_integer<std::uint32_t>(entry[offset + 1]) << 8u));
}
} // namespace

std::uint8_t RecordByte(const ItemTooltipFrame& frame, std::size_t offset)
{
    return std::to_integer<std::uint8_t>(frame.record[offset]);
}

bool IsExcellent(const World::ItemState& item)
{
    return (item.flags1b & ExcellentBits) != 0;
}

Core::Expected<SkillEntryFields, std::string> ReadTheSkillEntry(GameHudContext& context, std::uint32_t skill,
                                                                std::string_view where)
{
    if (context.skills == nullptr)
        return Core::Unexpected(std::format("{}: sin Skill.bmd", where));
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("{}: Skill.bmd no tiene la skill {}", where, skill));
    const std::span<const std::byte, Data::Tables::SkillRecordSize> entry = *record;
    const Render::Models::LogicalAccess access(context.world.Runtime().movement.skillDepths[skill], context.random,
                                               static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
    SkillEntryFields fields;
    for (const std::byte value : entry)
    {
        if (value == std::byte{0})
            break;
        fields.name.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    fields.byte21 = std::to_integer<std::uint8_t>(entry[SkillByte21]);
    fields.mana = WordAt(entry, SkillMana);
    fields.stamina = WordAt(entry, SkillStamina);
    fields.range = std::to_integer<std::uint8_t>(entry[SkillRange]);
    return fields;
}

Core::Expected<SkillEntryFields, std::string> ReadTheSkillEntry(ItemTooltipFrame& frame, std::uint32_t skill,
                                                                std::string_view where)
{
    if (frame.lines.Failed())
        return Core::Unexpected(frame.lines.Error());
    return ReadTheSkillEntry(frame.context, skill, where);
}

Core::Expected<std::optional<std::string>, std::string> ItemSetNameOf(const GameHudContext& context,
                                                                      const World::ItemState& item)
{
    return ItemSetNameOf(context, item.id, static_cast<std::uint8_t>(item.flags1c & SetKindMask));
}

Core::Expected<std::optional<std::string>, std::string> ItemSetNameOf(const GameHudContext& context, std::uint16_t type,
                                                                      std::uint8_t piece)
{
    const auto kind = static_cast<std::uint8_t>(piece & SetKindMask);
    if (kind == 0)
        return std::optional<std::string>{};
    if (context.setTypes == nullptr || context.setOptions == nullptr)
        return Core::Unexpected(std::string("0x40cf46: el nombre de un ítem de conjunto sin ItemSetType.bmd ni "
                                            "ItemSetOption.bmd"));
    const auto types = context.setTypes->Record(type);
    if (!types)
        return Core::Unexpected(std::format("0x40cf4d: el tipo {:#x} lee fuera de ItemSetType.bmd", type));
    const std::uint8_t set = std::to_integer<std::uint8_t>((*types)[kind - 1u]);
    if (set == NoSet || set == 0)
        return std::optional<std::string>{};
    const auto record = context.setOptions->Record(set);
    if (!record)
        return Core::Unexpected(std::format("0x40cf6b: el conjunto {} lee fuera de los 64 de ItemSetOption.bmd", set));
    std::string name;
    for (std::size_t i = 0; i < SetNameBytes; ++i)
    {
        const std::byte value = (*record)[i];
        if (value == std::byte{0})
        {
            name.push_back(' ');
            return std::optional<std::string>(name);
        }
        name.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return Core::Unexpected(std::format("0x40cf80: el nombre del conjunto {} no tiene '\\0' en sus 32 bytes (strlen "
                                        "sigue por la pila)",
                                        set));
}
} // namespace Client::Scenes
