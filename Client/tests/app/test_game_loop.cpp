#include "App/GameLoop.h"

#include "App/LoopTiming.h"
#include "App/PacingMode.h"
#include "App/WindowState.h"
#include "support/FakeClock.h"
#include "support/RecordingClient.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <numeric>

// Client::App::GameLoop en el modo normal (Decoupled): el paso fijo con el reloj falso.
namespace
{
using namespace std::chrono_literals;
using Client::App::GameLoop;
using Client::App::PacingMode;
using Client::App::WindowState;
using Client::Core::Duration;
using Client::Tests::FakeClock;
using Client::Tests::RecordingClient;

constexpr double AlphaTolerance = 1e-6;

// Lo que avanza el frame `frame` para que `framesPerSecond` frames sumen 1 s exacto.
Duration FrameTime(std::int64_t frame, std::int64_t framesPerSecond)
{
    const std::int64_t second = Duration{1s}.count();
    return Duration{(frame + 1) * second / framesPerSecond - frame * second / framesPerSecond};
}

class GameLoopTest : public ::testing::TestWithParam<int>
{
};

// En 4 s simulados, 100 pasos con cualquier tasa de frames. El primer frame solo fija el inicio.
TEST_P(GameLoopTest, TwentyFiveStepsPerSecondAtAnyFrameRate)
{
    const std::int64_t framesPerSecond = GetParam();
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    ASSERT_EQ(loop.RunFrame(WindowState{}), 0);

    const std::int64_t frames = 4 * framesPerSecond;
    for (std::int64_t frame = 0; frame < frames; ++frame)
    {
        clock.Advance(FrameTime(frame, framesPerSecond));
        loop.RunFrame(WindowState{});
    }

    EXPECT_EQ(clock.Now(), 4s);
    EXPECT_EQ(client.steps, 100u);
    EXPECT_EQ(loop.Stats().steps, 100u);
    EXPECT_EQ(loop.Stats().frames, static_cast<std::uint64_t>(frames + 1));
    EXPECT_EQ(loop.Stats().clampedFrames, 0u);
}

INSTANTIATE_TEST_SUITE_P(FrameRates, GameLoopTest, ::testing::Values(1, 25, 60, 144, 240));

TEST(GameLoopTest, ASlowFrameCatchesUpWithThreeSteps)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    client.renderCost = 130ms;
    ASSERT_EQ(loop.RunFrame(WindowState{}), 0);
    client.renderCost = 0ms;

    EXPECT_EQ(loop.RunFrame(WindowState{}), 3);
    EXPECT_NEAR(loop.LastAlpha(), 0.25, AlphaTolerance);
    ASSERT_EQ(client.alphas.size(), 2u);
    EXPECT_NEAR(client.alphas.back(), 0.25, AlphaTolerance);
}

// El breakpoint: un dibujo de 10 s corre 25 pasos (MaxFrameTime) y no 250 como el original.
TEST(GameLoopTest, AFrameLongerThanTheCapRunsOnlyTheCap)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    client.renderCost = 10s;
    ASSERT_EQ(loop.RunFrame(WindowState{}), 0);
    client.renderCost = 0ms;

    EXPECT_EQ(loop.RunFrame(WindowState{}), 25);
    EXPECT_EQ(loop.Stats().clampedFrames, 1u);
    for (int frame = 0; frame < 5; ++frame)
    {
        clock.Advance(40ms);
        EXPECT_EQ(loop.RunFrame(WindowState{}), 1) << "frame " << frame;
    }
    EXPECT_EQ(loop.Stats().clampedFrames, 1u);
}

TEST(GameLoopTest, AClockThatGoesBackRunsNoSteps)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    ASSERT_EQ(loop.RunFrame(WindowState{}), 0);

    clock.Advance(-5ms);
    EXPECT_EQ(loop.RunFrame(WindowState{}), 0);
    EXPECT_EQ(loop.Stats().accumulated, 0ns);
    EXPECT_NEAR(loop.LastAlpha(), 0.0, AlphaTolerance);

    // El siguiente se mide desde el valor nuevo.
    clock.Advance(40ms);
    EXPECT_EQ(loop.RunFrame(WindowState{}), 1);
}

// Con VSync apagado, dos frames pueden leer el mismo instante.
TEST(GameLoopTest, AClockThatDoesNotMoveRunsNoSteps)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    ASSERT_EQ(loop.RunFrame(WindowState{}), 0);
    clock.Advance(10ms);

    EXPECT_EQ(loop.RunFrame(WindowState{}), 0);
    const float alpha = loop.LastAlpha();
    EXPECT_EQ(loop.RunFrame(WindowState{}), 0);

    EXPECT_FALSE(std::isnan(loop.LastAlpha()));
    EXPECT_EQ(loop.LastAlpha(), alpha);
    EXPECT_NEAR(alpha, 0.25, AlphaTolerance);
    EXPECT_EQ(client.steps, 0u);
}

// Minimizada, la lógica sigue y el dibujo no: cada frame duerme lo que falta para el próximo
// paso, así que el bucle duerme en vez de girar.
TEST(GameLoopTest, AMinimizedWindowStepsWithoutDrawing)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    WindowState minimized;
    minimized.minimized = true;

    for (int frame = 0; frame < 26; ++frame)
    {
        loop.RunFrame(minimized);
    }

    EXPECT_EQ(client.steps, 25u);
    EXPECT_TRUE(client.alphas.empty());
    const auto sleeps = clock.Sleeps();
    EXPECT_EQ(std::accumulate(sleeps.begin(), sleeps.end(), Duration::zero()), 1040ms);
}

TEST(GameLoopTest, AlphaGrowsBetweenSteps)
{
    constexpr std::int64_t FramesPerSecond = 240;
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    ASSERT_EQ(loop.RunFrame(WindowState{}), 0);

    float previous = loop.LastAlpha();
    int drops = 0;
    for (std::int64_t frame = 0; frame < FramesPerSecond; ++frame)
    {
        clock.Advance(FrameTime(frame, FramesPerSecond));
        const int steps = loop.RunFrame(WindowState{});
        const float alpha = loop.LastAlpha();
        EXPECT_GE(alpha, 0.0f) << "frame " << frame;
        EXPECT_LT(alpha, 1.0f) << "frame " << frame;
        if (steps == 0)
        {
            EXPECT_GT(alpha, previous) << "frame " << frame;
        }
        else
        {
            EXPECT_LT(alpha, previous) << "frame " << frame;
            ++drops;
        }
        previous = alpha;
    }
    EXPECT_EQ(drops, 25);
    EXPECT_EQ(client.alphas.size(), static_cast<std::size_t>(FramesPerSecond + 1));
}
} // namespace
