#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Models
{
struct Skeleton;
}

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Actors::Drawing
{

// 4fcee0, productor común hasta4fd5bb: usa el modelo publicado antes del cuerpo y conserva el
// scratch global de pose. La continuación de los cuatro helpers pertenece al caller.
[[nodiscard]] Core::Expected<void, std::string>
EmitWeaponTrail(Kernel::ActorState& actor, const Models::Skeleton* skeleton, const CharacterStepContext& context);
} // namespace Client::Actors::Drawing
