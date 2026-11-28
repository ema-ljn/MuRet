#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
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
namespace Client::Effects
{
class ModelEffects;
struct ModelEffect;
struct ModelEffectStepServices;
} // namespace Client::Effects

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

namespace Client::CastleSiege
{

[[nodiscard]] bool IsSiegeVertexEffect(std::int16_t type);
// 0x51d2a9/0x51d5b8: prefix 0x518cf0 and fragments 0x12d..0x133 and 0x13c (with the 0x12d/2, the 0x12e/2 and the
// 0x12e/3). Full consumes no CRT.
[[nodiscard]] std::optional<std::size_t> CreateSiegeVertexEffect(const Kernel::ModelEffectArguments& arguments,
                                                                 Effects::ModelEffects& effects,
                                                                 Crt::CrtRandom& random);
// 0x5290d4/0x5293ef: the step's body; the caller runs the common prologue and epilogue of 0x5208a0. `worldClock` is
// the one of the particles of the 0x13c, the 0x12d/2 and the 0x12e/3.
[[nodiscard]] Core::Expected<void, std::string>
StepSiegeVertexEffect(Effects::ModelEffect& effect, Particles::Particles& particles, Crt::CrtRandom& random,
                      const Effects::ModelEffectStepServices& services, float worldClock);
} // namespace Client::CastleSiege
