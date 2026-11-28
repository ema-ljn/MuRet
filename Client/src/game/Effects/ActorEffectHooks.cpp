#include "Effects/ActorEffectHooks.h"

#include "Actors/EffectHooks.h"
#include "Core/Expected.h"
#include "Kernel/ModelEffectArguments.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldState.h"
#include "Effects/GuildMarkEffects.h"
#include "Effects/HighModelEffects.h"
#include "Effects/SkullEffects.h"
#include "Effects/WizardEffects.h"
#include "Effects/BlastModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Effects/SkinShellEffects.h"
#include "Effects/WizardModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Effects
{
bool ActorEffectHooks::KillTheEffectsOf(Effects::ModelEffects& effects, std::int16_t type, const void* owner,
                                        std::int32_t subtype) const
{
    return Effects::KillModelEffectsOf(effects, type, owner, subtype);
}

std::optional<std::size_t> ActorEffectHooks::CreateTheSkillFlare(Effects::ModelEffects& effects,
                                                                 const glm::vec3& position, const glm::vec3& angles,
                                                                 const glm::vec3& light, std::int32_t subtype,
                                                                 void* owner) const
{
    return effects.CreateSkillFlare(position, angles, light, subtype, owner);
}

std::optional<std::size_t> ActorEffectHooks::CreateTheFullSetFlare(Effects::ModelEffects& effects,
                                                                   const glm::vec3& position, const glm::vec3& angles,
                                                                   const glm::vec3& light, void* owner) const
{
    return effects.CreateFullSetFlare(position, angles, light, owner);
}

std::optional<std::size_t> ActorEffectHooks::CreateTheIceShard(Effects::ModelEffects& effects,
                                                               const glm::vec3& position, const glm::vec3& angles,
                                                               const glm::vec3& light, Crt::CrtRandom& random) const
{
    return effects.CreateIceShard(position, angles, light, random);
}

std::optional<std::size_t> ActorEffectHooks::CreateTheDebris(Effects::ModelEffects& effects, std::int16_t type,
                                                             const glm::vec3& position, const glm::vec3& angles,
                                                             const glm::vec3& light, Crt::CrtRandom& random,
                                                             std::int32_t subtype) const
{
    return effects.CreateDebris(type, position, angles, light, random, subtype);
}

std::optional<std::size_t> ActorEffectHooks::CreateTheSkinShell(Effects::ModelEffects& effects,
                                                                const Kernel::ModelEffectArguments& arguments,
                                                                Crt::CrtRandom& random) const
{
    return Effects::CreateSkinShell(arguments, effects, random);
}

Core::Expected<std::optional<std::size_t>, std::string>
ActorEffectHooks::CreateTheBlastEffect(Effects::ModelEffects& effects, const Kernel::ModelEffectArguments& arguments,
                                       Crt::CrtRandom& random) const
{
    return Effects::CreateBlastModelEffect(effects, arguments, Effects::ModelEffectOwner{}, random,
                                           Effects::BlastEffectServices{});
}

Core::Expected<std::optional<std::size_t>, std::string>
ActorEffectHooks::CreateTheHighEffect(const Kernel::ModelEffectArguments& arguments, Kernel::WorldState& world,
                                      Crt::CrtRandom& random, float worldClock) const
{
    return CreateHighModelEffect(arguments,
                                 HighEffectContext{.world = world, .random = random, .worldClock = worldClock});
}

Core::Expected<std::optional<std::size_t>, std::string>
ActorEffectHooks::CreateTheWizardEffect(Kernel::WorldState& world, Crt::CrtRandom& random,
                                        const Actors::WizardEffectScene& scene,
                                        const Kernel::ModelEffectArguments& arguments, glm::vec3* callerLight) const
{
    const Effects::WizardEffectServices services =
        WizardEffectServicesOf(world, random,
                               WizardSceneParts{.particles = &world.Particles(),
                                                .terrain = scene.terrain,
                                                .fixedHeight = scene.fixedHeight,
                                                .dynamicLighting = scene.dynamicLighting,
                                                .faunaModels = scene.faunaModels});
    return Effects::CreateWizardModelEffect(world.Map().effects, arguments, random, services, callerLight);
}

Core::Expected<void, std::string> ActorEffectHooks::PutTheSkullOn(Kernel::ActorState& actor, Kernel::WorldState& world,
                                                                  Crt::CrtRandom& random, std::uint32_t address) const
{
    return Effects::PutTheSkullOn(actor, world, random, address);
}

bool ActorEffectHooks::IsTheGuildMarkDue(const Kernel::ActorState& actor, float worldClock) const
{
    return GuildMarkIsDue(actor, worldClock);
}

Core::Expected<void, std::string> ActorEffectHooks::RaiseTheGuildMark(Kernel::ActorState& actor,
                                                                      const glm::vec3& bonePoint,
                                                                      Effects::ModelEffects& effects,
                                                                      Particles::Particles& particles,
                                                                      Crt::CrtRandom& random, float worldClock) const
{
    return Effects::RaiseTheGuildMark(actor, bonePoint, effects, particles, random, worldClock);
}
} // namespace Client::Effects
