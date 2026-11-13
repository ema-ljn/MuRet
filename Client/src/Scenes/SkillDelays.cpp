#include "Scenes/SkillDelays.h"

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
constexpr std::size_t DelayOffset = 0x28;       // int, la espera
constexpr std::size_t RequirementOffset = 0x2e; // WORD, lo que pide de +0x36 + +0x20
constexpr std::uint8_t NoDelaySkill = 0x4c;     // 0x552ee3: 0x552c50 no le pone espera

Render::Models::LogicalAccess OwnAccess(const SkillDelayContext& context)
{
    return Render::Models::LogicalAccess(context.world.Runtime().ownRecordDepth, context.random,
                                         Render::Models::OwnRecordCloseRandoms);
}

Render::Models::LogicalAccess EntryAccess(const SkillDelayContext& context, std::uint8_t skill)
{
    return Render::Models::LogicalAccess(context.world.Runtime().movement.skillDepths[skill], context.random,
                                         static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
}

Core::Expected<std::span<const std::byte, Data::Tables::SkillRecordSize>, std::string>
RecordOf(const SkillDelayContext& context, std::uint8_t skill, std::string_view where)
{
    if (context.skills == nullptr)
        return Core::Unexpected(std::format("{}: sin Skill.bmd", where));
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("{}: Skill.bmd no tiene la skill {}", where, skill));
    return *record;
}

std::uint32_t LittleEndian(std::span<const std::byte, Data::Tables::SkillRecordSize> record, std::size_t offset,
                           std::size_t size)
{
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < size; ++index)
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(record[offset + index])) << (8u * index);
    return value;
}

// Lo común de 0x552990 y 0x552c50 hasta la espera: la skill del lugar (con el registro abierto y cerrado) y la
// espera de su entrada (abierta y cerrada).
struct SlotDelay
{
    std::uint8_t skill = 0;
    std::int32_t delay = 0;
};

Core::Expected<SlotDelay, std::string> SlotDelayOf(std::uint8_t slot, const SkillDelayContext& context,
                                                   std::string_view where)
{
    SlotDelay read;
    {
        const auto own = OwnAccess(context);
        read.skill = OwnSkillByte(context.world.OwnCharacter(), slot);
    }
    const auto record = RecordOf(context, read.skill, where);
    if (!record.HasValue())
        return Core::Unexpected(record.Error());
    {
        const auto entry = EntryAccess(context, read.skill);
        read.delay = static_cast<std::int32_t>(LittleEndian(record.Value(), DelayOffset, 4));
    }
    return read;
}

// +0x36 + +0x20 del registro propio, en int (los MOV de WORD a registros en 0).
std::int32_t Allowance(const OwnCharacterRecord& own)
{
    return static_cast<std::int32_t>(own.word36) + static_cast<std::int32_t>(own.leadership);
}
} // namespace

Core::Expected<std::int32_t, std::string> SkillDelayRequirement(std::uint8_t skill, const SkillDelayContext& context)
{
    const auto record = RecordOf(context, skill, "0x5521f0");
    if (!record.HasValue())
        return Core::Unexpected(record.Error());
    const auto entry = EntryAccess(context, skill);
    return static_cast<std::int32_t>(LittleEndian(record.Value(), RequirementOffset, 2));
}

Core::Expected<void, std::string> StartSkillDelay(std::uint8_t slot, const SkillDelayContext& context)
{
    const auto read = SlotDelayOf(slot, context, "0x552990");
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    if (read.Value().delay <= 0)
        return {};
    OwnCharacterRecord& own = context.world.OwnCharacter();
    const auto access = OwnAccess(context);
    if (slot >= own.skillDelays.size())
        return Core::Unexpected(std::string("0x552990: la espera 255 pisa el equipo del registro propio"));
    if (own.skillDelays[slot] > 0)
        return {};
    const auto requirement = SkillDelayRequirement(read.Value().skill, context);
    if (!requirement.HasValue())
        return Core::Unexpected(requirement.Error());
    if (requirement.Value() > Allowance(own))
        return {};
    own.skillDelays[slot] = read.Value().delay;
    return {};
}

Core::Expected<bool, std::string> TryStartSkillDelay(std::uint8_t slot, const SkillDelayContext& context)
{
    const auto read = SlotDelayOf(slot, context, "0x552c50");
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    if (read.Value().delay <= 0)
        return true;
    OwnCharacterRecord& own = context.world.OwnCharacter();
    const auto access = OwnAccess(context);
    if (slot >= own.skillDelays.size())
        return Core::Unexpected(std::string("0x552c50: la espera 255 pisa el equipo del registro propio"));
    if (own.skillDelays[slot] > 0)
        return false;
    const auto requirement = SkillDelayRequirement(read.Value().skill, context);
    if (!requirement.HasValue())
        return Core::Unexpected(requirement.Error());
    if (requirement.Value() > Allowance(own))
        return false;
    if (read.Value().skill != NoDelaySkill)
        own.skillDelays[slot] = read.Value().delay;
    return true;
}

Core::Expected<void, std::string> CountDownSkillDelays(std::int32_t elapsed, const SkillDelayContext& context)
{
    OwnCharacterRecord& own = context.world.OwnCharacter();
    const auto access = OwnAccess(context);
    constexpr std::size_t MaximumSlots = 0x100;
    const std::size_t slots = std::min<std::size_t>(static_cast<std::size_t>(own.byte6a) + 2, MaximumSlots);
    // Con +0x6a de los 64 primeros lugares no pasa de 66; el 255 sería el equipo (+0xb68, Q-C1203).
    if (slots > own.skillDelays.size())
        return Core::Unexpected(std::string("0x5530bb: la espera 255 pisa el equipo del registro propio"));
    for (std::size_t slot = 0; slot < slots; ++slot)
    {
        std::int32_t& delay = own.skillDelays[slot];
        if (delay <= 0)
            continue;
        delay -= elapsed;
        if (delay < 0)
            delay = 0;
    }
    return {};
}
} // namespace Client::Scenes
