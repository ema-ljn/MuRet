#include "Scenes/OriginalFramesOfTheSteps.h"

#include "Kernel/LoopTiming.h"
#include "Core/IClock.h"
#include "Models/Animation.h"

#include <gtest/gtest.h>

#include <chrono>
#include <utility>
#include <vector>

namespace
{
using Client::Models::OriginalFramePace;
using Client::Scenes::OriginalFramesOfTheSteps;

// How the draw counts the frames of the original: by the scene's steps, or by the draw clock, as before #307.
enum class Count
{
    ByTheSteps,
    ByTheDrawClock
};

// The steps each frame of the original saw, with GameLoop's loop at `fps` (with `jitter` more or less on each draw, a
// fixed pattern): before each draw, the 40 ms steps the clock owes, and then the draw, which counts its frames of the
// original. A draw that runs more than one frame leaves one entry per frame, and the second saw no step.
std::vector<int> StepsPerOriginalFrame(double fps, int draws, Count count = Count::ByTheSteps,
                                       Client::Core::Duration jitter = {})
{
    OriginalFramesOfTheSteps frames;
    Client::Models::OriginalFrameCarry drawClockFrames;
    const auto drawTime = std::chrono::duration_cast<Client::Core::Duration>(std::chrono::duration<double>(1.0 / fps));
    Client::Core::Duration owed{};
    Client::Core::Duration clock{};
    int stepsSinceTheLast = 0;
    std::vector<int> seen;
    for (int draw = 0; draw < draws; ++draw)
    {
        const Client::Core::Duration elapsed = drawTime + (draw % 3 == 0   ? jitter
                                                           : draw % 3 == 1 ? -jitter
                                                                           : jitter * 0);
        owed += elapsed;
        clock += elapsed;
        while (owed >= Client::Kernel::LogicStep)
        {
            owed -= Client::Kernel::LogicStep;
            frames.Step();
            ++stepsSinceTheLast;
        }
        const int original = count == Count::ByTheSteps
                                 ? frames.FramesAtDraw(OriginalFramePace::ByTheClock)
                                 : drawClockFrames.FramesAt(clock, OriginalFramePace::ByTheClock);
        for (int frame = 0; frame < original; ++frame)
        {
            if (draw > 0)
            {
                seen.push_back(stepsSinceTheLast);
            }
            stepsSinceTheLast = 0;
        }
    }
    return seen;
}

// 02 § El ritmo: the original runs the steps it owes and then draws, so each frame sees 1 or 2 steps (75 every 64
// frames). Counted by the scene's steps, the same at any MuRet FPS from 25; by the draw clock, at 60 FPS, about 40
// per minute saw none and the next saw 2: the walker's cape jerked (#307).
TEST(OriginalFramesOfTheStepsTest, EachOriginalFrameSeesOneOrTwoStepsAtAnyFrameRate)
{
    for (const double fps : {25.0, 30.0, 59.94, 60.0, 75.0, 144.0, 240.0})
    {
        const std::vector<int> seen = StepsPerOriginalFrame(fps, static_cast<int>(fps * 60));
        ASSERT_FALSE(seen.empty()) << fps << " FPS";
        int twos = 0;
        for (const int steps : seen)
        {
            EXPECT_TRUE(steps == 1 || steps == 2) << fps << " FPS: un frame del original vio " << steps << " pasos";
            twos += steps == 2 ? 1 : 0;
        }
        // 11 of every 64 frames see 2 steps: 220 of the 1279 in a minute.
        EXPECT_NEAR(static_cast<double>(twos) / static_cast<double>(seen.size()), 11.0 / 64.0, 0.01) << fps << " FPS";
    }
}

// Below 25 FPS a draw can owe more than 46.875 ms of steps. The original never runs two frames in one draw
// (0x637791..0x6381a5: the steps it owes and one draw): each frame sees at least one step, and below 21.33 FPS, more
// than two, like the original on a slow machine. With a little jitter, the same.
TEST(OriginalFramesOfTheStepsTest, BelowTwentyFiveFpsNoOriginalFrameIsLeftWithoutSteps)
{
    using namespace std::chrono_literals;
    for (const auto& [fps, jitter] :
         {std::pair{10.0, Client::Core::Duration{}}, std::pair{15.0, Client::Core::Duration{}},
          std::pair{20.0, Client::Core::Duration{}}, std::pair{21.33, Client::Core::Duration{}},
          std::pair{22.0, Client::Core::Duration{}}, std::pair{24.0, Client::Core::Duration{}},
          std::pair{25.0, Client::Core::Duration{1ms}}, std::pair{60.0, Client::Core::Duration{4ms}}})
    {
        const std::vector<int> seen = StepsPerOriginalFrame(fps, static_cast<int>(fps * 60), Count::ByTheSteps, jitter);
        ASSERT_FALSE(seen.empty()) << fps << " FPS";
        for (const int steps : seen)
        {
            EXPECT_GE(steps, 1) << fps << " FPS: un frame del original sin pasos";
            EXPECT_LE(steps, 3) << fps << " FPS";
        }
    }
}

// With the draw clock, at 22 FPS there were no frames without steps; counted by the steps without the one-per-draw
// cap, there were 139 per minute. With the cap, none, and each draw is at most one frame.
TEST(OriginalFramesOfTheStepsTest, ADrawIsAtMostOneOriginalFrame)
{
    OriginalFramesOfTheSteps frames;
    EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 1);
    for (int step = 0; step < 5; ++step)
    {
        frames.Step();
    }
    // 200 ms of steps: the original would have drawn it in a single frame.
    EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 1);
    // What was left over is not enough for another frame without new steps.
    EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 0);
    frames.Step();
    EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 1) << "lo que sobró del frame más 40 ms";
}

