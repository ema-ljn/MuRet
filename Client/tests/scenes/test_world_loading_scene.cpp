#include "Scenes/WorldLoadingScene.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Input/InputState.h"
#include "Models/Cloth.h"
#include "Render/TextureSlot.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Assets/TextureSets.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

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

// Client::Scenes::WorldLoadingScene: scene 3 (0x6356f0), the wait counter, the flag, the LSBg01 to 04 mosaic and the
// game after one frame (06 § La carga del mundo).
namespace
{
using Client::Assets::WorldLoadingTextureSet;
using Client::Crt::CrtRandom;
using Client::Kernel::SceneId;
using Client::Kernel::WorldLoadingWait;
using Client::Kernel::WorldRuntime;
using Client::Kernel::WorldState;
using Client::Models::ClothWorld;
using Client::Render::TextureSlot;
using Client::Scenes::SceneContext;
using Client::Scenes::WorldLoadingScene;
using Client::Tests::FakeTextureStore;
using Client::Tests::FrameEvent;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::TemporaryFolder;

constexpr std::uint32_t Seed = 0x2468ace1u;

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
    // LSBg04, bottom right: from (400, 512) to (800, 600), with y going up.
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

// 0x6356f0 with the flag at 0: opens the counter, writes 9999999 (0x6357eb) and closes it (0x412890), which spends 4
// rand() only if it leaves it with no accesses; then it raises the flag (0x635980) and uploads the textures.
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

// 0x63571d: with the flag already at 1 the original skips the counter, the flag and the textures and draws with the
// mosaic object null. The selection always lowers it before scene 3 and nobody else raises it, so it is never
// reached: MuRet says so with the error and touches nothing.
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

// The wait lives in the WorldState and its close spends the cloth's rand(): without either, the scene does not enter
// instead of skipping the counter.
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
