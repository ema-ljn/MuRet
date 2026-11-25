#include "support/CorePrinters.h"

#include "Core/Id.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace
{
using Client::Core::Id;

TEST(CorePrintersTest, AnIdPrintsItsValueAsANumber)
{
    EXPECT_EQ(::testing::PrintToString(Id<struct WideTag, std::int32_t>{-25}), "-25");
    EXPECT_EQ(::testing::PrintToString(Id<struct ByteTag, std::uint8_t>{65}), "65");
    EXPECT_EQ(::testing::PrintToString(Id<struct SignedByteTag, std::int8_t>{-3}), "-3");
}
} // namespace
