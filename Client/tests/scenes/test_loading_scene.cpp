#include "Scenes/LoadingScene.h"

#include "App/WindowState.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Render/QuadCorner.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Scenes/GameData.h"
#include "Scenes/IScene.h"
#include "Scenes/LoginScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneManager.h"
#include "Scenes/TextureSets.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/ScenePrinters.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Client::Scenes::LoadingScene: la escena 1 (0x622e40, con su UI de 0x65eeb0 y sus frames de
// 0x65f200), con un Data\ sintético y la tabla de texturas falsa.
namespace
{
using Client::App::PixelSize;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Input::InputState;
using Client::Render::BlendMode;
using Client::Render::DesignScale;
using Client::Render::QuadCorner;
using Client::Render::QuadCorners;
using Client::Render::TextureSlot;
using Client::Render::UiImage;
using Client::Render::UiImageCorners;
using Client::Scenes::LoadingAdvances;
using Client::Scenes::LoadingScene;
using Client::Scenes::LoadingTextureSet;
using Client::Scenes::LoginScene;
using Client::Scenes::LoginTextureSet;
using Client::Scenes::SceneContext;
using Client::Scenes::SceneId;
using Client::Scenes::SceneManager;
using Client::Scenes::WindowTextureSet;
using Client::Tests::FakeClock;
using Client::Tests::FakeTextureStore;
using Client::Tests::FrameEvent;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;
using Client::Tests::StoreCall;
using Client::Tests::TemporaryFolder;
using Client::Tests::WriteTextureSet;

// Las cinco imágenes de 0x65eeb0 (0x647050 con el ancho, el alto y el slot, y 0x6472e0 con la x y
// la y, en 800 × 600), en el orden en que las dibuja 0x65f200 (el bucle de 0x65f22f, de a 0xa8
// bytes), y la barra (0x6402d0 y 0x6404b0).
struct DesignImage
{
    std::uint16_t slot = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};
constexpr std::array<DesignImage, 5> Images = {{
    {0x71e, 0, 60, 512, 478},
    {0x71f, 512, 60, 288, 478},
    {0x720, 3, 80, 93, 48},
    {0x721, 544, 60, 256, 206},
    {0x722, 307, 225, 187, 151},
}};
constexpr DesignImage Bar{0x723, 72, 482, 656, 15};

// Las esquinas de un quad anotado: el fan (0, 1, 2) y (0, 2, 3) tiene la primera en el vértice
// 0, la segunda en el 1, la tercera en el 2 y la cuarta en el 5.
QuadCorners CornersOf(const FrameEvent& event)
{
    QuadCorners corners{};
    constexpr std::array<std::size_t, 4> fan = {0, 1, 2, 5};
    for (std::size_t i = 0; i < fan.size(); ++i)
    {
        const auto& vertex = event.vertices[fan[i]];
        corners[i] = QuadCorner{vertex.x, vertex.y, vertex.u, vertex.v};
    }
    return corners;
}

void ExpectSameCorners(const QuadCorners& actual, const QuadCorners& expected)
{
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(actual[i].x, expected[i].x);
        EXPECT_EQ(actual[i].y, expected[i].y);
        EXPECT_EQ(actual[i].u, expected[i].u);
        EXPECT_EQ(actual[i].v, expected[i].v);
    }
}

struct Harness
{
    explicit Harness(PixelSize screen = {640, 480}) : log(sink, LogLevel::Debug), target(screen)
    {
        WriteTextureSet(folder.Path(), LoadingTextureSet);
    }

    // Un frame: BeginFrame, el Render de la escena y Present. Devuelve lo que se dibujó.
    std::vector<FrameEvent> Frame()
    {
        Client::Scenes::SceneCanvas& canvas = target.BeginFrame(std::chrono::milliseconds{40});
        scene.Render(canvas, 0.0f);
        target.Present();
        return target.frames.back().events;
    }

    // La imagen de 0x65eeb0 con el tamaño del buffer de su textura, como la sube la tabla.
    UiImage Expected(const DesignImage& image, PixelSize screen) const
    {
        const auto loaded = store.Find(TextureSlot{image.slot});
        EXPECT_TRUE(loaded.has_value()) << image.slot;
        UiImage expected{.slot = TextureSlot{image.slot},
                         .x = image.x,
                         .y = image.y,
                         .width = image.width,
                         .height = image.height,
                         .scaleX = DesignScale(screen.width, 800),
                         .scaleY = DesignScale(screen.height, 600)};
        if (loaded)
        {
            expected.textureWidth = static_cast<float>(loaded->texture.textureWidth);
            expected.textureHeight = static_cast<float>(loaded->texture.textureHeight);
        }
        return expected;
    }

