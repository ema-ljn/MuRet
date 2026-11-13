#include "Scenes/HeroSkillSupport.h"

#include "Core/Expected.h"
#include "Protocol/Movement.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ActorState.h"
#include "Scenes/HeroSkillCasts.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
constexpr std::string_view SilentName = "webzen";
} // namespace

bool HasTheSilentName(const WorldState& world)
{
    // El nombre como lo ve el strlen de 0x58a15d: los diez bytes de +0x1d9, +0x1e3 y, como MuRet no tiene +0x1e4, un 0.
    const ActorState& published = world.Actors()[world.Runtime().hero];
    std::string name;
    bool ended = false;
    for (const std::byte letter : published.name)
    {
        if (letter == std::byte{0})
        {
            ended = true;
            break;
        }
        name.push_back(static_cast<char>(letter));
    }
    if (!ended && published.byte1e3 != 0)
        name.push_back(static_cast<char>(published.byte1e3));
    return name.find(SilentName) != std::string::npos;
}

Core::Expected<void, std::string> SendTheTurn(std::uint8_t x, std::uint8_t y, float angle,
                                              const HeroSkillContext& context, std::string_view where)
{
    const std::array<Protocol::Movement::Cell, 1> cell{{{x, y}}};
    auto turn = Protocol::Movement::WriteWalk(cell, cell[0], angle);
    if (!turn.HasValue())
        return Core::Unexpected(std::format("{}: el rumbo del D3 de girar no se puede escribir", where));
    SendTheSkillPacket(context, std::move(turn).Value());
    return {};
}

ActorCombatContext CombatContextOf(const HeroSkillContext& context)
{
    return ActorCombatContext{.world = context.world,
                              .random = context.random,
                              .playerSkeleton = context.playerSkeleton,
                              .models = nullptr,
                              .items = context.items};
}

std::size_t SlotOf(const WorldState& world, const ActorState& actor)
{
    const auto actors = world.Actors();
    for (std::size_t slot = 0; slot < actors.size(); ++slot)
    {
        if (&actors[slot] == &actor)
            return slot;
    }
    return actors.size();
}

std::uint8_t ReadTheSkillCell(std::uint8_t& depth, std::uint8_t cell, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(depth, random, Render::Models::SmallLogicalCloseRandoms);
    return cell;
}
} // namespace Client::Scenes
