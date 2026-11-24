#include "Crt/CrtRandom.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace
{
using Client::Crt::CrtRandom;

CrtRandom AfterNextCalls(std::uint32_t seed, std::uint32_t count)
{
    CrtRandom random(seed);
    for (std::uint32_t call = 0; call < count; ++call)
        (void)random.Next();
    return random;
}
} // namespace

TEST(CrtRandomTest, SeedOneGivesTheCrtSequence)
{
    CrtRandom random;
    constexpr std::array<int, 5> Expected{41, 18467, 6334, 26500, 19169};

    for (const int value : Expected)
        EXPECT_EQ(random.Next(), value);
}

TEST(CrtRandomTest, NextStaysBetweenZeroAndTheCrtMaximum)
{
    CrtRandom random(0xffffffffu);

    for (int call = 0; call < 1000; ++call)
    {
        const int value = random.Next();
        EXPECT_GE(value, 0);
        EXPECT_LE(value, 0x7fff);
    }
}

TEST(CrtRandomTest, DiscardMatchesThatManyNextCalls)
{
    for (const std::uint32_t count : {0u, 1u, 2u, 3u, 7u, 8u, 100u, 1023u, 1024u, 65537u})
    {
        CrtRandom discarded(12345);
        discarded.Discard(count);

        EXPECT_EQ(discarded.State(), AfterNextCalls(12345, count).State()) << count;
    }
}
