#include "Scenes/WorldEntry.h"

#include "Core/Expected.h"
#include "Scenes/TextureMetadata.h"
#include "Render/TextureSlot.h"
#include "Render/Models/Cloth.h"
#include "Render/ModelBatch.h"
#include "Scenes/GameData.h"
#include "Scenes/GameModels.h"
#include "Scenes/LoadedWorld.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldResources.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/LeafWeather.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"
#include "support/SessionHarness.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <string>
#include <utility>

namespace
{
class LeafWorldLoadingTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        Client::Tests::GoldenDataTest::SetUp();
        auto data = Client::Scenes::LoadGameData(m_dataRoot);
        ASSERT_TRUE(data.HasValue()) << data.Error();
        m_data.emplace(std::move(data.Value()));
        const auto models = m_models.Load(m_dataRoot, m_textures, m_online.log);
        ASSERT_TRUE(models.HasValue()) << models.Error();
        m_world.Runtime().ownRecordDepth = 1;
        m_world.Runtime().warpDepth = 1;
        m_cloth.openCounterAccesses = 1;
        m_world.Runtime().leafWeather = {.target = 7, .current = 4, .phase = 1234};
    }
    Client::Scenes::SceneContext Context(Client::Scenes::GameModels* textureModels)
    {
        return {.dataRoot = m_dataRoot,
                .textures = m_textures,
                .log = m_online.log,
                .cloth = &m_cloth,
                .camera = &m_camera,
                .selection = &m_selection,
                .world = &m_world,
                .worldTextureModels = textureModels,
                .modelBatch = &m_batch};
    }
    Client::Tests::LoggedInSession m_online;
    Client::Tests::FakeTextureStore m_textures;
    Client::Scenes::WorldState m_world{m_textures};
    Client::Scenes::SelectionState m_selection{m_world};
    Client::Render::Models::ClothWorld m_cloth;
    Client::World::GameCamera m_camera;
    Client::Scenes::GameModels m_models;
    Client::Render::ModelBatch m_batch;
    std::optional<Client::Scenes::GameData> m_data;
};
TEST_F(LeafWorldLoadingTest, MissingBorrowedTextureOwnerIsReportedAtTheRealDaviasLoadPoint)
{
    const auto applied =
        Client::Scenes::ApplyWorldEntry({.map = 2}, m_selection, Context(nullptr), *m_data, m_models, m_online.session);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("GameModels"), std::string::npos) << applied.Error();
    EXPECT_TRUE(m_world.Resources()->terrain.IsLoaded());
    EXPECT_EQ(m_world.Runtime().leafWeather.current, 4);
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 1234);
}
TEST_F(LeafWorldLoadingTest, ConflictingBorrowDoesNotPublishThroughASecondRegistry)
{
    Client::Scenes::GameModels other;
    const auto applied =
        Client::Scenes::ApplyWorldEntry({.map = 2}, m_selection, Context(&other), *m_data, m_models, m_online.session);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("GameModels"), std::string::npos) << applied.Error();
    EXPECT_TRUE(m_world.Resources()->terrain.IsLoaded());
    EXPECT_FALSE(other.TextureMetadataOf(100));
}
TEST_F(LeafWorldLoadingTest, RealLoadPublishesTheOrderedDaviasRowsAndSameMapTeleportDoesNotReloadThem)
{
    const std::size_t before = m_textures.calls.size();
    const auto context = Context(&m_models);
    const auto applied =
        Client::Scenes::ApplyWorldEntry({.map = 2}, m_selection, context, *m_data, m_models, m_online.session);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    std::vector<std::uint16_t> leafLoads;
    for (std::size_t i = before; i < m_textures.calls.size(); ++i)
    {
        const auto& call = m_textures.calls[i];
        if (call.kind == Client::Tests::StoreCall::Kind::Load && (call.slot == 100 || call.slot == 101))
            leafLoads.push_back(call.slot);
    }
    EXPECT_EQ(leafLoads, (std::vector<std::uint16_t>{100, 100, 101}));
    const auto first = m_models.TextureMetadataOf(100);
    const auto second = m_models.TextureMetadataOf(101);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    // Headers archivados del asset: TGA32x32/RGBA seguido por JPEG16x16/RGB al mismo100.
    EXPECT_EQ(first->size.x, 16);
    EXPECT_EQ(first->size.y, 16);
    EXPECT_EQ(first->components, 3);
    EXPECT_EQ(second->size.x, 16);
    EXPECT_EQ(second->size.y, 16);
    EXPECT_EQ(second->components, 3);
    EXPECT_EQ(m_models.TextureSlotOf(100), Client::Render::TextureSlot{100});
    EXPECT_EQ(m_models.TextureSlotOf(101), Client::Render::TextureSlot{101});
    const std::size_t afterEntry = m_textures.calls.size();
    const auto warped = Client::Scenes::ApplyTeleport({.gate = 1, .map = 2, .x = 0, .y = 0, .direction = 1},
                                                      m_selection, context, *m_data, m_models, m_online.session);
    ASSERT_TRUE(warped.HasValue()) << warped.Error();
    EXPECT_EQ(m_textures.calls.size(), afterEntry);
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 1234);
}

