#include "Scenes/WorldModels.h"

#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "Data/Models/Model.h"
#include "Data/Textures/Sampling.h"
#include "Render/TextureSlot.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SyntheticModels.h"
#include "support/TemporaryFolder.h"
#include "support/TextureFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace
{
using Client::Render::TextureSlot;
using Client::Scenes::WorldModels;

struct Request
{
    std::uint16_t model;
    std::string_view folder;
    std::string_view file;
    TextureSlot slot;
};

// 05 § Los especiales de cada mapa; ASM 6173af..617440, 617520..6175e2, 6176ba..6176e6 y
// 617853..61787f. Dungeon y Lost Tower comparten Object2, no la carpeta de su terreno.
constexpr std::array<Request, 3> Dungeon{{{0xda, "Object2", "DungeonStone01.bmd", TextureSlot{0x69}},
                                          {0xaf, "Object2", "Bat01.bmd", TextureSlot{0x6a}},
                                          {0xb0, "Object2", "Rat01.bmd", TextureSlot{0x6b}}}};
constexpr std::array<Request, 4> Noria{{{0xae, "Object1", "Butterfly01.bmd", TextureSlot{0x69}},
                                        {0x142, "Npc", "warp01.bmd", TextureSlot{0x6a}},
                                        {0x143, "Npc", "warp02.bmd", TextureSlot{0x6b}},
                                        {0x144, "Npc", "warp03.bmd", TextureSlot{0x6c}}}};
constexpr std::array<Request, 1> Stadium{{{0xb1, "Object7", "Bug01.bmd", TextureSlot{0x69}}}};
constexpr std::array<Request, 1> Tarkan{{{0xb2, "Object9", "Bug02.bmd", TextureSlot{0x69}}}};

struct MapCase
{
    std::uint8_t map;
    std::string_view objects;
    std::span<const Request> specials;
};

constexpr std::array<MapCase, 5> Cases{{{1, "Object2", Dungeon},
                                        {4, "Object5", Dungeon},
                                        {3, "Object4", Noria},
                                        {6, "Object7", Stadium},
                                        {8, "Object9", Tarkan}}};

std::string TextureName(std::uint16_t model)
{
    return "special" + std::to_string(model) + ".tga";
}

void WriteModel(const std::filesystem::path& root, std::string_view folder, std::string_view file,
                std::uint16_t modelId)
{
    Client::Data::Models::Model model = Client::Tests::SampleModel();
    model.meshes.front().textureName = TextureName(modelId);
    constexpr std::uint8_t ClearVersion = 0x0a;
    Client::Tests::WriteSceneFile(root, std::filesystem::path(folder) / file,
                                  Client::Tests::PlainBmd(Client::Tests::BmdContent(model), ClearVersion));
    constexpr std::int16_t TextureSide = 2;
    Client::Tests::WriteSceneFile(root, std::filesystem::path(folder) / ("special" + std::to_string(modelId) + ".OZT"),
                                  Client::Tests::PlainOzt(TextureSide, TextureSide));
}

void WriteMap(const std::filesystem::path& root, const MapCase& probe)
{
    for (const Request& request : probe.specials)
    {
        WriteModel(root, request.folder, request.file, request.model);
    }
    WriteModel(root, probe.objects, "Object01.bmd", 0);
    if (probe.map == 1)
        WriteModel(root, probe.objects, "Object41.bmd", 40);
}

TEST(WorldSpecialModelsTest, TheFirstFamiliesAssignTheirSpecialsBeforeTheGenericObjects)
{
    for (const MapCase& probe : Cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        Client::Tests::TemporaryFolder folder{"special-model-slots"};
        WriteMap(folder.Path(), probe);
        Client::Tests::FakeTextureStore store;
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        WorldModels models;

        const auto loaded = models.Load(folder.Path(), probe.map, store, log, nullptr);

        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        EXPECT_EQ(models.Count(), probe.specials.size() + (probe.map == 1 ? 2 : 1));
        for (const Request& request : probe.specials)
        {
            SCOPED_TRACE(request.model);
            ASSERT_NE(models.Find(request.model), nullptr);
            ASSERT_NE(models.SkeletonOf(request.model), nullptr);
            const auto slots = models.MeshTextures(request.model);
            ASSERT_EQ(slots.size(), 1u);
            EXPECT_EQ(slots.front(), request.slot);
            const auto texture = store.Find(request.slot);
            ASSERT_TRUE(texture.has_value());
            EXPECT_EQ(texture->sampling.filter, Client::Data::Textures::Filter::Nearest);
            EXPECT_EQ(texture->sampling.wrap, Client::Data::Textures::Wrap::Repeat);
        }
        ASSERT_NE(models.Find(0), nullptr);
        ASSERT_EQ(models.MeshTextures(0).size(), 1u);
        EXPECT_EQ(models.MeshTextures(0).front(), TextureSlot{0x41c});
        models.Release();
        EXPECT_EQ(store.Size(), 0u);
    }
}

TEST(WorldSpecialModelsTest, ReloadingAnotherFamilyReleasesThePreviousSpecials)
{
    Client::Tests::TemporaryFolder folder{"special-model-reentry"};
    WriteMap(folder.Path(), Cases[1]); // Lost Tower: los especiales vienen de Object2.
    WriteMap(folder.Path(), Cases[3]); // Stadium.
    Client::Tests::FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(folder.Path(), 4, store, log, nullptr).HasValue());
    ASSERT_NE(models.Find(0xda), nullptr);

    ASSERT_TRUE(models.Load(folder.Path(), 6, store, log, nullptr).HasValue());

    EXPECT_EQ(models.Count(), 2u);
    EXPECT_NE(models.Find(0xb1), nullptr);
    for (const Request& previous : Dungeon)
    {
        EXPECT_EQ(models.Find(previous.model), nullptr);
        EXPECT_EQ(models.SkeletonOf(previous.model), nullptr);
        EXPECT_TRUE(models.MeshTextures(previous.model).empty());
    }
    EXPECT_EQ(store.Size(), 2u);
    EXPECT_FALSE(store.Find(TextureSlot{0x6a}).has_value());
    EXPECT_FALSE(store.Find(TextureSlot{0x6b}).has_value());
}

