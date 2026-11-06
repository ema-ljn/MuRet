#include "support/HexFloatFixture.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <ios>
#include <sstream>
#include <string>

TEST(HexFloatFixtureTest, ReadsExactBitsIncludingSignedZeroAndNaNPayload)
{
    std::istringstream words("00000000 80000000 3F800000 7fc12345 ffffffff");
    constexpr std::array<std::uint32_t, 5> Bits{0u, 0x80000000u, 0x3f800000u, 0x7fc12345u, 0xffffffffu};
    for (const auto expected : Bits)
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(Client::Tests::ReadHexFloat(words)), expected);
        ASSERT_FALSE(words.fail());
    }
}

TEST(HexFloatFixtureTest, RejectsTruncatedOverflowSignedPrefixedAndPartialTokens)
{
    constexpr std::array Bad{"",         "0",        "3f80000",  "100000000",    "-0000001",
                             "+0000001", "0x800000", "3f80000g", "3f800000junk", "ffffffffffffffffffffffff"};
    for (const auto* token : Bad)
    {
        SCOPED_TRACE(token);
        std::istringstream words(token);
        EXPECT_EQ(Client::Tests::ReadHexFloat(words), 0.0f);
        EXPECT_TRUE(words.fail());
    }
}

TEST(HexFloatFixtureTest, LeavesFollowingFieldsAndExistingFailureUntouched)
{
    std::istringstream words("3f800000 next");
    EXPECT_EQ(Client::Tests::ReadHexFloat(words), 1.0f);
    std::string next;
    words >> next;
    EXPECT_EQ(next, "next");
    words.setstate(std::ios::failbit);
    EXPECT_EQ(Client::Tests::ReadHexFloat(words), 0.0f);
    EXPECT_TRUE(words.fail());
}
