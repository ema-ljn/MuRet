#include "Actors/In/Transformed.h"

#include "Actors/ActorEffectFlags.h"
#include "Actors/ViewportHooks.h"
#include "Actors/ViewportRecord.h"
#include "Core/Expected.h"
#include "Kernel/ActorState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Protocol/Actors/Viewport.h"
#include "Session/WorldEntry.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Actors::In
{
Core::Expected<void, std::string> ApplyTransformed(const Protocol::Actors::Transformed& transformed,
                                                   const Scenes::WorldEntryResources& resources,
                                                   const IViewportHooks& hooks)
{
    for (const Protocol::Actors::TransformedRecord& record : transformed.records)
    {
        // 0x4c9307..0x4c9311.
        if (IsHiddenName(record.name))
            continue;
        // 0x4c9317..0x4c934c: the published hero's class before the creation and 0x5141e0(type, X, Y, key), without
        // looking at whether it returns 0.
        const std::uint8_t heroClass = hooks.PublishedHeroClass();
        const auto created = hooks.CreateMonster(record.type, record.x, record.y, record.key);
        if (!created.HasValue())
            return Core::Unexpected(created.Error());
        if (auto finished = FinishTheTransformed(created.Value(), record, heroClass, resources, hooks);
            !finished.HasValue())
            return finished;
    }
    return {};
}

Core::Expected<void, std::string>
FinishTheTransformed(std::size_t index, const Protocol::Actors::TransformedRecord& record, std::uint8_t heroClass,
                     const Scenes::WorldEntryResources& resources, const IViewportHooks& hooks)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    auto& random = resources.cloth.random;
    Kernel::ActorState& actor = world.Actors()[index];
    // 0x4c9356..0x4c9385.
    constexpr std::uint16_t SmallerWord376 = 7;
    if (actor.word376 == SmallerWord376)
        actor.scale = 0.8f;
    actor.characterClass = heroClass;
    actor.pk = PkOf(record.directionAndPk);
    actor.byte88 = 1;
    actor.byte3e3 = 1;
    // 0x4c938c..0x4c9396.
    if (auto flagged = ApplyEffectFlags(actor, record.effects, index == runtime.hero, resources); !flagged.HasValue())
        return flagged;
    ApplyCastleSiegeFlags(actor, record.effects, runtime.map);
    WriteTheCells(actor, record.x, record.y, random);
    actor.angles.z = AngleOf(record.directionAndPk);
    if (record.isNew)
    {
        // 0x4c9549..0x4c9652.
        PutInTheCellCenter(actor, random);
        actor.alpha = 0.0f;
        // 0x4c961f..0x4c967f: 0x518cf0(0xec, +0x10, +0x1c, +0xf4, 0, the character, −1, 0, 0, 0, 0), the magic circle
        // (World/BlastModelEffects), and 0x53fe70(0x5d5, +0x10, +0x1c, +0xf4, 2, 1.0f, the character), the ray that
        // follows it on its way; neither draws a rand(). With the table full, nothing.
        if (auto circle = hooks.CreateTheMagicCircle(actor); !circle.HasValue())
            return Core::Unexpected(std::format("0x4c965c: {}", circle.Error()));
        const Particles::ParticleOwner owner{Particles::ParticleOwnerKind::Actor, index};
        if (auto bolt = world.Particles().CreateOwnedChecked(Particles::LightningParticle, actor.position, actor.angles,
                                                             actor.light, Particles::LightningFollowSubtype, 1.0f, 0.0f,
                                                             random, 0, owner);
            !bolt.HasValue())
            return Core::Unexpected(
                std::format("0x4c9674: particle 0x5d5/2, error {}", static_cast<unsigned>(bolt.Error())));
    }
    else if (auto walked = WalkToTheTarget(actor, record.targetX, record.targetY, resources); !walked.HasValue())
        return walked;
    // 0x4c9733..0x4c975d.
    actor.byte1e3 = 0;
    actor.name = record.name;
    return hooks.DressThePublishedHero();
}
} // namespace Client::Actors::In
