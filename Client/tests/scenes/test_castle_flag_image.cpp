#include "CastleSiege/CastleFlagImage.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{
TEST(CastleFlagImageTest, TheOriginal64SquareHasTheMarkTheGrayFieldAndTheBorder)
{
    std::array<std::uint8_t, 64> mark{};
    mark[0] = 4;   // rojo
    mark[63] = 12; // azul
    const auto painted = Client::Scenes::PaintTheCastleFlag(mark, 64, 64);
    ASSERT_TRUE(painted.HasValue()) << painted.Error();
    const auto& image = painted.Value();
    ASSERT_EQ(image.pixels.size(), 64u * 64u * 4u);
    const auto pixel = [&image](int x, int y)
    {
        const auto at = static_cast<std::size_t>(y * 64 + x) * 4;
        return std::array<std::uint8_t, 4>{image.pixels[at], image.pixels[at + 1], image.pixels[at + 2],
                                           image.pixels[at + 3]};
    };
    EXPECT_EQ(pixel(16, 16), (std::array<std::uint8_t, 4>{255, 0, 0, 255}));
    EXPECT_EQ(pixel(19, 19), (std::array<std::uint8_t, 4>{255, 0, 0, 255}));
    EXPECT_EQ(pixel(20, 16), (std::array<std::uint8_t, 4>{0, 0, 0, 0}));
    EXPECT_EQ(pixel(47, 47), (std::array<std::uint8_t, 4>{0, 0, 255, 255}));
    EXPECT_EQ(pixel(3, 10), (std::array<std::uint8_t, 4>{10, 10, 10, 255}));
    EXPECT_EQ(pixel(60, 54), (std::array<std::uint8_t, 4>{54, 54, 54, 255}));
    // 0x5f1abb: las dos operaciones flotantes se suman antes del último _ftol.
    EXPECT_EQ(pixel(0, 1), (std::array<std::uint8_t, 4>{53, 51, 0, 255}));
    EXPECT_EQ(pixel(2, 10), (std::array<std::uint8_t, 4>{76, 66, 0, 255}));
    EXPECT_EQ(pixel(0, 63), (std::array<std::uint8_t, 4>{211, 150, 0, 255}));
}
} // namespace