    TemporaryFolder folder{"loading-scene"};
    FakeTextureStore store;
    RecordingSink sink;
    Log log;
    RecordingFrameTarget target;
    LoadingScene scene{SceneContext{folder.Path(), store, log}};
    InputState input;
};

// 0x622e40 sube sus seis con su muestreo, en el orden de la tabla.
TEST(LoadingSceneTest, EnterLoadsItsSixTextures)
{
    Harness tested;

    const auto entered = tested.scene.Enter();

    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    EXPECT_EQ(tested.store.loaded, (std::vector<std::uint16_t>{0x71e, 0x71f, 0x720, 0x721, 0x722, 0x723}));
    for (const auto& entry : LoadingTextureSet)
    {
        const auto loaded = tested.store.Find(TextureSlot{entry.slot});
        ASSERT_TRUE(loaded.has_value()) << entry.slot;
        EXPECT_EQ(loaded->sampling.filter, entry.sampling.filter) << entry.slot;
        EXPECT_EQ(loaded->sampling.wrap, entry.sampling.wrap) << entry.slot;
    }
}

TEST(LoadingSceneTest, AMissingTextureFailsWithItsName)
{
    Harness tested;
    std::filesystem::remove(tested.folder.Path() / "Interface/lo_mu_logo.OZT");

    const auto entered = tested.scene.Enter();

    ASSERT_FALSE(entered.HasValue());
    EXPECT_NE(entered.Error().find("Interface\\lo_mu_logo.tga"), std::string::npos) << entered.Error();
    EXPECT_EQ(tested.store.Size(), 0u);
}

// 0x65f200: las cinco imágenes y la barra, cada una con la mezcla normal que dejó 0x622e40
// (0x620a00(1), antes de 0x61ccb0), en 800 × 600 pasado a la pantalla por eje.
TEST(LoadingSceneTest, DrawsTheFiveImagesAndTheBar)
{
    for (const PixelSize screen : {PixelSize{640, 480}, PixelSize{1024, 768}})
    {
        SCOPED_TRACE(screen.width);
        Harness tested(screen);
        ASSERT_TRUE(tested.scene.Enter().HasValue());

        const std::vector<FrameEvent> events = tested.Frame();

        ASSERT_EQ(events.size(), Images.size() + 1);
        for (std::size_t i = 0; i < Images.size(); ++i)
        {
            SCOPED_TRACE(Images[i].slot);
            EXPECT_EQ(events[i].kind, FrameEvent::Kind::Quad);
            EXPECT_EQ(events[i].texture, TextureSlot{Images[i].slot});
            EXPECT_EQ(events[i].blend, BlendMode::Alpha);
            EXPECT_TRUE(events[i].alphaTest);
            ExpectSameCorners(CornersOf(events[i]), UiImageCorners(tested.Expected(Images[i], screen), screen));
        }
        EXPECT_EQ(events.back().texture, TextureSlot{Bar.slot});
    }
}

// 0x622e40 pone la mezcla normal (0x620a00(1)) antes de dibujar: con la aditiva en el lote (el
// estado pasa de un frame al otro, Q-C75), las imágenes salen igual con la normal y el alpha test.
TEST(LoadingSceneTest, DrawsWithTheNormalBlendWhateverCameBefore)
{
    Harness tested;
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    Client::Scenes::SceneCanvas& canvas = tested.target.BeginFrame(std::chrono::milliseconds{40});
    canvas.sprites.SetBlend(BlendMode::Additive);
    tested.scene.Render(canvas, 0.0f);
    tested.target.Present();

    const std::vector<FrameEvent>& events = tested.target.frames.back().events;
    ASSERT_EQ(events.size(), Images.size() + 1);
    for (const FrameEvent& event : events)
    {
        EXPECT_EQ(event.blend, BlendMode::Alpha);
        EXPECT_TRUE(event.alphaTest);
    }
}

// En 640 × 480 el fondo de la izquierda empieza arriba en (600 − 60) · fy = 432, como dice el 06.
TEST(LoadingSceneTest, TheLeftBackgroundStartsWhereTheResearchSays)
{
    Harness tested;
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    const QuadCorners corners = CornersOf(tested.Frame()[0]);

    EXPECT_EQ(corners[0].x, 0.0f);
    EXPECT_EQ(corners[0].y, 432.0f);
}

