#include "Scenes/WorldLoadingScene.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/TextureSets.h"

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
#include <vector>

// Client::Scenes::WorldLoadingScene: la escena 3 (0x6356f0), el mosaico de LSBg01 a 04 y el juego
// después de un frame (06 § La carga del mundo).
namespace
{
using Client::Render::TextureSlot;
using Client::Scenes::SceneContext;
using Client::Scenes::SceneId;
using Client::Scenes::WorldLoadingScene;
using Client::Scenes::WorldLoadingTextureSet;
using Client::Tests::FakeTextureStore;
using Client::Tests::FrameEvent;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::TemporaryFolder;

struct Harness
{
    Harness() : log(sink, Client::Core::LogLevel::Debug), scene(SceneContext{folder.Path(), store, log})
    {
        Client::Tests::WriteTextureSet(folder.Path(), WorldLoadingTextureSet);
    }

    TemporaryFolder folder{"world-loading-scene"};
    FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log;
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
} // namespace
