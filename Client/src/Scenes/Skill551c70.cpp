#include "Scenes/Skill551c70.h"

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/SkillDelays.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::size_t ManaOffset = 0x22;
constexpr std::size_t StaminaOffset = 0x24;
constexpr std::size_t RangeOffset = 0x26;
} // namespace

Core::Expected<SkillCosts, std::string> SkillCostsOf(std::uint8_t skill, const SkillDelayContext& context)
{
    if (context.skills == nullptr)
        return Core::Unexpected(std::string("0x551c70: sin Skill.bmd"));
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("0x551c70: Skill.bmd no tiene la skill {}", skill));
    const auto byteAt = [&record](std::size_t offset) { return std::to_integer<std::int32_t>((*record)[offset]); };
    const Render::Models::LogicalAccess entry(context.world.Runtime().movement.skillDepths[skill], context.random,
                                              static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
    return SkillCosts{.mana = byteAt(ManaOffset) | (byteAt(ManaOffset + 1) << 8),
                      .range = byteAt(RangeOffset),
                      .stamina = byteAt(StaminaOffset) | (byteAt(StaminaOffset + 1) << 8)};
}
} // namespace Client::Scenes
