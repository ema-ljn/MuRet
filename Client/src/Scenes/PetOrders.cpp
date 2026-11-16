#include "Scenes/PetOrders.h"

#include "Protocol/Pet/PetOrders.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <cstddef>
#include <cstdint>

namespace Client::Scenes
{
namespace
{
constexpr std::uint8_t ChargingState = 5;
constexpr std::uint8_t ShoutingState = 6;

// 0x4164f0(estado): +0x6c (la lejanía) en 0 y el estado.
void SetRavenState(DarkSpirit& spirit, std::uint8_t state)
{
    spirit.far = 0;
    spirit.state = state;
}
} // namespace

void ApplyPetOrder(const Protocol::Pet::PetOrder& order, WorldState& world)
{
    ActorState& hero = world.Actors()[world.Runtime().hero];
    if (!hero.darkSpirit)
        return;
    DarkSpirit& spirit = *hero.darkSpirit;
    spirit.order = order.order;
    if (spirit.state != ChargingState && spirit.state != ShoutingState)
        spirit.byte1ad = 0;
    if (order.order != Protocol::Pet::FollowTargetOrder)
        return;
    // 0x41653d..0x41655e: +0x1ad en 1 y el blanco en el personaje de la clave, esté o no.
    spirit.byte1ad = 1;
    spirit.target = ActorWithKey(world, order.key);
}

void ApplyPetAttack(const Protocol::Pet::PetAttack& attack, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    ActorState& owner = world.Actors()[ActorWithKey(world, attack.owner)];
    if (!owner.darkSpirit)
        return;
    DarkSpirit& spirit = *owner.darkSpirit;
    const std::size_t target = ActorWithKey(world, attack.target);
    spirit.target = target;
    constexpr std::uint32_t Stopped = 0x1000;
    constexpr std::uint32_t Hidden = 0x4000;
    if ((owner.flags78 & Stopped) == Stopped)
        return;
    spirit.target3b0 = static_cast<std::uint16_t>(target);
    const auto state = static_cast<std::uint8_t>(attack.kind + 5);
    if ((owner.flags78 & Hidden) == Hidden)
    {
        SetRavenState(spirit, state);
        return;
    }
    spirit.byte38d = 0;
    SetRavenState(spirit, state);
    if (spirit.state == ChargingState)
    {
        spirit.byte1ad = 1;
        // 0x4165ff..0x41662b: rand() % 10 por CDQ/IDIV, FILD y FADD 20 (0x6b86d0).
        const int roll = resources.cloth.random.Next() % 10;
        spirit.floatF0 = 0.5f;
        spirit.speed = static_cast<float>(roll) + 20.0f;
        world.Runtime().lastSoundId = 0x1d2;
        return;
    }
    if (spirit.state == ShoutingState)
        world.Runtime().lastSoundId = 0x1d0;
}
} // namespace Client::Scenes
