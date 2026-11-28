#include "Skills/Skill551f40.h"

#include "Core/Expected.h"
#include "Tables/SkillTable.h"
#include "Tables/SkillRecord.h"
#include "Models/LogicalAccess.h"
#include "Skills/SkillDelays.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Skills
{
namespace
{
constexpr std::size_t LevelOffset = Tables::SkillRecordLayout::Level;
constexpr std::size_t EnergyOffset = Tables::SkillRecordLayout::Energy; // with 0, the skill asks for no energy
constexpr std::int32_t EnergyFactor = 4;
constexpr std::int32_t EnergyDivisor = 100; // 0x5520f4: IMUL by 0x51eb851f and SAR 5
constexpr std::int32_t EnergyBase = 20;
} // namespace

Core::Expected<std::int32_t, std::string> SkillEnergyRequirement(std::uint8_t skill, const SkillDelayContext& context)
{
    if (context.skills == nullptr)
        return Core::Unexpected(std::string("0x551f40: sin Skill.bmd"));
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("0x551f40: Skill.bmd no tiene la skill {}", skill));
    const Models::LogicalAccess entry(context.world.Runtime().skillDepths[skill], context.random,
                                      static_cast<std::uint16_t>(Tables::SkillRecordSize));
    const auto energy = std::to_integer<std::int32_t>((*record)[EnergyOffset]);
    if (energy == 0)
        return 0;
    const auto level = std::to_integer<std::int32_t>((*record)[LevelOffset]);
    return level * energy * EnergyFactor / EnergyDivisor + EnergyBase;
}
} // namespace Client::Skills
