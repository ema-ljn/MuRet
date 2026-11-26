#include "support/Fnv1a.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace
{
using Client::Tests::Fnv1a64;

std::uint64_t HashOf(std::string_view text)
{
    return Fnv1a64(std::as_bytes(std::span(text)));
}

static_assert(Fnv1a64({}) == Client::Tests::Fnv1aOffsetBasis);

// The authors' values for the 64-bit FNV-1a (test_fnv.c, on Fnv1a.h's page). The same implementation is in
// tools/hash6b.c, which wrote lab-6b.txt.
TEST(Fnv1a64, GivesTheAuthorsValues)
{
    EXPECT_EQ(HashOf(""), 0xcbf29ce484222325U);
    EXPECT_EQ(HashOf("a"), 0xaf63dc4c8601ec8cU);
    EXPECT_EQ(HashOf("foobar"), 0x85944171f73967e8U);
}
} // namespace
