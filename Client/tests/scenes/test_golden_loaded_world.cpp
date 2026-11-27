#include "Assets/LoadedWorld.h"

#include "Data/DataPath.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"
#include "Terrain/TerrainHeight.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainCulling.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include <memory>

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// Client::Assets::LoadedWorld with a copy of the lab's World56 (and Interface\accept_box01 and Object8\drop01), from
// which each test removes or breaks a file (06 § El terreno del login, Review Focus 2 of the CL7 plan: what is
// missing is skipped with a notice and the login goes on).
namespace
{
using Client::Assets::LoadedWorld;
using Client::Render::TextureSlot;
using Client::Tests::FakeTextureStore;

constexpr std::uint8_t Login = 55;
constexpr std::uint16_t FirstTile = 0x23;
constexpr std::uint16_t LastTile = 0x30;
constexpr std::uint16_t AcceptBox = 0x122;
// 0x61ac3e..0x61ad7a: 64/65 from World56 and 66/67/68 from World1/World10; only the JPG64 alternative is missing. With
// 0x616ec0's drop (Object8\drop01, 0x607). 32 new caustics; light01 takes 68 before rain03 replaces that same slot.
constexpr std::size_t LoadedLoginTextureCount = 21 + 32;
constexpr std::uint16_t CausticFirstSlot = 0x41;
constexpr std::uint16_t CausticCount = 32;
constexpr std::size_t OptionalLoginWeatherSkips = 1;

std::string Lower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

class GoldenLoadedWorld : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        Client::Tests::GoldenDataTest::SetUp();
        std::error_code error;
        const auto world = Client::Data::ResolveDataPath(m_dataRoot, "World56", error);
        ASSERT_TRUE(world.has_value()) << "World56";
        std::filesystem::copy(*world, m_copy.Path() / "World56", std::filesystem::copy_options::recursive);
        const auto interface = Client::Data::ResolveDataPath(m_dataRoot, "Interface", error);
        ASSERT_TRUE(interface.has_value()) << "Interface";
        std::filesystem::create_directories(m_copy.Path() / "Interface");
        for (const auto& entry : std::filesystem::directory_iterator(*interface))
        {
            if (Lower(entry.path().stem().string()) == "accept_box01")
            {
                std::filesystem::copy(entry.path(), m_copy.Path() / "Interface" / entry.path().filename());
            }
        }
        std::vector<std::string> globalFiles{"World1/rain01.OZT", "World1/rain02.OZT", "World10/rain03.OZT",
                                             "Object8/drop01.OZJ", "Object8/light01.OZJ"};
        // 0x618ddc..0x618e29: wt00..wt31, before light01 (0x618e40) and 0x61a560's tiles.
        for (std::uint16_t index = 0; index < CausticCount; ++index)
            globalFiles.push_back("Object8/wt" + std::string(index < 10 ? "0" : "") + std::to_string(index) + ".OZJ");
        for (const std::string& relative : globalFiles)
        {
            const auto weather = Client::Data::ResolveDataPath(m_dataRoot, relative, error);
            ASSERT_TRUE(weather.has_value()) << relative;
            const auto destination = m_copy.Path() / relative;
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(*weather, destination);
        }
    }

    void CopyWorld(const std::string& folder) const
    {
        std::error_code error;
        const auto world = Client::Data::ResolveDataPath(m_dataRoot, folder, error);
        ASSERT_TRUE(world.has_value()) << folder;
        // World10 already holds the global rain03 since SetUp; the full copy keeps the same resource.
        std::filesystem::copy(*world, m_copy.Path() / folder,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
    }

    // The copy's files whose name without extension is `stem`, case-insensitive.
    std::vector<std::filesystem::path> FilesNamed(const std::string& folder, const std::string& stem) const
    {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(m_copy.Path() / folder))
        {
            if (Lower(entry.path().stem().string()) == Lower(stem))
            {
                files.push_back(entry.path());
            }
        }
        return files;
    }

    void Remove(const std::string& folder, const std::string& stem) const
    {
        const auto files = FilesNamed(folder, stem);
        ASSERT_FALSE(files.empty()) << stem;
        for (const auto& file : files)
        {
            std::filesystem::remove(file);
        }
    }

    void Break(const std::string& folder, const std::string& stem) const
    {
        const auto files = FilesNamed(folder, stem);
        ASSERT_FALSE(files.empty()) << stem;
        for (const auto& file : files)
        {
            std::ofstream(file, std::ios::binary | std::ios::trunc) << "esto no es una textura";
        }
    }

    Client::Tests::TemporaryFolder m_copy{"loaded-world"};
    FakeTextureStore m_store;
};

