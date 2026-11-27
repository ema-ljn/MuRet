#include "Assets/SceneTextures.h"

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

// Client::Assets::SceneTextures: a scene's texture set, uploaded in its Enter and released in its Leave, as the load
// (0x622e40 and 0x63c150) and the login (0x61c7c0) do.
namespace
{
using Client::Assets::SceneTextures;
using Client::Assets::TextureBufferSize;
using Client::Data::Textures::Filter;
using Client::Data::Textures::Sampling;
using Client::Data::Textures::TextureSetEntry;
using Client::Data::Textures::Wrap;
using Client::Render::TextureSlot;
using Client::Tests::FakeTextureStore;
using Client::Tests::SceneOztSide;
using Client::Tests::TemporaryFolder;
using Client::Tests::WriteTextureSet;

// A made-up set: one OZJ and one OZT, with slots no scene uses.
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

// The size of the uploaded buffer (the floats at +0x20 and +0x24 of the slot), for the new UI's UVs; 1 × 1 for a slot
// that did not upload.
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

// The raw32 metadata of each uploaded slot (+0x20, +0x24 and +0x28 of its place), which the particles drawn with a
// scene texture ask for (the selection's 0x729 and 0x719); absent outside the set and after releasing it.
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

// Releases the whole set, in order, even if the upload stopped halfway: what did upload does not stay in the table.
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
