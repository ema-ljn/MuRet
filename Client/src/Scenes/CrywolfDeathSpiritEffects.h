#pragma once
#include "Core/Expected.h"
#include <glm/ext/vector_float3.hpp>
#include <cstddef>
#include <optional>
#include <string>
namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
struct ModelEffect;
struct ModelEffectArguments;
struct ModelEffectStepServices;
class ModelEffects;
class Particles;
} // namespace Client::World
namespace Client::Scenes
{
// 51c952:147/0 y /1; owner apunta al ActorState del blanco. Lleno no lee al dueño ni crea partículas.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateCrywolfDeathSpirit(const World::ModelEffectArguments&, World::ModelEffects&, World::Particles*,
                         Render::Models::CrtRandom&, float worldClock = 0);
// 5192a3:104/0, sin RNG. El dueño del trail permanece como ModelEffect::owner.effect.
[[nodiscard]] std::optional<std::size_t> CreateCrywolfSpiritTrail(const World::ModelEffectArguments&,
                                                                  World::ModelEffects&);
// Cuerpos de52709b y5212e7: el dispatcher conserva CRT, animación y vida comunes.
// nativeStackPoint es [EBP−28] al entrar: la primera61e/1 de vida<10 lo lee antes de escribirlo.
[[nodiscard]] Core::Expected<void, std::string>
StepCrywolfDeathSpirit(World::ModelEffect&, Render::Models::CrtRandom&, const World::ModelEffectStepServices&,
                       std::optional<glm::vec3> nativeStackPoint = std::nullopt);
void StepCrywolfSpiritTrail(World::ModelEffect&);
} // namespace Client::Scenes
