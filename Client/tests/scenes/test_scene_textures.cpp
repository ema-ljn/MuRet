#include "Scenes/SceneTextures.h"

#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"
#include "support/FakeTextureStore.h"
#include "support/SceneDataFiles.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Client::Scenes::SceneTextures: el juego de texturas de una escena, subido en su Enter y soltado
// en su Leave, como lo hacen la carga (0x622e40 y 0x63c150) y el login (0x61c7c0).
namespace
{
using Client::Data::Textures::Filter;
using Client::Data::Textures::Sampling;
using Client::Data::Textures::TextureSetEntry;
using Client::Data::Textures::Wrap;
using Client::Render::TextureSlot;
using Client::Scenes::SceneTextures;
using Client::Scenes::TextureBufferSize;
using Client::Tests::FakeTextureStore;
using Client::Tests::SceneOztSide;
using Client::Tests::TemporaryFolder;
using Client::Tests::WriteTextureSet;

// Un juego inventado: un OZJ y un OZT, con slots que no usa ninguna escena.
constexpr std::array<TextureSetEntry, 2> Set{{
    {"Prueba\\fondo.jpg", 0x300, Sampling{Filter::Linear, Wrap::Clamp}},
    {"Prueba\\marco.tga", 0x301, Sampling{Filter::Nearest, Wrap::Repeat}},
}};

struct Harness
{
    Harness()
    {
        WriteTextureSet(folder.Path(), Set);
    }

    TemporaryFolder folder{"scene-textures"};
    FakeTextureStore store;
    SceneTextures textures{store, Set};
};

TEST(SceneTexturesTest, LoadsTheSetInItsOrder)
{
    Harness tested;

    const auto loaded = tested.textures.Load(tested.folder.Path());

    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    EXPECT_EQ(tested.store.loaded, (std::vector<std::uint16_t>{0x300, 0x301}));
    EXPECT_EQ(tested.store.loadCalls, 1);
}

// El tamaño del buffer que se subió (los floats de +0x20 y +0x24 del slot), para las UV de la UI
// nueva; de un slot que no subió, 1 × 1.
TEST(SceneTexturesTest, KnowsTheBufferSizeOfEachSlot)
{
    Harness tested;
    ASSERT_TRUE(tested.textures.Load(tested.folder.Path()).HasValue());

    for (const TextureSetEntry& entry : Set)
    {
        const auto stored = tested.store.Find(TextureSlot{entry.slot});
        ASSERT_TRUE(stored.has_value());
        const TextureBufferSize size = tested.textures.BufferSize(TextureSlot{entry.slot});
        EXPECT_EQ(size.width, stored->texture.textureWidth) << entry.slot;
        EXPECT_EQ(size.height, stored->texture.textureHeight) << entry.slot;
    }
    EXPECT_EQ(tested.textures.BufferSize(TextureSlot{0x301}).width, SceneOztSide);
    EXPECT_EQ(tested.textures.BufferSize(TextureSlot{0x302}).width, 1);
    EXPECT_EQ(tested.textures.BufferSize(TextureSlot{0x302}).height, 1);
}

// La metadata raw32 de cada slot subido (+0x20, +0x24 y +0x28 de su lugar), la que piden las partículas que
// se dibujan con una textura de la escena (la 0x729 y la 0x719 de la selección); ausente fuera del juego y
// después de soltarlo.
TEST(SceneTexturesTest, KnowsTheMetadataOfEachUploadedSlotUntilItsRelease)
{
    Harness tested;
    EXPECT_FALSE(tested.textures.MetadataOf(0x300).has_value());
    ASSERT_TRUE(tested.textures.Load(tested.folder.Path()).HasValue());

    for (const TextureSetEntry& entry : Set)
    {
        const auto stored = tested.store.Find(TextureSlot{entry.slot});
        ASSERT_TRUE(stored.has_value());
        const auto metadata = tested.textures.MetadataOf(entry.slot);
        ASSERT_TRUE(metadata.has_value()) << entry.slot;
        EXPECT_EQ(metadata->size.x, static_cast<float>(stored->texture.textureWidth)) << entry.slot;
        EXPECT_EQ(metadata->size.y, static_cast<float>(stored->texture.textureHeight)) << entry.slot;
        EXPECT_EQ(metadata->components,
                  static_cast<std::uint8_t>(Client::Data::Textures::BytesPerPixel(stored->texture.format)));
    }
    EXPECT_FALSE(tested.textures.MetadataOf(0x302).has_value());
    EXPECT_FALSE(tested.textures.MetadataOf(0x10300).has_value());
    tested.textures.Release();
    EXPECT_FALSE(tested.textures.MetadataOf(0x300).has_value());
}

TEST(SceneTexturesTest, AMissingFileFailsWithItsNameAndUploadsNothing)
{
    Harness tested;
    std::filesystem::remove(tested.folder.Path() / "Prueba/marco.OZT");

    const auto loaded = tested.textures.Load(tested.folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("Prueba\\marco.tga"), std::string::npos) << loaded.Error();
    EXPECT_EQ(tested.store.loadCalls, 0);
}

TEST(SceneTexturesTest, AFailedUploadFailsWithTheErrorOfTheTable)
{
    Harness tested;
    tested.store.failNextLoad = true;

    const auto loaded = tested.textures.Load(tested.folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("la tabla falsa no sube"), std::string::npos) << loaded.Error();
}

// Suelta todo el juego, en orden, aunque la subida haya cortado a la mitad: lo que sí subió no
// queda en la tabla.
TEST(SceneTexturesTest, ReleasesTheWholeSetOnce)
{
    Harness tested;
    ASSERT_TRUE(tested.textures.Load(tested.folder.Path()).HasValue());

    tested.textures.Release();
    tested.textures.Release();

    EXPECT_EQ(tested.store.released, (std::vector<std::uint16_t>{0x300, 0x301}));
    EXPECT_EQ(tested.store.Size(), 0u);
    EXPECT_EQ(tested.textures.BufferSize(TextureSlot{0x300}).width, 1);
}

TEST(SceneTexturesTest, WithoutALoadReleasesNothing)
{
    Harness tested;

    tested.textures.Release();

    EXPECT_TRUE(tested.store.released.empty());
}

TEST(SceneTexturesTest, AfterAFailedUploadReleasesTheSet)
{
    Harness tested;
    tested.store.failNextLoad = true;
    ASSERT_FALSE(tested.textures.Load(tested.folder.Path()).HasValue());

    tested.textures.Release();

    EXPECT_EQ(tested.store.released, (std::vector<std::uint16_t>{0x300, 0x301}));
}
} // namespace
