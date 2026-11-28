#include "Scenes/LoadingScene.h"

#include "Assets/GameModels.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Render/QuadCorner.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Tables/GameData.h"
#include "Scenes/IScene.h"
#include "Scenes/LoginScene.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Scenes/SceneManager.h"
#include "Assets/TextureSets.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/ScenePrinters.h"
#include "support/TemporaryFolder.h"
#include "FrontEnd/LoginModels.h"
#include "support/FixedRandom.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Client::Scenes::LoadingScene: scene 1 (0x622e40, with its UI of 0x65eeb0 and its frames of 0x65f200), with a
// synthetic Data\ and the fake texture table.
namespace
{
using Client::Assets::LoadingTextureSet;
using Client::Assets::LoginTextureSet;
using Client::Assets::WindowTextureSet;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Core::PixelSize;
using Client::Input::InputState;
using Client::Kernel::SceneId;
using Client::Render::BlendMode;
using Client::Render::DesignScale;
using Client::Render::QuadCorner;
using Client::Render::QuadCorners;
using Client::Render::TextureSlot;
using Client::Render::UiImage;
using Client::Render::UiImageCorners;
using Client::Scenes::LoadingAdvances;
using Client::Scenes::LoadingScene;
using Client::Scenes::LoginScene;
using Client::Scenes::SceneContext;
using Client::Scenes::SceneManager;
using Client::Tests::FakeClock;
using Client::Tests::FakeTextureStore;
using Client::Tests::FixedSeed;
using Client::Tests::FrameEvent;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;
using Client::Tests::StoreCall;
using Client::Tests::TemporaryFolder;
using Client::Tests::WriteTextureSet;

// The five images of 0x65eeb0 (0x647050 with the width, the height and the slot, and 0x6472e0 with x and y, at
// 800 × 600), in the order 0x65f200 draws them (the loop of 0x65f22f, 0xa8 bytes at a time), and the bar (0x6402d0
// and 0x6404b0).
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

// The corners of a recorded quad: the fan (0, 1, 2) and (0, 2, 3) has the first at vertex 0, the second at 1, the
// third at 2 and the fourth at 5.
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

    std::vector<FrameEvent> Frame()
    {
        Client::Render::Frame::SceneCanvas& canvas = target.BeginFrame(std::chrono::milliseconds{40});
        scene.Render(canvas, 0.0f);
        target.Present();
        return target.frames.back().events;
    }

    // The image of 0x65eeb0 with the size of its texture's buffer, as the table uploads it.
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

// 0x622e40 uploads its six with their sampling, in the table's order.
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

// 0x65f200: the five images and the bar, each with the normal blending 0x622e40 left (0x620a00(1), before 0x61ccb0),
// at 800 × 600 mapped to the screen per axis.
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

// 0x622e40 sets the normal blending (0x620a00(1)) before drawing: with additive in the batch (the state carries from
// one frame to the next, Q-C75), the images come out the same with normal and the alpha test.
TEST(LoadingSceneTest, DrawsWithTheNormalBlendWhateverCameBefore)
{
    Harness tested;
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    Client::Render::Frame::SceneCanvas& canvas = tested.target.BeginFrame(std::chrono::milliseconds{40});
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

// At 640 × 480 the left background starts at the top at (600 − 60) · fy = 432, as 06 says.
TEST(LoadingSceneTest, TheLeftBackgroundStartsWhereTheResearchSays)
{
    Harness tested;
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    const QuadCorners corners = CornersOf(tested.Frame()[0]);

    EXPECT_EQ(corners[0].x, 0.0f);
    EXPECT_EQ(corners[0].y, 432.0f);
}

// 0x640580(current, 10): the bar measures 656 · current / 10 (unsigned integer division) and its u reaches that width
// over the texture's, with no half texel (0x647370(width, 0, 1)); the bottom v, at 15 / th, from 0x647370(0, 15, 2)
// at 0x6402d0. One advance per step, like one frame of 0x65f200 per advance of 0x61ccb0.
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

// Eleven frames, 0 to 10, and on the next step it asks for the login (0x622f93).
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
    EXPECT_FALSE(tested.scene.WantsToQuit());
    EXPECT_FALSE(tested.scene.WantsTextInput());
}

// Like 0x63c150 with the six slots, at the end of 0x622e40.
TEST(LoadingSceneTest, LeaveReleasesItsSixSlots)
{
    Harness tested;
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Leave();

    EXPECT_EQ(tested.store.released, (std::vector<std::uint16_t>{0x71e, 0x71f, 0x720, 0x721, 0x722, 0x723}));
    EXPECT_EQ(tested.store.Size(), 0u);
}
// With the real SceneManager and login: the loading releases its six before the login uploads its 18 and the
// windows' 3, like 0x63c150 at the end of 0x622e40, before 0x634c60 calls 0x6349a0.
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
    const Client::Tables::GameData data;
    const Client::FrontEnd::SingleModelLibrary models;
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
                return std::make_unique<LoginScene>(context, data, models, nullptr, "0.0.0", FixedSeed);
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
// The loading reads the models in its advance 1 (0x61ccb0). Without Player.bmd, which the original requires (Q-C23),
// it does not go on: the scene asks to close and the error names the file (CL6.3).
TEST(LoadingSceneTest, AMissingPlayerFailsTheLoad)
{
    TemporaryFolder folder{"loading-scene-models"};
    WriteTextureSet(folder.Path(), LoadingTextureSet);
    FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Client::Assets::GameModels models;
    LoadingScene scene{SceneContext{folder.Path(), store, log}, &models};
    ASSERT_TRUE(scene.Enter().HasValue());
    const Client::Input::InputState input;

    scene.Step(input);

    EXPECT_TRUE(scene.WantsToQuit());
    EXPECT_TRUE(std::ranges::any_of(sink.lines, [](const std::string& line)
                                    { return line.find("Player.bmd") != std::string::npos; }));
    for (int step = 0; step < 20; ++step)
    {
        scene.Step(input);
    }
    EXPECT_EQ(scene.Next(), std::nullopt);
}

} // namespace
