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

// 441260→42fa40 y 426d90: cintas de armas propias de los monstruos, sobre la pose compartida.
[[nodiscard]] Core::Expected<void, std::string> EmitEarlyMonsterRibbonTrails(ActorState& actor,
                                                                             const Render::Models::Skeleton* skeleton,
                                                                             const CharacterStepContext& context);
[[nodiscard]] Core::Expected<void, std::string> EmitLateMonsterRibbonTrails(ActorState& actor,
                                                                            const Render::Models::Skeleton* skeleton,
                                                                            const CharacterStepContext& context);
} // namespace Client::Scenes