TEST_F(LeafWorldLoadingTest, LorenciaPreservesItsRealOzt100AndContinuesAfterBothMissingJpgRequests)
{
    const std::size_t beforeCalls = m_textures.calls.size();
    const std::size_t beforeLines = m_online.sink.lines.size();
    m_world.Runtime().leafSelector = -7;
    const auto context = Context(&m_models);
    const auto applied =
        Client::Scenes::ApplyWorldEntry({.map = 0}, m_selection, context, *m_data, m_models, m_online.session);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    std::vector<std::uint16_t> uploaded;
    for (std::size_t i = beforeCalls; i < m_textures.calls.size(); ++i)
    {
        const auto& call = m_textures.calls[i];
        if (call.kind == Client::Tests::StoreCall::Kind::Load && (call.slot == 100 || call.slot == 101))
            uploaded.push_back(call.slot);
    }
    EXPECT_EQ(uploaded, (std::vector<std::uint16_t>{100}));
    std::vector<std::string> missing;
    for (std::size_t i = beforeLines; i < m_online.sink.lines.size(); ++i)
    {
        const auto& line = m_online.sink.lines[i];
        if (line.find("World1\\leaf01.jpg") != std::string::npos)
            missing.emplace_back("leaf01.jpg");
        if (line.find("World1\\leaf02.jpg") != std::string::npos)
            missing.emplace_back("leaf02.jpg");
    }
    EXPECT_EQ(missing, (std::vector<std::string>{"leaf01.jpg", "leaf02.jpg"}));
    const auto first = m_models.TextureMetadataOf(100);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->components, 4); // World1 sólo tiene leaf01.OZT; ningún JPEG exitoso lo reemplaza.
    EXPECT_EQ(m_models.TextureSlotOf(100), Client::Render::TextureSlot{100});
    EXPECT_FALSE(m_models.TextureMetadataOf(101));
    EXPECT_FALSE(m_models.TextureSlotOf(101));
    EXPECT_EQ(m_world.Runtime().leafSelector, -7);
    EXPECT_EQ(m_world.Runtime().leafWeather.current, 4);
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 1234);
    const std::size_t afterEntry = m_textures.calls.size();
    const auto warped = Client::Scenes::ApplyTeleport({.gate = 1, .map = 0, .x = 0, .y = 0, .direction = 1},
                                                      m_selection, context, *m_data, m_models, m_online.session);
    ASSERT_TRUE(warped.HasValue()) << warped.Error();
    EXPECT_EQ(m_textures.calls.size(), afterEntry);
}

