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

namespace Client::Monsters::Drawing
{

// 441260→42fa40 y 426d90: cintas de armas propias de los monstruos, sobre la pose compartida.
[[nodiscard]] Core::Expected<void, std::string>
EmitEarlyMonsterRibbonTrails(Kernel::ActorState& actor, const Models::Skeleton* skeleton,
                             const Actors::CharacterStepContext& context);
[[nodiscard]] Core::Expected<void, std::string>
EmitLateMonsterRibbonTrails(Kernel::ActorState& actor, const Models::Skeleton* skeleton,
                            const Actors::CharacterStepContext& context);
} // namespace Client::Monsters::Drawing
