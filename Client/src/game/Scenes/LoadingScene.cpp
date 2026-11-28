#include "Scenes/LoadingScene.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Assets/FaunaModels.h"
#include "Assets/GameModels.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Assets/SceneTextures.h"
#include "Assets/TextureSets.h"
#include "Kernel/WorldState.h"
#include "Session/ModuleStates.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
// The size the loading UI is designed in: the inverses of 0x6ba884 and 0x6ba880.
constexpr int DesignWidth = 800;
constexpr int DesignHeight = 600;

// An image of 0x65eeb0: the slot, 0x647050's size and 0x6472e0's position, in 800 × 600.
struct LoadingImage
{
    std::uint16_t slot = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// In the order 0x65f200 draws them (the 0xa8-byte objects from +0x8dd4).
constexpr std::array<LoadingImage, 5> Images = {{
    {.slot = 0x71e, .x = 0, .y = 60, .width = 512, .height = 478},
    {.slot = 0x71f, .x = 512, .y = 60, .width = 288, .height = 478},
    {.slot = 0x720, .x = 3, .y = 80, .width = 93, .height = 48},
    {.slot = 0x721, .x = 544, .y = 60, .width = 256, .height = 206},
    {.slot = 0x722, .x = 307, .y = 225, .width = 187, .height = 151},
}};

// The bar (0x6402d0 and 0x6404b0), which 0x6407e0 draws after the five.
constexpr LoadingImage Bar{.slot = 0x723, .x = 72, .y = 482, .width = 656, .height = 15};

Render::UiImage ImageOf(const LoadingImage& image, const Assets::SceneTextures& textures, Core::PixelSize screen)
{
    const Assets::TextureBufferSize buffer = textures.BufferSize(Render::TextureSlot{image.slot});
    return Render::UiImage{.slot = Render::TextureSlot{image.slot},
                           .x = image.x,
                           .y = image.y,
                           .width = image.width,
                           .height = image.height,
                           .textureWidth = static_cast<float>(buffer.width),
                           .textureHeight = static_cast<float>(buffer.height),
                           .scaleX = Render::DesignScale(screen.width, DesignWidth),
                           .scaleY = Render::DesignScale(screen.height, DesignHeight)};
}
} // namespace

LoadingScene::LoadingScene(const SceneContext& context, Assets::GameModels* models)
    : m_context(context), m_models(models), m_textures(context.textures, Assets::LoadingTextureSet)
{
}

Core::Expected<void, std::string> LoadingScene::Enter()
{
    return m_textures.Load(m_context.dataRoot);
}

void LoadingScene::Step(const Input::InputState& /*input*/)
{
    if (m_failed)
    {
        return;
    }
    if (m_progress < LoadingAdvances)
    {
        ++m_progress;
        // 0x61ccb0's advance 1 reads the models (0x60dbe0 and 0x60ff50) and their textures.
        if (m_progress == 1 && m_models != nullptr)
        {
            if (auto loaded = LoadModels(); !loaded.HasValue())
            {
                m_context.log.Error("la carga no puede seguir: {}", loaded.Error());
                m_failed = true;
            }
        }
        return;
    }
    m_done = true;
}

Core::Expected<void, std::string> LoadingScene::LoadModels()
{
    // 0x60dc4c picks the global table's base before opening the first BMD; the owner keeps that choice even if a file
    // fails or the loading is entered again.
    if (m_context.states != nullptr && m_context.cloth != nullptr)
    {
        auto& fauna = m_context.states->assets.fauna;
        if (!fauna.PrefixOffset())
        {
            constexpr int NativePrefixMask = 1023;
            const auto offset = static_cast<std::uint16_t>(m_context.cloth->random.Next() & NativePrefixMask);
            if (auto initialized = fauna.InitializePrefix(offset); !initialized.HasValue())
                return initialized;
        }
    }
    return m_models->Load(m_context.dataRoot, m_context.textures, m_context.log);
}

void LoadingScene::Render(Render::Frame::SceneCanvas& canvas, float /*alpha*/)
{
    canvas.sprites.SetBlend(Render::BlendMode::Alpha);
    for (const LoadingImage& image : Images)
    {
        canvas.sprites.Image(ImageOf(image, m_textures, canvas.screen));
    }
    // 0x640580(actual, 10): the width is 656 · actual / 10, in unsigned integers (with actual from 0 to 10, the same as
    // signed), and 0x647370(width, 0, 1) sets it: the right u reaches the width, without half a texel. The bottom v
    // already reached 15 / th: 0x6402d0 builds it 4 wide and calls 0x647370(0, 15, 2).
    Render::UiImage bar = ImageOf(Bar, m_textures, canvas.screen);
    bar.width = Bar.width * m_progress / LoadingAdvances;
    bar.resizedWidth = true;
    bar.resizedHeight = true;
    canvas.sprites.Image(bar);
}

void LoadingScene::Leave()
{
    m_textures.Release();
}

std::optional<Kernel::SceneId> LoadingScene::Next() const
{
    if (m_done)
    {
        return Kernel::SceneId::Login;
    }
    return std::nullopt;
}

bool LoadingScene::WantsToQuit() const
{
    return m_failed;
}

bool LoadingScene::WantsTextInput() const
{
    return false;
}

bool LoadingScene::HoldsTheLastFrame() const
{
    return false;
}

int LoadingScene::Progress() const
{
    return m_progress;
}
} // namespace Client::Scenes
