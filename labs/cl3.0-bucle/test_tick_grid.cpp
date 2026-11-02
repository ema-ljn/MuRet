#include "TickGrid.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

// Labs::TickGrid: lo que lee GetTickCount con un tick de 15,625 ms y con uno de 10 ms (CL3.0, paso
// 3), y que entre dos ticks no se mueve.
namespace
{
using Labs::TenMsTickUs;
using Labs::TickGrid;
using Labs::XpTickUs;

// Lo que lee Ms en los ticks 0 a 4.
std::vector<std::uint32_t> FirstFiveTicks(const TickGrid& grid)
{
    std::vector<std::uint32_t> readings;
    for (std::int64_t tick = 0; tick < 5; ++tick)
    {
        readings.push_back(grid.Ms(tick * grid.TickUs()));
    }
    return readings;
}

// 15,625 se trunca a 15, 31,25 a 31 y 46,875 a 46: los ms de GetTickCount no son parejos.
TEST(TickGridTest, TheXpTickReads0_15_31_46_62)
{
    const TickGrid grid{XpTickUs};
    EXPECT_EQ(FirstFiveTicks(grid), (std::vector<std::uint32_t>{0, 15, 31, 46, 62}));
}

TEST(TickGridTest, TheTenMsTickReads0_10_20_30_40)
{
    const TickGrid grid{TenMsTickUs};
    EXPECT_EQ(FirstFiveTicks(grid), (std::vector<std::uint32_t>{0, 10, 20, 30, 40}));
}

// Resolución contra precisión: el reloj cuenta en ms, pero entre dos ticks no se mueve. Un µs
// antes del tick 1 todavía lee 0, y un µs antes del 2 todavía lee 15.
TEST(TickGridTest, BetweenTwoTicksTheReadingDoesNotMove)
{
    const TickGrid grid{XpTickUs};
    EXPECT_EQ(grid.Ms(XpTickUs - 1), 0u);
    EXPECT_EQ(grid.Ms(XpTickUs), 15u);
    EXPECT_EQ(grid.Ms(2 * XpTickUs - 1), 15u);
    EXPECT_EQ(grid.Ms(2 * XpTickUs), 31u);
}

// La truncación no se acumula: el tick 64 lee 1.000 justos, porque cada lectura se trunca sola y
// no se suman los 15 de cada tick.
TEST(TickGridTest, SixtyFourXpTicksAreOneSecond)
{
    const TickGrid grid{XpTickUs};
    EXPECT_EQ(grid.Ms(64 * XpTickUs), 1'000u);
    EXPECT_EQ(grid.Ms(192 * XpTickUs), 3'000u);
}
} // namespace
