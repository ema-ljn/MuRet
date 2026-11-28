#include "App/GameLoop.h"

#include "Kernel/PacingMode.h"
#include "Platform/WindowState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Scenes/IScene.h"
#include "Render/Frame/SceneCanvas.h"
#include "Kernel/SceneId.h"
#include "Scenes/SceneManager.h"
#include "support/FakeClock.h"
#include "support/RecordingClient.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

// Client::App::GameLoop in normal mode (Decoupled): the fixed step with the fake clock. And the network Poll in both
// modes (CL4.1).
namespace
{
using namespace std::chrono_literals;
using Client::App::GameLoop;
using Client::Core::Duration;
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Kernel::PacingMode;
using Client::Platform::WindowState;
using Client::Tests::FakeClock;
using Client::Tests::LoopCall;
using Client::Tests::RecordingClient;

struct AbortedSceneCalls
{
    int steps = 0;
    int renders = 0;
    int held = 0;
    int leaves = 0;
    bool holdFrame = false;
};

class AbortedFrameScene final : public Client::Scenes::IScene
{
public:
    AbortedFrameScene(AbortedSceneCalls& calls, FakeClock& clock, Duration renderCost)
        : m_calls(calls), m_clock(clock), m_renderCost(renderCost)
    {
    }
    Client::Core::Expected<void, std::string> Enter() override
    {
        return {};
    }
    void Step(const InputState&) override
    {
        ++m_calls.steps;
    }
    void Render(Client::Render::Frame::SceneCanvas&, float) override
    {
        ++m_calls.renders;
        m_clock.Advance(m_renderCost);
    }
    void Leave() override
    {
        ++m_calls.leaves;
    }
    void OnFrameHeld() override
    {
        ++m_calls.held;
    }
    std::optional<Client::Kernel::SceneId> Next() const override
    {
        return std::nullopt;
    }
    bool WantsToQuit() const override
    {
        return false;
    }
    bool WantsTextInput() const override
    {
        return false;
    }
    bool HoldsTheLastFrame() const override
    {
        return m_calls.holdFrame;
    }

private:
    AbortedSceneCalls& m_calls;
    FakeClock& m_clock;
    Duration m_renderCost;
};

// NoStepOnAbort is observed through the real manager, in both modes and with several steps pending.
TEST(GameLoopTest, ASessionApplicationFailureStopsTheSceneInTheSameFrame)
{
    for (const PacingMode mode : {PacingMode::Decoupled, PacingMode::Faithful})
    {
        for (const auto elapsed : {0ms, 16ms, 120ms})
        {
            AbortedSceneCalls calls;
            Client::Tests::RecordingFrameTarget target;
            Client::Tests::RecordingSink sink;
            Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
            FakeClock clock;
            int polls = 0;
            {
                Client::Scenes::SceneManager manager(
                    [&calls, &clock, elapsed](Client::Kernel::SceneId)
                    { return std::make_unique<AbortedFrameScene>(calls, clock, elapsed); }, target, clock,
                    [&polls]() -> Client::Core::Expected<void, std::string>
                    {
                        ++polls;
                        if (polls == 1)
                        {
                            return {};
                        }
                        return Client::Core::Unexpected(std::string("falló la recepción"));
                    },
                    log);
                ASSERT_TRUE(manager.Start(Client::Kernel::SceneId::Loading).HasValue());
                GameLoop loop(clock, manager, mode);
                InputState input;
                loop.RunFrame(WindowState{}, input);
                calls = {};
                calls.holdFrame = true;
                target.frames.clear();
                const int attemptedSteps = loop.RunFrame(WindowState{}, input);
                if (elapsed == 120ms)
                {
                    EXPECT_GE(attemptedSteps, 2);
                }
                manager.Request(Client::Kernel::SceneId::Login);
                EXPECT_TRUE(manager.Failure().has_value());
                EXPECT_EQ(calls.steps, 0);
                EXPECT_EQ(calls.renders, 0);
                EXPECT_EQ(calls.held, 0);
                EXPECT_EQ(calls.leaves, 0);
                EXPECT_TRUE(target.frames.empty());
                loop.RunFrame(WindowState{}, input);
                EXPECT_EQ(polls, 2);
            }
            EXPECT_EQ(calls.leaves, 1);
        }
    }
}

// The alpha tolerance the plan sets.
constexpr double AlphaTolerance = 1e-6;

// How far frame `frame` advances so that `framesPerSecond` frames add up to exactly 1 s.
Duration FrameTime(std::int64_t frame, std::int64_t framesPerSecond)
{
    const std::int64_t second = Duration{1s}.count();
    return Duration{(frame + 1) * second / framesPerSecond - frame * second / framesPerSecond};
}

class GameLoopTest : public ::testing::TestWithParam<int>
{
};

// In 4 simulated s, 100 steps at any frame rate. The first frame only sets the start. Also the frame that runs the
// most steps: 1 at 25 FPS or more, 25 at 1 FPS (like largestStep in MuMain's test_reference_frame_steps.cpp).
// Without it, a loop that ran 2 steps every two frames would give the same total.
TEST_P(GameLoopTest, TwentyFiveStepsPerSecondAtAnyFrameRate)
{
    const std::int64_t framesPerSecond = GetParam();
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);

