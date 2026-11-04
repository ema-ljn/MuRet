#include "Scenes/WorldLoadingScene.h"

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/TextureSets.h"

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
