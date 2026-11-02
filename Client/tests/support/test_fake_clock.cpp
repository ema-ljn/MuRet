#include "support/FakeClock.h"

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <chrono>

// Client::Tests::FakeClock: el reloj falso de los tests del bucle.
namespace
{
using namespace std::chrono_literals;
using Client::Tests::FakeClock;

TEST(FakeClockTest, NowDoesNotMoveByItself)
{
    FakeClock clock;

    EXPECT_EQ(clock.Now(), 0ns);
    EXPECT_EQ(clock.Now(), 0ns);
    EXPECT_TRUE(clock.Sleeps().empty());
}

TEST(FakeClockTest, SleepForMovesItAndWritesItDown)
{
    FakeClock clock;
    clock.SleepFor(5ms);

    EXPECT_EQ(clock.Now(), 5ms);
    ASSERT_EQ(clock.Sleeps().size(), 1u);
    EXPECT_EQ(clock.Sleeps()[0], 5ms);
}

TEST(FakeClockTest, AdvanceCanGoBack)
{
    FakeClock clock;
    clock.Advance(10ms);
    clock.Advance(-3ms);

    EXPECT_EQ(clock.Now(), 7ms);
    EXPECT_TRUE(clock.Sleeps().empty());
}

// Un sueño de 0 o para atrás es un error del bucle: el reloj real dormiría 0 y nadie lo vería.
TEST(FakeClockTest, SleepingZeroOrBackIsAFailure)
{
    EXPECT_NONFATAL_FAILURE(
        {
            FakeClock clock;
            clock.SleepFor(0ns);
        },
        "SleepFor");
    EXPECT_NONFATAL_FAILURE(
        {
            FakeClock clock;
            clock.SleepFor(-1ms);
        },
        "SleepFor");
}
} // namespace
