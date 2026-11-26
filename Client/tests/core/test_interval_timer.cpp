#include "Core/IntervalTimer.h"

#include "Core/IClock.h"

#include <gtest/gtest.h>

#include <chrono>

namespace
{
using Client::Core::Duration;
using Client::Core::IntervalTimer;
using std::chrono::milliseconds;
using std::chrono::seconds;

constexpr Duration Period = seconds{20};

// Started at 0: due at 20 s and not before, once, and the next at 40 s although the 20 s one was seen 16 ms late. No
// drift: the 16 ms do not add up.
TEST(IntervalTimerTest, FiresEveryPeriod)
{
    IntervalTimer timer(Period);
    timer.Start(Duration::zero());

    EXPECT_FALSE(timer.Due(milliseconds{19999}));
    EXPECT_TRUE(timer.Due(milliseconds{20016}));
    EXPECT_FALSE(timer.Due(milliseconds{20016}));
    EXPECT_FALSE(timer.Due(milliseconds{39999}));
    EXPECT_TRUE(timer.Due(seconds{40}));
    EXPECT_FALSE(timer.Due(seconds{40}));
}

// Checked at 65 s, two periods late: fires once, not three times, and the next is a period after 65 s, not at the 80 s
// it was due.
TEST(IntervalTimerTest, ALongPauseFiresOnce)
{
    IntervalTimer timer(Period);
    timer.Start(Duration::zero());

    EXPECT_TRUE(timer.Due(seconds{65}));
    EXPECT_FALSE(timer.Due(seconds{65}));
    EXPECT_FALSE(timer.Due(seconds{80}));
    EXPECT_FALSE(timer.Due(milliseconds{84999}));
    EXPECT_TRUE(timer.Due(seconds{85}));
}

// Checked exactly a period late, at 40 s: fires once, and the next is at 60 s.
TEST(IntervalTimerTest, ExactlyAPeriodLateFiresOnce)
{
    IntervalTimer timer(Period);
    timer.Start(Duration::zero());

    EXPECT_TRUE(timer.Due(seconds{40}));
    EXPECT_FALSE(timer.Due(seconds{40}));
    EXPECT_FALSE(timer.Due(milliseconds{59999}));
    EXPECT_TRUE(timer.Due(seconds{60}));
}

// Less than a period late, the next stays on the grid: checked at 39.999 s, the next is at 40 s, not 59.999 s.
TEST(IntervalTimerTest, LessThanAPeriodLateKeepsTheGrid)
{
    IntervalTimer timer(Period);
    timer.Start(Duration::zero());

    EXPECT_TRUE(timer.Due(milliseconds{39999}));
    EXPECT_TRUE(timer.Due(seconds{40}));
}

TEST(IntervalTimerTest, AClockThatGoesBackDoesNotFire)
{
    IntervalTimer timer(Period);
    timer.Start(seconds{10});

    EXPECT_FALSE(timer.Due(seconds{5}));
    EXPECT_FALSE(timer.Due(milliseconds{29999}));
    EXPECT_TRUE(timer.Due(seconds{30}));
}

// Start restarts from the instant it receives.
TEST(IntervalTimerTest, StoppedItDoesNotFire)
{
    IntervalTimer timer(Period);
    EXPECT_FALSE(timer.Running());
    EXPECT_FALSE(timer.Due(seconds{100}));

    timer.Start(Duration::zero());
    EXPECT_TRUE(timer.Running());
    timer.Stop();
    EXPECT_FALSE(timer.Running());
    EXPECT_FALSE(timer.Due(seconds{100}));

    timer.Start(seconds{100});
    EXPECT_FALSE(timer.Due(milliseconds{119999}));
    EXPECT_TRUE(timer.Due(seconds{120}));
    EXPECT_TRUE(timer.Running());
}
} // namespace