// Q-C23: que falte un especial no reserva una textura ni impide los siguientes.
TEST(WorldSpecialModelsTest, AMissingSpecialDoesNotConsumeATextureSlot)
{
    Client::Tests::TemporaryFolder folder{"special-model-missing"};
    WriteModel(folder.Path(), "Object2", "Bat01.bmd", 0xaf);
    WriteModel(folder.Path(), "Object2", "Rat01.bmd", 0xb0);
    WriteModel(folder.Path(), "Object2", "Object41.bmd", 40);
    Client::Tests::FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    WorldModels models;

    ASSERT_TRUE(models.Load(folder.Path(), 1, store, log, nullptr).HasValue());

    EXPECT_EQ(models.Find(0xda), nullptr);
    ASSERT_EQ(models.MeshTextures(0xaf).size(), 1u);
    ASSERT_EQ(models.MeshTextures(0xb0).size(), 1u);
    EXPECT_EQ(models.MeshTextures(0xaf).front(), TextureSlot{0x69});
    EXPECT_EQ(models.MeshTextures(0xb0).front(), TextureSlot{0x6a});
}

TEST(WorldSpecialModelsTest, BloodCastleNeedsTheGlobalLibraryBeforeItLoadsModels)
{
    Client::Tests::TemporaryFolder folder{"special-model-frontiers"};
    Client::Tests::FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    WorldModels models;
    for (const std::uint8_t map : {std::uint8_t{11}, std::uint8_t{17}})
    {
        SCOPED_TRACE(static_cast<int>(map));

        const auto loaded = models.Load(folder.Path(), map, store, log, nullptr);

        ASSERT_FALSE(loaded.HasValue());
        EXPECT_NE(loaded.Error().find("0x6179f0"), std::string::npos) << loaded.Error();
        EXPECT_EQ(models.Count(), 0u);
        EXPECT_EQ(models.TextureSlots(), nullptr);
        EXPECT_EQ(store.Size(), 0u);
    }
}

using GoldenWorldSpecialModels = Client::Tests::GoldenDataTest;

TEST_F(GoldenWorldSpecialModels, TheFirstFamiliesFindTheSpecialsInTheRealData)
{
    for (const MapCase& probe : Cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        Client::Tests::FakeTextureStore store;
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        WorldModels models;

        const auto loaded = models.Load(m_dataRoot, probe.map, store, log, nullptr);

        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        for (const Request& request : probe.specials)
        {
            SCOPED_TRACE(request.model);
            EXPECT_NE(models.Find(request.model), nullptr);
            EXPECT_NE(models.SkeletonOf(request.model), nullptr);
            EXPECT_FALSE(models.MeshTextures(request.model).empty());
        }
        models.Release();
        EXPECT_EQ(store.Size(), 0u);
    }
}
} // namespace