    const std::int64_t frames = 4 * framesPerSecond;
    int largestStep = 0;
    for (std::int64_t frame = 0; frame < frames; ++frame)
    {
        clock.Advance(FrameTime(frame, framesPerSecond));
        largestStep = std::max(largestStep, loop.RunFrame(WindowState{}, input));
    }

    EXPECT_EQ(clock.Now(), 4s);
    EXPECT_EQ(client.steps, 100u);
    EXPECT_EQ(loop.Stats().steps, 100u);
    EXPECT_EQ(loop.Stats().frames, static_cast<std::uint64_t>(frames + 1));
    EXPECT_EQ(loop.Stats().clampedFrames, 0u);
    EXPECT_EQ(largestStep, framesPerSecond >= 25 ? 1 : 25 / framesPerSecond);
}

INSTANTIATE_TEST_SUITE_P(FrameRates, GameLoopTest, ::testing::Values(1, 25, 60, 144, 240));

TEST(GameLoopTest, ASlowFrameCatchesUpWithThreeSteps)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    client.renderCost = 130ms;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);
    client.renderCost = 0ms;

    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 3);
    EXPECT_NEAR(loop.LastAlpha(), 0.25, AlphaTolerance);
    ASSERT_EQ(client.alphas.size(), 2u);
    EXPECT_NEAR(client.alphas.back(), 0.25, AlphaTolerance);
}

// The breakpoint: a 10 s drawing runs 25 steps (MaxFrameTime) and not 250 like the original.
TEST(GameLoopTest, AFrameLongerThanTheCapRunsOnlyTheCap)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    client.renderCost = 10s;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);
    client.renderCost = 0ms;

    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 25);
    EXPECT_EQ(loop.Stats().clampedFrames, 1u);
    for (int frame = 0; frame < 5; ++frame)
    {
        clock.Advance(40ms);
        EXPECT_EQ(loop.RunFrame(WindowState{}, input), 1) << "frame " << frame;
    }
    EXPECT_EQ(loop.Stats().clampedFrames, 1u);
}

// Entering a scene runs inside a step (0x6349a0 from 0x634c8e, the load outside the loop), and the original
// measures from after the steps (0x63852b): the time to load a scene is not game time. A step that changes scene and
// takes 2 s leaves the next frame at 1 step; the same step without the scene change does count, and the next frame
// catches up to the cap.
TEST(GameLoopTest, TheTimeOfASceneChangeDoesNotCount)
{
    for (const bool changesTheScene : {true, false})
    {
        SCOPED_TRACE(changesTheScene);
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, PacingMode::Decoupled};
        InputState input;
        ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);
        clock.Advance(40ms);
        client.stepCost = 2s;
        client.changesTheSceneInAStep = changesTheScene;
        ASSERT_EQ(loop.RunFrame(WindowState{}, input), 1);
        client.stepCost = 0ms;
        client.changesTheSceneInAStep = false;

        clock.Advance(40ms);
        EXPECT_EQ(loop.RunFrame(WindowState{}, input), changesTheScene ? 1 : 25);
    }
}

