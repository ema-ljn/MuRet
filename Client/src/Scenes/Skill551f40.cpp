#include "Scenes/Skill551f40.h"

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
constexpr std::size_t LevelOffset = 0x20;
constexpr std::size_t EnergyOffset = 0x2c;
constexpr std::int32_t EnergyBase = 20;
} // namespace

Core::Expected<std::int32_t, std::string> SkillEnergyOf(std::uint8_t skill, const HeroSkillContext& context,
                                                        std::string_view where)
{
    if (context.skills == nullptr)
        return Core::Unexpected(std::format("{}: 0x551f40 sin Skill.bmd", where));
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("{}: Skill.bmd no tiene la skill {}", where, skill));
    const Render::Models::LogicalAccess entry(context.world.Runtime().movement.skillDepths[skill], context.random,
                                              static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
    const auto energy = static_cast<std::int32_t>(std::to_integer<std::uint8_t>((*record)[EnergyOffset]));
    if (energy == 0)
        return 0;
    const auto level = static_cast<std::int32_t>(std::to_integer<std::uint8_t>((*record)[LevelOffset]));
    return level * energy * 4 / 100 + EnergyBase;
}
} // namespace Client::Scenes
