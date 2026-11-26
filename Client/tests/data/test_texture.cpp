#include "Data/Textures/Texture.h"

#include <gtest/gtest.h>

namespace
{
using Client::Data::Textures::BytesPerPixel;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::TextureMaxSide;
using Client::Data::Textures::TextureSide;

TEST(TextureTest, TheSideIsThePowerOf2ThatCoversTheImage)
{
    EXPECT_EQ(TextureSide(1), 1);
    EXPECT_EQ(TextureSide(2), 2);
    EXPECT_EQ(TextureSide(3), 4);
    EXPECT_EQ(TextureSide(64), 64);
    EXPECT_EQ(TextureSide(65), 128);
    EXPECT_EQ(TextureSide(166), 256);
    EXPECT_EQ(TextureSide(511), 512);
}

// The loaders reject a larger image first.
TEST(TextureTest, TheSideStopsAt512)
{
    EXPECT_EQ(TextureMaxSide, 512);
    EXPECT_EQ(TextureSide(512), 512);
    EXPECT_EQ(TextureSide(513), 512);
    EXPECT_EQ(TextureSide(800), 512);
}

// The OZT lets a zero or negative side through.
TEST(TextureTest, ZeroOrANegativeSideGives1)
{
    EXPECT_EQ(TextureSide(0), 1);
    EXPECT_EQ(TextureSide(-1), 1);
    EXPECT_EQ(TextureSide(-32768), 1);
}

TEST(TextureTest, BytesPerPixel)
{
    EXPECT_EQ(BytesPerPixel(PixelFormat::Rgb), 3u);
    EXPECT_EQ(BytesPerPixel(PixelFormat::Rgba), 4u);
}
} // namespace
