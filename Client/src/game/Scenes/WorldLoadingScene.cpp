#include "Scenes/WorldLoadingScene.h"

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Assets/SceneTextures.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Assets/TextureSets.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x640c10's mosaic in 800 × 600: LSBg01 top left (400 × 512), 02 top right, 03 bottom left (400 × 88, at y = 512)
// and 04 bottom right.
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
    : m_context(context), m_textures(context.textures, Assets::WorldLoadingTextureSet)
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
    Kernel::WorldRuntime& runtime = m_context.world->Runtime();
    // 0x63571d: with the flag up it skips the counter, the flag and the textures, and draws with the UI's mosaic
    // object, which outside this call is null (0x65ed2e, 0x635a87): 0x647650 would read it at [8 + 0xa0]. The selection
    // always lowers it before going to scene 3 and nobody else writes it, so the original does not get here; MuRet
    // should not either.
    if (runtime.loadingOnceFlag != 0)
    {
        return Core::Unexpected(
            std::string("la carga del mundo encontró levantada la bandera 0x7d3a13e: el original no llega a esa "
                        "rama, que dibuja con el mosaico nulo (0x635a52)"));
    }
    {
        // 0x635723..0x635963: opens the counter, writes 0x98967f and closes it (0x412890).
        const Models::LogicalAccess access(runtime.warpDepth, m_context.cloth->random,
                                           Models::SmallLogicalCloseRandoms);
        runtime.warpCounter = Kernel::WorldLoadingWait;
    }
    // 0x635980, before the textures.
    runtime.loadingOnceFlag = 1;
    return m_textures.Load(m_context.dataRoot);
}

void WorldLoadingScene::Step(const Input::InputState& /*input*/)
{
    m_done = m_drawn;
}

void WorldLoadingScene::Render(Render::Frame::SceneCanvas& canvas, float /*alpha*/)
{
    canvas.sprites.SetBlend(Render::BlendMode::Alpha);
    for (const Tile& tile : Tiles)
    {
        const Assets::TextureBufferSize buffer = m_textures.BufferSize(Render::TextureSlot{tile.slot});
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

std::optional<Kernel::SceneId> WorldLoadingScene::Next() const
{
    return m_done ? std::optional<Kernel::SceneId>{Kernel::SceneId::Game} : std::nullopt;
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
