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
// 63750d..6375e5. El pase2 vuelve a la misma cámara y dibuja agua: no refleja la vista.
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
    // 437850 se lee dos veces; el byte de modo por mapa no reemplaza el estado723c48.
    std::function<Core::Expected<bool, std::string>()> hasKalimaWorld;
};
[[nodiscard]] std::uint8_t GameTailFirstMode(std::int32_t map);
[[nodiscard]] Core::Expected<void, std::string> RunGameTailPasses(std::int32_t map, std::int32_t heroCellLayer,
                                                                  const GameTailPassCallbacks& callbacks);
// Captura antes del flush:6213e0 conserva material;620db0 reinicia sus ejes acreditados.
void BeginGameTailPass(Render::ModelBatch& batch, const Render::Pass3D& world, bool eye,
                       const std::function<void()>& flushModels);
} // namespace Client::Scenes