// With everything: the 14 tiles at 0x23..0x30 and the 0x122; on release, the table is empty.
TEST_F(GoldenLoadedWorld, LoadsTheFourteenTilesAndTheAcceptBox)
{
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    EXPECT_EQ(world.LoadedMap(), std::nullopt);
    const auto loaded = world.Load(m_copy.Path(), Login, skipped);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    EXPECT_TRUE(world.IsLoaded());
    EXPECT_EQ(world.LoadedMap(), Login);
    EXPECT_NE(world.Lighting(), nullptr);
    ASSERT_EQ(skipped.size(), OptionalLoginWeatherSkips);
    EXPECT_NE(skipped.front().find("leaf01.jpg"), std::string::npos);
    for (std::uint16_t slot = FirstTile; slot <= LastTile; ++slot)
    {
        EXPECT_TRUE(m_store.Find(TextureSlot{slot}).has_value()) << std::hex << slot;
    }
    EXPECT_TRUE(m_store.Find(TextureSlot{AcceptBox}).has_value());
    EXPECT_EQ(m_store.Size(), LoadedLoginTextureCount);
    // The ASM's literal table: drop, 32 waters, light01, 14 tiles, five weather textures and accept_box.
    std::vector<std::uint16_t> uploads{0x607};
    for (std::uint16_t index = 0; index < CausticCount; ++index)
    {
        const std::uint16_t slot = static_cast<std::uint16_t>(CausticFirstSlot + index);
        uploads.push_back(slot);
        const auto actual = m_store.Find(TextureSlot{slot});
        ASSERT_TRUE(actual.has_value());
        EXPECT_EQ(actual->sampling.filter, Client::Data::Textures::Filter::Linear);
        EXPECT_EQ(actual->sampling.wrap, Client::Data::Textures::Wrap::Repeat);
        EXPECT_EQ(actual->texture.format, Client::Data::Textures::PixelFormat::Rgb);
        const std::string path = "Object8\\wt" + std::string(index < 10 ? "0" : "") + std::to_string(index) + ".jpg";
        const std::array<Client::Data::Textures::TextureSetEntry, 1> entry{{{path, slot, actual->sampling}}};
        const auto original = Client::Data::Textures::LoadTextureSet(m_dataRoot, entry);
        ASSERT_TRUE(original.HasValue());
        EXPECT_EQ(actual->texture.pixels, original.Value().front().texture.pixels) << path;
    }
    uploads.push_back(0x68);
    for (std::uint16_t slot = FirstTile; slot <= LastTile; ++slot)
        uploads.push_back(slot);
    for (const std::uint16_t slot : {std::uint16_t{0x64}, std::uint16_t{0x65}, std::uint16_t{0x66}, std::uint16_t{0x67},
                                     std::uint16_t{0x68}, std::uint16_t{0x122}})
        uploads.push_back(slot);
    EXPECT_EQ(m_store.loaded, uploads);
    const auto weather = m_store.Find(TextureSlot{0x68});
    ASSERT_TRUE(weather.has_value());
    EXPECT_EQ(weather->sampling.filter, Client::Data::Textures::Filter::Nearest);
    EXPECT_EQ(weather->sampling.wrap, Client::Data::Textures::Wrap::Clamp);
    EXPECT_EQ(weather->texture.format, Client::Data::Textures::PixelFormat::Rgba);

    world.Release();
    EXPECT_FALSE(world.IsLoaded());
    EXPECT_EQ(world.LoadedMap(), std::nullopt);
    EXPECT_EQ(world.Lighting(), nullptr);
    EXPECT_EQ(m_store.Size(), 0u);
    EXPECT_EQ(m_store.released.size(), LoadedLoginTextureCount);
    EXPECT_EQ(std::ranges::count(m_store.released, std::uint16_t{0x68}), 1);
}

