#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>

namespace
{
using Client::Tests::SameBytes;
using Client::Tests::SameBytesWithoutValues;

// They differ at byte 2: 0xAB (171) against 0xCD (205).
constexpr std::array<std::byte, 4> Actual = {std::byte{0x01}, std::byte{0x02}, std::byte{0xAB}, std::byte{0x04}};
constexpr std::array<std::byte, 4> Expected = {std::byte{0x01}, std::byte{0x02}, std::byte{0xCD}, std::byte{0x04}};

TEST(SameBytesTest, EqualBuffersPass)
{
    EXPECT_TRUE(SameBytes(Actual, Actual));
    EXPECT_TRUE(SameBytesWithoutValues(Actual, Actual));
}

TEST(SameBytesTest, SaysTheFirstDifferenceWithItsValues)
{
    const ::testing::AssertionResult result = SameBytes(Actual, Expected);

    ASSERT_FALSE(result);
    const std::string message = result.message();
    EXPECT_NE(message.find("byte 2"), std::string::npos) << message;
    EXPECT_NE(message.find("171"), std::string::npos) << message;
    EXPECT_NE(message.find("205"), std::string::npos) << message;
}

// No value, neither in decimal nor in hex.
TEST(SameBytesTest, WithoutValuesSaysOnlyWhere)
{
    const ::testing::AssertionResult result = SameBytesWithoutValues(Actual, Expected);

    ASSERT_FALSE(result);
    const std::string message = result.message();
    EXPECT_NE(message.find("byte 2"), std::string::npos) << message;
    for (const char* value : {"171", "205", "AB", "ab", "CD", "cd"})
    {
        EXPECT_EQ(message.find(value), std::string::npos) << value;
    }
}

TEST(SameBytesTest, WithoutValuesSaysTheSizes)
{
    const std::array<std::byte, 2> shorter = {std::byte{0x01}, std::byte{0x02}};

    const ::testing::AssertionResult result = SameBytesWithoutValues(shorter, Expected);

    ASSERT_FALSE(result);
    EXPECT_EQ(std::string(result.message()), "tiene 2 bytes y se esperaban 4");
}
} // namespace
