#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Render
{
class ModelBatch;
struct Pass3D;
} // namespace Client::Render
namespace Client::Scenes
{
// 0x63750d..0x6375e5. Pass 2 goes back to the same camera and draws water: it does not reflect the view.
enum class GameTailStage : std::uint8_t
{
    WorldLeaves,
    Joints,
    ModelEffects,
    Ribbons,
    MarkSpritesSeen,
    BeginEye,
    EyeLeaves,
    Sprites,
    Particles,
    Damage,
    BeginWorld,
    KalimaWater,
};
struct GameTailPassCallbacks
{
    std::function<Core::Expected<void, std::string>(GameTailStage, std::uint8_t)> draw;
    // 0x437850 is read twice; the per-map mode byte does not replace the state at 0x723c48.
    std::function<Core::Expected<bool, std::string>()> hasKalimaWorld;
};
[[nodiscard]] std::uint8_t GameTailFirstMode(std::int32_t map);
[[nodiscard]] Core::Expected<void, std::string> RunGameTailPasses(std::int32_t map, std::int32_t heroCellLayer,
                                                                  const GameTailPassCallbacks& callbacks);
// Captures before the flush: 0x6213e0 keeps the material; 0x620db0 resets its attested axes.
void BeginGameTailPass(Render::ModelBatch& batch, const Render::Pass3D& world, bool eye,
                       const std::function<void()>& flushModels);
} // namespace Client::Scenes