TEST_F(GoldenLoadedWorld, DrawingUsesTheDynamicLightWithoutReplacingItsBase)
{
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    ASSERT_TRUE(world.Load(m_copy.Path(), Login, skipped).HasValue());
    const float base = world.Lighting()->cells[1000][0];
    auto dynamic = std::make_unique<Client::Terrain::TerrainLighting>();
    dynamic->cells.fill({1.0f, 0.0f, 0.0f});
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const Client::Terrain::TerrainRange range{.firstX = 0, .lastX = 128, .firstY = 120, .lastY = 251};
    Client::Terrain::TerrainPolygon polygon;
    polygon.everything = true;
    world.DrawTerrain(batch, {}, range, polygon, Client::Terrain::FixedCornerHeight, dynamic.get());
    ASSERT_FALSE(batch.Vertices().empty());
    EXPECT_TRUE(std::ranges::all_of(batch.Vertices(), [](const Client::Render::ModelVertex& vertex)
                                    { return vertex.color.green == 0.0f && vertex.color.blue == 0.0f; }));
    EXPECT_EQ(world.Lighting()->cells[1000][0], base);
}

// The tables stay alive between scenes; the tiles still belong to the LoadedWorld.
TEST_F(GoldenLoadedWorld, TakingTheTerrainKeepsCpuDataAfterTheSceneAndReleasesTilesOnce)
{
    std::optional<Client::Terrain::WorldTerrain> retained;
    double height = 0.0;
    {
        LoadedWorld world(m_store);
        std::vector<std::string> skipped;
        ASSERT_TRUE(world.Load(m_copy.Path(), Login, skipped).HasValue());
        const auto* heights = world.Terrain()->heights.get();
        const auto* attributes = world.Terrain()->attributes.get();
        height = Client::Terrain::TerrainHeightAt(*heights, *attributes, 50.0f, 50.0f);
        retained = world.TakeTerrain();
        ASSERT_TRUE(retained.has_value());
        EXPECT_EQ(retained->heights.get(), heights);
        EXPECT_EQ(retained->attributes.get(), attributes);
        EXPECT_FALSE(world.IsLoaded());
        EXPECT_EQ(world.LoadedMap(), std::nullopt);
        EXPECT_EQ(world.Terrain(), nullptr);
        EXPECT_EQ(world.Lighting(), nullptr);
        EXPECT_FALSE(world.TakeTerrain().has_value());
        EXPECT_EQ(m_store.Size(), LoadedLoginTextureCount);
        world.Release();
        EXPECT_EQ(m_store.Size(), 0u);
        EXPECT_EQ(m_store.released.size(), LoadedLoginTextureCount);
        world.Release();
        EXPECT_EQ(m_store.released.size(), LoadedLoginTextureCount);
    }
    ASSERT_TRUE(retained.has_value());
    EXPECT_DOUBLE_EQ(Client::Terrain::TerrainHeightAt(*retained->heights, *retained->attributes, 50.0f, 50.0f), height);
}

// A tile that does not warn and is missing (0x63b870 with the sixth argument at 0) is skipped, and the rest uploads.
TEST_F(GoldenLoadedWorld, AMissingTileIsSkipped)
{
    Remove("World56", "TileRock07");
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    ASSERT_TRUE(world.Load(m_copy.Path(), Login, skipped).HasValue());
    ASSERT_EQ(skipped.size(), OptionalLoginWeatherSkips + 1);
    EXPECT_NE(skipped.front().find("TileRock07"), std::string::npos) << skipped.front();
    EXPECT_FALSE(m_store.Find(TextureSlot{LastTile}).has_value());
    EXPECT_EQ(m_store.Size(), LoadedLoginTextureCount - 1);
}

// And so is one that is there but unreadable (Q-C14: the original returns false without a notice and goes on): the
// terrain still loads.
TEST_F(GoldenLoadedWorld, ABrokenTileIsSkippedToo)
{
    Break("World56", "TileRock07");
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    const auto loaded = world.Load(m_copy.Path(), Login, skipped);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_EQ(skipped.size(), OptionalLoginWeatherSkips + 1);
    EXPECT_NE(skipped.front().find("TileRock07"), std::string::npos) << skipped.front();
    EXPECT_TRUE(world.IsLoaded());
    EXPECT_EQ(world.LoadedMap(), Login);
    EXPECT_EQ(m_store.Size(), LoadedLoginTextureCount - 1);
}

