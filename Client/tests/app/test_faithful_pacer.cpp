#include "Kernel/FaithfulPacer.h"

#include "App/GameLoop.h"
#include "Kernel/LoopTiming.h"
#include "Kernel/PacingMode.h"
#include "Platform/WindowState.h"
#include "Input/InputState.h"
#include "support/FakeClock.h"
#include "support/RecordingClient.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

// Client::Kernel::FaithfulPacer and GameLoop's Faithful mode: the pacing of 0x637700 with the 64-ticks-per-second
// clock. The numbers are those of the lab model (labs/cl3.0-bucle/jefe).
namespace
{
using namespace std::chrono_literals;
using Client::App::GameLoop;
using Client::Kernel::FaithfulTick;
using Client::Kernel::FaithfulWaitMs;
using Client::Kernel::PacingMode;
using Client::Kernel::TickCountMs;
using Client::Platform::WindowState;
using Client::Tests::FakeClock;
using Client::Tests::LoopCall;
using Client::Tests::RecordingClient;

// On the 15.625 ms grid 40 is never measured: with a wait of 32 to 46 ms the same tests would pass, and this pins it.
static_assert(FaithfulWaitMs == 40);

// A FakeClock at t = 0, a client with 0 ms drawings and the loop in faithful mode.
struct FaithfulLoop
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Faithful};
    // Empty: these tests do not look at the input.
    Client::Input::InputState input;

    std::vector<int> StepsPerFrame(int frames, const WindowState& state = WindowState{})
    {
        std::vector<int> steps;
        for (int frame = 0; frame < frames; ++frame)
        {
            steps.push_back(loop.RunFrame(state, input));
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
    // Between two ticks it reads the earlier one.
    EXPECT_EQ(TickCountMs(FaithfulTick - 1ns), 0u);
}

// Below 0, which no IClock gives and only comes from calling directly, as here, the grid follows the same count,
// floor(tick × 15.625): tick -1 reads -16 and not -15 (duration_cast would truncate toward 0 and mirror it), and
// from one tick to the next there are always 15 or 16 ms, also crossing 0.
TEST(FaithfulPacerTest, TickCountMsFollowsTheGridBelowZero)
{
    const std::vector<std::int32_t> expected{-63, -47, -32, -16, 0, 15, 31, 46, 62};
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        const std::int64_t tick = static_cast<std::int64_t>(i) - 4;
        EXPECT_EQ(TickCountMs(tick * FaithfulTick), static_cast<std::uint32_t>(expected[i])) << "tick " << tick;
    }
    // Between two ticks it reads the earlier one, also below 0.
    EXPECT_EQ(TickCountMs(-1ns), static_cast<std::uint32_t>(-16));
    EXPECT_EQ(TickCountMs(-FaithfulTick - 1ns), static_cast<std::uint32_t>(-32));
}

// Each frame waits 3 ticks: 64 frames are 192 ticks, exactly 3 s (21.33 FPS).
TEST(FaithfulPacerTest, SixtyFourFramesEveryThreeSeconds)
{
    FaithfulLoop faithful;
    faithful.StepsPerFrame(64);

    EXPECT_EQ(faithful.clock.Now(), 3s);
    EXPECT_EQ(faithful.clock.Now(), 192 * FaithfulTick);
    // 3 sleeps per frame, each of one tick: edge to edge.
    const auto sleeps = faithful.clock.Sleeps();
    EXPECT_EQ(sleeps.size(), 192u);
    EXPECT_TRUE(std::all_of(sleeps.begin(), sleeps.end(), [](auto sleep) { return sleep == FaithfulTick; }));
    // The 75 steps of 3 s at 25 per second, plus the one of the initial value: 74 run and 80 ms.
    EXPECT_EQ(faithful.loop.Stats().steps, 74u);
    EXPECT_EQ(faithful.loop.Stats().accumulated, 80ms);
    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 2);
}

TEST(FaithfulPacerTest, SeventyFiveStepsEverySixtyFourFrames)
{
    FaithfulLoop faithful;
    const std::vector<int> steps = faithful.StepsPerFrame(300);

    const std::vector<int> firstThirteen{1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2};
    EXPECT_EQ(std::vector<int>(steps.begin(), steps.begin() + 13), firstThirteen);

    // From frame 2 (index 1): the first one runs the initial value's step.
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
    // Frames 31 to 36.
    const std::vector<int> thirtyOneToThirtySix{2, 1, 1, 1, 1, 2};
    EXPECT_EQ(std::vector<int>(steps.begin() + 30, steps.begin() + 36), thirtyOneToThirtySix);
}

TEST(FaithfulPacerTest, EachFrameMeasuresThreeTicks)
{
    FaithfulLoop faithful;
    for (int frame = 0; frame < 300; ++frame)
    {
        faithful.loop.RunFrame(WindowState{}, faithful.input);
        const std::int32_t measured = faithful.loop.Stats().lastMeasuredMs;
        EXPECT_TRUE(measured == 46 || measured == 47) << measured << " en el frame " << frame + 1;
    }
}

// A drawing already past 40 ms does not wait: it adds what GetTickCount measured, in ticks.
TEST(FaithfulPacerTest, ADrawLongerThanTheWaitAddsItWhole)
{
    FaithfulLoop faithful;
    faithful.client.renderCost = 130ms;

    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 1);
    EXPECT_EQ(faithful.loop.Stats().lastMeasuredMs, 125); // 8 ticks
    EXPECT_TRUE(faithful.clock.Sleeps().empty());

    faithful.client.renderCost = 0ms;
    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 3);
}

