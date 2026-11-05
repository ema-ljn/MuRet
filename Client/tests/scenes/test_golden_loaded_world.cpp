#include "Scenes/LoadedWorld.h"

#include "Data/DataPath.h"
#include "Render/TextureSlot.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
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
    const auto loaded = world.Load(m_copy.Path(), Login, skipped);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    EXPECT_TRUE(world.IsLoaded());
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
    EXPECT_EQ(world.Lighting(), nullptr);
    EXPECT_EQ(m_store.Size(), 0u);
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
}

// Sin el .map, el .att y el .obj no hay terreno.
TEST_F(GoldenLoadedWorld, WithoutTheEncTerrainFilesThereIsNoTerrain)
{
    Remove("World56", "EncTerrain56");
    LoadedWorld world(m_store);
    std::vector<std::string> skipped;
    EXPECT_FALSE(world.Load(m_copy.Path(), Login, skipped).HasValue());
    EXPECT_FALSE(world.IsLoaded());
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
} // namespace
