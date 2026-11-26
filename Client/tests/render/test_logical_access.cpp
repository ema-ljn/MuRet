#include "Models/LogicalAccess.h"

#include "Crt/CrtRandom.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

// One access to an entry of the original's protection table (06 § La tabla de protección, Q-C125 and Q-C183). The
// expected rand() calls are those of the research's static reading: 4 when closing a 4-byte variable (0x412890, and
// inline at 0x40b570 and 0x40b800) and 3848 when closing the own record (0x40b4d0, one per byte of the 0xf08). The
// entry keeps in a byte how many accesses hold it open, and the close that leaves it at 0 spends.
namespace
{
using Client::Crt::CrtRandom;
using Client::Models::LogicalAccess;
using Client::Models::OwnRecordCloseRandoms;
using Client::Models::SmallLogicalCloseRandoms;

constexpr std::uint32_t Seed = 0x2f1d;
// What each close spends according to the research, written apart from the constants under test.
constexpr int SmallVariableRandoms = 4;
constexpr int OwnRecordRandoms = 3848;

// Each kind of close: the constant the access receives and what the research fixes.
struct CloseCost
{
    std::uint16_t closeRandoms;
    int researchRandoms;
};
constexpr std::array CloseCosts{CloseCost{SmallLogicalCloseRandoms, SmallVariableRandoms},
                                CloseCost{OwnRecordCloseRandoms, OwnRecordRandoms}};

std::uint32_t StateAfter(int count)
{
    CrtRandom random(Seed);
    for (int i = 0; i < count; ++i)
    {
        static_cast<void>(random.Next());
    }
    return random.State();
}

TEST(LogicalAccessTest, TheLastCloseSpendsItsRandoms)
{
    EXPECT_EQ(SmallLogicalCloseRandoms, SmallVariableRandoms);
    EXPECT_EQ(OwnRecordCloseRandoms, OwnRecordRandoms);
    for (const CloseCost& cost : CloseCosts)
    {
        SCOPED_TRACE(cost.researchRandoms);
        std::uint8_t depth = 0;
        CrtRandom random(Seed);
        {
            const LogicalAccess access(depth, random, cost.closeRandoms);
            // Opening costs nothing: for each open and close the research counts only the close.
            EXPECT_EQ(depth, 1);
            EXPECT_EQ(random.State(), Seed);
        }
        EXPECT_EQ(depth, 0);
        EXPECT_EQ(random.State(), StateAfter(cost.researchRandoms));
    }
}

// A close that does not leave the count at 0 does not encrypt again: the own record inside another access of its own,
// and a variable someone else already had open (the cloth with the counter open, Q-C125).
TEST(LogicalAccessTest, NestedClosesSpendNothing)
{
    std::uint8_t recordDepth = 0;
    CrtRandom random(Seed);
    {
        const LogicalAccess outer(recordDepth, random, OwnRecordCloseRandoms);
        {
            const LogicalAccess inner(recordDepth, random, OwnRecordCloseRandoms);
            EXPECT_EQ(recordDepth, 2);
        }
        EXPECT_EQ(recordDepth, 1);
        EXPECT_EQ(random.State(), Seed);
    }
    EXPECT_EQ(recordDepth, 0);
    EXPECT_EQ(random.State(), StateAfter(OwnRecordRandoms));

    std::uint8_t alreadyOpen = 1;
    CrtRandom untouched(Seed);
    {
        const LogicalAccess access(alreadyOpen, untouched, SmallLogicalCloseRandoms);
        EXPECT_EQ(alreadyOpen, 2);
    }
    EXPECT_EQ(alreadyOpen, 1);
    EXPECT_EQ(untouched.State(), Seed);
}

// The count is a byte: with 255, opening leaves it at 0 and closing takes it back to 255 without spending. The close
// that leaves it at 0 spends, even if it is not the outermost one.
TEST(LogicalAccessTest, TheDepthByteWraps)
{
    std::uint8_t depth = 255;
    CrtRandom random(Seed);
    {
        const LogicalAccess outer(depth, random, SmallLogicalCloseRandoms);
        EXPECT_EQ(depth, 0);
        {
            const LogicalAccess inner(depth, random, SmallLogicalCloseRandoms);
            EXPECT_EQ(depth, 1);
            EXPECT_EQ(random.State(), Seed);
        }
        EXPECT_EQ(depth, 0);
        EXPECT_EQ(random.State(), StateAfter(SmallVariableRandoms));
    }
    EXPECT_EQ(depth, 255);
    EXPECT_EQ(random.State(), StateAfter(SmallVariableRandoms));
}

// 0x4128e2..0x4128f9 (CL9 milestone 2): the close that leaves the count at 0 puts in each byte of the variable the
// rand() & 0xff of its turn, the first one lowest; one that does not leave it at 0 leaves it in the clear.
TEST(LogicalAccessTest, TheLastCloseLeavesItsRandomBytes)
{
    std::uint8_t depth = 0;
    std::uint32_t left = 0x5a;
    CrtRandom random(Seed);
    {
        const LogicalAccess access(depth, random, SmallLogicalCloseRandoms, left);
        EXPECT_EQ(left, 0x5au);
    }
    CrtRandom expected(Seed);
    std::uint32_t bytes = 0;
    for (std::uint32_t index = 0; index < 4; ++index)
        bytes |= (static_cast<std::uint32_t>(expected.Next()) & 0xffu) << (8u * index);
    EXPECT_EQ(left, bytes);
    EXPECT_EQ(random.State(), expected.State());

    std::uint8_t alreadyOpen = 1;
    std::uint32_t plain = 0x3e;
    {
        const LogicalAccess access(alreadyOpen, random, SmallLogicalCloseRandoms, plain);
    }
    EXPECT_EQ(plain, 0x3eu);
    EXPECT_EQ(random.State(), expected.State());
}
} // namespace
