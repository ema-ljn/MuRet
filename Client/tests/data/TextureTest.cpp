// Client::Data::Textures::TextureSide: el lado de la textura para el de una imagen.

#include "Data/Textures/Texture.h"

#include <gtest/gtest.h>

namespace
{

using Client::Data::Textures::BytesPerPixel;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::TextureMaxSide;
using Client::Data::Textures::TextureSide;

// La potencia de 2 que cubre el lado, desde 1.
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

// Con tope en 512, aunque la imagen sea más grande: los cargadores la rechazan antes.
TEST(TextureTest, TheSideStopsAt512)
{
    EXPECT_EQ(TextureMaxSide, 512);
    EXPECT_EQ(TextureSide(512), 512);
    EXPECT_EQ(TextureSide(513), 512);
    EXPECT_EQ(TextureSide(800), 512);
}

// El OZT deja pasar un lado de cero o negativo, y la textura queda de 1.
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
