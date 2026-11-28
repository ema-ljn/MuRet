#include "Session/CharacterStepHooks.h"

#include "Actors/CharacterStep.h"
#include "Actors/CharacterStepHooks.h"
#include "Audio/SoundId.h"
#include "Core/Expected.h"
#include "Kernel/ActorSpirit.h"
#include "Kernel/ActorState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Player/OwnTimers.h"
#include "Protocol/Pets/PetOrders.h"
#include "Combat/ActorCombat.h"
#include "Skills/AttackSkillCases.h"
#include "Skills/AttackStep.h"
#include "Pets/DarkSpirit.h"
#include "Joints/FaunaJointCreation.h"
#include "Hud/HealthBars.h"
#include "Monsters/ModelBody.h"
#include "Monsters/ModelEffects.h"
#include "Monsters/Drawing/MonsterHelpers.h"
#include "Monsters/Drawing/MonsterRibbonTrails.h"
#include "Monsters/MonsterStepCases.h"
#include "Monsters/NpcChatter.h"
#include "Party/PartyActors.h"
#include "CastleSiege/SiegeBattleUi.h"
#include "Kernel/JointRequest.h"
#include "Effects/ObjectAlpha.h"
#include "Terrain/DynamicWorldLight.h"
#include "Particles/ObjectFire.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace Client::Session
{
namespace
{
// 0x417e62: the raven's shout.
constexpr Audio::SoundId RavenShoutSound{0x1d1};

// What the raven's step reads from the pool (DarkSpiritTargets): the character of +8 and that of +0x3b0, signed.
// Between 0 and 399 it looks at their flags (0x416a8b..0x416ab9); 6 reads its position with any value other than
// 0xffff (0x417858): the slots the A8 writes (0 to 400) are in MuRet's pool, and another value stops.
Core::Expected<Pets::DarkSpiritTargets, std::string> RavenTargetsOf(const Kernel::DarkSpirit& spirit,
                                                                    Kernel::WorldState& world, std::size_t slot)
{
    constexpr std::int16_t PoolActors = 400;
    constexpr std::uint16_t NoAttackTarget = 0xffff;
    const auto actors = world.Actors();
    Pets::DarkSpiritTargets targets;
    if (spirit.target < actors.size())
    {
        targets.target = actors[spirit.target].position;
    }
    const auto attack = static_cast<std::int16_t>(spirit.target3b0);
    if (attack >= 0 && attack < PoolActors)
    {
        targets.attackInPool = true;
        targets.attackFlags = actors[static_cast<std::size_t>(attack)].flags78;
    }
    if (spirit.target3b0 != NoAttackTarget)
    {
        if (attack < 0 || static_cast<std::size_t>(attack) >= actors.size())
        {
            return Core::Unexpected(
                std::format("0x417869: el +0x3b0 del cuervo del lugar {} ({}) cae fuera del pool", slot, attack));
        }
        targets.attackPosition = actors[static_cast<std::size_t>(attack)].position;
    }
    return targets;
}

Actors::ModelActionEnd StepEndOf(Monsters::ModelActionEnd end)
{
    switch (end)
    {
    case Monsters::ModelActionEnd::AdvanceAgain:
        return Actors::ModelActionEnd::AdvanceAgain;
    case Monsters::ModelActionEnd::LeaveRemains:
        return Actors::ModelActionEnd::LeaveRemains;
    case Monsters::ModelActionEnd::Done:
        break;
    }
    return Actors::ModelActionEnd::Done;
}
} // namespace

Core::Expected<void, std::string> CharacterStepHooks::RefreshTheSiegePanel(Kernel::WorldRuntime& runtime) const
{
    return CastleSiege::RefreshTheSiegePanel(runtime);
}

Core::Expected<void, std::string>
CharacterStepHooks::QueueTheGateName(const Kernel::ActorState& actor, const Actors::CharacterStepContext& context) const
{
    return Monsters::QueueTheGateName(actor, context.texts, context.world.Runtime().worldTexts);
}

Core::Expected<void, std::string>
CharacterStepHooks::StepTheOwnTimers(const Actors::CharacterStepContext& context) const
{
    return Player::StepTheOwnTimers(context.world, context.items, context.random);
}

void CharacterStepHooks::StepTheFallingBall(Kernel::ActorState& actor,
                                            const std::function<double(float, float)>& height) const
{
    Monsters::StepTheFallingBall(actor, height);
}

void CharacterStepHooks::StepTheAlpha(Kernel::ActorState& actor) const
{
    Effects::StepObjectAlpha(actor.alphaTarget, actor.alpha, actor.blendMeshLight, actor.alphaMode);
}

bool CharacterStepHooks::TurnTheNpc(Kernel::ActorState& actor, std::int32_t map) const
{
    return Monsters::TurnTheNpc(actor, map);
}

Core::Expected<void, std::string> CharacterStepHooks::SayTheNpcLine(const Kernel::ActorState& actor, std::size_t slot,
                                                                    const Actors::CharacterStepContext& context) const
{
    return Monsters::SayTheNpcLine(actor, slot, context);
}

Core::Expected<void, std::string> CharacterStepHooks::SayTheNamedLine(const Kernel::ActorState& actor, std::size_t slot,
                                                                      const Actors::CharacterStepContext& context) const
{
    return Monsters::SayTheNamedLine(actor, slot, context, m_monsters);
}

bool CharacterStepHooks::IsInTheParty(const Kernel::WorldState& world, const Kernel::ActorState& actor) const
{
    return Party::IsInTheParty(world, actor);
}

float CharacterStepHooks::ModelSpeedOf(std::uint16_t type, std::uint8_t action, float frame, float speed) const
{
    return Monsters::ModelSpeedOf(type, action, frame, speed);
}

Core::Expected<void, std::string>
CharacterStepHooks::FinishTheTurnedNpc(Kernel::ActorState& actor, std::size_t slot,
                                       const Actors::CharacterStepContext& context) const
{
    return Monsters::FinishTheTurnedNpc(actor, slot, context);
}

Core::Expected<Actors::ModelActionEnd, std::string>
CharacterStepHooks::FinishTheModelAction(Kernel::ActorState& actor, const Models::Skeleton& skeleton,
                                         Kernel::WorldRuntime& runtime, Crt::CrtRandom& random) const
{
    auto end = Monsters::FinishTheModelAction(actor, skeleton, runtime, random);
    if (!end.HasValue())
        return Core::Unexpected(std::move(end).Error());
    return StepEndOf(end.Value());
}

Core::Expected<void, std::string> CharacterStepHooks::SetActorDie(Kernel::ActorState& actor,
                                                                  const Actors::CharacterStepContext& context) const
{
    return Combat::SetActorDie(actor, Combat::ActorCombatContext{.world = context.world,
                                                                 .random = context.random,
                                                                 .playerSkeleton = context.playerSkeleton,
                                                                 .models = context.models,
                                                                 .items = context.items,
                                                                 .terrain = context.terrain,
                                                                 .dynamicLighting = context.dynamicLighting,
                                                                 .faunaModels = context.faunaModels});
}

Core::Expected<void, std::string> CharacterStepHooks::CreateTheJoint(const Actors::StepJoint& joint,
                                                                     const Actors::CharacterStepContext& context) const
{
    constexpr std::int32_t NoKey = -1, NoTarget = -1;
    const Kernel::JointRequest request{.type = joint.type,
                                       .firstPosition = joint.first,
                                       .secondPosition = joint.second,
                                       .angles = joint.angles,
                                       .subtype = joint.subtype,
                                       .ownerContext = joint.owner,
                                       .scale = joint.width,
                                       .key = NoKey,
                                       .skill = 0,
                                       .skillSerial = 0,
                                       .target = NoTarget};
    auto created = Joints::CreateFaunaJoint(context.joints, request, context.random);
    if (!created.HasValue())
        return Core::Unexpected(std::move(created).Error());
    return {};
}

Core::Expected<void, std::string> CharacterStepHooks::StepTheAttack(Kernel::ActorState& actor, std::size_t slot,
                                                                    const Actors::CharacterStepContext& context,
                                                                    const std::optional<glm::vec3>& flareLight) const
{
    return Skills::StepTheAttack(actor, slot, context, flareLight.value_or(Skills::BodyLight), m_monsters);
}

void CharacterStepHooks::QueueTheHealthBar(const Kernel::ActorState& actor,
                                           std::deque<Kernel::QueuedHealthBar>& queue) const
{
    Hud::QueueTheHealthBar(actor, queue);
}

Core::Expected<void, std::string>
CharacterStepHooks::EmitEarlyMonsterRibbonTrails(Kernel::ActorState& actor, const Models::Skeleton* skeleton,
                                                 const Actors::CharacterStepContext& context) const
{
    return Monsters::Drawing::EmitEarlyMonsterRibbonTrails(actor, skeleton, context);
}

Core::Expected<void, std::string>
CharacterStepHooks::StepTheSwingsOf1abTo1b4(Kernel::ActorState& actor, std::size_t slot,
                                            const Actors::CharacterStepContext& context) const
{
    return Monsters::Drawing::StepTheSwingsOf1abTo1b4(actor, slot, context.world, context.faunaModels, context.random,
                                                      context.particles, context.worldClock);
}

Core::Expected<void, std::string>
CharacterStepHooks::EmitLateMonsterRibbonTrails(Kernel::ActorState& actor, const Models::Skeleton* skeleton,
                                                const Actors::CharacterStepContext& context) const
{
    return Monsters::Drawing::EmitLateMonsterRibbonTrails(actor, skeleton, context);
}

// The class 0 fire without an offset, with the 0xf1's angles, position and light.
Core::Expected<void, std::string>
CharacterStepHooks::EmitTheFallingBallFire(const Kernel::ActorState& actor,
                                           const Actors::CharacterStepContext& context) const
{
    const Kernel::ObjectStepGlobals globals{
        .worldClock = context.worldClock, .dynamicLighting = context.dynamicLighting, .particles = context.particles};
    return Particles::EmitObjectFire(0, actor.angles, actor.position, actor.light, glm::vec3(0.0f), context.random,
                                     globals);
}

// The caller takes the raven as existing and without +0x78 & 0x1000.
Core::Expected<void, std::string> CharacterStepHooks::StepTheRaven(Kernel::ActorState& actor, std::size_t slot,
                                                                   const Actors::CharacterStepContext& context) const
{
    const Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (context.darkSpiritSkeleton == nullptr || context.particles == nullptr)
    {
        return Core::Unexpected(
            std::format("0x416a20: faltan el modelo o las partículas del cuervo del lugar {}", slot));
    }
    auto targets = RavenTargetsOf(*actor.darkSpirit, context.world, slot);
    if (!targets.HasValue())
    {
        return Core::Unexpected(targets.Error());
    }
    const Kernel::SharedModelPlacement& player = context.world.PlayerModel();
    const Pets::DarkSpiritStepped stepped =
        Pets::StepDarkSpirit(*actor.darkSpirit, *context.darkSpiritSkeleton,
                             {.position = actor.position,
                              .flags = actor.flags78,
                              .angles = actor.angles,
                              .pet = actor.pet.model,
                              .safeZone = actor.safeZone,
                              .pose = actor.bodyPose,
                              .modelScale = player.scale,
                              .modelOrigin = player.origin},
                             static_cast<std::uint8_t>(runtime.map), *context.particles, context.worldClock,
                             context.random, context.counter, targets.Value());
    // 0x416fc8 and 0x4175de: the A7 that stops the attack, through send(); without the session, it does not go out.
    if (stepped.stopsTheAttack && context.send)
    {
        context.send(Protocol::Pets::WritePetCommand({.pet = Protocol::Pets::RavenPet,
                                                      .command = Protocol::Pets::StopTheAttackCommand,
                                                      .key = Protocol::Pets::NoTargetKey}));
    }
    // 0x417e67: 0x41ed10(0x1d1).
    if (stepped.shouts && context.soundRequest)
    {
        context.soundRequest(RavenShoutSound);
    }
    return {};
}

Core::Expected<void, std::string>
CharacterStepHooks::SetTheModelHeadTarget(Kernel::ActorState& actor, const Actors::CharacterStepContext& context) const
{
    return Monsters::SetTheModelHeadTarget(actor, context.world.Actors(), context.random);
}

Core::Expected<void, std::string> CharacterStepHooks::AddTheHandLight(const glm::vec3& position, const glm::vec3& color,
                                                                      std::int32_t radius,
                                                                      const Actors::CharacterStepContext& context) const
{
    return Terrain::AddDynamicLight(context.dynamicLighting, position, color, radius, "0x506b50");
}

Core::Expected<void, std::string>
CharacterStepHooks::StepTheModelEffects(Kernel::ActorState& actor, std::size_t slot, const Models::Skeleton& skeleton,
                                        const Actors::CharacterStepContext& context) const
{
    Monsters::ModelEffectsPass pass{.actor = actor,
                                    .slot = slot,
                                    .world = context.world,
                                    .random = context.random,
                                    .skeleton = skeleton,
                                    .particles = context.particles,
                                    .sprites = context.sprites,
                                    .worldClock = context.worldClock,
                                    .map = context.world.Runtime().map,
                                    .groundMarks = context.groundMarks,
                                    .dynamicLighting = context.dynamicLighting,
                                    .faunaModels = context.faunaModels};
    return Monsters::StepTheModelEffects(pass);
}

Core::Expected<void, std::string> CharacterStepHooks::StepTheCastleObject(Kernel::ActorState& actor,
                                                                          const Actors::CharacterStepContext& context,
                                                                          std::function<double(float, float)> height,
                                                                          const Models::Skeleton* skeleton) const
{
    const Monsters::CastleObjectStep castle{.runtime = context.world.Runtime(),
                                            .pool = context.world.Actors(),
                                            .height = std::move(height),
                                            .skeleton = skeleton};
    return Monsters::StepTheCastleObject(actor, castle);
}
} // namespace Client::Session
