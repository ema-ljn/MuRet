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

// 0x4fcee0, the common producer up to 0x4fd5bb: it uses the model published before the body and keeps the global pose
// scratch. The continuation of the four helpers belongs to the caller.
[[nodiscard]] Core::Expected<void, std::string>
EmitWeaponTrail(Kernel::ActorState& actor, const Models::Skeleton* skeleton, const CharacterStepContext& context);
} // namespace Client::Actors::Drawing
