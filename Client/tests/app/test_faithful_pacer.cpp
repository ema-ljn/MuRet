#include "App/FaithfulPacer.h"

#include "App/GameLoop.h"
#include "App/LoopTiming.h"
#include "App/PacingMode.h"
#include "App/WindowState.h"
#include "support/FakeClock.h"
#include "support/RecordingClient.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

// Client::App::FaithfulPacer y el modo Faithful de GameLoop: el ritmo de 0x637700 con el reloj de
// 64 ticks por segundo. Los números son los del modelo del lab (labs/cl3.0-bucle/jefe).
namespace
{
using namespace std::chrono_literals;
using Client::App::FaithfulTick;
using Client::App::GameLoop;
using Client::App::PacingMode;
using Client::App::TickCountMs;
using Client::App::WindowState;
using Client::Tests::FakeClock;
using Client::Tests::RecordingClient;

// Un FakeClock en t = 0, un cliente con dibujos de 0 ms y el bucle en el modo fiel.
struct FaithfulLoop
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Faithful};

    std::vector<int> StepsPerFrame(int frames, const WindowState& state = WindowState{})
    {
        std::vector<int> steps;
        for (int frame = 0; frame < frames; ++frame)
        {
            steps.push_back(loop.RunFrame(state));
        }
        return steps;
    }
};

int Sum(const std::vector<int>& steps, std::size_t first, std::size_t count)
{
    const auto begin = steps.begin() + static_cast<std::ptrdiff_t>(first);
    return std::accumulate(begin, begin + static_cast<std::ptrdiff_t>(count), 0);
}

TEST(FaithfulPacerTest, TickCountMsFollowsTheGrid)
{
    const std::vector<std::uint32_t> expected{0, 15, 31, 46, 62};
    for (std::size_t tick = 0; tick < expected.size(); ++tick)
    {
        EXPECT_EQ(TickCountMs(static_cast<std::int64_t>(tick) * FaithfulTick), expected[tick]) << "tick " << tick;
    }
    // Entre dos ticks lee el anterior.
    EXPECT_EQ(TickCountMs(FaithfulTick - 1ns), 0u);
}

// Cada frame espera 3 ticks: 64 frames son 192 ticks, 3 s justos (21,33 FPS).
TEST(FaithfulPacerTest, SixtyFourFramesEveryThreeSeconds)
{
    FaithfulLoop faithful;
    faithful.StepsPerFrame(64);

    EXPECT_EQ(faithful.clock.Now(), 3s);
    EXPECT_EQ(faithful.clock.Now(), 192 * FaithfulTick);
    // Los 75 pasos de 3 s a 25 por segundo, más el del valor inicial: 74 corridos y 80 ms.
    EXPECT_EQ(faithful.loop.Stats().steps, 74u);
    EXPECT_EQ(faithful.loop.Stats().accumulated, 80ms);
    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}), 2);
}

TEST(FaithfulPacerTest, SeventyFiveStepsEverySixtyFourFrames)
{
    FaithfulLoop faithful;
    const std::vector<int> steps = faithful.StepsPerFrame(300);

    const std::vector<int> firstThirteen{1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2};
    EXPECT_EQ(std::vector<int>(steps.begin(), steps.begin() + 13), firstThirteen);

    // Desde el frame 2 (el índice 1): el primero corre el paso del valor inicial.
    for (std::size_t first = 1; first + 64 <= steps.size(); ++first)
    {
        const auto begin = steps.begin() + static_cast<std::ptrdiff_t>(first);
        EXPECT_EQ(Sum(steps, first, 64), 75) << "desde el frame " << first + 1;
        EXPECT_EQ(std::count(begin, begin + 64, 2), 11) << "desde el frame " << first + 1;
    }
    for (std::size_t first = 1; first + 6 <= steps.size(); ++first)
    {
        const int six = Sum(steps, first, 6);
        EXPECT_TRUE(six == 7 || six == 8) << six << " desde el frame " << first + 1;
    }
    // Los frames 31 a 36.
    const std::vector<int> thirtyOneToThirtySix{2, 1, 1, 1, 1, 2};
    EXPECT_EQ(std::vector<int>(steps.begin() + 30, steps.begin() + 36), thirtyOneToThirtySix);
}

TEST(FaithfulPacerTest, EachFrameMeasuresThreeTicks)
{
    FaithfulLoop faithful;
    for (int frame = 0; frame < 300; ++frame)
    {
        faithful.loop.RunFrame(WindowState{});
        const std::int32_t measured = faithful.loop.Stats().lastMeasuredMs;
        EXPECT_TRUE(measured == 46 || measured == 47) << measured << " en el frame " << frame + 1;
    }
}

// Un dibujo que ya pasó los 40 ms no espera: suma lo que midió GetTickCount, de a ticks.
TEST(FaithfulPacerTest, ADrawLongerThanTheWaitAddsItWhole)
{
    FaithfulLoop faithful;
    faithful.client.renderCost = 130ms;

    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}), 1);
    EXPECT_EQ(faithful.loop.Stats().lastMeasuredMs, 125); // 8 ticks
    EXPECT_TRUE(faithful.clock.Sleeps().empty());

    faithful.client.renderCost = 0ms;
    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}), 3);
}

// La medición empieza después de los pasos (0x63852b): lo que tardan se pierde. Midiendo desde
// antes de los pasos serían 64 frames y 74 pasos, no 61 y 71.
TEST(FaithfulPacerTest, TimeSpentInStepsIsNotAccumulated)
{
    FaithfulLoop faithful;
    faithful.client.stepCost = 10ms;

    std::uint64_t frames = 0;
    while (faithful.clock.Now() < 3s)
    {
        faithful.loop.RunFrame(WindowState{});
        ++frames;
    }

    EXPECT_EQ(frames, 61u);
    EXPECT_EQ(faithful.client.steps, 71u);
    EXPECT_EQ(faithful.clock.Now(), 3'015'625us);
}

// El original no tiene tope: un breakpoint de 10 s son 250 pasos seguidos.
TEST(FaithfulPacerTest, HasNoCap)
{
    FaithfulLoop faithful;
    faithful.client.renderCost = 10s;

    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}), 1);
    EXPECT_EQ(faithful.loop.Stats().lastMeasuredMs, 10'000);

    faithful.client.renderCost = 0ms;
    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}), 250);
}

// Minimizada, la espera de GetTickCount sigue: sin ella el acumulador no suma.
TEST(FaithfulPacerTest, MinimizedKeepsTheWait)
{
    FaithfulLoop faithful;
    WindowState minimized;
    minimized.minimized = true;
    faithful.StepsPerFrame(64, minimized);

    EXPECT_EQ(faithful.clock.Now(), 3s);
    EXPECT_EQ(faithful.loop.Stats().steps, 74u);
    EXPECT_EQ(faithful.loop.RunFrame(minimized), 2);
    EXPECT_TRUE(faithful.client.alphas.empty());
}

// El dibujo del modo fiel no se interpola: el estado después de los pasos.
TEST(FaithfulPacerTest, DrawsWithAlphaZero)
{
    FaithfulLoop faithful;
    faithful.StepsPerFrame(10);

    ASSERT_EQ(faithful.client.alphas.size(), 10u);
    for (const float alpha : faithful.client.alphas)
    {
        EXPECT_EQ(alpha, 0.0f);
    }
}
} // namespace
