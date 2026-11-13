#include "Scenes/Skill57ce80.h"

#include "Core/Expected.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/OriginalFtol.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a}); // [0x6b86c0], 0,01
} // namespace

Core::Expected<bool, std::string> AimTheHeroSkill(ActorState& hero, const HeroSkillContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    glm::vec3 point{};
    const std::int32_t chosen = runtime.selectedTargets[2];
    if (chosen != -1)
    {
        if (chosen < 0 || static_cast<std::size_t>(chosen) >= context.world.Actors().size())
            return Core::Unexpected(std::string("0x57ce8e: el elegido está fuera de la tabla de personajes"));
        point = context.world.Actors()[static_cast<std::size_t>(chosen)].position;
    }
    else
    {
        if (!context.pickTerrainPoint)
            return Core::Unexpected(std::string("0x57ceea: 0x5fc260(1) y 0x5fac90 sin el rayo del mouse"));
        const auto picked = context.pickTerrainPoint();
        if (!picked.HasValue())
            return Core::Unexpected(picked.Error());
        if (!picked.Value())
            return false;
        point = *picked.Value();
    }
    const auto x = World::OriginalFtolLow(static_cast<double>(point.x) * static_cast<double>(CellReciprocal));
    const auto y = World::OriginalFtolLow(static_cast<double>(point.y) * static_cast<double>(CellReciprocal));
    if (!x || !y)
        return Core::Unexpected(std::string("0x57ceaa: la celda del blanco fuera del perfil ftol"));
    runtime.movement.targetX = std::bit_cast<std::uint32_t>(*x);
    runtime.movement.targetY = std::bit_cast<std::uint32_t>(*y);
    hero.attackPosition = point;
    return true;
}
} // namespace Client::Scenes
