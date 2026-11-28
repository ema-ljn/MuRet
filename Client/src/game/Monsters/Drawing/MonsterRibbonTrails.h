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

// 0x441260 → 0x42fa40 and 0x426d90: the monsters' own weapon ribbons, over the shared pose.
[[nodiscard]] Core::Expected<void, std::string>
EmitEarlyMonsterRibbonTrails(Kernel::ActorState& actor, const Models::Skeleton* skeleton,
                             const Actors::CharacterStepContext& context);
[[nodiscard]] Core::Expected<void, std::string>
EmitLateMonsterRibbonTrails(Kernel::ActorState& actor, const Models::Skeleton* skeleton,
                            const Actors::CharacterStepContext& context);
} // namespace Client::Monsters::Drawing
