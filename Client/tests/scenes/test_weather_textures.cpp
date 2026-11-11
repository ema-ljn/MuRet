#include "Scenes/TerrainTiles.h"

#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"
#include "Scenes/ModelTextureTable.h"
#include "Scenes/TextureMetadata.h"
#include "World/WorldTerrain.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/SceneDataFiles.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
using Client::Data::Textures::Filter;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Wrap;
using Client::Render::TextureSlot;
using Client::Scenes::TerrainTiles;
using Client::Tests::FakeTextureStore;

TEST(WeatherTexturesTest, NativeWeatherTableKeepsTheTwoAlternativesAndGlobalRainSources)
{
    for (const std::uint8_t map :
         {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{7}, std::uint8_t{9}, std::uint8_t{10},
          std::uint8_t{18}, std::uint8_t{30}, std::uint8_t{31}, std::uint8_t{33}, std::uint8_t{34}, std::uint8_t{54}})
    {
        const auto weather = Client::World::WorldWeatherTextures(map);
        ASSERT_EQ(weather.size(), 6u) << static_cast<int>(map);
        const auto folder = "World" + std::to_string(static_cast<int>(map) + 1) + "\\";
        EXPECT_EQ(weather[0].path, folder + "leaf01.tga");
        EXPECT_EQ(weather[1].path, folder + "leaf01.jpg");
        EXPECT_EQ(weather[2].path, folder + "leaf02.jpg");
        EXPECT_EQ(weather[3].path, map == 34 ? "World1\\rain011.tga" : "World1\\rain01.tga");
        EXPECT_EQ(weather[4].path, "World1\\rain02.tga");
        EXPECT_EQ(weather[5].path, "World10\\rain03.tga");
        const std::array<std::uint16_t, 6> slots{0x64, 0x64, 0x65, 0x66, 0x67, 0x68};
        for (std::size_t index = 0; index < weather.size(); ++index)
        {
            EXPECT_EQ(weather[index].slot, slots[index]);
            EXPECT_EQ(weather[index].sampling.filter, Filter::Nearest);
            EXPECT_EQ(weather[index].sampling.wrap, Wrap::Clamp);
            EXPECT_FALSE(weather[index].required);
        }
    }
    for (const std::uint8_t map : {std::uint8_t{24}, std::uint8_t{29}, std::uint8_t{36}})
        EXPECT_TRUE(Client::World::WorldWeatherTextures(map).empty());
}

TEST(WeatherTexturesTest, ReferenceMapsUploadRealDecodedWeatherIntoThePhysicalSlots)
{
    FakeTextureStore store;
    TerrainTiles tiles(store);
    const auto root = Client::Tests::RepoDataDir();
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{7}, std::uint8_t{9},
                                   std::uint8_t{10}, std::uint8_t{18}, std::uint8_t{34}})
    {
        std::vector<std::string> skipped;
        const auto loaded = tiles.Load(root, map, skipped);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        const auto leaf = store.Find(TextureSlot{0x64});
        // World10 no tiene leaf01 en ninguno de los dos formatos: sus200gotas usan66.
        if (map == 9)
            EXPECT_FALSE(leaf.has_value());
        else
        {
            ASSERT_TRUE(leaf.has_value()) << static_cast<int>(map);
            EXPECT_EQ(leaf->texture.format, map == 2 || map == 7 || map == 18 ? PixelFormat::Rgb : PixelFormat::Rgba);
            EXPECT_GT(leaf->texture.width, 0);
            EXPECT_GT(leaf->texture.height, 0);
            EXPECT_EQ(leaf->texture.pixels.size(), static_cast<std::size_t>(leaf->texture.textureWidth) *
                                                       static_cast<std::size_t>(leaf->texture.textureHeight) *
                                                       Client::Data::Textures::BytesPerPixel(leaf->texture.format));
        }
        if (map == 2)
        {
            const auto largeSnow = store.Find(TextureSlot{0x65});
            ASSERT_TRUE(largeSnow.has_value());
            EXPECT_EQ(largeSnow->texture.format, PixelFormat::Rgb);
            EXPECT_GT(largeSnow->texture.textureWidth, 0);
        }
        for (const std::uint16_t slot : {std::uint16_t{0x66}, std::uint16_t{0x67}, std::uint16_t{0x68}})
        {
            const auto rain = store.Find(TextureSlot{slot});
            ASSERT_TRUE(rain.has_value());
            EXPECT_EQ(rain->texture.format, PixelFormat::Rgba);
            EXPECT_EQ(rain->sampling.filter, Filter::Nearest);
            EXPECT_EQ(rain->sampling.wrap, Wrap::Clamp);
            EXPECT_GT(rain->texture.pixels.size(), 0u);
        }
    }
    tiles.Release();
    EXPECT_EQ(store.Size(), 0u);
}

