#include "Scenes/WorldLoadingScene.h"

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/TextureSets.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
// El mosaico de 0x640c10 en 800 × 600: LSBg01 arriba a la izquierda (400 × 512), 02 arriba a la
// derecha, 03 abajo a la izquierda (400 × 88, en y 512) y 04 abajo a la derecha.
struct Tile
{
    std::uint16_t slot = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};
constexpr std::array<Tile, 4> Tiles = {{
    {0x71e, 0, 0, 400, 512},
    {0x71f, 400, 0, 400, 512},
    {0x720, 0, 512, 400, 88},
    {0x721, 400, 512, 400, 88},
}};
constexpr int MosaicWidth = 800;
constexpr int MosaicHeight = 600;
} // namespace

WorldLoadingScene::WorldLoadingScene(const SceneContext& context)
    : m_context(context), m_textures(context.textures, WorldLoadingTextureSet)
{
}

Core::Expected<void, std::string> WorldLoadingScene::Enter()
{
    if (m_context.world == nullptr || m_context.cloth == nullptr)
    {
        return Core::Unexpected(
            std::string("la carga del mundo no entra sin el WorldState y el rand() de la CRT: el contador de la "
                        "espera vive ahí y su cierre gasta rand()"));
    }
    WorldRuntime& runtime = m_context.world->Runtime();
    // 0x63571d: con la bandera levantada saltea el contador, la bandera y las texturas, y dibuja con el
    // objeto del mosaico de la UI, que fuera de esta llamada es nulo (0x65ed2e, 0x635a87): 0x647650 lo
    // leería en [8 + 0xa0]. La selección la baja siempre antes de pasar a la escena 3 y nadie más la
    // escribe, así que el original no llega acá; MuRet tampoco debería.
    if (runtime.loadingOnceFlag != 0)
    {
        return Core::Unexpected(
            std::string("la carga del mundo encontró levantada la bandera 0x7d3a13e: el original no llega a esa "
                        "rama, que dibuja con el mosaico nulo (0x635a52)"));
    }
    {
        // 0x635723..0x635963: abre el contador, escribe 0x98967f y lo cierra (0x412890).
        const Render::Models::LogicalAccess access(runtime.warpDepth, m_context.cloth->random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        runtime.warpCounter = WorldLoadingWait;
    }
    // 0x635980, antes de las texturas.
    runtime.loadingOnceFlag = 1;
    return m_textures.Load(m_context.dataRoot);
}

void WorldLoadingScene::Step(const Input::InputState& /*input*/)
{
    m_done = m_drawn;
}

void WorldLoadingScene::Render(SceneCanvas& canvas, float /*alpha*/)
{
    canvas.sprites.SetBlend(Render::BlendMode::Alpha);
    for (const Tile& tile : Tiles)
    {
        const TextureBufferSize buffer = m_textures.BufferSize(Render::TextureSlot{tile.slot});
        canvas.sprites.Image(Render::UiImage{.slot = Render::TextureSlot{tile.slot},
                                             .x = tile.x,
                                             .y = tile.y,
                                             .width = tile.width,
                                             .height = tile.height,
                                             .textureWidth = static_cast<float>(buffer.width),
                                             .textureHeight = static_cast<float>(buffer.height),
                                             .scaleX = Render::DesignScale(canvas.screen.width, MosaicWidth),
                                             .scaleY = Render::DesignScale(canvas.screen.height, MosaicHeight)});
    }
    m_drawn = true;
}

void WorldLoadingScene::Leave()
{
    m_textures.Release();
}

std::optional<SceneId> WorldLoadingScene::Next() const
{
    return m_done ? std::optional<SceneId>{SceneId::Game} : std::nullopt;
}

bool WorldLoadingScene::WantsToQuit() const
{
    return false;
}

bool WorldLoadingScene::WantsTextInput() const
{
    return false;
}

bool WorldLoadingScene::HoldsTheLastFrame() const
{
    return false;
}
} // namespace Client::Scenes
