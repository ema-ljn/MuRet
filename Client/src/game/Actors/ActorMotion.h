#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Actors/ActorWalk.h"
#include "Actors/ActorCooldownServices.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Kernel
{
struct ActorState;
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Actors
{
struct ActorMotionContext
{
    Kernel::WorldRuntime& runtime;
    Crt::CrtRandom& random;
    std::function<double(float, float)> height;
    // 0x4eac0a..0x4eac41: the A* flag 0x4eac00 computes on entry (Events::Crywolf::CrywolfAltarGoal) for the path
    // retry (0x508e0e). Nothing in 0x508ba0 before the retry changes what it looks at.
    bool altarGoal = false;
};
// MovePath's logical accesses are kept before and after each store, without touching the target.
[[nodiscard]] bool MoveActorPath(Kernel::ActorState& actor, std::uint8_t rotate, Crt::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> UpdateActorPosition(Kernel::ActorState& actor,
                                                                    const ActorMotionContext& context);
// 0x508ba0. The caller leaves the hero out. Standing still it retries without moving; walking it advances even
// after the arrival snap and after fixing the yaw of D3. It uses the live TerrainWall, without rebuilding it.
[[nodiscard]] Core::Expected<void, std::string> StepOtherActorMotion(Kernel::ActorState& actor,
                                                                     const ActorWalkContext& walk,
                                                                     const ActorMotionContext& motion,
                                                                     Data::Terrain::TerrainLayer<std::uint16_t>& walls);
// Only 0x505de0 calls it, for seen characters. The subtraction happens before its effects.
[[nodiscard]] Core::Expected<void, std::string> StepActorCooldown(Kernel::ActorState& actor, Crt::CrtRandom& random,
                                                                  const ActorCooldownServices& services = {});
} // namespace Client::Actors
