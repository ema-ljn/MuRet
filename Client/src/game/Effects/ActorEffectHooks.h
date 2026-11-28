#pragma once

#include "Actors/EffectHooks.h"
#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

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

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

// The model effects the characters ask for (Actors/EffectHooks.h), each slot with the effects' function it names. It
// keeps no state: whoever builds a pass, a step or a world entry can own one.
namespace Client::Effects
{
class ActorEffectHooks final : public Actors::IEffectHooks
{
public:
    bool KillTheEffectsOf(Effects::ModelEffects& effects, std::int16_t type, const void* owner,
                          std::int32_t subtype) const override;
    [[nodiscard]] std::optional<std::size_t> CreateTheSkillFlare(Effects::ModelEffects& effects,
                                                                 const glm::vec3& position, const glm::vec3& angles,
                                                                 const glm::vec3& light, std::int32_t subtype,
                                                                 void* owner) const override;
    [[nodiscard]] std::optional<std::size_t> CreateTheFullSetFlare(Effects::ModelEffects& effects,
                                                                   const glm::vec3& position, const glm::vec3& angles,
                                                                   const glm::vec3& light, void* owner) const override;
    [[nodiscard]] std::optional<std::size_t> CreateTheIceShard(Effects::ModelEffects& effects,
                                                               const glm::vec3& position, const glm::vec3& angles,
                                                               const glm::vec3& light,
                                                               Crt::CrtRandom& random) const override;
    [[nodiscard]] std::optional<std::size_t> CreateTheDebris(Effects::ModelEffects& effects, std::int16_t type,
                                                             const glm::vec3& position, const glm::vec3& angles,
                                                             const glm::vec3& light, Crt::CrtRandom& random,
                                                             std::int32_t subtype) const override;
    [[nodiscard]] std::optional<std::size_t> CreateTheSkinShell(Effects::ModelEffects& effects,
                                                                const Kernel::ModelEffectArguments& arguments,
                                                                Crt::CrtRandom& random) const override;
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
    CreateTheBlastEffect(Effects::ModelEffects& effects, const Kernel::ModelEffectArguments& arguments,
                         Crt::CrtRandom& random) const override;
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
    CreateTheHighEffect(const Kernel::ModelEffectArguments& arguments, Kernel::WorldState& world,
                        Crt::CrtRandom& random, float worldClock) const override;
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
    CreateTheWizardEffect(Kernel::WorldState& world, Crt::CrtRandom& random, const Actors::WizardEffectScene& scene,
                          const Kernel::ModelEffectArguments& arguments, glm::vec3* callerLight) const override;
    [[nodiscard]] Core::Expected<void, std::string> PutTheSkullOn(Kernel::ActorState& actor, Kernel::WorldState& world,
                                                                  Crt::CrtRandom& random,
                                                                  std::uint32_t address) const override;
    [[nodiscard]] bool IsTheGuildMarkDue(const Kernel::ActorState& actor, float worldClock) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    RaiseTheGuildMark(Kernel::ActorState& actor, const glm::vec3& bonePoint, Effects::ModelEffects& effects,
                      Particles::Particles& particles, Crt::CrtRandom& random, float worldClock) const override;
};
} // namespace Client::Effects
