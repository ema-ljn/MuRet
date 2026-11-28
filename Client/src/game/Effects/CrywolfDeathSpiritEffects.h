#pragma once
#include "Core/Expected.h"
#include <glm/ext/vector_float3.hpp>
#include <cstddef>
#include <optional>
#include <string>
namespace Client::Kernel
{
struct ModelEffectArguments;
} // namespace Client::Kernel

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

namespace Client::Effects
{
struct ModelEffect;
struct ModelEffectStepServices;
class ModelEffects;
} // namespace Client::Effects
namespace Client::Effects
{
// 0x51c952: 0x147/0 and /1; owner points to the target's ActorState. When full, it neither reads the owner nor creates
// particles.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateCrywolfDeathSpirit(const Kernel::ModelEffectArguments&, Effects::ModelEffects&, Particles::Particles*,
                         Crt::CrtRandom&, float worldClock = 0);
// Body of 0x52709b: the dispatcher keeps the common CRT, animation and life.
// nativeStackPoint is [EBP−28] on entry: the first 0x61e/1 with life < 10 reads it before writing it.
[[nodiscard]] Core::Expected<void, std::string>
StepCrywolfDeathSpirit(Effects::ModelEffect&, Crt::CrtRandom&, const Effects::ModelEffectStepServices&,
                       std::optional<glm::vec3> nativeStackPoint = std::nullopt);
} // namespace Client::Effects