TEST_F(LeafWorldLoadingTest, MapChangesKeepTheLastSuccessful101AndTheCanonicalLeafState)
{
    const auto context = Context(&m_models);
    m_world.Runtime().leafSelector = -7;
    ASSERT_TRUE(Client::Scenes::ApplyWorldEntry({.map = 2}, m_selection, context, *m_data, m_models, m_online.session)
                    .HasValue());
    const auto previous101 = m_models.TextureMetadataOf(101);
    ASSERT_TRUE(previous101);
    ASSERT_EQ(previous101->components, 3);
    auto* const canonicalPool = m_world.Resources()->leaves.All().data();
    m_world.Resources()->leaves.All()[0].alive = 0x80;
    const auto lorencia = Client::Scenes::ApplyTeleport({.gate = 1, .map = 0, .x = 0, .y = 0, .direction = 1},
                                                        m_selection, context, *m_data, m_models, m_online.session);
    ASSERT_TRUE(lorencia.HasValue()) << lorencia.Error();
    ASSERT_TRUE(m_models.TextureMetadataOf(100));
    EXPECT_EQ(m_models.TextureMetadataOf(100)->components, 4);
    // El JPG101 ausente de World1 no borra la última fila exitosa heredada de World3.
    ASSERT_TRUE(m_models.TextureMetadataOf(101));
    EXPECT_EQ(m_models.TextureMetadataOf(101)->components, 3);
    EXPECT_EQ(m_models.TextureMetadataOf(101)->size, previous101->size);
    EXPECT_EQ(m_models.TextureSlotOf(101), Client::Render::TextureSlot{101});
    EXPECT_EQ(m_world.Resources()->leaves.All().data(), canonicalPool);
    EXPECT_EQ(m_world.Resources()->leaves.All()[0].alive, 0);
    EXPECT_EQ(m_world.Runtime().leafSelector, -7);
    EXPECT_EQ(m_world.Runtime().leafWeather.current, 4);
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 1234);
    const auto davias = Client::Scenes::ApplyTeleport({.gate = 1, .map = 2, .x = 0, .y = 0, .direction = 1},
                                                      m_selection, context, *m_data, m_models, m_online.session);
    ASSERT_TRUE(davias.HasValue()) << davias.Error();
    ASSERT_TRUE(m_models.TextureMetadataOf(100));
    EXPECT_EQ(m_models.TextureMetadataOf(100)->components, 3);
    EXPECT_EQ(m_world.Resources()->leaves.All().data(), canonicalPool);
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 1234);
}

TEST_F(LeafWorldLoadingTest, PendingStateCutsBeforeBorrowEvacuationReleaseOrAnyNewLoad)
{
    const auto context = Context(&m_models);
    ASSERT_TRUE(Client::Scenes::ApplyWorldEntry({.map = 2}, m_selection, context, *m_data, m_models, m_online.session)
                    .HasValue());
    const auto* terrain = m_world.Resources()->terrain.Terrain();
    const std::size_t calls = m_textures.calls.size();
    m_batch.ApplyState({.texture = std::nullopt, .blend = Client::Render::ModelBlend::Additive});
    const auto serial = m_batch.StateSerial();
    const auto failed =
        Client::Scenes::ApplyWorldEntry({.map = 2}, m_selection, context, *m_data, m_models, m_online.session);
    ASSERT_FALSE(failed.HasValue());
    EXPECT_EQ(m_world.Resources()->terrain.Terrain(), terrain);
    EXPECT_EQ(m_selection.Terrain(), terrain);
    EXPECT_EQ(m_textures.calls.size(), calls);
    EXPECT_EQ(m_batch.StateSerial(), serial);
    EXPECT_FALSE(m_batch.Commands().empty());
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 1234);
}
TEST_F(LeafWorldLoadingTest, AnOpenPrimitiveIsNotClosedOrFlushedToPermitResourceMutation)
{
    const auto context = Context(&m_models);
    ASSERT_TRUE(Client::Scenes::ApplyWorldEntry({.map = 2}, m_selection, context, *m_data, m_models, m_online.session)
                    .HasValue());
    const auto* terrain = m_world.Resources()->terrain.Terrain();
    const std::size_t calls = m_textures.calls.size();
    ASSERT_TRUE(m_batch.BeginPrimitive().HasValue());
    const auto failed =
        Client::Scenes::ApplyWorldEntry({.map = 2}, m_selection, context, *m_data, m_models, m_online.session);
    ASSERT_FALSE(failed.HasValue());
    EXPECT_EQ(m_textures.calls.size(), calls);
    EXPECT_EQ(m_world.Resources()->terrain.Terrain(), terrain);
    EXPECT_TRUE(m_batch.HasOpenPrimitive());
}
} // namespace