// 0x640580(actual, 10): la barra mide 656 · actual / 10 (división entera sin signo) y su u llega a
// ese ancho sobre el de la textura, sin medio téxel (0x647370(ancho, 0, 1)); la v de abajo, a
// 15 / th, de 0x647370(0, 15, 2) en 0x6402d0. Un avance por paso, como un frame de 0x65f200 por
// avance de 0x61ccb0.
TEST(LoadingSceneTest, TheBarGrowsWithEachAdvance)
{
    Harness tested;
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    const PixelSize screen{640, 480};

    for (int advance = 0; advance <= LoadingAdvances; ++advance)
    {
        SCOPED_TRACE(advance);
        EXPECT_EQ(tested.scene.Progress(), advance);
        UiImage bar = tested.Expected(Bar, screen);
        bar.width = Bar.width * advance / LoadingAdvances;
        bar.resizedWidth = true;
        bar.resizedHeight = true;

        ExpectSameCorners(CornersOf(tested.Frame().back()), UiImageCorners(bar, screen));
        tested.scene.Step(tested.input);
    }
}

// Once frames, del 0 al 10, y al paso siguiente pide el login (0x622f93).
TEST(LoadingSceneTest, GoesToTheLoginAfterTheLastAdvance)
{
    Harness tested;
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    for (int step = 0; step < LoadingAdvances; ++step)
    {
        EXPECT_EQ(tested.scene.Next(), std::nullopt) << step;
        tested.scene.Step(tested.input);
    }
    EXPECT_EQ(tested.scene.Progress(), LoadingAdvances);
    EXPECT_EQ(tested.scene.Next(), std::nullopt);

    tested.scene.Step(tested.input);

    EXPECT_EQ(tested.scene.Next(), SceneId::Login);
}

// Como 0x63c150 con los seis slots, al final de 0x622e40.
TEST(LoadingSceneTest, LeaveReleasesItsSixSlots)
{
    Harness tested;
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Leave();

    EXPECT_EQ(tested.store.released, (std::vector<std::uint16_t>{0x71e, 0x71f, 0x720, 0x721, 0x722, 0x723}));
    EXPECT_EQ(tested.store.Size(), 0u);
}
// Con el SceneManager y el login de verdad: la carga suelta sus seis antes de que el login suba
// sus 18 y las 3 de las ventanas, como 0x63c150 al final de 0x622e40, antes de que 0x634c60 llame a
// 0x6349a0.
TEST(LoadingSceneTest, ReleasesItsSlotsBeforeTheLoginLoadsItsOwn)
{
    TemporaryFolder folder{"loading-to-login"};
    WriteTextureSet(folder.Path(), LoadingTextureSet);
    WriteTextureSet(folder.Path(), LoginTextureSet);
    WriteTextureSet(folder.Path(), WindowTextureSet);
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    RecordingFrameTarget target;
    FakeClock clock;
    const Client::Scenes::GameData data;
    const SceneContext context{folder.Path(), store, log};
    SceneManager manager(
        [&](SceneId id) -> std::unique_ptr<Client::Scenes::IScene>
        {
            if (id == SceneId::Loading)
            {
                return std::make_unique<LoadingScene>(context);
            }
            if (id == SceneId::Login)
            {
                return std::make_unique<LoginScene>(context, data, nullptr, "0.0.0");
            }
            return nullptr;
        },
        target, clock, {}, log);
    ASSERT_TRUE(manager.Start(SceneId::Loading).HasValue());
    const InputState input;

    for (int step = 0; step < LoadingAdvances; ++step)
    {
        manager.Step(input);
        ASSERT_EQ(manager.Current(), SceneId::Loading) << step;
    }
    manager.Step(input);

    ASSERT_EQ(manager.Current(), SceneId::Login);
    const std::size_t loginSlots = LoginTextureSet.size() + WindowTextureSet.size();
    ASSERT_EQ(store.calls.size(), LoadingTextureSet.size() * 2 + loginSlots);
    for (std::size_t i = 0; i < store.calls.size(); ++i)
    {
        SCOPED_TRACE(i);
        const bool loading = i < LoadingTextureSet.size();
        const bool releasing = !loading && i < LoadingTextureSet.size() * 2;
        const StoreCall::Kind expected = releasing ? StoreCall::Kind::Release : StoreCall::Kind::Load;
        EXPECT_EQ(store.calls[i].kind, expected);
    }
    EXPECT_EQ(store.Size(), loginSlots);
}
} // namespace