// TileGrass01 warns (the sixth argument at 1): without it there is no terrain, the error names it and nothing stays
// uploaded.
TEST_F(GoldenLoadedWorld, WithoutTileGrass01ThereIsNoTerrain)
{
    Remove("World56", "TileGrass01");
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    const auto loaded = world.Load(m_copy.Path(), Login, skipped);
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("TileGrass01"), std::string::npos) << loaded.Error();
    EXPECT_FALSE(world.IsLoaded());
    EXPECT_EQ(world.LoadedMap(), std::nullopt);
    EXPECT_EQ(m_store.Size(), 0u);
}

// The 0x122 warns too (0x61ae60 with the sixth argument at 1).
TEST_F(GoldenLoadedWorld, WithoutTheAcceptBoxThereIsNoTerrain)
{
    Remove("Interface", "accept_box01");
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    const auto loaded = world.Load(m_copy.Path(), Login, skipped);
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("accept_box01"), std::string::npos) << loaded.Error();
    EXPECT_EQ(m_store.Size(), 0u);
    EXPECT_EQ(world.LoadedMap(), std::nullopt);
}

// Without the .map, the .att and the .obj there is no terrain.
TEST_F(GoldenLoadedWorld, WithoutTheEncTerrainFilesThereIsNoTerrain)
{
    Remove("World56", "EncTerrain56");
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    EXPECT_FALSE(world.Load(m_copy.Path(), Login, skipped).HasValue());
    EXPECT_FALSE(world.IsLoaded());
    EXPECT_EQ(world.LoadedMap(), std::nullopt);
    EXPECT_EQ(m_store.Size(), 0u);
}

// If the table does not upload, nothing stays uploaded or loaded.
TEST_F(GoldenLoadedWorld, ATableThatDoesNotUploadLeavesNothing)
{
    m_store.failNextLoad = true;
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    EXPECT_FALSE(world.Load(m_copy.Path(), Login, skipped).HasValue());
    EXPECT_FALSE(world.IsLoaded());
    EXPECT_EQ(world.LoadedMap(), std::nullopt);
    EXPECT_EQ(m_store.Size(), 0u);
}

// Each entry to the login loads it again (0x61a560): the second Load releases the first's and uploads the same,
// leaving nothing extra.
TEST_F(GoldenLoadedWorld, LoadingAgainReleasesTheFirst)
{
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    ASSERT_TRUE(world.Load(m_copy.Path(), Login, skipped).HasValue());
    ASSERT_TRUE(world.Load(m_copy.Path(), Login, skipped).HasValue());
    EXPECT_EQ(m_store.Size(), LoadedLoginTextureCount);
    EXPECT_EQ(m_store.released.size(), LoadedLoginTextureCount);
    world.Release();
    EXPECT_EQ(m_store.Size(), 0u);
}

