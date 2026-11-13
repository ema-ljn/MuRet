#include "Scenes/ActorFlags.h"

#include "Data/Models/PlayerModels.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"

#include <cstdint>
#include <span>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t AnySubtype = -1;
// Los efectos y los joints que se van con las banderas (0x4e6910).
constexpr std::int16_t ShieldEffect = 0x5ac;
constexpr std::int16_t SwordEffect = 0x628;
constexpr std::int32_t FlameJoint = 0x10f;
// 0x4e6c3d..0x4e6c4d: los tipos que guardan 0x10000 en +0x7c en vez de +0x78.
constexpr std::uint16_t FirstOtherFlagsType = 0x204;
constexpr std::uint16_t LastOtherFlagsType = 0x208;

// La forma de casi todos los casos: si el bit está, lo resta.
void Lower(std::uint32_t& flags, std::uint32_t bit)
{
    if ((flags & bit) == bit)
        flags -= bit;
}

void KillTheFlames(ActorState& actor, WorldState& world)
{
    KillJointsOf(world.DrawTail().joints, FlameJoint, &actor, 4);
    KillJointsOf(world.DrawTail().joints, FlameJoint, &actor, 9);
}
} // namespace

bool KillModelEffectsOf(World::ModelEffects& effects, std::int16_t type, const void* owner, std::int32_t subtype)
{
    bool killed = false;
    for (World::ModelEffect& effect : effects.Effects())
    {
        if (!effect.alive || effect.type != type)
            continue;
        if (subtype != AnySubtype && subtype != effect.subtype)
            continue;
        if (effect.ownerContext != owner)
            continue;
        effect.alive = false;
        killed = true;
    }
    return killed;
}

void KillJointsOf(std::span<JointState> joints, std::int32_t type, const void* owner, std::int32_t subtype)
{
    for (JointState& joint : joints)
    {
        if (!joint.alive || joint.type != type || joint.ownerContext != owner)
            continue;
        if (subtype != AnySubtype && joint.subtype != subtype)
            continue;
        joint.alive = false;
    }
}

void RemoveActorFlag(std::uint32_t mask, ActorState& actor, WorldState& world)
{
    World::ModelEffects& effects = world.Map().effects;
    switch (mask)
    {
    // 0x4e6950..0x4e6a06 y 0x4e6a8c..0x4e6b07: solo la bandera.
    case 0x1:
    case 0x2:
    case 0x4:
    case 0x20:
    case 0x80:
    case 0x200:
    case 0x800:
    case 0x2000:
    case 0x4000:
    case 0x8000:
    case 0x1000000:
        Lower(actor.flags78, mask);
        return;
    // 0x4e69a5: el 8, y los joints 0x10f/4 y 0x10f/9 aunque no estuviera.
    case 0x8:
        Lower(actor.flags78, mask);
        KillTheFlames(actor, world);
        return;
    case 0x10:
        Lower(actor.flags78, mask);
        KillModelEffectsOf(effects, ShieldEffect, &actor, 1);
        return;
    case 0x40:
        Lower(actor.flags78, mask);
        KillModelEffectsOf(effects, SwordEffect, &actor, 0);
        return;
    // 0x4e6a4f: el joint 0x10f/0 solo en un jugador.
    case 0x100:
        Lower(actor.flags78, mask);
        if (actor.type == Data::Models::PlayerSkeletonModel)
            KillJointsOf(world.DrawTail().joints, FlameJoint, &actor, 0);
        return;
    case 0x400:
        Lower(actor.flags78, mask);
        KillModelEffectsOf(effects, ShieldEffect, &actor, 2);
        return;
    case 0x1000:
        Lower(actor.flags78, mask);
        KillModelEffectsOf(effects, SwordEffect, &actor, 5);
        return;
    // 0x4e6c23: con el bit en +0x78, los tipos 0x204 a 0x208 lo restan de +0x7c, lo tenga o no (envuelve).
    case 0x10000:
        if ((actor.flags78 & mask) != mask)
            return;
        if (actor.type >= FirstOtherFlagsType && actor.type <= LastOtherFlagsType)
            actor.flags7c -= mask;
        else
            actor.flags78 -= mask;
        return;
    case 0x20000:
    case 0x40000:
    case 0x80000:
        Lower(actor.flags7c, mask);
        return;
    // 0x4e6cb6..0x4e6e4c: el 0x100000 de +0x7c y trece de +0x78, en este orden, y después los efectos y los joints.
    case 0x100000:
    {
        Lower(actor.flags7c, mask);
        for (const std::uint32_t bit :
             {0x1u, 0x2u, 0x4u, 0x8u, 0x400u, 0x10u, 0x20u, 0x40u, 0x100u, 0x800u, 0x1000u, 0x4000u, 0x200u})
            Lower(actor.flags78, bit);
        KillModelEffectsOf(effects, ShieldEffect, &actor, 2);
        KillModelEffectsOf(effects, ShieldEffect, &actor, 1);
        KillModelEffectsOf(effects, SwordEffect, &actor, 0);
        KillTheFlames(actor, world);
        KillJointsOf(world.DrawTail().joints, FlameJoint, &actor, 0);
        return;
    }
    // 0x4e6ede: como el 8.
    case 0x2000000:
        Lower(actor.flags78, mask);
        KillTheFlames(actor, world);
        return;
    // 0x4e6ea5: el de +0x7c y, después, el 0x20000.
    case 0x10000000:
        Lower(actor.flags7c, mask);
        Lower(actor.flags7c, 0x20000);
        return;
    default:
        return;
    }
}

void SettleMonsterFlags(ActorState& actor)
{
    constexpr std::uint32_t Stunned = 0x100;
    constexpr std::uint32_t Frozen = 0x8;
    if (actor.type == Data::Models::PlayerSkeletonModel)
        return;
    if ((actor.flags78 & Stunned) == Stunned)
    {
        actor.flags78 -= Stunned;
        actor.state110 = 0x21;
    }
    else if ((actor.flags78 & Frozen) == Frozen)
    {
        actor.flags78 -= Frozen;
        actor.state110 = 0x22;
    }
}
} // namespace Client::Scenes