TEST(GameLoopTest, AClockThatGoesBackRunsNoSteps)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    // Lejos de 0: Now() no baja de 0 (IClock).
    clock.Advance(100ms);
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);

    clock.Advance(-5ms);
    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 0);
    EXPECT_EQ(loop.Stats().accumulated, 0ns);
    EXPECT_NEAR(loop.LastAlpha(), 0.0, AlphaTolerance);

    // The next one is measured from the new value.
    clock.Advance(40ms);
    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 1);
}

// With VSync off, two frames can read the same instant.
TEST(GameLoopTest, AClockThatDoesNotMoveRunsNoSteps)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);
    clock.Advance(10ms);

    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 0);
    const float alpha = loop.LastAlpha();
    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 0);

    EXPECT_FALSE(std::isnan(loop.LastAlpha()));
    EXPECT_EQ(loop.LastAlpha(), alpha);
    EXPECT_NEAR(alpha, 0.25, AlphaTolerance);
    EXPECT_EQ(client.steps, 0u);
}

// Each frame sleeps what is left until the next step, so the loop sleeps instead of spinning.
TEST(GameLoopTest, AMinimizedWindowStepsWithoutDrawing)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    WindowState minimized;
    minimized.minimized = true;

    for (int frame = 0; frame < 26; ++frame)
    {
        loop.RunFrame(minimized, input);
    }

    EXPECT_EQ(client.steps, 25u);
    EXPECT_TRUE(client.alphas.empty());
    const auto sleeps = clock.Sleeps();
    EXPECT_EQ(std::accumulate(sleeps.begin(), sleeps.end(), Duration::zero()), 1040ms);
}

// With the frame held (the login's connect, which in the original blocks before the drawing, Q-C85), like
// minimized: the network and the steps go on, it does not draw and sleeps until the next step. When released, it
// draws again.
TEST(GameLoopTest, AHeldFrameStepsWithoutDrawing)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    client.holdsTheLastFrame = true;

    for (int frame = 0; frame < 26; ++frame)
    {
        loop.RunFrame(WindowState{}, input);
    }

    EXPECT_EQ(client.polls, 26u);
    EXPECT_EQ(client.steps, 25u);
    EXPECT_TRUE(client.alphas.empty());
    const auto sleeps = clock.Sleeps();
    EXPECT_EQ(std::accumulate(sleeps.begin(), sleeps.end(), Duration::zero()), 1040ms);

    client.holdsTheLastFrame = false;
    loop.RunFrame(WindowState{}, input);
    EXPECT_EQ(client.alphas.size(), 1u);
}

// The held frame is checked after the steps: the step that starts the connect (the one that enters the login) no
// longer lets its frame draw. Checked before, that frame would be the login without the notice.
TEST(GameLoopTest, AHoldFromAStepSkipsThatFramesDrawing)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    client.holdsAfterAStep = true;

    loop.RunFrame(WindowState{}, input);
    clock.Advance(40ms);
    loop.RunFrame(WindowState{}, input);

    EXPECT_EQ(client.calls, (std::vector<LoopCall>{LoopCall::Poll, LoopCall::Render, LoopCall::Poll, LoopCall::Step}));
}

// 10 ms accumulated are 30 ms of sleep, not 40.
TEST(GameLoopTest, AMinimizedWindowSleepsWhatIsLeftOfTheStep)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    client.renderCost = 10ms;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);
    WindowState minimized;
    minimized.minimized = true;

    EXPECT_EQ(loop.RunFrame(minimized, input), 0);
    ASSERT_EQ(clock.Sleeps().size(), 1u);
    EXPECT_EQ(clock.Sleeps()[0], 30ms);
    EXPECT_EQ(loop.RunFrame(minimized, input), 1);
}

