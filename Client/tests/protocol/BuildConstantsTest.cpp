#include "Protocol/BuildConstants.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

// Client::Protocol::BuildConstants: la versión y el serial del build c32bbd5f.
namespace
{
using Client::Protocol::ClientVersion;
using Client::Protocol::VersionSize;

// 0x6c8284 guarda la versión con cada byte más su índice más 1, y 0x4c1e20 compara el byte i del
// saludo con el guardado menos i menos 1: 0x32 - 0 - 1 es '1', 0x32 - 1 - 1 es '0', y así.
TEST(BuildConstantsTest, TheVersionIsTheOneStoredAt0x6c8284)
{
    const std::array<std::uint8_t, VersionSize> stored{0x32, 0x32, 0x35, 0x34, 0x3E};

    for (std::size_t i = 0; i < VersionSize; ++i)
    {
        EXPECT_EQ(static_cast<std::size_t>(ClientVersion[i]), stored[i] - i - 1) << "byte " << i;
    }
}
} // namespace
