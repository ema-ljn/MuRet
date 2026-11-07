#include "Scenes/LoadedWorld.h"

#include "Data/DataPath.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Data/Textures/Sampling.h"
#include "Render/TextureSlot.h"
#include "World/TerrainHeight.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// Client::Scenes::LoadedWorld con una copia del World56 del laboratorio (y el accept_box01 de
// Interface\), a la que cada test le saca o le rompe un archivo (06 § El terreno del login, Review
// Focus 2 del plan de la CL7: lo que falta se saltea con un aviso y el login sigue).
namespace
{
using Client::Render::TextureSlot;
using Client::Scenes::LoadedWorld;
using Client::Tests::FakeTextureStore;

constexpr std::uint8_t Login = 55;
constexpr std::uint16_t FirstTile = 0x23;
constexpr std::uint16_t LastTile = 0x30;
constexpr std::uint16_t AcceptBox = 0x122;

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
    }

    void CopyWorld(const std::string& folder) const
    {
        std::error_code error;
        const auto world = Client::Data::ResolveDataPath(m_dataRoot, folder, error);
        ASSERT_TRUE(world.has_value()) << folder;
        std::filesystem::copy(*world, m_copy.Path() / folder, std::filesystem::copy_options::recursive);
    }

    // Los archivos de la copia cuyo nombre sin extensión es `stem`, sin mirar mayúsculas.
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

// Con todo: los 14 tiles en 0x23..0x30 y el 0x122; al soltarlo, la tabla queda vacía.
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
    EXPECT_TRUE(skipped.empty());
    for (std::uint16_t slot = FirstTile; slot <= LastTile; ++slot)
    {
        EXPECT_TRUE(m_store.Find(TextureSlot{slot}).has_value()) << std::hex << slot;
    }
    EXPECT_TRUE(m_store.Find(TextureSlot{AcceptBox}).has_value());
    EXPECT_EQ(m_store.Size(), 15u);

    world.Release();
    EXPECT_FALSE(world.IsLoaded());
    EXPECT_EQ(world.LoadedMap(), std::nullopt);
    EXPECT_EQ(world.Lighting(), nullptr);
    EXPECT_EQ(m_store.Size(), 0u);
}

// Las tablas siguen vivas entre escenas; los tiles siguen perteneciendo al LoadedWorld.
TEST_F(GoldenLoadedWorld, TakingTheTerrainKeepsCpuDataAfterTheSceneAndReleasesTilesOnce)
{
    std::optional<Client::World::WorldTerrain> retained;
    double height = 0.0;
    {
        LoadedWorld world(m_store);
        std::vector<std::string> skipped;
        ASSERT_TRUE(world.Load(m_copy.Path(), Login, skipped).HasValue());
        const auto* heights = world.Terrain()->heights.get();
        const auto* attributes = world.Terrain()->attributes.get();
        height = Client::World::TerrainHeightAt(*heights, *attributes, 50.0f, 50.0f);
        retained = world.TakeTerrain();
        ASSERT_TRUE(retained.has_value());
        EXPECT_EQ(retained->heights.get(), heights);
        EXPECT_EQ(retained->attributes.get(), attributes);
        EXPECT_FALSE(world.IsLoaded());
        EXPECT_EQ(world.LoadedMap(), std::nullopt);
        EXPECT_EQ(world.Terrain(), nullptr);
        EXPECT_EQ(world.Lighting(), nullptr);
        EXPECT_FALSE(world.TakeTerrain().has_value());
        EXPECT_EQ(m_store.Size(), 15u);
        world.Release();
        EXPECT_EQ(m_store.Size(), 0u);
        EXPECT_EQ(m_store.released.size(), 15u);
        world.Release();
        EXPECT_EQ(m_store.released.size(), 15u);
    }
    ASSERT_TRUE(retained.has_value());
    EXPECT_DOUBLE_EQ(Client::World::TerrainHeightAt(*retained->heights, *retained->attributes, 50.0f, 50.0f), height);
}

// Un tile que no avisa y falta (0x63b870 con el sexto argumento en 0) se saltea, y el resto sube.
TEST_F(GoldenLoadedWorld, AMissingTileIsSkipped)
{
    Remove("World56", "TileRock07");
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    ASSERT_TRUE(world.Load(m_copy.Path(), Login, skipped).HasValue());
    ASSERT_EQ(skipped.size(), 1u);
    EXPECT_NE(skipped.front().find("TileRock07"), std::string::npos) << skipped.front();
    EXPECT_FALSE(m_store.Find(TextureSlot{LastTile}).has_value());
    EXPECT_EQ(m_store.Size(), 14u);
}

// Y uno que está pero no se lee, también (Q-C14: el original devuelve false sin avisar y sigue): no se
// lleva el terreno.
TEST_F(GoldenLoadedWorld, ABrokenTileIsSkippedToo)
{
    Break("World56", "TileRock07");
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    const auto loaded = world.Load(m_copy.Path(), Login, skipped);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_EQ(skipped.size(), 1u);
    EXPECT_NE(skipped.front().find("TileRock07"), std::string::npos) << skipped.front();
    EXPECT_TRUE(world.IsLoaded());
    EXPECT_EQ(world.LoadedMap(), Login);
    EXPECT_EQ(m_store.Size(), 14u);
}

// TileGrass01 avisa (el sexto argumento en 1): sin él no hay terreno, el error lo nombra y no queda
// nada subido.
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

// El 0x122 también avisa (0x61ae60 con el sexto argumento en 1).
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

// Sin el .map, el .att y el .obj no hay terreno.
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

// Si la tabla no sube, no queda nada subido ni cargado.
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

// Cada entrada al login lo vuelve a cargar (0x61a560): el segundo Load suelta lo del primero y sube lo
// mismo, sin dejar nada de más.
TEST_F(GoldenLoadedWorld, LoadingAgainReleasesTheFirst)
{
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    ASSERT_TRUE(world.Load(m_copy.Path(), Login, skipped).HasValue());
    ASSERT_TRUE(world.Load(m_copy.Path(), Login, skipped).HasValue());
    EXPECT_EQ(m_store.Size(), 15u);
    EXPECT_EQ(m_store.released.size(), 15u);
    world.Release();
    EXPECT_EQ(m_store.Size(), 0u);
}

// Sólo está World10 en la copia además del login: ambos pedidos deben cargar ese mismo Data real.
// Perder la normalización dejaría LoadedMap=32 aunque la carpeta legacy ya diera World10.
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

// Compartir World12 no convierte a los mapas 11 y 17 en el ID de su carpeta ni entre sí.
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

// La carga local suelta el mundo previo al reentrar: un error no conserva el ID del éxito anterior.
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
