#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Scenes/ActorWalk.h"
#include "Scenes/ActorCooldownServices.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::Scenes
{
struct ActorState;
struct WorldRuntime;
struct ActorMotionContext
{
    WorldRuntime& runtime;
    Render::Models::CrtRandom& random;
    std::function<double(float, float)> height;
};
// Los accesos lógicos de MovePath se conservan antes/después de cada store, sin tocar el blanco.
[[nodiscard]] bool MoveActorPath(ActorState& actor, std::uint8_t rotate, Render::Models::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> UpdateActorPosition(ActorState& actor,
                                                                    const ActorMotionContext& context);
// 0x508ba0. El héroe lo excluye el caller. Quieto reintenta sin moverse; caminando avanza incluso
// después del snap de llegada y de fijar el yaw de D3. Usa el TerrainWall vivo, sin reconstruirlo.
[[nodiscard]] Core::Expected<void, std::string> StepOtherActorMotion(ActorState& actor, const ActorWalkContext& walk,
                                                                     const ActorMotionContext& motion,
                                                                     Data::Terrain::TerrainLayer<std::uint16_t>& walls);
// Sólo lo llama 0x505de0 para personajes vistos. La resta ocurre antes de sus efectos.
[[nodiscard]] Core::Expected<void, std::string> StepActorCooldown(ActorState& actor, Render::Models::CrtRandom& random,
                                                                  const ActorCooldownServices& services = {});
} // namespace Client::Scenes
