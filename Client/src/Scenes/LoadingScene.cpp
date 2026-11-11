#include "Scenes/LoadingScene.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Render/Models/Cloth.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameModels.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/TextureSets.h"
#include "Scenes/WorldState.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
// El tamaño en que se diseña la UI de la carga: las inversas de 0x6ba884 y 0x6ba880.
constexpr int DesignWidth = 800;
constexpr int DesignHeight = 600;

// Una imagen de 0x65eeb0: el slot, el tamaño de 0x647050 y la posición de 0x6472e0, en 800 × 600.
struct LoadingImage
{
    std::uint16_t slot = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// En el orden en que las dibuja 0x65f200 (los objetos de 0xa8 bytes desde +0x8dd4).
constexpr std::array<LoadingImage, 5> Images = {{
    {.slot = 0x71e, .x = 0, .y = 60, .width = 512, .height = 478},
    {.slot = 0x71f, .x = 512, .y = 60, .width = 288, .height = 478},
    {.slot = 0x720, .x = 3, .y = 80, .width = 93, .height = 48},
    {.slot = 0x721, .x = 544, .y = 60, .width = 256, .height = 206},
    {.slot = 0x722, .x = 307, .y = 225, .width = 187, .height = 151},
}};

// La barra (0x6402d0 y 0x6404b0), que 0x6407e0 dibuja después de las cinco.
constexpr LoadingImage Bar{.slot = 0x723, .x = 72, .y = 482, .width = 656, .height = 15};

Render::UiImage ImageOf(const LoadingImage& image, const SceneTextures& textures, App::PixelSize screen)
{
    const TextureBufferSize buffer = textures.BufferSize(Render::TextureSlot{image.slot});
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

LoadingScene::LoadingScene(const SceneContext& context, GameModels* models)
    : m_context(context), m_models(models), m_textures(context.textures, LoadingTextureSet)
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
        // El avance 1 de 0x61ccb0 lee los modelos (0x60dbe0 y 0x60ff50) y sus texturas.
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
    // 60dc4c elige la base de la tabla global antes de abrir el primer BMD; el dueño
    // conserva esa elección aunque falle un archivo o se vuelva a entrar en la carga.
    if (m_context.world != nullptr && m_context.cloth != nullptr)
    {
        auto& fauna = m_context.world->FaunaResources();
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

void LoadingScene::Render(SceneCanvas& canvas, float /*alpha*/)
{
    canvas.sprites.SetBlend(Render::BlendMode::Alpha);
    for (const LoadingImage& image : Images)
    {
        canvas.sprites.Image(ImageOf(image, m_textures, canvas.screen));
    }
    // 0x640580(actual, 10): el ancho es 656 · actual / 10, en enteros sin signo (con actual de 0 a
    // 10, lo mismo que con signo), y 0x647370(ancho, 0, 1) lo pone: la u de la derecha llega al
    // ancho, sin medio téxel. La v de abajo ya llegaba a 15 / th: 0x6402d0 la arma con 4 de ancho
    // y llama a 0x647370(0, 15, 2).
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

std::optional<SceneId> LoadingScene::Next() const
{
    if (m_done)
    {
        return SceneId::Login;
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
