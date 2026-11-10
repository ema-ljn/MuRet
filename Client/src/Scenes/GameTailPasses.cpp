#include "Scenes/GameTailPasses.h"

#include "Core/Expected.h"
#include "Render/ModelBatch.h"
#include "Render/Pass3D.h"

#include <glm/ext/matrix_float4x4.hpp>

#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
Core::Expected<void, std::string> Draw(const GameTailPassCallbacks& callbacks, GameTailStage stage,
                                       std::uint8_t mode = 0)
{
    if (!callbacks.draw)
        return Core::Unexpected(std::string("0x63750d: falta el renderer de la cola del frame"));
    return callbacks.draw(stage, mode);
}
Core::Expected<bool, std::string> HasKalimaWorld(const GameTailPassCallbacks& callbacks)
{
    if (!callbacks.hasKalimaWorld)
        return Core::Unexpected(std::string("0x437850: falta la consulta del mundo Kalima 0x723c48"));
    return callbacks.hasKalimaWorld();
}
Core::Expected<void, std::string> Bodies(const GameTailPassCallbacks& callbacks, std::uint8_t joints,
                                         std::uint8_t effects)
{
    if (auto result = Draw(callbacks, GameTailStage::Joints, joints); !result.HasValue())
        return result;
    if (auto result = Draw(callbacks, GameTailStage::ModelEffects, effects); !result.HasValue())
        return result;
    if (auto result = Draw(callbacks, GameTailStage::Ribbons); !result.HasValue())
        return result;
    return Draw(callbacks, GameTailStage::MarkSpritesSeen, joints);
}
Core::Expected<void, std::string> Eye(const GameTailPassCallbacks& callbacks, std::uint8_t mode, bool deviasLeaves)
{
    if (auto result = Draw(callbacks, GameTailStage::BeginEye, mode); !result.HasValue())
        return result;
    if (deviasLeaves)
        if (auto result = Draw(callbacks, GameTailStage::EyeLeaves); !result.HasValue())
            return result;
    if (auto result = Draw(callbacks, GameTailStage::Sprites, mode); !result.HasValue())
        return result;
    return Draw(callbacks, GameTailStage::Particles, mode);
}
} // namespace

std::uint8_t GameTailFirstMode(std::int32_t map)
{
    constexpr std::int32_t FirstKalima = 24;
    constexpr std::int32_t LastKalima = 29;
    constexpr std::int32_t SeventhKalima = 36;
    return static_cast<std::uint8_t>((map >= FirstKalima && map <= LastKalima) || map == SeventhKalima);
}

Core::Expected<void, std::string> RunGameTailPasses(std::int32_t map, std::int32_t heroCellLayer,
                                                    const GameTailPassCallbacks& callbacks)
{
    const auto mode = GameTailFirstMode(map); // 6373dc..6373f5.
    const bool deviasLeaves = map == 2 && heroCellLayer != 3 && heroCellLayer < 10;
    if (auto result = Draw(callbacks, GameTailStage::WorldLeaves); !result.HasValue())
        return result;
    if (auto result = Bodies(callbacks, mode, 0); !result.HasValue())
        return result;
    if (auto result = Eye(callbacks, mode, deviasLeaves); !result.HasValue())
        return result;
    const auto firstWorld = HasKalimaWorld(callbacks); // 637560: sólo omite daño, no consulta el mapa.
    if (!firstWorld.HasValue())
        return Core::Unexpected(firstWorld.Error());
    if (!firstWorld.Value())
        if (auto result = Draw(callbacks, GameTailStage::Damage, mode); !result.HasValue())
            return result;
    const auto secondWorld = HasKalimaWorld(callbacks); // 637577, después del primer6213f0.
    if (!secondWorld.HasValue())
        return Core::Unexpected(secondWorld.Error());
    if (!secondWorld.Value())
        return {};
    if (auto result = Draw(callbacks, GameTailStage::BeginWorld, 2); !result.HasValue())
        return result;
    if (auto result = Draw(callbacks, GameTailStage::KalimaWater); !result.HasValue())
        return result;
    if (auto result = Bodies(callbacks, 2, 1); !result.HasValue())
        return result;
    if (auto result = Eye(callbacks, 2, deviasLeaves); !result.HasValue())
        return result;
    return Draw(callbacks, GameTailStage::Damage, 2);
}

void BeginGameTailPass(Render::ModelBatch& batch, const Render::Pass3D& world, bool eye)
{
    auto pass = world;
    if (eye)
        pass.view = glm::mat4(1.0f);
    auto state = batch.State();
    // space describe las coordenadas del emisor, no un eje del material GL que persista entre matrices.
    state.space = Render::ModelCoordinateSpace::World;
    batch.Begin(pass);
    batch.SetState(state);
}
} // namespace Client::Scenes
