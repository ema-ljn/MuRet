#pragma once

#include "Core/Expected.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
struct ClothWorld;
} // namespace Client::Render::Models

namespace Client::World
{
class Particles;
}

// 530860: ownerContext es FaunaObject durable salvo613/4 y614/14, donde permanece opaco.
// Se escribe sobre el primer muerto sin reconstruirlo; pool lleno no lee los argumentos ni RNG.
namespace Client::Scenes
{
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateFaunaJoint(std::span<JointState> joints, const World::FishEffectRequest& request,
                 Render::Models::CrtRandom& random);
// 535840: 697/0 y618/4 siguen el+40 del dueño;610/8 sigue position con z300;
// 10f/3 usa vida/RNG, seno y color del dueño.614/14 gira e itera su cola sin leer al dueño;
// sólo la llegada con width50 necesita particles, y el humo usa worldClock.
// El overload sin RNG conserva la frontera535f18 tras movimiento y comprobación del dueño.
[[nodiscard]] Core::Expected<void, std::string> StepJoints(std::span<JointState> joints);
[[nodiscard]] Core::Expected<void, std::string> StepJoints(std::span<JointState> joints,
                                                           Render::Models::CrtRandom& random,
                                                           Render::Models::ClothWorld* cloth = nullptr,
                                                           World::Particles* particles = nullptr, float worldClock = 0);
} // namespace Client::Scenes
