#include "Assets/TextureSets.h"

#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

// The load, login and window sets against the lab's Data\: all are there, all read, each with its slot, and the
// image has the size of the one the original read. Sizes are compared, never pixels.
namespace
{
using Client::Assets::LoadingTextureSet;
using Client::Assets::LoginTextureSet;
using Client::Assets::SelectionTextureSet;
using Client::Assets::WindowTextureSet;
using Client::Assets::WorldLoadingTextureSet;
using Client::Data::Textures::LoadedTexture;
using Client::Data::Textures::LoadTextureSet;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::TextureLoader;
using Client::Data::Textures::TextureLoaderOf;
using Client::Data::Textures::TextureSetEntry;
using Client::Data::Textures::TextureSide;

using GoldenTextureSet = Client::Tests::GoldenDataTest;

// The width and height of each entry's image, in the set's order: measured in the lab's Data\.
struct ImageSize
{
    int width = 0;
    int height = 0;
};

inline constexpr std::array<ImageSize, 18> LoginImageSizes{{
    {512, 512},
    {512, 512},
    {256, 256},
    {256, 256},
    {4, 8},
    {108, 104},
    {193, 78},
    {160, 4},
    {159, 95},
    {54, 90},
    {54, 90},
    {189, 103},
    {329, 245},
    {156, 23},
    {4, 4},
    {512, 12},
    {6, 4},
    {290, 41},
}};
static_assert(LoginImageSizes.size() == LoginTextureSet.size());

inline constexpr std::array<ImageSize, 6> LoadingImageSizes{{
    {512, 478},
    {288, 478},
    {93, 48},
    {256, 206},
    {187, 151},
    {4, 15},
}};
static_assert(LoadingImageSizes.size() == LoadingTextureSet.size());

// The two cursors, OK and cancel, with their three states one above the other, the notice box, which is as big as
// the window (0x6429b0), and the delete code box (0x642bf0). Then those of the Esc window, with what the table of
// 0x6478c0 gives them: the stone, the two borders, the two sides of a line and the buttons of four 30-pixel frames.
// The credits' close has three 30-pixel frames (0x63f202); the options', what 0x644120 and 0x646640 give them: the
// top border of 65, the checkboxes of two 16-pixel frames, the knob, the fill and the bar.
inline constexpr std::array<ImageSize, 18> WindowImageSizes{{
    {32, 32},
    {32, 32},
    {54, 90},
    {54, 90},
    {54, 90},
    {352, 113},
    {171, 23},
    {128, 128},
    {213, 64},
    {213, 43},
    {5, 8},
    {5, 8},
    {108, 120},
    {213, 65},
    {16, 32},
    {13, 13},
    {4, 7},
    {98, 13},
}};
static_assert(WindowImageSizes.size() == WindowTextureSet.size());

// The selection's: the eight buttons and frames of Interface\, the two 512 × 512 backgrounds, Impack03 and the
// chasellight.
inline constexpr std::array<ImageSize, 12> SelectionImageSizes{{
    {346, 38},
    {108, 104},
    {189, 103},
    {54, 120},
    {54, 90},
    {54, 120},
    {54, 120},
    {118, 54},
    {512, 512},
    {512, 512},
    {64, 64},
    {16, 128},
}};
static_assert(SelectionImageSizes.size() == SelectionTextureSet.size());

// The four of the world loading mosaic: 400 × 512 on top and 400 × 88 below (06 § La carga del mundo).
inline constexpr std::array<ImageSize, 4> WorldLoadingImageSizes{{{400, 512}, {400, 512}, {400, 88}, {400, 88}}};
static_assert(WorldLoadingImageSizes.size() == WorldLoadingTextureSet.size());

// Each loaded texture against its entry: the slot, its loader's format, the image size and the power-of-2 buffer
// that covers it.
void ExpectTheSet(std::span<const TextureSetEntry> set, std::span<const ImageSize> sizes,
                  const std::vector<LoadedTexture>& loaded)
{
    ASSERT_EQ(loaded.size(), set.size());
    for (std::size_t i = 0; i < set.size(); ++i)
    {
        const LoadedTexture& texture = loaded[i];
        SCOPED_TRACE(testing::Message() << "la entrada " << i << ", slot " << set[i].slot);
        EXPECT_EQ(texture.slot, set[i].slot);
        const PixelFormat format =
            TextureLoaderOf(set[i].path) == TextureLoader::Ozj ? PixelFormat::Rgb : PixelFormat::Rgba;
        EXPECT_EQ(texture.texture.format, format);
        EXPECT_EQ(texture.texture.width, sizes[i].width);
        EXPECT_EQ(texture.texture.height, sizes[i].height);
        EXPECT_EQ(texture.texture.textureWidth, TextureSide(sizes[i].width));
        EXPECT_EQ(texture.texture.textureHeight, TextureSide(sizes[i].height));
    }
}

// The 18 of 0x61c7c0, which the original uploads on entering the login (06 § El login).
TEST_F(GoldenTextureSet, TheLoginSetLoads)
{
    const auto loaded = LoadTextureSet(m_dataRoot, LoginTextureSet);

    ASSERT_TRUE(loaded.HasValue()) << Describe(loaded.Error());
    ExpectTheSet(LoginTextureSet, LoginImageSizes, loaded.Value());
}

// The six of 0x61ccb0 that the login and the selection use: the cursor and the windows.
TEST_F(GoldenTextureSet, TheWindowSetLoads)
{
    const auto loaded = LoadTextureSet(m_dataRoot, WindowTextureSet);

    ASSERT_TRUE(loaded.HasValue()) << Describe(loaded.Error());
    ExpectTheSet(WindowTextureSet, WindowImageSizes, loaded.Value());
}

// The six of 0x622e40, the loading scene's (06 § La carga).
TEST_F(GoldenTextureSet, TheLoadingSetLoads)
{
    const auto loaded = LoadTextureSet(m_dataRoot, LoadingTextureSet);

    ASSERT_TRUE(loaded.HasValue()) << Describe(loaded.Error());
    ExpectTheSet(LoadingTextureSet, LoadingImageSizes, loaded.Value());
}
// The twelve of 0x61ca30, the selection's (06 § La selección de personaje).
TEST_F(GoldenTextureSet, TheSelectionSetLoads)
{
    const auto loaded = LoadTextureSet(m_dataRoot, SelectionTextureSet);

    ASSERT_TRUE(loaded.HasValue()) << Describe(loaded.Error());
    ExpectTheSet(SelectionTextureSet, SelectionImageSizes, loaded.Value());
}

// The four of 0x6356f0, the world loading's.
TEST_F(GoldenTextureSet, TheWorldLoadingSetLoads)
{
    const auto loaded = LoadTextureSet(m_dataRoot, WorldLoadingTextureSet);

    ASSERT_TRUE(loaded.HasValue()) << Describe(loaded.Error());
    ExpectTheSet(WorldLoadingTextureSet, WorldLoadingImageSizes, loaded.Value());
}
} // namespace
