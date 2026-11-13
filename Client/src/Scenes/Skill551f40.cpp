#include "Scenes/Skill551f40.h"

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
constexpr std::size_t LevelOffset = 0x20;  // byte
constexpr std::size_t EnergyOffset = 0x2c; // byte: con 0, la skill no pide energía
constexpr std::int32_t EnergyFactor = 4;
constexpr std::int32_t EnergyDivisor = 100; // 0x5520f4: IMUL por 0x51eb851f y SAR 5
constexpr std::int32_t EnergyBase = 20;
} // namespace

Core::Expected<std::int32_t, std::string> SkillEnergyRequirement(std::uint8_t skill, const SkillDelayContext& context)
{
    if (context.skills == nullptr)
        return Core::Unexpected(std::string("0x551f40: sin Skill.bmd"));
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("0x551f40: Skill.bmd no tiene la skill {}", skill));
    const Render::Models::LogicalAccess entry(context.world.Runtime().movement.skillDepths[skill], context.random,
                                              static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
    const auto energy = std::to_integer<std::int32_t>((*record)[EnergyOffset]);
    if (energy == 0)
        return 0;
    const auto level = std::to_integer<std::int32_t>((*record)[LevelOffset]);
    return level * energy * EnergyFactor / EnergyDivisor + EnergyBase;
}
} // namespace Client::Scenes
