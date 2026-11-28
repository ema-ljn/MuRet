#include "App/GameClient.h"

#include "Scenes/ILoopClient.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/LocalTime.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Tables/GameData.h"
#include "support/BlackScreenReader.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/LetterMeasure.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/ScenePrinters.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace Client::App
{
namespace
{
constexpr Core::PixelSize Screen{.width = 640, .height = 480};

TEST(GameExitTest, NothingToReportKeepsTheLoopRunning)
{
    EXPECT_FALSE(GameExitOf({}, {}, {}, false, false).has_value());
}

TEST(GameExitTest, TheFirstReasonInMainsOrderWins)
{
    EXPECT_EQ(GameExitOf("data", "scene", "selection", true, true)->kind, GameExitKind::DataError);
    EXPECT_EQ(GameExitOf({}, "scene", "selection", true, true)->kind, GameExitKind::SceneFailure);
    EXPECT_EQ(GameExitOf({}, {}, "selection", true, true)->kind, GameExitKind::SelectionFailure);
    EXPECT_EQ(GameExitOf({}, {}, {}, true, true)->kind, GameExitKind::ServerOrder);
    EXPECT_EQ(GameExitOf({}, {}, {}, false, true)->kind, GameExitKind::PlayerQuit);
}

TEST(GameExitTest, AFailureCarriesItsMessage)
{
    EXPECT_EQ(GameExitOf("Data\\Local\\Text.bmd", {}, {}, false, false)->message, "Data\\Local\\Text.bmd");
    EXPECT_EQ(GameExitOf({}, "scene", {}, false, false)->message, "scene");
    EXPECT_EQ(GameExitOf({}, {}, "selection", false, false)->message, "selection");
}

class GameClientTest : public Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        ASSERT_NO_FATAL_FAILURE(GoldenDataTest::SetUp());
        auto data = Tables::LoadGameData(m_dataRoot);
        ASSERT_TRUE(data.HasValue()) << data.Error();
        m_data = std::make_unique<Tables::GameData>(std::move(data).Value());
    }

    // One frame as GameLoop runs it with one step, then the clock moves 40 ms.
    void RunFrame(GameClient& client)
    {
        Scenes::ILoopClient& loop = client.Loop();
        loop.Poll();
        loop.Step(m_input);
        m_input.ConsumeEdges();
        static_cast<void>(loop.TakeTheSceneChange());
        if (!loop.HoldsTheLastFrame())
        {
            loop.Render(0.0f);
        }
        loop.FinishTheLoop();
        m_clock.Advance(std::chrono::milliseconds{40});
    }

    std::unique_ptr<Tables::GameData> m_data;
    Tests::RecordingSink m_sink;
    Core::Log m_log{m_sink, Core::LogLevel::Info};
    Tests::FakeClock m_clock;
    Tests::FakeTextureStore m_textures;
    Tests::RecordingFrameTarget m_frame{Screen};
    Tests::LetterMeasure m_measure;
    Tests::BlackScreenReader m_screenReader;
    Input::InputState m_input;
    GameClientDevices m_devices{.frame = m_frame,
                                .textures = m_textures,
                                .textMeasure = m_measure,
                                .screenReader = m_screenReader,
                                .clock = m_clock,
                                .log = m_log,
                                .input = m_input,
                                .screen = [] { return Screen; },
                                .localTime = []() -> Core::Expected<Core::LocalTime, std::string>
                                { return Core::LocalTime{}; }};
};

TEST_F(GameClientTest, RunsWithoutAServer)
{
    GameClient client(
        *m_data, m_devices,
        GameClientOptions{
            .dataRoot = m_dataRoot, .version = "0.0.0", .crtSeed = 1, .loginSeed = [] { return std::uint32_t{1}; }},
        std::nullopt);
    const auto started = client.Start();
    ASSERT_TRUE(started.HasValue()) << started.Error();
    for (int frame = 0; frame < 3; ++frame)
    {
        RunFrame(client);
    }
    EXPECT_TRUE(client.CurrentScene().has_value());
    EXPECT_EQ(client.Session(), nullptr);
    EXPECT_FALSE(client.Exit().has_value());
}
} // namespace
} // namespace Client::App
