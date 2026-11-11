#include "Scenes/MapLoad.h"
#include "Scenes/ModelTextureTable.h"
#include "Scenes/SceneId.h"
#include "Scenes/TerrainTiles.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "Data/Textures/Texture.h"
#include "Render/ITextureStore.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "World/KalimaResources.h"
#include "World/KalimaWorld.h"
#include "World/KalimaWaves.h"
#include "World/WorldTerrain.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/SceneDataFiles.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
using Client::Render::TextureSlot;
using Client::Scenes::ModelTextureTable;
using Client::Scenes::TerrainTiles;
class SlotFailureStore final : public Client::Render::ITextureStore
{
public:
    std::optional<std::uint16_t> failSlot;
    Client::Tests::FakeTextureStore stored;
    Client::Core::Expected<void, std::string>
    Load(std::span<const Client::Data::Textures::LoadedTexture> textures) override
    {
        if (failSlot && std::ranges::any_of(textures, [&](const auto& texture) { return texture.slot == *failSlot; }))
            return Client::Core::Unexpected(std::string("falla al subir el slot36"));
        return stored.Load(textures);
    }
    void Release(TextureSlot slot) override
    {
        stored.Release(slot);
    }
};
class KalimaSceneLoadingTest : public testing::Test
{
protected:
    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
    Client::Render::Models::CrtRandom m_random{1};
    std::filesystem::path m_root = Client::Tests::RepoDataDir();
    Client::Core::Expected<void, std::string> Load(std::uint8_t map, const std::filesystem::path& root)
    {
        m_world->Runtime().map = map;
        return Client::Scenes::LoadMap(map, {.dataRoot = root,
                                             .world = *m_world,
                                             .scene = Client::Scenes::SceneId::Game,
                                             .playerKey = 5,
                                             .random = m_random});
    }
};
TEST_F(KalimaSceneLoadingTest, AllSevenMapsLoadRealCpuDataAndWaterResourcesBeforeTheZeroStepFrame)
{
    SlotFailureStore store;
    ModelTextureTable table;
    TerrainTiles tiles(store);
    tiles.BindTextureTable(table);
    for (const std::uint8_t map : {std::uint8_t{24}, std::uint8_t{25}, std::uint8_t{26}, std::uint8_t{27},
                                   std::uint8_t{28}, std::uint8_t{29}, std::uint8_t{36}})
    {
        SCOPED_TRACE(static_cast<int>(map));
        const auto loaded = Load(map, m_root);
        EXPECT_TRUE(loaded.HasValue()) << loaded.Error();
        if (!loaded.HasValue())
            continue;
        ASSERT_NE(m_world->Map().kalima, nullptr);
        EXPECT_EQ(m_world->Map().kalima->Map(), map);
        EXPECT_NE(m_world->Map().terrain.attributes, nullptr);
        EXPECT_FALSE(m_world->Map().objects.empty());
        std::vector<std::string> skipped;
        ASSERT_TRUE(tiles.Load(m_root, map, skipped).HasValue());
        EXPECT_EQ(tiles.LoadedMap(), map);
        EXPECT_EQ(table.References(TextureSlot{0x23}), 1);
        EXPECT_EQ(table.References(TextureSlot{0x24}), 1);
        const auto water = store.stored.Find(TextureSlot{0x23});
        ASSERT_TRUE(water);
        EXPECT_EQ(water->texture.format, Client::Data::Textures::PixelFormat::Rgba);
        const auto state = m_random.State();
        Client::Render::ModelBatch frame;
        frame.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(m_world->Map().kalima->Draw(frame, 64, 64).HasValue());
        EXPECT_EQ(frame.Vertices().size(), 20172u);
        EXPECT_EQ(m_random.State(), state);
        EXPECT_FALSE(m_world->Map().kalima->Waves().selected);
    }
    tiles.Release();
    EXPECT_EQ(table.References(TextureSlot{0x23}), 0);
    EXPECT_EQ(table.References(TextureSlot{0x24}), 0);
    EXPECT_EQ(store.stored.Size(), 0u);
}
TEST_F(KalimaSceneLoadingTest, ReentryAndAnOrdinaryMapReplaceTheOwnerOnlyAfterTheNativeLoadStage)
{
    m_world->Map().kalima = std::make_unique<Client::World::KalimaWorld>(24);
    ASSERT_TRUE(m_world->Map().kalima->Disturb(64, 64, 3, 1000000, m_random).HasValue());
    const auto* previous = m_world->Map().kalima.get();
    Client::Tests::TemporaryFolder missing("kalima-failed-stage");
    const auto failed = Load(0, missing.Path());
    ASSERT_FALSE(failed.HasValue());
    EXPECT_EQ(m_world->Map().kalima.get(), previous);
    EXPECT_EQ(m_world->Map().kalima->Waves().planes[0][128 * 256 + 128], 3u);
    ASSERT_TRUE(Load(0, m_root).HasValue());
    EXPECT_EQ(m_world->Map().kalima, nullptr);
    ASSERT_TRUE(Load(24, m_root).HasValue());
    ASSERT_NE(m_world->Map().kalima, nullptr);
    ASSERT_TRUE(m_world->Map().kalima->Disturb(64, 64, 3, 1000000, m_random).HasValue());
    ASSERT_TRUE(Load(24, m_root).HasValue());
    ASSERT_NE(m_world->Map().kalima, nullptr);
    EXPECT_EQ(m_world->Map().kalima->Waves().planes[0][128 * 256 + 128], 0u);
    EXPECT_FALSE(m_world->Map().kalima->Waves().selected);
}
TEST_F(KalimaSceneLoadingTest, LoadRevisionInvalidatesEveryAttemptIncludingFailuresAndSameMapReentry)
{
    EXPECT_EQ(m_world->Map().loadRevision, 0u);
    m_world->Map().kalima = std::make_unique<Client::World::KalimaWorld>(24);
    const auto* previous = m_world->Map().kalima.get();
    Client::Tests::TemporaryFolder missing("kalima-revision-failure");
    const auto state = m_random.State();
    EXPECT_FALSE(Load(55, missing.Path()).HasValue());
    EXPECT_EQ(m_world->Map().loadRevision, 1u);
    EXPECT_EQ(m_world->Map().kalima.get(), previous);
    EXPECT_EQ(m_random.State(), state);
    EXPECT_FALSE(Load(0, missing.Path()).HasValue());
    EXPECT_EQ(m_world->Map().loadRevision, 2u);
    EXPECT_EQ(m_world->Map().kalima.get(), previous);
    ASSERT_TRUE(Load(24, m_root).HasValue());
    EXPECT_EQ(m_world->Map().loadRevision, 3u);
    ASSERT_TRUE(m_world->Map().kalima->Disturb(64, 64, 3, 1000000, m_random).HasValue());
    ASSERT_TRUE(Load(24, m_root).HasValue());
    EXPECT_EQ(m_world->Map().loadRevision, 4u);
    EXPECT_EQ(m_world->Map().kalima->Waves().planes[0][128 * 256 + 128], 0u);
    ASSERT_TRUE(Load(0, m_root).HasValue());
    EXPECT_EQ(m_world->Map().loadRevision, 5u);
    EXPECT_EQ(m_world->Map().kalima, nullptr);
}

