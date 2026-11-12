#include "Scenes/TextureSets.h"

#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

// Los juegos de la carga, del login y de sus ventanas contra el Data\ del laboratorio: todas
// están, todas se leen, cada una con su slot, y la imagen tiene el tamaño de la que leyó el
// original. Se comparan tamaños, nunca píxeles.
namespace
{
using Client::Data::Textures::LoadedTexture;
using Client::Data::Textures::LoadTextureSet;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::TextureLoader;
using Client::Data::Textures::TextureLoaderOf;
using Client::Data::Textures::TextureSetEntry;
using Client::Data::Textures::TextureSide;
using Client::Scenes::LoadingTextureSet;
using Client::Scenes::LoginTextureSet;
using Client::Scenes::SelectionTextureSet;
using Client::Scenes::WindowTextureSet;
using Client::Scenes::WorldLoadingTextureSet;

using GoldenTextureSet = Client::Tests::GoldenDataTest;

// El ancho y el alto de la imagen de cada entrada, en el orden del juego: medidos en el Data\ del
// laboratorio.
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

// Los dos cursores, el OK y el cancelar, con sus tres estados uno arriba del otro, la caja del aviso, que
// mide lo que la ventana (0x6429b0), y la del código del borrado (0x642bf0). Después, las de la ventana
// de Esc, con lo que les pone la tabla de 0x6478c0: la piedra, los dos bordes, los dos lados de una línea
// y los botones de cuatro cuadros de 30.
inline constexpr std::array<ImageSize, 12> WindowImageSizes{{
    {32, 32},
    {32, 32},
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
}};
static_assert(WindowImageSizes.size() == WindowTextureSet.size());

// Las de la selección (CL6.3): los ocho botones y marcos de Interface\, los dos fondos de 512 × 512,
// Impack03 y la chasellight.
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

// Las cuatro del mosaico de la carga del mundo: 400 × 512 arriba y 400 × 88 abajo (06 § La carga del
// mundo).
inline constexpr std::array<ImageSize, 4> WorldLoadingImageSizes{{{400, 512}, {400, 512}, {400, 88}, {400, 88}}};
static_assert(WorldLoadingImageSizes.size() == WorldLoadingTextureSet.size());

// Cada textura cargada contra su entrada: el slot, el formato de su cargador, el tamaño de la
// imagen y el buffer de lados potencia de 2 que la cubre.
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

// Las 18 de 0x61c7c0, que el original sube al entrar al login (06 § El login).
TEST_F(GoldenTextureSet, TheLoginSetLoads)
{
    const auto loaded = LoadTextureSet(m_dataRoot, LoginTextureSet);

    ASSERT_TRUE(loaded.HasValue()) << Describe(loaded.Error());
    ExpectTheSet(LoginTextureSet, LoginImageSizes, loaded.Value());
}

// Las seis de 0x61ccb0 que usan el login y la selección: el cursor y las ventanas.
TEST_F(GoldenTextureSet, TheWindowSetLoads)
{
    const auto loaded = LoadTextureSet(m_dataRoot, WindowTextureSet);

    ASSERT_TRUE(loaded.HasValue()) << Describe(loaded.Error());
    ExpectTheSet(WindowTextureSet, WindowImageSizes, loaded.Value());
}

// Las seis de 0x622e40, las de la escena de carga (06 § La carga).
TEST_F(GoldenTextureSet, TheLoadingSetLoads)
{
    const auto loaded = LoadTextureSet(m_dataRoot, LoadingTextureSet);

    ASSERT_TRUE(loaded.HasValue()) << Describe(loaded.Error());
    ExpectTheSet(LoadingTextureSet, LoadingImageSizes, loaded.Value());
}
// Las doce de 0x61ca30, las de la selección (06 § La selección de personaje).
TEST_F(GoldenTextureSet, TheSelectionSetLoads)
{
    const auto loaded = LoadTextureSet(m_dataRoot, SelectionTextureSet);

    ASSERT_TRUE(loaded.HasValue()) << Describe(loaded.Error());
    ExpectTheSet(SelectionTextureSet, SelectionImageSizes, loaded.Value());
}

// Las cuatro de 0x6356f0, las de la carga del mundo.
TEST_F(GoldenTextureSet, TheWorldLoadingSetLoads)
{
    const auto loaded = LoadTextureSet(m_dataRoot, WorldLoadingTextureSet);

    ASSERT_TRUE(loaded.HasValue()) << Describe(loaded.Error());
    ExpectTheSet(WorldLoadingTextureSet, WorldLoadingImageSizes, loaded.Value());
}
} // namespace
