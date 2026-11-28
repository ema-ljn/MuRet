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
// 51c952:147/0 y /1; owner apunta al ActorState del blanco. Lleno no lee al dueño ni crea partículas.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateCrywolfDeathSpirit(const Kernel::ModelEffectArguments&, Effects::ModelEffects&, Particles::Particles*,
                         Crt::CrtRandom&, float worldClock = 0);
// Cuerpo de52709b: el dispatcher conserva CRT, animación y vida comunes.
// nativeStackPoint es [EBP−28] al entrar: la primera61e/1 de vida<10 lo lee antes de escribirlo.
[[nodiscard]] Core::Expected<void, std::string>
StepCrywolfDeathSpirit(Effects::ModelEffect&, Crt::CrtRandom&, const Effects::ModelEffectStepServices&,
                       std::optional<glm::vec3> nativeStackPoint = std::nullopt);
} // namespace Client::Effects