// Only World10 is in the copy besides the login: both requests must load that same real Data. Losing the
// normalization would leave LoadedMap=32 even though the legacy folder already gave World10.
TEST_F(GoldenLoadedWorld, ThirtyTwoLoadsTheSameTerrainAndTilesAsNineAndPublishesNine)
{
    CopyWorld("World10");
    LoadedWorld remapped(m_store);
    FakeTextureStore nineStore;
    LoadedWorld nine(nineStore);
    std::vector<std::string> remappedSkipped;
    std::vector<std::string> nineSkipped;
    const auto loaded = remapped.Load(m_copy.Path(), 32, remappedSkipped);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_TRUE(nine.Load(m_copy.Path(), 9, nineSkipped).HasValue());
    EXPECT_EQ(remapped.LoadedMap(), 9);
    EXPECT_EQ(nine.LoadedMap(), 9);
    EXPECT_EQ(remappedSkipped, nineSkipped);
    EXPECT_EQ(remapped.Terrain()->mapping->layer1, nine.Terrain()->mapping->layer1);
    EXPECT_EQ(remapped.Terrain()->mapping->layer2, nine.Terrain()->mapping->layer2);
    EXPECT_EQ(remapped.Terrain()->mapping->alpha, nine.Terrain()->mapping->alpha);
    EXPECT_EQ(remapped.Terrain()->heights->cells, nine.Terrain()->heights->cells);
    EXPECT_EQ(remapped.Terrain()->attributes->cells, nine.Terrain()->attributes->cells);
    EXPECT_EQ(remapped.Lighting()->cells, nine.Lighting()->cells);
    EXPECT_EQ(m_store.loaded, nineStore.loaded);
    EXPECT_GT(m_store.Size(), 1u);
    for (const std::uint16_t slot : m_store.loaded)
    {
        const auto actual = m_store.Find(TextureSlot{slot});
        const auto expected = nineStore.Find(TextureSlot{slot});
        ASSERT_TRUE(actual.has_value());
        ASSERT_TRUE(expected.has_value());
        EXPECT_EQ(actual->sampling.filter, expected->sampling.filter);
        EXPECT_EQ(actual->sampling.wrap, expected->sampling.wrap);
        EXPECT_EQ(actual->texture.width, expected->texture.width);
        EXPECT_EQ(actual->texture.height, expected->texture.height);
        EXPECT_EQ(actual->texture.textureWidth, expected->texture.textureWidth);
        EXPECT_EQ(actual->texture.textureHeight, expected->texture.textureHeight);
        EXPECT_EQ(actual->texture.format, expected->texture.format);
        EXPECT_EQ(actual->texture.pixels, expected->texture.pixels);
    }
    remapped.Release();
    nine.Release();
}

// Sharing World12 does not turn maps 11 and 17 into their folder's ID or into each other.
TEST_F(GoldenLoadedWorld, SharedFolderPreservesEachMapIdentity)
{
    CopyWorld("World12");
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    for (const std::uint8_t map : std::array<std::uint8_t, 2>{11, 17})
    {
        const auto loaded = world.Load(m_copy.Path(), map, skipped);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        EXPECT_EQ(world.LoadedMap(), map);
    }
    world.Release();
}

TEST_F(GoldenLoadedWorld, LorenciaAndSceneMapsKeepTheirIdentity)
{
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    for (const std::uint8_t map : std::array<std::uint8_t, 3>{0, 54, 55})
    {
        const auto loaded = world.Load(m_dataRoot, map, skipped);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        EXPECT_EQ(world.LoadedMap(), map);
    }
    world.Release();
    EXPECT_EQ(world.LoadedMap(), std::nullopt);
}

// The local load releases the previous world on reentry: an error does not keep the previous success's ID.
TEST_F(GoldenLoadedWorld, ReenteringFromThirtyTwoReplacesIdentityAndFailureClearsIt)
{
    CopyWorld("World10");
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    ASSERT_TRUE(world.Load(m_copy.Path(), 32, skipped).HasValue());
    EXPECT_EQ(world.LoadedMap(), 9);
    ASSERT_TRUE(world.Load(m_copy.Path(), Login, skipped).HasValue());
    EXPECT_EQ(world.LoadedMap(), Login);
    ASSERT_TRUE(world.Load(m_copy.Path(), 32, skipped).HasValue());
    EXPECT_EQ(world.LoadedMap(), 9);
    m_store.failNextLoad = true;
    EXPECT_FALSE(world.Load(m_copy.Path(), Login, skipped).HasValue());
    EXPECT_EQ(world.LoadedMap(), std::nullopt);
    EXPECT_EQ(m_store.Size(), 0u);
    ASSERT_TRUE(world.Load(m_copy.Path(), 32, skipped).HasValue());
    Remove("World10", "EncTerrain10");
    EXPECT_FALSE(world.Load(m_copy.Path(), 32, skipped).HasValue());
    EXPECT_EQ(world.LoadedMap(), std::nullopt);
    EXPECT_EQ(m_store.Size(), 0u);
}

TEST_F(GoldenLoadedWorld, AnEmptyDataFolderDoesNotPublishMapZero)
{
    Client::Tests::TemporaryFolder empty{"empty-world"};
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    EXPECT_EQ(world.LoadedMap(), std::nullopt);
    EXPECT_FALSE(world.Load(empty.Path(), 0, skipped).HasValue());
    EXPECT_EQ(world.LoadedMap(), std::nullopt);
}
} // namespace