// The measurement starts after the steps (0x63852b): their time is lost. Measuring from before the steps would give
// 64 frames and 74 steps, not 61 and 71.
TEST(FaithfulPacerTest, TimeSpentInStepsIsNotAccumulated)
{
    FaithfulLoop faithful;
    faithful.client.stepCost = 10ms;

    std::uint64_t frames = 0;
    while (faithful.clock.Now() < 3s)
    {
        faithful.loop.RunFrame(WindowState{}, faithful.input);
        ++frames;
    }

    EXPECT_EQ(frames, 61u);
    EXPECT_EQ(faithful.client.steps, 71u);
    EXPECT_EQ(faithful.clock.Now(), 3'015'625us);
}

// The original has no cap: a 10 s breakpoint is 250 steps in a row.
TEST(FaithfulPacerTest, HasNoCap)
{
    FaithfulLoop faithful;
    faithful.client.renderCost = 10s;

    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 1);
    EXPECT_EQ(faithful.loop.Stats().lastMeasuredMs, 10'000);

    faithful.client.renderCost = 0ms;
    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 250);
}

// Minimized, the GetTickCount wait goes on: without it the accumulator does not grow.
TEST(FaithfulPacerTest, MinimizedKeepsTheWait)
{
    FaithfulLoop faithful;
    WindowState minimized;
    minimized.minimized = true;
    faithful.StepsPerFrame(64, minimized);

    EXPECT_EQ(faithful.clock.Now(), 3s);
    EXPECT_EQ(faithful.loop.Stats().steps, 74u);
    EXPECT_EQ(faithful.loop.RunFrame(minimized, faithful.input), 2);
    EXPECT_TRUE(faithful.client.alphas.empty());
}

// With the frame held (Q-C85), like minimized: the GetTickCount wait goes on and it does not draw.
TEST(FaithfulPacerTest, AHeldFrameKeepsTheWait)
{
    FaithfulLoop faithful;
    faithful.client.holdsTheLastFrame = true;
    faithful.StepsPerFrame(64);

    EXPECT_EQ(faithful.clock.Now(), 3s);
    EXPECT_EQ(faithful.loop.Stats().steps, 74u);
    EXPECT_TRUE(faithful.client.alphas.empty());

    faithful.client.holdsTheLastFrame = false;
    faithful.StepsPerFrame(1);
    EXPECT_EQ(faithful.client.alphas.size(), 1u);
}

// As in Decoupled, the held frame is checked after the steps: the first frame's step turns it on and that frame is
// no longer drawn.
TEST(FaithfulPacerTest, AHoldFromAStepSkipsThatFramesDrawing)
{
    FaithfulLoop faithful;
    faithful.client.holdsAfterAStep = true;

    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 1);

    EXPECT_EQ(faithful.client.calls, (std::vector<LoopCall>{LoopCall::Poll, LoopCall::Step}));
}

// The faithful mode drawing is not interpolated: the state after the steps.
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

// The DWORD wrap (49.7 days) changes nothing: the subtraction wraps the same as the time. It starts 67 ticks before
// the first one that reads 0 after the wrap, 274877907, and at a multiple of 8 ticks (exactly 125 ms), so each frame
// measures the same as from 0 (like the lab, labs/cl3.0-bucle/jefe).
TEST(FaithfulPacerTest, TheGetTickCountWrapChangesNothing)
{
    const auto start = 274'877'840 * FaithfulTick;
    FaithfulLoop nearTheWrap;
    nearTheWrap.clock.Advance(start);
    FaithfulLoop fromZero;

    EXPECT_EQ(nearTheWrap.StepsPerFrame(64), fromZero.StepsPerFrame(64));
    EXPECT_EQ(nearTheWrap.clock.Now() - start, 3s);
    EXPECT_EQ(nearTheWrap.loop.Stats().accumulated, 80ms);
}

// The wait comparison is signed (JGE at 0x638616): if the clock goes back during the drawing, the subtraction is
// negative and the wait goes on until 40 ms after the start. Unsigned, the subtraction would wrap, the wait would
// exit at once and add -31.
TEST(FaithfulPacerTest, AClockThatGoesBackKeepsWaiting)
{
    FaithfulLoop faithful;
    faithful.clock.Advance(10 * FaithfulTick); // the start reads 156
    faithful.client.renderCost = -20ms;        // ends at tick 8, which reads 125

    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 1);
    EXPECT_EQ(faithful.loop.Stats().lastMeasuredMs, 47); // tick 13 reads 203
    EXPECT_EQ(faithful.clock.Now(), 13 * FaithfulTick);
}

// The accumulator adds like x86's ADD: a drawing of INT32_MAX ms makes it wrap, without UB, and it ends negative:
// like the original, it runs no steps until it is back at 40.
TEST(FaithfulPacerTest, TheAccumulatorWrapsLikeTheAdd)
{
    FaithfulLoop faithful;
    ASSERT_EQ(faithful.StepsPerFrame(2), (std::vector<int>{1, 1}));
    faithful.client.renderCost = std::chrono::milliseconds{std::numeric_limits<std::int32_t>::max()};

    // Frame 3 runs its step and leaves 13; the drawing goes from tick 6 (93 ms) to 2147483734 ms.
    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 1);
    EXPECT_EQ(faithful.loop.Stats().lastMeasuredMs, 2'147'483'641);
    // 13 + 2147483641 = 2147483654, which in int32 is 2147483654 - 2^32.
    EXPECT_EQ(faithful.loop.Stats().accumulated, std::chrono::milliseconds{-2'147'483'642});

    faithful.client.renderCost = 0ms;
    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 0);
}
} // namespace