TEST_F(KalimaSceneLoadingTest, TextureTransitionsReplaceTheTwoPhysicalSlotsAndReleaseExactlyTheirReferences)
{
    SlotFailureStore store;
    ModelTextureTable table;
    TerrainTiles tiles(store);
    tiles.BindTextureTable(table);
    for (const std::uint8_t map :
         {std::uint8_t{0}, std::uint8_t{24}, std::uint8_t{29}, std::uint8_t{0}, std::uint8_t{36}, std::uint8_t{0}})
    {
        ASSERT_TRUE(Load(map, m_root).HasValue());
        std::vector<std::string> skipped;
        ASSERT_TRUE(tiles.Load(m_root, map, skipped).HasValue());
        const auto water = store.stored.Find(TextureSlot{0x23});
        ASSERT_TRUE(water);
        EXPECT_EQ(water->texture.format,
                  map == 0 ? Client::Data::Textures::PixelFormat::Rgb : Client::Data::Textures::PixelFormat::Rgba);
        EXPECT_EQ(static_cast<bool>(m_world->Map().kalima), map != 0);
        EXPECT_EQ(table.References(TextureSlot{0x23}), 1);
        EXPECT_EQ(table.References(TextureSlot{0x24}), 1);
    }
    tiles.Release();
    tiles.Release();
    EXPECT_EQ(store.stored.Size(), 0u);
}
TEST(KalimaSceneTextureTest, APartialUploadRollsBackOnlyItsSuccessfulPhysicalReference)
{
    SlotFailureStore store;
    ModelTextureTable table;
    table.Bind(store);
    const auto decoded = Client::World::LoadKalimaTextures(Client::Tests::RepoDataDir());
    ASSERT_TRUE(decoded.HasValue());
    ASSERT_TRUE(table.Upload(decoded.Value()[0]).HasValue());
    table.Acquire(TextureSlot{0x23});
    ASSERT_EQ(table.References(TextureSlot{0x23}), 2);
    TerrainTiles tiles(store);
    tiles.BindTextureTable(table);
    store.failSlot = std::uint16_t{0x24};
    std::vector<std::string> skipped;
    const auto loaded = tiles.Load(Client::Tests::RepoDataDir(), 24, skipped);
    EXPECT_FALSE(loaded.HasValue());
    EXPECT_FALSE(tiles.LoadedMap());
    EXPECT_EQ(table.References(TextureSlot{0x23}), 1);
    EXPECT_EQ(table.References(TextureSlot{0x24}), 0);
    EXPECT_TRUE(store.stored.Find(TextureSlot{0x23}));
    EXPECT_FALSE(store.stored.Find(TextureSlot{0x24}));
    tiles.Release();
    EXPECT_EQ(table.References(TextureSlot{0x23}), 1);
    table.Release(TextureSlot{0x23});
    EXPECT_EQ(store.stored.Size(), 0u);
}
TEST(KalimaSceneTextureTest, MissingRequiredWaterFailsBeforeAnyPhysicalUpload)
{
    Client::Tests::TemporaryFolder missing("kalima-missing-water");
    Client::Tests::WriteTextureSet(missing.Path(), Client::World::KalimaTextureSet(), std::uint16_t{0x24});
    SlotFailureStore store;
    TerrainTiles tiles(store);
    std::vector<std::string> skipped;
    const auto loaded = tiles.Load(missing.Path(), 24, skipped);
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("water2.jpg"), std::string::npos);
    EXPECT_EQ(store.stored.loadCalls, 0);
    EXPECT_FALSE(tiles.LoadedMap());
}
} // namespace

TEST(KalimaSceneTextureTest, TheNativeOptionalAlphaWaterMayBeSkippedWhileTheRequiredWaterStillLoads)
{
    Client::Tests::TemporaryFolder folder("kalima-optional-alpha");
    Client::Tests::WriteTextureSet(folder.Path(), Client::World::KalimaTextureSet(), std::uint16_t{0x23});
    const auto box = Client::World::AcceptBoxTexture();
    const Client::Data::Textures::TextureSetEntry entry{box.path, box.slot, box.sampling};
    Client::Tests::WriteTextureSet(folder.Path(), std::span(&entry, 1));
    SlotFailureStore store;
    TerrainTiles tiles(store);
    std::vector<std::string> skipped;
    const auto loaded = tiles.Load(folder.Path(), 24, skipped);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    // water1 opcional y las32cáusticas de618e1d faltan en este fixture mínimo.
    ASSERT_EQ(skipped.size(), 33u);
    EXPECT_NE(skipped[0].find("water1.tga"), std::string::npos);
    EXPECT_FALSE(store.stored.Find(TextureSlot{0x23}));
    EXPECT_TRUE(store.stored.Find(TextureSlot{0x24}));
    tiles.Release();
}