// Before #307, by the draw clock.
TEST(OriginalFramesOfTheStepsTest, ByTheDrawClockSomeOriginalFramesSawNoStepAt60Fps)
{
    const std::vector<int> seen = StepsPerOriginalFrame(60.0, 60 * 60, Count::ByTheDrawClock);
    int none = 0;
    for (const int steps : seen)
    {
        none += steps == 0 ? 1 : 0;
    }
    EXPECT_GT(none, 0);
}

TEST(OriginalFramesOfTheStepsTest, WithoutStepsTheDrawsAreIntermediate)
{
    OriginalFramesOfTheSteps frames;
    EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 1) << "el primero corre uno";
    for (int draw = 0; draw < 10; ++draw)
    {
        EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 0);
    }
    frames.Step();
    frames.Step();
    EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 1) << "80 ms de pasos: un frame de 46,875";
}

// The game's PrepareFrame asks before the draw whether it will be a frame of the original, to forget 0x505820's scan
// only then: the answer is that of the FramesAtDraw that follows, at any FPS and in both modes, and asking does not
// count the draw. Above 25 FPS in decoupled mode there are intermediate draws; in faithful, none.
TEST(OriginalFramesOfTheStepsTest, TheQuestionBeforeTheDrawIsWhatTheDrawCounts)
{
    for (const OriginalFramePace pace : {OriginalFramePace::ByTheClock, OriginalFramePace::OncePerDraw})
    {
        for (const double fps : {10.0, 21.33, 25.0, 60.0, 144.0})
        {
            OriginalFramesOfTheSteps frames;
            const auto drawTime =
                std::chrono::duration_cast<Client::Core::Duration>(std::chrono::duration<double>(1.0 / fps));
            Client::Core::Duration owed{};
            int intermediate = 0;
            for (int draw = 0; draw < static_cast<int>(fps * 10.0); ++draw)
            {
                owed += drawTime;
                while (owed >= Client::Kernel::LogicStep)
                {
                    owed -= Client::Kernel::LogicStep;
                    frames.Step();
                }
                const bool asked = frames.NextDrawIsAnOriginalFrame(pace);
                ASSERT_EQ(frames.NextDrawIsAnOriginalFrame(pace), asked) << fps << " FPS, dibujo " << draw;
                const int counted = frames.FramesAtDraw(pace);
                ASSERT_EQ(asked, counted > 0) << fps << " FPS, dibujo " << draw;
                intermediate += counted == 0 ? 1 : 0;
            }
            if (pace == OriginalFramePace::OncePerDraw)
            {
                EXPECT_EQ(intermediate, 0) << fps << " FPS";
            }
            else if (fps > 25.0)
            {
                EXPECT_GT(intermediate, 0) << fps << " FPS";
            }
        }
    }
}

// The faithful draw already comes after its steps.
TEST(OriginalFramesOfTheStepsTest, InTheFaithfulModeEachDrawIsOneOriginalFrame)
{
    OriginalFramesOfTheSteps frames;
    for (int draw = 0; draw < 10; ++draw)
    {
        EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::OncePerDraw), 1);
    }
}
} // namespace
