#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
class ModelEffects;
class Particles;
struct ModelEffect;
struct ModelEffectArguments;
struct ModelEffectStepServices;

[[nodiscard]] bool IsSiegeVertexEffect(std::int16_t type);
// 51d2a9/51d5b8: prefijo518cf0 y fragmentos12d..133. Lleno no consume CRT.
[[nodiscard]] std::optional<std::size_t> CreateSiegeVertexEffect(const ModelEffectArguments& arguments,
                                                                 ModelEffects& effects,
                                                                 Render::Models::CrtRandom& random);
// 5291ce/5293ef: cuerpo del paso; el caller ejecuta el prólogo y epílogo comunes de5208a0.
[[nodiscard]] Core::Expected<void, std::string> StepSiegeVertexEffect(ModelEffect& effect, Particles& particles,
                                                                      Render::Models::CrtRandom& random,
                                                                      const ModelEffectStepServices& services);
} // namespace Client::World
