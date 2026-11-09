#include "Scenes/WorldLoadingScene.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Render/Models/Cloth.h"
#include "Render/TextureSlot.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/TextureSets.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include "support/FakeTextureStore.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/ScenePrinters.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Client::Scenes::WorldLoadingScene: la escena 3 (0x6356f0), el contador de la espera, la bandera, el
// mosaico de LSBg01 a 04 y el juego después de un frame (06 § La carga del mundo).
namespace
{
using Client::Render::TextureSlot;
using Client::Render::Models::ClothWorld;
using Client::Render::Models::CrtRandom;
using Client::Scenes::SceneContext;
using Client::Scenes::SceneId;
using Client::Scenes::WorldLoadingScene;
using Client::Scenes::WorldLoadingTextureSet;
using Client::Scenes::WorldLoadingWait;
using Client::Scenes::WorldRuntime;
using Client::Scenes::WorldState;
using Client::Tests::FakeTextureStore;
using Client::Tests::FrameEvent;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::TemporaryFolder;

constexpr std::uint32_t Seed = 0x2468ace1u;

// El estado del rand() de la CRT sembrado con Seed después de `draws` llamadas.
std::uint32_t StateAfter(int draws)
{
    CrtRandom random(Seed);
    for (int i = 0; i < draws; ++i)
    {
        static_cast<void>(random.Next());
    }
    return random.State();
}

struct Harness
{
    Harness()
        : log(sink, Client::Core::LogLevel::Debug),
          scene(
              SceneContext{.dataRoot = folder.Path(), .textures = store, .log = log, .cloth = &cloth, .world = &world})
    {
        Client::Tests::WriteTextureSet(folder.Path(), WorldLoadingTextureSet);
    }

    [[nodiscard]] WorldRuntime& Runtime()
    {
        return world.Runtime();
    }

    TemporaryFolder folder{"world-loading-scene"};
    FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log;
    ClothWorld cloth{.random = CrtRandom(Seed)};
    WorldState world;
    WorldLoadingScene scene;
    RecordingFrameTarget target{{800, 600}};
    const Client::Input::InputState input;
};

// Un frame con las cuatro en su lugar del mosaico de 800 × 600, y el juego en el paso que sigue.
TEST(WorldLoadingSceneTest, DrawsTheFourTilesAndGoesToTheGame)
{
    Harness tested;
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    tested.scene.Step(tested.input);
    EXPECT_EQ(tested.scene.Next(), std::nullopt);

    auto& canvas = tested.target.BeginFrame(std::chrono::milliseconds{40});
    tested.scene.Render(canvas, 0.0f);
    tested.target.Present();
    tested.scene.Step(tested.input);

    const std::vector<FrameEvent>& events = tested.target.frames.back().events;
    ASSERT_EQ(events.size(), 4u);
    for (std::size_t k = 0; k < events.size(); ++k)
    {
        EXPECT_EQ(events[k].texture, std::optional<TextureSlot>{TextureSlot{static_cast<std::uint16_t>(0x71e + k)}});
    }
    // LSBg04, abajo a la derecha: de (400, 512) a (800, 600), con la y para arriba.
    EXPECT_FLOAT_EQ(events[3].vertices[0].x, 400.0f);
    EXPECT_FLOAT_EQ(events[3].vertices[0].y, 88.0f);
    EXPECT_EQ(tested.scene.Next(), SceneId::Game);
}

TEST(WorldLoadingSceneTest, ReleasesItsFourImages)
{
    Harness tested;
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Leave();

    EXPECT_EQ(tested.store.released, (std::vector<std::uint16_t>{0x71e, 0x71f, 0x720, 0x721}));
}

// 0x6356f0 con la bandera en 0: abre el contador, escribe 9.999.999 (0x6357eb) y lo cierra (0x412890),
// que gasta 4 rand() solo si lo deja sin accesos; después sube la bandera (0x635980) y las texturas.
TEST(WorldLoadingSceneTest, TheFirstLoadArmsTheCounterAndTheFlag)
{
    struct Case
    {
        std::uint8_t depth;
        int draws;
    };
    for (const Case& probe : {Case{0, 4}, Case{1, 0}, Case{255, 0}})
    {
        SCOPED_TRACE(static_cast<int>(probe.depth));
        Harness tested;
        tested.Runtime().warpCounter = 12;
        tested.Runtime().warpDepth = probe.depth;

        ASSERT_TRUE(tested.scene.Enter().HasValue());

        EXPECT_EQ(tested.Runtime().warpCounter, WorldLoadingWait);
        EXPECT_EQ(tested.Runtime().loadingOnceFlag, 1);
        EXPECT_EQ(tested.Runtime().warpDepth, probe.depth);
        EXPECT_EQ(tested.cloth.random.State(), StateAfter(probe.draws));
        EXPECT_EQ(tested.store.loaded, (std::vector<std::uint16_t>{0x71e, 0x71f, 0x720, 0x721}));
    }
}

// 0x63571d: con la bandera ya en 1 el original saltea el contador, la bandera y las texturas y dibuja
// con el objeto del mosaico nulo. La selección la baja siempre antes de la escena 3 y nadie más la
// sube, así que no se llega: MuRet lo dice con el error y no toca nada.
TEST(WorldLoadingSceneTest, ARaisedFlagKeepsTheCounter)
{
    Harness tested;
    tested.Runtime().warpCounter = 30;
    tested.Runtime().loadingOnceFlag = 1;

    const auto entered = tested.scene.Enter();

    ASSERT_FALSE(entered.HasValue());
    EXPECT_NE(entered.Error().find("0x7d3a13e"), std::string::npos);
    EXPECT_EQ(tested.Runtime().warpCounter, 30);
    EXPECT_EQ(tested.Runtime().loadingOnceFlag, 1);
    EXPECT_EQ(tested.Runtime().warpDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), StateAfter(0));
    EXPECT_EQ(tested.store.loadCalls, 0);
}

// La espera vive en el WorldState y su cierre gasta el rand() de la tela: sin alguno de los dos, la
// escena no entra en vez de saltear el contador.
TEST(WorldLoadingSceneTest, DoesNotEnterWithoutTheWorldOrTheRandom)
{
    Harness tested;
    WorldLoadingScene withoutWorld(SceneContext{
        .dataRoot = tested.folder.Path(), .textures = tested.store, .log = tested.log, .cloth = &tested.cloth});
    WorldLoadingScene withoutRandom(SceneContext{
        .dataRoot = tested.folder.Path(), .textures = tested.store, .log = tested.log, .world = &tested.world});

    EXPECT_FALSE(withoutWorld.Enter().HasValue());
    EXPECT_FALSE(withoutRandom.Enter().HasValue());
    EXPECT_EQ(tested.Runtime().loadingOnceFlag, 0);
    EXPECT_EQ(tested.store.loadCalls, 0);
}
} // namespace
