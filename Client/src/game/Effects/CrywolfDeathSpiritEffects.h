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
namespace Client::World
{
struct ModelEffect;
struct ModelEffectStepServices;
class ModelEffects;
class Particles;
} // namespace Client::World
namespace Client::Scenes
{
// 51c952:147/0 y /1; owner apunta al ActorState del blanco. Lleno no lee al dueño ni crea partículas.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateCrywolfDeathSpirit(const Kernel::ModelEffectArguments&, World::ModelEffects&, World::Particles*, Crt::CrtRandom&,
                         float worldClock = 0);
// Cuerpo de52709b: el dispatcher conserva CRT, animación y vida comunes.
// nativeStackPoint es [EBP−28] al entrar: la primera61e/1 de vida<10 lo lee antes de escribirlo.
[[nodiscard]] Core::Expected<void, std::string>
StepCrywolfDeathSpirit(World::ModelEffect&, Crt::CrtRandom&, const World::ModelEffectStepServices&,
                       std::optional<glm::vec3> nativeStackPoint = std::nullopt);
} // namespace Client::Scenes