class WeatherTextureUploadTest : public testing::Test
{
protected:
    Client::Tests::TemporaryFolder m_folder{"weather-textures"};
    FakeTextureStore m_store;
    TerrainTiles m_tiles{m_store};

    void WriteFiles(bool jpegAlternative)
    {
        std::vector<Client::Data::Textures::TextureSetEntry> entries{
            {"World1\\TileGrass01.jpg", 0x23}, {"Interface\\accept_box01.jpg", 0x122}, {"World1\\leaf01.tga", 0x64},
            {"World1\\leaf02.jpg", 0x65},      {"World1\\rain01.tga", 0x66},           {"World1\\rain02.tga", 0x67},
            {"World10\\rain03.tga", 0x68},     {"Object8\\drop01.jpg", 0x607}};
        if (jpegAlternative)
            entries.push_back({"World1\\leaf01.jpg", 0x64});
        Client::Tests::WriteTextureSet(m_folder.Path(), entries);
    }
};

TEST_F(WeatherTextureUploadTest, TheLaterJpegReplacesTheTgaAndEachPhysicalSlotIsReleasedOnce)
{
    WriteFiles(true);
    std::vector<std::string> skipped;
    ASSERT_TRUE(m_tiles.Load(m_folder.Path(), 0, skipped).HasValue());
    const auto leaf = m_store.Find(TextureSlot{0x64});
    ASSERT_TRUE(leaf.has_value());
    EXPECT_EQ(leaf->texture.format, PixelFormat::Rgb);
    EXPECT_EQ(std::ranges::count(m_store.loaded, std::uint16_t{0x64}), 2);
    EXPECT_EQ(m_store.Find(TextureSlot{0x67})->texture.textureWidth, Client::Tests::SceneOztSide);
    m_tiles.Release();
    EXPECT_EQ(std::ranges::count(m_store.released, std::uint16_t{0x64}), 1);
    EXPECT_EQ(m_store.Size(), 0u);
}

TEST_F(WeatherTextureUploadTest, AMissingOptionalJpegKeepsTheTgaLoaded)
{
    WriteFiles(false);
    std::vector<std::string> skipped;
    ASSERT_TRUE(m_tiles.Load(m_folder.Path(), 0, skipped).HasValue());
    const auto leaf = m_store.Find(TextureSlot{0x64});
    ASSERT_TRUE(leaf.has_value());
    EXPECT_EQ(leaf->texture.format, PixelFormat::Rgba);
    EXPECT_EQ(leaf->texture.textureWidth, Client::Tests::SceneOztSide);
    EXPECT_TRUE(std::ranges::any_of(skipped, [](const std::string& value)
                                    { return value.find("leaf01.jpg") != std::string::npos; }));
}