// With SdlClock, the first Now() comes after creating the window, and the time that took is not steps.
TEST(GameLoopTest, TheFirstFrameOnlySetsTheStart)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    clock.Advance(5s);

    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 0);
    EXPECT_EQ(loop.Stats().clampedFrames, 0u);
    EXPECT_EQ(client.steps, 0u);
    clock.Advance(40ms);
    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 1);
}

TEST(GameLoopTest, AlphaGrowsBetweenSteps)
{
    constexpr std::int64_t FramesPerSecond = 240;
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);

    float previous = loop.LastAlpha();
    int drops = 0;
    for (std::int64_t frame = 0; frame < FramesPerSecond; ++frame)
    {
        clock.Advance(FrameTime(frame, FramesPerSecond));
        const int steps = loop.RunFrame(WindowState{}, input);
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

// An 80 ms frame runs 2 steps with a press: the first sees it, and the second does not (Q-C45). The key stays down:
// ConsumeEdges clears the edges and not the state.
TEST(GameLoopTest, APressIsSeenByExactlyOneStep)
{
    FakeClock clock;
    RecordingClient client{clock};
    client.watchedKey = Key::A;
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);

    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = Key::A});
    clock.Advance(80ms);

    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 2);
    EXPECT_EQ(client.sawPress, (std::vector<bool>{true, false}));
    EXPECT_TRUE(input.IsDown(Key::A));
    EXPECT_FALSE(input.WasPressed(Key::A));
}

// At 240 FPS, a press and its release in a frame without steps: the frame does not touch the input, and the first
// step that comes sees the press, even if the key is already up. The step after it does not.
TEST(GameLoopTest, APressInAFrameWithoutStepsWaitsForTheNextStep)
{
    constexpr std::int64_t FramesPerSecond = 240;
    FakeClock clock;
    RecordingClient client{clock};
    client.watchedKey = Key::Space;
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);

    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = Key::Space});
    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyUp, .key = Key::Space});
    clock.Advance(FrameTime(0, FramesPerSecond));
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);
    EXPECT_TRUE(input.WasPressed(Key::Space));

    std::int64_t frame = 1;
    while (client.steps < 2 && frame < FramesPerSecond)
    {
        clock.Advance(FrameTime(frame, FramesPerSecond));
        loop.RunFrame(WindowState{}, input);
        ++frame;
    }

    EXPECT_EQ(client.sawPress, (std::vector<bool>{true, false}));
    EXPECT_FALSE(input.IsDown(Key::Space));
}

// The FPS cap (MaxFps, MuMain's FpsCap): with 100, a frame that draws in 3 ms sleeps the 7 it lacks to reach its
// target, 10 ms after the previous one. In 1 simulated s, 100 frames run and the logic stays at 25 steps.
TEST(GameLoopTest, AnFpsCapSleepsWhatIsLeftOfTheFrame)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    client.renderCost = 3ms;

    // The first frame sets the start; the next 100 measure 10 ms each.
    for (int frame = 0; frame <= 100; ++frame)
    {
        loop.RunFrame(WindowState{}, input);
    }

    EXPECT_EQ(client.steps, 25u);
    EXPECT_EQ(client.alphas.size(), 101u);
    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 101u);
    EXPECT_TRUE(std::ranges::all_of(sleeps, [](Duration sleep) { return sleep == 7ms; }));
    EXPECT_EQ(clock.Now(), 1010ms);
}

// With 50 (20 ms) and 5 ms steps, the frame that runs a step sleeps 15 ms.
TEST(GameLoopTest, TheFpsCapCountsTheStepsToo)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 50};
    InputState input;
    client.stepCost = 5ms;

    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 0);
    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 0);
    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 1);

    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 3u);
    EXPECT_EQ(sleeps[0], 20ms);
    EXPECT_EQ(sleeps[1], 20ms);
    EXPECT_EQ(sleeps[2], 15ms);
}

