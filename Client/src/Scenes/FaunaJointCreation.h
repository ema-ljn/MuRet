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
}

// 530860 para los joints emitidos por fauna. ownerContext debe ser un FaunaObject durable.
// Se escribe sobre el primer muerto sin reconstruirlo; pool lleno no lee los argumentos ni RNG.
namespace Client::Scenes
{
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateFaunaJoint(std::span<JointState> joints, const World::FishEffectRequest& request,
                 Render::Models::CrtRandom& random);
// 535840: 697/0 y618/4 siguen el+40 del dueño;610/8 sigue position con z300;
// 10f/3 usa vida/RNG, seno y color del dueño.
// El overload sin RNG conserva la frontera535f18 tras movimiento y comprobación del dueño.
[[nodiscard]] Core::Expected<void, std::string> StepJoints(std::span<JointState> joints);
[[nodiscard]] Core::Expected<void, std::string> StepJoints(std::span<JointState> joints,
                                                           Render::Models::CrtRandom& random);
} // namespace Client::Scenes
