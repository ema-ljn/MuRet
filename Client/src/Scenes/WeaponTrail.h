#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Render::Models
{
struct Skeleton;
}

namespace Client::Scenes
{
struct ActorState;
struct CharacterStepContext;

// 4fcee0, productor común hasta4fd5bb: usa el modelo publicado antes del cuerpo y conserva el
// scratch global de pose. La continuación de los cuatro helpers pertenece al caller.
[[nodiscard]] Core::Expected<void, std::string>
EmitWeaponTrail(ActorState& actor, const Render::Models::Skeleton* skeleton, const CharacterStepContext& context);
} // namespace Client::Scenes