// A frame that already took longer than the cap does not sleep, and neither does one without a cap (0, the
// default).
TEST(GameLoopTest, AFrameLongerThanTheFpsCapDoesNotSleep)
{
    for (const int maxFps : {100, 0})
    {
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, PacingMode::Decoupled, maxFps};
        InputState input;
        client.renderCost = maxFps == 0 ? 3ms : 15ms;

        for (int frame = 0; frame < 10; ++frame)
        {
            loop.RunFrame(WindowState{}, input);
        }

        EXPECT_TRUE(clock.Sleeps().empty()) << "maxFps " << maxFps;
    }
}

// Minimized, the cap is not used: it sleeps what is left until the next step, as without a cap. And on return, the
// target starts over from the frame's Now(). With 40 (25 ms): the visible frame sleeps 25; the minimized one 15,
// until the 40 ms step and not until the 50 target; and the visible one after it 25, until 65 and not until the old
// target's 50.
TEST(GameLoopTest, AMinimizedWindowIgnoresTheFpsCap)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 40};
    InputState input;
    WindowState minimized;
    minimized.minimized = true;

    loop.RunFrame(WindowState{}, input);
    loop.RunFrame(minimized, input);
    loop.RunFrame(WindowState{}, input);

    EXPECT_EQ(client.steps, 1u);
    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 3u);
    EXPECT_EQ(sleeps[0], 25ms);
    EXPECT_EQ(sleeps[1], 15ms);
    EXPECT_EQ(sleeps[2], 25ms);
}

// If the clock goes back during the frame (only the FakeClock, without going below 0), the cap does not sleep: the
// subtraction would give more than a whole frame.
TEST(GameLoopTest, AClockThatGoesBackDuringTheFrameDoesNotSleep)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    clock.Advance(100ms);
    client.renderCost = -5ms;

    loop.RunFrame(WindowState{}, input);

    EXPECT_TRUE(clock.Sleeps().empty());
}

// What runs between two RunFrame (the events, --stats) does not add up: each frame's target is the previous one's
// plus 10 ms. With 1 ms outside and 2 of drawing, the first frame sleeps 8 and the others 7, and 100 frames are still
// 1 s, not 1.1.
TEST(GameLoopTest, TheFpsCapDoesNotAddTheTimeBetweenFrames)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    client.renderCost = 2ms;

    for (int frame = 0; frame < 100; ++frame)
    {
        loop.RunFrame(WindowState{}, input);
        clock.Advance(1ms);
    }

    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 100u);
    EXPECT_EQ(sleeps[0], 8ms);
    EXPECT_TRUE(std::all_of(sleeps.begin() + 1, sleeps.end(), [](Duration sleep) { return sleep == 7ms; }));
    EXPECT_EQ(clock.Now(), 1001ms);
}

// If a frame overruns the target, the next counts from where it ended, not from the following frame's Now(): what
// runs between the two does not add up either. With 100 (10 ms), 6 ms of drawing and 5 outside, each frame lasts 11:
// only the first sleeps (4 ms), and 6 frames are 70 ms, the 66 without a cap plus those 4. Counting from the Now(),
// one in two would sleep 4 and they would be 78: the cap would give fewer FPS than no cap.
TEST(GameLoopTest, AfterAMissedDeadlineTheTimeBetweenFramesStillCounts)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    client.renderCost = 6ms;

    for (int frame = 0; frame < 6; ++frame)
    {
        loop.RunFrame(WindowState{}, input);
        clock.Advance(5ms);
    }

    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 1u);
    EXPECT_EQ(sleeps[0], 4ms);
    EXPECT_EQ(clock.Now(), 70ms);
}

// A gap between frames longer than the cap counts too: the next frame is already late for its target and does not
// sleep, and the one after goes back to the cap. With 100, 2 ms of drawing and 15 ms outside after the first.
TEST(GameLoopTest, ATimeBetweenFramesLongerThanTheFpsCapCountsToo)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    client.renderCost = 2ms;

    // Draws until 2 ms and sleeps until the target, at 10.
    loop.RunFrame(WindowState{}, input);
    clock.Advance(15ms);
    // The target was at 20, and the frame starts at 25: it draws until 27 without sleeping.
    loop.RunFrame(WindowState{}, input);
    // The target, 10 ms after 27: it draws until 29 and sleeps 8.
    loop.RunFrame(WindowState{}, input);

    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 2u);
    EXPECT_EQ(sleeps[0], 8ms);
    EXPECT_EQ(sleeps[1], 8ms);
    EXPECT_EQ(clock.Now(), 37ms);
}