TEST_F(WeatherTextureUploadTest, TheSharedPhysicalRegistryPublishesWeatherMetadataAndOwnsOneReference)
{
    Client::Scenes::ModelTextureTable registry;
    registry.Bind(m_store);
    m_tiles.BindTextureTable(registry);
    WriteFiles(true);
    std::vector<std::string> skipped;
    ASSERT_TRUE(m_tiles.Load(m_folder.Path(), 0, skipped).HasValue());
    const auto metadata = registry.Metadata(0x64);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->components, 3);
    EXPECT_GT(metadata->size.x, 0);
    EXPECT_EQ(registry.References(TextureSlot{0x64}), 1);
    EXPECT_TRUE(registry.Metadata(0x68).has_value());
    m_tiles.Release();
    EXPECT_FALSE(registry.Metadata(0x64).has_value());
    EXPECT_FALSE(registry.Metadata(0x68).has_value());
    EXPECT_EQ(m_store.Size(), 0u);
}
TEST_F(WeatherTextureUploadTest, ABrokenOptionalJpegKeepsTheEarlierTga)
{
    WriteFiles(true);
    const std::array<std::byte, 1> broken{std::byte{0}};
    Client::Tests::WriteSceneFile(m_folder.Path(), "World1/leaf01.OZJ", broken);
    std::vector<std::string> skipped;
    ASSERT_TRUE(m_tiles.Load(m_folder.Path(), 0, skipped).HasValue());
    const auto leaf = m_store.Find(TextureSlot{0x64});
    ASSERT_TRUE(leaf.has_value());
    EXPECT_EQ(leaf->texture.format, PixelFormat::Rgba);
    EXPECT_TRUE(std::ranges::any_of(skipped, [](const std::string& value)
                                    { return value.find("leaf01.jpg") != std::string::npos; }));
}

TEST_F(WeatherTextureUploadTest, CrywolfDoesNotSubstituteNormalRainWhenItsOptionalRain011IsMissing)
{
    WriteFiles(true);
    const std::array<Client::Data::Textures::TextureSetEntry, 1> required{{{"World35\\TileGrass01.jpg", 0x23}}};
    Client::Tests::WriteTextureSet(m_folder.Path(), required);
    std::vector<std::string> skipped;
    ASSERT_TRUE(m_tiles.Load(m_folder.Path(), 34, skipped).HasValue());
    EXPECT_FALSE(m_store.Find(TextureSlot{0x66}).has_value());
    EXPECT_TRUE(m_store.Find(TextureSlot{0x67}).has_value());
    EXPECT_TRUE(m_store.Find(TextureSlot{0x68}).has_value());
    EXPECT_TRUE(std::ranges::any_of(skipped, [](const std::string& value)
                                    { return value.find("rain011.tga") != std::string::npos; }));
}

TEST_F(WeatherTextureUploadTest, ReloadingAMapWithoutLeafImagesRemovesThePreviousPhysicalLeafAndMetadata)
{
    Client::Scenes::ModelTextureTable registry;
    m_tiles.BindTextureTable(registry);
    WriteFiles(true);
    const std::array<Client::Data::Textures::TextureSetEntry, 1> required{{{"World3\\TileGrass01.jpg", 0x23}}};
    Client::Tests::WriteTextureSet(m_folder.Path(), required);
    std::vector<std::string> skipped;
    ASSERT_TRUE(m_tiles.Load(m_folder.Path(), 0, skipped).HasValue());
    ASSERT_TRUE(registry.Metadata(0x64).has_value());
    ASSERT_TRUE(m_tiles.Load(m_folder.Path(), 2, skipped).HasValue());
    EXPECT_FALSE(m_store.Find(TextureSlot{0x64}).has_value());
    EXPECT_FALSE(registry.Metadata(0x64).has_value());
    EXPECT_TRUE(registry.Metadata(0x66).has_value());
}

TEST_F(WeatherTextureUploadTest, AFailedUploadReleasesDuplicateCandidatesOnlyOnce)
{
    WriteFiles(true);
    m_store.failNextLoad = true;
    std::vector<std::string> skipped;
    EXPECT_FALSE(m_tiles.Load(m_folder.Path(), 0, skipped).HasValue());
    EXPECT_FALSE(m_tiles.LoadedMap().has_value());
    EXPECT_EQ(m_store.Size(), 0u);
    EXPECT_EQ(std::ranges::count(m_store.released, std::uint16_t{0x64}), 1);
}
} // namespace
