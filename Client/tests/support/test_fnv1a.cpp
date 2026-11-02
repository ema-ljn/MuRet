#include "support/Fnv1a.h"

#include <gtest/gtest.h>

#include <cstddef>
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

// Sin bytes, el hash es la base.
static_assert(Fnv1a64({}) == Client::Tests::Fnv1aOffsetBasis);

// Los valores de los autores para el FNV-1a de 64 bits (test_fnv.c, en la página de Fnv1a.h). La
// misma implementación está en tools/hash6b.c, que escribió lab-6b.txt.
TEST(Fnv1a64, GivesTheAuthorsValues)
{
    EXPECT_EQ(HashOf(""), 0xcbf29ce484222325U);
    EXPECT_EQ(HashOf("a"), 0xaf63dc4c8601ec8cU);
    EXPECT_EQ(HashOf("foobar"), 0x85944171f73967e8U);
}
} // namespace
