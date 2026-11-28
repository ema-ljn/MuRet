#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Assets
{
class FaunaModels;
} // namespace Client::Assets

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Kernel
{
struct ActorState;
struct ModelEffectArguments;
class WorldState;
} // namespace Client::Kernel

namespace Client::Terrain
{
struct TerrainLighting;
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::Actors
{
// What the wizards' effects read from the scene besides the world (Effects::WizardSceneParts): the terrain's heights,
// the camera's fixed height, the dynamic light and the models a creation loads.
struct WizardEffectScene
{
    const Terrain::WorldTerrain* terrain = nullptr;
    float fixedHeight = 1200.0f;
    Terrain::TerrainLighting* dynamicLighting = nullptr;
    Assets::FaunaModels* faunaModels = nullptr;
};

// 0x518cf0 and 0x518ac0, where the characters' model effects are born and die: the flares, the ice and the skull of
// the flags, the ice shards of the remains, the full set's flare, the windows of the actions, the guild mark, the
// raven's attack and the stones and shells of 0x4ee770. The effects' table and the global rand() are the caller's;
// game/Effects creates them (Scenes/ActorEffectHooks.h), each slot with the function it names.
class IEffectHooks
{
public:
    virtual ~IEffectHooks() = default;

    // Effects::KillModelEffectsOf.
    virtual bool KillTheEffectsOf(Effects::ModelEffects& effects, std::int16_t type, const void* owner,
                                  std::int32_t subtype) const = 0;
    // Effects::ModelEffects::CreateSkillFlare.
    [[nodiscard]] virtual std::optional<std::size_t>
    CreateTheSkillFlare(Effects::ModelEffects& effects, const glm::vec3& position, const glm::vec3& angles,
                        const glm::vec3& light, std::int32_t subtype, void* owner) const = 0;
    // Effects::ModelEffects::CreateFullSetFlare.
    [[nodiscard]] virtual std::optional<std::size_t>
    CreateTheFullSetFlare(Effects::ModelEffects& effects, const glm::vec3& position, const glm::vec3& angles,
                          const glm::vec3& light, void* owner) const = 0;
    // Effects::ModelEffects::CreateIceShard.
    [[nodiscard]] virtual std::optional<std::size_t> CreateTheIceShard(Effects::ModelEffects& effects,
                                                                       const glm::vec3& position,
                                                                       const glm::vec3& angles, const glm::vec3& light,
                                                                       Crt::CrtRandom& random) const = 0;
    // Effects::ModelEffects::CreateDebris.
    [[nodiscard]] virtual std::optional<std::size_t> CreateTheDebris(Effects::ModelEffects& effects, std::int16_t type,
                                                                     const glm::vec3& position, const glm::vec3& angles,
                                                                     const glm::vec3& light, Crt::CrtRandom& random,
                                                                     std::int32_t subtype) const = 0;
    // Effects::CreateSkinShell.
    [[nodiscard]] virtual std::optional<std::size_t> CreateTheSkinShell(Effects::ModelEffects& effects,
                                                                        const Kernel::ModelEffectArguments& arguments,
                                                                        Crt::CrtRandom& random) const = 0;
    // Effects::CreateBlastModelEffect, without an owner read live (+0x108) and without services.
    [[nodiscard]] virtual Core::Expected<std::optional<std::size_t>, std::string>
    CreateTheBlastEffect(Effects::ModelEffects& effects, const Kernel::ModelEffectArguments& arguments,
                         Crt::CrtRandom& random) const = 0;
    // Effects::CreateHighModelEffect, with the world, its rand() and its clock.
    [[nodiscard]] virtual Core::Expected<std::optional<std::size_t>, std::string>
    CreateTheHighEffect(const Kernel::ModelEffectArguments& arguments, Kernel::WorldState& world,
                        Crt::CrtRandom& random, float worldClock) const = 0;
    // Effects::CreateWizardModelEffect in the world's table, with the services of Effects::WizardEffectServicesOf: the
    // world's particles and `scene`.
    [[nodiscard]] virtual Core::Expected<std::optional<std::size_t>, std::string>
    CreateTheWizardEffect(Kernel::WorldState& world, Crt::CrtRandom& random, const WizardEffectScene& scene,
                          const Kernel::ModelEffectArguments& arguments, glm::vec3* callerLight) const = 0;
    // Effects::PutTheSkullOn.
    [[nodiscard]] virtual Core::Expected<void, std::string> PutTheSkullOn(Kernel::ActorState& actor,
                                                                          Kernel::WorldState& world,
                                                                          Crt::CrtRandom& random,
                                                                          std::uint32_t address) const = 0;
    // Effects::GuildMarkIsDue.
    [[nodiscard]] virtual bool IsTheGuildMarkDue(const Kernel::ActorState& actor, float worldClock) const = 0;
    // Effects::RaiseTheGuildMark.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    RaiseTheGuildMark(Kernel::ActorState& actor, const glm::vec3& bonePoint, Effects::ModelEffects& effects,
                      Particles::Particles& particles, Crt::CrtRandom& random, float worldClock) const = 0;
};
} // namespace Client::Actors