// If the clock goes back between two frames (only the FakeClock), the previous target is left ahead and is of no
// use: the frame counts from its Now(). With 100 and 2 ms of drawing, after going back 5 ms it sleeps 8, not the 13
// left for the old target plus 10.
TEST(GameLoopTest, AClockThatGoesBackBetweenFramesStartsTheFpsCapOver)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    client.renderCost = 2ms;

    loop.RunFrame(WindowState{}, input);
    clock.Advance(-5ms);
    loop.RunFrame(WindowState{}, input);

    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 2u);
    EXPECT_EQ(sleeps[0], 8ms);
    EXPECT_EQ(sleeps[1], 8ms);
}

// After a frame longer than the cap, the next counts from where the long one ended: it sleeps the whole 7 ms and does
// not catch up with a shorter frame.
TEST(GameLoopTest, AfterALongFrameTheFpsCapStartsOver)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;

    client.renderCost = 3ms;
    loop.RunFrame(WindowState{}, input);
    client.renderCost = 15ms;
    loop.RunFrame(WindowState{}, input);
    client.renderCost = 3ms;
    loop.RunFrame(WindowState{}, input);

    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 2u);
    EXPECT_EQ(sleeps[0], 7ms);
    EXPECT_EQ(sleeps[1], 7ms);
}

// In Faithful the cap is not used (Resolve does not allow it either): it waits like the original. With 1, which
// outside Faithful would sleep almost 1 s per frame, the sleeps are the same as without a cap.
TEST(GameLoopTest, AFaithfulLoopIgnoresTheFpsCap)
{
    std::vector<std::vector<Duration>> sleeps;
    for (const int maxFps : {0, 1})
    {
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, PacingMode::Faithful, maxFps};
        InputState input;
        client.renderCost = 3ms;
        for (int frame = 0; frame < 10; ++frame)
        {
            loop.RunFrame(WindowState{}, input);
        }
        sleeps.emplace_back(clock.Sleeps().begin(), clock.Sleeps().end());
    }

    EXPECT_FALSE(sleeps[0].empty());
    EXPECT_EQ(sleeps[0], sleeps[1]);
}
// The order of a frame (CL4.1): the network once, before the steps, and the drawing at the end. The second frame
// runs two steps: in Decoupled, 80 ms after the first, which only sets the start; in Faithful, with a first drawing
// of 100 ms, which measures 6 ticks (93 ms) and leaves the accumulator at 93 after the initial value's step. A Poll
// after the steps or one per step would give another order; in faithful mode, FaithfulDoesNotMeasureThePoll does not
// tell them apart, and this one does.
TEST(GameLoopTest, PollsOncePerFrameBeforeTheSteps)
{
    struct Case
    {
        PacingMode mode;
        const char* name;
        Duration firstRender;
        Duration betweenFrames;
    };
    for (const Case& mode :
         {Case{PacingMode::Decoupled, "Decoupled", 0ms, 80ms}, Case{PacingMode::Faithful, "Faithful", 100ms, 0ms}})
    {
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, mode.mode};
        InputState input;
        client.renderCost = mode.firstRender;
        loop.RunFrame(WindowState{}, input);
        EXPECT_EQ(client.polls, 1u) << mode.name;
        ASSERT_FALSE(client.calls.empty()) << mode.name;
        EXPECT_EQ(client.calls.front(), LoopCall::Poll) << mode.name;

        client.renderCost = 0ms;
        client.calls.clear();
        clock.Advance(mode.betweenFrames);

        EXPECT_EQ(loop.RunFrame(WindowState{}, input), 2) << mode.name;
        EXPECT_EQ(client.calls,
                  (std::vector<LoopCall>{LoopCall::Poll, LoopCall::Step, LoopCall::Step, LoopCall::Render}))
            << mode.name;
    }
}

