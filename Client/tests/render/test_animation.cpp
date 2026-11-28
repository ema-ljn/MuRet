#include "Models/Animation.h"

#include "App/FixedStep.h"
#include "Core/IClock.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Client::Models: the animation advance (0x4ec200), the action change (0x4ea030), the pet and the wings by time
// (Q-C20) and the render copy.
namespace
{
using Client::App::FixedStep;
using Client::Core::Duration;
using Client::Models::ActionInfo;
using Client::Models::AdvanceAnimation;
using Client::Models::AdvanceByTime;
using Client::Models::AnimationScene;
using Client::Models::AnimationState;
using Client::Models::Interpolated;
using Client::Models::MaxOriginalFramesPerDraw;
using Client::Models::OriginalFrameCarry;
using Client::Models::OriginalFramePace;
using Client::Models::OriginalFrameTime;
using Client::Models::PetKeysPerOriginalFrame;
using Client::Models::SetAction;
using Client::Models::Skeleton;
using Client::Models::TimedAnimation;

using namespace std::chrono_literals;

constexpr AnimationScene Other = AnimationScene::Other;
constexpr float Tolerance = 1e-4f;

// Like the actions of Player.bmd.
ActionInfo Repeating(std::int16_t keys, float speed = 0.0f)
{
    return ActionInfo{true, speed, keys, false};
}

void Steps(AnimationState& state, const ActionInfo& action, int steps, AnimationScene scene = Other)
{
    for (int k = 0; k < steps; ++k)
    {
        (void)AdvanceAnimation(state, action, action.speed, scene);
    }
}

TEST(AnimationTest, AdvancesBySpeedPerStep)
{
    AnimationState state{3, 1.0f, 3, 0};

    const bool ended = AdvanceAnimation(state, Repeating(7), 0.26f, Other);

    EXPECT_FALSE(ended);
    EXPECT_FLOAT_EQ(state.frame, 1.26f);
}

// Past the last key it wraps modulo the keys, keeping the fractional part, and ends.
TEST(AnimationTest, WrapsModuloTheKeys)
{
    AnimationState state{3, 6.9f, 3, 6};

    const bool ended = AdvanceAnimation(state, Repeating(7), 0.26f, Other);

    EXPECT_TRUE(ended);
    EXPECT_NEAR(state.frame, 0.16f, Tolerance);
}

// With lock_positions, the modulo is the keys minus 1: the last key is the same as key 0.
TEST(AnimationTest, WrapsModuloKeysMinusOneWithLockPositions)
{
    AnimationState state{3, 5.9f, 3, 5};

    const bool ended = AdvanceAnimation(state, ActionInfo{true, 0.0f, 7, true}, 0.26f, Other);

    EXPECT_TRUE(ended);
    EXPECT_NEAR(state.frame, 0.16f, Tolerance);
}

// One that does not repeat stays at the keys minus 0.01 (0x6b88d4), and ends.
TEST(AnimationTest, HoldsJustBeforeTheLastKeyWithoutRepeat)
{
    AnimationState state{68, 6.9f, 68, 6};

    const bool ended = AdvanceAnimation(state, ActionInfo{false, 0.0f, 7, false}, 0.26f, Other);

    EXPECT_TRUE(ended);
    EXPECT_FLOAT_EQ(state.frame, 7.0f - 0.01f);
}

// With the frame at 0 or less, it adds the keys minus 1.
TEST(AnimationTest, ANegativeFrameAddsKeysMinusOne)
{
    AnimationState state{3, 0.1f, 3, 0};

    (void)AdvanceAnimation(state, Repeating(7), -0.3f, Other);

    EXPECT_NEAR(state.frame, 6.0f - 0.2f, Tolerance);
}

// Q-C87: an action the original gives no speed advances by 0. At frame 0, the rule above moves it to the last key,
// and it stays there. The whole key did not change on adding, so the previous one is the one it had: with t at 0, the
// pose shows that one.
TEST(AnimationTest, AnActionWithoutSpeedGoesToItsLastKeyAndStays)
{
    AnimationState state{40, 0.0f, 12, 3};

    Steps(state, Repeating(5), 10);

    EXPECT_FLOAT_EQ(state.frame, 4.0f);
    EXPECT_EQ(state.previousAction, 12);
    EXPECT_EQ(state.previousKey, 3);
}

// With lock_positions (action 0 of Player.bmd, of three keys, the only one without speed that has it), the last key
// is the modulo: it goes back to 0 on each advance, and ends on each one.
TEST(AnimationTest, AnActionWithoutSpeedAndWithLockStaysAtZero)
{
    AnimationState state{0, 0.0f, 0, 0};

    for (int k = 0; k < 10; ++k)
    {
        EXPECT_TRUE(AdvanceAnimation(state, ActionInfo{true, 0.0f, 3, true}, 0.0f, Other));
        EXPECT_EQ(state.frame, 0.0f);
    }
}

// With a negative speed, the whole key truncates toward 0, like the original's __ftol: from 0.2 to −0.3 it does not
// change, and the previous one stays as it was.
TEST(AnimationTest, ANegativeSpeedTruncatesTowardZero)
{
    AnimationState state{3, 0.2f, 1, 4};

    (void)AdvanceAnimation(state, Repeating(7), -0.5f, Other);

    EXPECT_EQ(state.previousAction, 1);
    EXPECT_EQ(state.previousKey, 4);
    EXPECT_NEAR(state.frame, 6.0f - 0.3f, Tolerance);
}

// A step that crosses two keys (with 0x4f9580's multipliers a speed reaches 2) stores the key from before, not the
// one it crossed in between.
TEST(AnimationTest, CrossingTwoKeysStoresTheOldOne)
{
    AnimationState state{3, 1.5f, 3, 0};

    (void)AdvanceAnimation(state, Repeating(7), 2.0f, Other);

    EXPECT_EQ(state.previousKey, 1);
    EXPECT_FLOAT_EQ(state.frame, 3.5f);
}

// Landing exactly on the key count is already past the last one: a repeating action goes back to 0 and one that does
// not stays at the keys minus 0.01.
TEST(AnimationTest, LandingOnTheKeyCountIsPastTheLastKey)
{
    AnimationState repeating{3, 6.75f, 3, 6};
    AnimationState holding{68, 6.5f, 68, 6};

    EXPECT_TRUE(AdvanceAnimation(repeating, Repeating(7), 0.25f, Other));
    EXPECT_TRUE(AdvanceAnimation(holding, ActionInfo{false, 0.0f, 7, false}, 0.5f, Other));

    EXPECT_EQ(repeating.frame, 0.0f);
    EXPECT_FLOAT_EQ(holding.frame, 7.0f - 0.01f);
}

// The selection's + 2 is only for the actions that repeat: one that does not repeat ends with the real frame.
TEST(AnimationTest, TheSelectionDoesNotLookAheadWithoutRepeat)
{
    AnimationState state{68, 4.9f, 68, 4};

    EXPECT_FALSE(AdvanceAnimation(state, ActionInfo{false, 0.0f, 7, false}, 0.26f, AnimationScene::CharacterSelect));
    EXPECT_NEAR(state.frame, 5.16f, Tolerance);
}

// When the whole key changes, the old one and its action become the previous ones; without a change, the earlier ones
// stay.
TEST(AnimationTest, StoresThePreviousKeyWhenItCrossesOne)
{
    AnimationState state{3, 1.9f, 1, 4};

    (void)AdvanceAnimation(state, Repeating(7), 0.05f, Other);
    EXPECT_EQ(state.previousAction, 1);
    EXPECT_EQ(state.previousKey, 4);

    (void)AdvanceAnimation(state, Repeating(7), 0.26f, Other);
    EXPECT_EQ(state.previousAction, 3);
    EXPECT_EQ(state.previousKey, 1);
}

// Q-C21: in the selection, "ended" comes from frame + 2, two keys early, but the frame follows the real one and wraps
// only past the last key.
TEST(AnimationTest, TheSelectionEndsTwoKeysEarlyButWrapsOnTheRealFrame)
{
    AnimationState selection{3, 4.9f, 3, 4};
    AnimationState other = selection;

    EXPECT_TRUE(AdvanceAnimation(selection, Repeating(7), 0.26f, AnimationScene::CharacterSelect));
    EXPECT_FALSE(AdvanceAnimation(other, Repeating(7), 0.26f, Other));
    EXPECT_FLOAT_EQ(selection.frame, other.frame);
    EXPECT_NEAR(selection.frame, 5.16f, Tolerance);
}

// With one key or none, it neither advances nor ends.
TEST(AnimationTest, AnActionOfOneKeyDoesNotMove)
{
    for (const std::int16_t keys : {std::int16_t{0}, std::int16_t{1}})
    {
        AnimationState state{3, 0.0f, 3, 0};

        EXPECT_FALSE(AdvanceAnimation(state, Repeating(keys), 0.26f, Other));
        EXPECT_EQ(state.frame, 0.0f);
    }
}

Skeleton ThreeActions()
{
    Skeleton skeleton;
    skeleton.actions = {Repeating(7), Repeating(7), Repeating(7)};
    return skeleton;
}

TEST(AnimationTest, SetActionResetsOnlyForAnotherAction)
{
    AnimationState state{1, 3.5f, 0, 2};

    SetAction(state, ThreeActions(), 1);
    EXPECT_EQ(state.action, 1);
    EXPECT_EQ(state.frame, 3.5f);
    EXPECT_EQ(state.previousKey, 2);

    SetAction(state, ThreeActions(), 2);
    EXPECT_EQ(state.action, 2);
    EXPECT_EQ(state.frame, 0.0f);
}

// The old action and its key become the previous ones: the pose goes from there to the new one's key 0.
TEST(AnimationTest, SetActionKeepsTheOldOneAsPrevious)
{
    AnimationState state{1, 3.5f, 0, 2};

    SetAction(state, ThreeActions(), 2);

    EXPECT_EQ(state.previousAction, 1);
    EXPECT_EQ(state.previousKey, 3);
}

TEST(AnimationTest, SetActionIgnoresAnActionTheModelDoesNotHave)
{
    AnimationState state{1, 3.5f, 0, 2};

    SetAction(state, ThreeActions(), 3);

    EXPECT_EQ(state.action, 1);
    EXPECT_EQ(state.frame, 3.5f);
    EXPECT_EQ(state.previousAction, 0);
}

// The selection trace (06): action 3, of seven keys, at 0.26 per step. The table's frames, plus 1 as the original
// stores them, jump 33, 33, 34 and 34 steps.
TEST(AnimationTest, TheSelectionJumpsAreThoseOfTheTrace)
{
    constexpr std::array<float, 5> Frames = {1.740f, 3.320f, 4.900f, 6.740f, 1.580f};
    constexpr std::array<int, 4> StepsBetween = {33, 33, 34, 34};
    const ActionInfo action = Repeating(7, 0.26f);
    AnimationState state{3, Frames[0] + 1.0f, 3, 1};

    for (std::size_t k = 0; k < StepsBetween.size(); ++k)
    {
        Steps(state, action, StepsBetween[k]);
        EXPECT_NEAR(state.frame, std::fmod(Frames[k + 1] + 1.0f, 7.0f), 0.002f) << "swap " << k + 1;
    }
}

// Lorencia's: action 1, of six keys, at 0.28; the guard jumps 20, 19, 19 and 19 steps.
TEST(AnimationTest, TheLorenciaJumpsAreThoseOfTheTrace)
{
    constexpr std::array<float, 5> Frames = {0.681f, 0.281f, 5.601f, 4.921f, 4.241f};
    constexpr std::array<int, 4> StepsBetween = {20, 19, 19, 19};
    const ActionInfo action = Repeating(6, 0.28f);
    AnimationState state{1, Frames[0] + 1.0f, 1, 0};

    for (std::size_t k = 0; k < StepsBetween.size(); ++k)
    {
        Steps(state, action, StepsBetween[k]);
        EXPECT_NEAR(state.frame, std::fmod(Frames[k + 1] + 1.0f, 6.0f), 0.002f) << "swap " << k + 1;
    }
}

// The body, with the CL3 fixed step.
AnimationState BodyAfterThreeSeconds(Duration frameTime)
{
    const ActionInfo action = Repeating(7, 0.26f);
    AnimationState state{3, 0.0f, 3, 0};
    FixedStep step;
    for (Duration elapsed{}; elapsed < 3s; elapsed += frameTime)
    {
        Steps(state, action, step.Advance(frameTime));
    }
    return state;
}

// The body advances per 40 ms step: the same keys per second at 21.33, 60 and 240 FPS.
TEST(AnimationTest, TheBodyRunsTheSameKeysPerSecondAtEveryFrameRate)
{
    const AnimationState original = BodyAfterThreeSeconds(OriginalFrameTime);

    for (const Duration frameTime : {Duration{16'666'667}, Duration{4'166'667}})
    {
        const AnimationState state = BodyAfterThreeSeconds(frameTime);
        EXPECT_FLOAT_EQ(state.frame, original.frame) << frameTime.count();
    }
    // 75 steps of 0.26: 19.5 keys, modulo 7.
    EXPECT_NEAR(original.frame, 5.5f, 0.001f);
}

TimedAnimation PetAfterThreeSeconds(Duration frameTime)
{
    TimedAnimation pet;
    for (Duration elapsed{}; elapsed < 3s; elapsed += frameTime)
    {
        AdvanceByTime(pet, Repeating(7), PetKeysPerOriginalFrame, frameTime);
    }
    return pet;
}

// Q-C20 corrected: the pet does 0.5 keys every 46.875 ms, 10.67 per second, at any FPS.
TEST(AnimationTest, ThePetRunsByTimeAtEveryFrameRate)
{
    for (const Duration frameTime : {OriginalFrameTime, Duration{16'666'667}, Duration{4'166'667}})
    {
        const TimedAnimation pet = PetAfterThreeSeconds(frameTime);
        // 64 frames of the original: 32 keys, modulo 7.
        EXPECT_FLOAT_EQ(pet.state.frame, 4.0f) << frameTime.count();
    }
}

TEST(AnimationTest, ANegativeTimeCountsZero)
{
    TimedAnimation pet;

    AdvanceByTime(pet, Repeating(7), PetKeysPerOriginalFrame, -OriginalFrameTime);
    AdvanceByTime(pet, Repeating(7), PetKeysPerOriginalFrame, OriginalFrameTime);

    EXPECT_FLOAT_EQ(pet.state.frame, 0.5f);
}

// The render copy with alpha 1 is a step, and stores the key it crosses like the step. The state is not touched.
TEST(AnimationTest, TheRenderCopyCrossesKeysLikeTheStep)
{
    const AnimationState state{3, 1.9f, 3, 0};
    AnimationState stepped = state;
    (void)AdvanceAnimation(stepped, Repeating(7), 0.26f, Other);

    const AnimationState copy = Interpolated(state, Repeating(7), 0.26f, 1.0f);
    const AnimationState half = Interpolated(state, Repeating(7), 0.26f, 0.5f);

    EXPECT_FLOAT_EQ(copy.frame, stepped.frame);
    EXPECT_EQ(copy.previousKey, stepped.previousKey);
    EXPECT_EQ(copy.previousKey, 1);
    EXPECT_FLOAT_EQ(half.frame, 2.03f);
    EXPECT_EQ(state.frame, 1.9f);
}

// What the original runs once per drawn frame (the cloth and the scepter), by the clock (Q-C124 and Q-C20
// corrected): the first drawing runs one; after that, one per 46.875 ms passed, with the remainder for the next. A
// clock that goes back does not count, and a jump of more than 64 runs 64 and drops the rest.
TEST(AnimationTest, ByTheClockTheCarryCountsTheOriginalFramesThatPassed)
{
    constexpr OriginalFramePace Clock = OriginalFramePace::ByTheClock;
    OriginalFrameCarry carry;
    const Duration start = 1s;
    EXPECT_EQ(carry.FramesAt(start, Clock), 1);
    EXPECT_EQ(carry.FramesAt(start + OriginalFrameTime, Clock), 1);
    EXPECT_EQ(carry.FramesAt(start + OriginalFrameTime + 20ms, Clock), 0);
    EXPECT_EQ(carry.FramesAt(start + OriginalFrameTime + 50ms, Clock), 1); // 3.125 ms left over
    EXPECT_EQ(carry.FramesAt(start + OriginalFrameTime + 45ms, Clock), 0); // went back 5 ms
    EXPECT_EQ(carry.FramesAt(start + OriginalFrameTime + 89ms, Clock), 1); // 3.125 + 44 ms
    EXPECT_EQ(carry.FramesAt(100s, Clock), MaxOriginalFramesPerDraw);
    EXPECT_EQ(carry.FramesAt(100s + 40ms, Clock), 0) << "el salto descarta lo que sobró";
}

// In faithful mode, one per drawing, like the original on each drawn frame. The faithful loop's frame lasts one of
// the original's, but not exactly 46.875 ms: by the clock, a drawing that comes before the first would run 0 and the
// next 2. It does not catch up either on what was not drawn (the connect that holds the frame).
TEST(AnimationTest, InTheFaithfulModeTheCarryIsOnePerDraw)
{
    const Duration first = 3 * OriginalFrameTime + 300us;
    const Duration early = 4 * OriginalFrameTime + 100us;
    const Duration late = 5 * OriginalFrameTime + 300us;
    OriginalFrameCarry faithful;
    EXPECT_EQ(faithful.FramesAt(first, OriginalFramePace::OncePerDraw), 1);
    EXPECT_EQ(faithful.FramesAt(early, OriginalFramePace::OncePerDraw), 1);
    EXPECT_EQ(faithful.FramesAt(late, OriginalFramePace::OncePerDraw), 1);
    EXPECT_EQ(faithful.FramesAt(late + 2s, OriginalFramePace::OncePerDraw), 1);

    OriginalFrameCarry byTheClock;
    EXPECT_EQ(byTheClock.FramesAt(first, OriginalFramePace::ByTheClock), 1);
    EXPECT_EQ(byTheClock.FramesAt(early, OriginalFramePace::ByTheClock), 0);
    EXPECT_EQ(byTheClock.FramesAt(late, OriginalFramePace::ByTheClock), 2);
}
} // namespace
