#include "Diagnostics/MemoryTotals.h"

#include "Diagnostics/CrashReport.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace
{
using Client::Diagnostics::MemoryLoadPercent;
using Client::Diagnostics::MemoryStatus32;
using Client::Diagnostics::MemoryTotals;
using Client::Diagnostics::ToMemoryStatus32;

constexpr std::uint64_t GiB = std::uint64_t{1} << 30;
} // namespace

// In MEMORYSTATUS order.
TEST(MemoryTotalsTest, KeepsValuesThatFit)
{
    const MemoryTotals totals{.totalPhysical = GiB,
                              .availablePhysical = GiB / 2,
                              .totalPageFile = 2 * GiB,
                              .availablePageFile = GiB + 1,
                              .totalVirtual = 2 * GiB - 0x20000,
                              .availableVirtual = 0x7A000000};

    const MemoryStatus32 status = ToMemoryStatus32(totals, 50);

    EXPECT_EQ(status.memoryLoad, 50u);
    EXPECT_EQ(status.totalPhysical, 0x40000000u);
    EXPECT_EQ(status.availablePhysical, 0x20000000u);
    EXPECT_EQ(status.totalPageFile, 0x80000000u);
    EXPECT_EQ(status.availablePageFile, 0x40000001u);
    EXPECT_EQ(status.totalVirtual, 0x7FFE0000u);
    EXPECT_EQ(status.availableVirtual, 0x7A000000u);
}

// 0xFFFFFFFF, GlobalMemoryStatus' -1: the 16 GiB of a current machine and the 128 TiB of addresses of a 64-bit
// process.
TEST(MemoryTotalsTest, SaturatesValuesThatDoNotFit)
{
    const MemoryTotals totals{.totalPhysical = 16 * GiB,
                              .availablePhysical = 4 * GiB,
                              .totalPageFile = 20 * GiB,
                              .availablePageFile = 4 * GiB - 1,
                              .totalVirtual = std::uint64_t{1} << 47,
                              .availableVirtual = std::numeric_limits<std::uint64_t>::max()};

    const MemoryStatus32 status = ToMemoryStatus32(totals, 75);

    EXPECT_EQ(status.totalPhysical, 0xFFFFFFFFu);
    EXPECT_EQ(status.availablePhysical, 0xFFFFFFFFu);
    EXPECT_EQ(status.totalPageFile, 0xFFFFFFFFu);
    EXPECT_EQ(status.availablePageFile, 0xFFFFFFFFu);
    EXPECT_EQ(status.totalVirtual, 0xFFFFFFFFu);
    EXPECT_EQ(status.availableVirtual, 0xFFFFFFFFu);
}

TEST(MemoryTotalsTest, ComputesTheLoadLikeWindows)
{
    EXPECT_EQ(MemoryLoadPercent(16 * GiB, 4 * GiB), 75u);
    EXPECT_EQ(MemoryLoadPercent(3, 2), 33u);
    EXPECT_EQ(MemoryLoadPercent(GiB, GiB), 0u);
    EXPECT_EQ(MemoryLoadPercent(GiB, 0), 100u);
    // More available than total does not go negative, and with no total there is no percentage.
    EXPECT_EQ(MemoryLoadPercent(GiB, 2 * GiB), 0u);
    EXPECT_EQ(MemoryLoadPercent(0, 0), 0u);
    // Huge totals, without overflow.
    EXPECT_EQ(MemoryLoadPercent(std::numeric_limits<std::uint64_t>::max(), 0), 100u);
}