// P-C61: each loop with the window not minimized ends with FinishTheLoop, after the drawing and also with the frame
// held, where the original runs the frame without presenting it; minimized, it does not (WinMain's frameless
// branch). In both modes.
TEST(GameLoopTest, EachLoopNotMinimizedFinishesAfterTheDrawing)
{
    WindowState minimized;
    minimized.minimized = true;
    for (const PacingMode mode : {PacingMode::Decoupled, PacingMode::Faithful})
    {
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, mode};
        InputState input;

        loop.RunFrame(WindowState{}, input);
        ASSERT_EQ(client.finishedLoops.size(), 1u);
        EXPECT_EQ(client.finishedLoops[0], client.calls.size());
        EXPECT_EQ(client.calls.back(), LoopCall::Render);

        client.holdsTheLastFrame = true;
        clock.Advance(10ms);
        loop.RunFrame(WindowState{}, input);
        EXPECT_EQ(client.finishedLoops.size(), 2u);
        EXPECT_EQ(client.alphas.size(), 1u) << "retenido no dibuja";

        client.holdsTheLastFrame = false;
        clock.Advance(10ms);
        loop.RunFrame(minimized, input);
        EXPECT_EQ(client.finishedLoops.size(), 2u);
    }
}

// Minimized there is no drawing, but the network goes on, as in the original (0x4e1d50 runs on every loop of
// WinMain): 26 frames are 26 polls, with the 25 steps of AMinimizedWindowStepsWithoutDrawing. In faithful mode, also
// one per frame.
TEST(GameLoopTest, PollsWhileMinimized)
{
    WindowState minimized;
    minimized.minimized = true;
    {
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, PacingMode::Decoupled};
        InputState input;
        for (int frame = 0; frame < 26; ++frame)
        {
            loop.RunFrame(minimized, input);
        }

        EXPECT_EQ(client.polls, 26u);
        EXPECT_EQ(client.steps, 25u);
        EXPECT_TRUE(client.alphas.empty());
    }
    {
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, PacingMode::Faithful};
        InputState input;
        for (int frame = 0; frame < 26; ++frame)
        {
            loop.RunFrame(minimized, input);
        }

        EXPECT_EQ(client.polls, 26u);
        EXPECT_TRUE(client.alphas.empty());
    }
}

// In faithful mode the Poll goes before BeginFrame, outside the measurement, like the original's message loop: its
// time is lost in whole ticks, like the steps' time (Q-C44). With 20 ms polls, which always cross a tick edge, each
// frame lasts 4 ticks and not 3: in 3 s, 48 frames and 56 steps. If the poll were inside the measurement, the 40 ms
// wait would hide it and they would be 64 and 74, as without a poll. With 10 ms polls, which cross none, nothing is
// lost: 64 and 74.
TEST(GameLoopTest, FaithfulDoesNotMeasureThePoll)
{
    struct Case
    {
        Duration pollCost;
        std::uint64_t frames;
        std::uint64_t steps;
    };
    for (const Case& expected : {Case{20ms, 48, 56}, Case{10ms, 64, 74}})
    {
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, PacingMode::Faithful};
        InputState input;
        client.pollCost = expected.pollCost;

        std::uint64_t frames = 0;
        while (clock.Now() < 3s)
        {
            loop.RunFrame(WindowState{}, input);
            ++frames;
        }

        const auto pollMs = std::chrono::duration_cast<std::chrono::milliseconds>(expected.pollCost).count();
        EXPECT_EQ(frames, expected.frames) << "polls de " << pollMs << " ms";
        EXPECT_EQ(client.steps, expected.steps) << "polls de " << pollMs << " ms";
        EXPECT_EQ(client.polls, frames) << "polls de " << pollMs << " ms";
        EXPECT_EQ(clock.Now(), 3s) << "polls de " << pollMs << " ms";
    }
}
} // namespace
