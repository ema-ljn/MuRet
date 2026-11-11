#include "Scenes/MapLoad.h"

#include "test_game_t16_support.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Scenes/SceneManager.h"
#include "support/Bytes.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/WorldTerrain.h"
#include "support/GoldenDir.h"
#include "support/TemporaryFolder.h"
#include "support/TerrainFiles.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr std::uint32_t Seed = 0x5eed;
class MapDataFailureTest : public Client::Tests::GoldenDataTest
{
protected:
    auto Load(std::uint8_t map, const std::filesystem::path& root)
    {
        m_world->Runtime().map = map;
        return Client::Scenes::LoadMap(map, {.dataRoot = root,
                                             .world = *m_world,
                                             .scene = Client::Scenes::SceneId::Game,
                                             .playerKey = 5,
                                             .random = m_random});
    }
    void CopyWorldOne(const std::filesystem::path& root)
    {
        std::filesystem::create_directories(root / "World1");
        for (const std::string_view name :
             {"EncTerrain1.map", "EncTerrain1.att", "EncTerrain1.obj", "TerrainHeight.OZB", "TerrainLight.OZJ"})
        {
            const auto source = DataFile("World1\\" + std::string(name));
            ASSERT_TRUE(source.has_value());
            std::filesystem::copy_file(*source, root / "World1" / name);
        }
    }

    void Write(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(file.good());
    }

    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
    Client::Render::Models::CrtRandom m_random{Seed};
};

TEST_F(MapDataFailureTest, WorldThirtySixReachesItsMissingAttributesAfterTheMissingMapping)
{
    const auto loaded = Load(35, m_dataRoot);
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("Data\\World36\\EncTerrain36.att"), std::string::npos) << loaded.Error();
    EXPECT_EQ(m_world->Runtime().fatalDataMessage, "Data\\World36\\EncTerrain36.att file not found.");
    EXPECT_FALSE(m_world->Map().terrain.mapping);
    EXPECT_FALSE(m_world->Map().terrain.attributes);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 65536).State());
}

TEST_F(MapDataFailureTest, WorldSixCreatesItsObjectsBeforeTheMissingHeights)
{
    const auto records = Client::World::LoadWorldObjects(m_dataRoot, 5);
    ASSERT_TRUE(records.HasValue()) << records.Error();
    const auto loaded = Load(5, m_dataRoot);
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("TerrainHeight.OZB"), std::string::npos);
    EXPECT_EQ(m_world->Map().objectRecords.size(), records.Value().size());
    EXPECT_EQ(m_world->Map().objects.size(), records.Value().size());
    EXPECT_TRUE(m_world->Map().terrain.mapping);
    EXPECT_TRUE(m_world->Map().terrain.attributes);
    EXPECT_FALSE(m_world->Map().terrain.heights);
    EXPECT_EQ(m_world->Runtime().fatalDataMessage, "Data\\World6\\TerrainHeight.OZB file not found.");
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 65536).State());
}
// El OBJ real de World6 no tiene registros. World1 acredita un prefijo no vacío antes del fallo.
TEST_F(MapDataFailureTest, MissingHeightsPreserveTheCreatedObjectsAndTheRandomPrefix)
{
    const Client::Tests::TemporaryFolder folder("fatal-map-data");
    ASSERT_NO_FATAL_FAILURE(CopyWorldOne(folder.Path()));
    std::filesystem::remove(folder.Path() / "World1" / "TerrainHeight.OZB");
    const auto records = Client::World::LoadWorldObjects(m_dataRoot, 0);
    ASSERT_TRUE(records.HasValue());
    ASSERT_FALSE(records.Value().empty());

    const auto loaded = Load(0, folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(m_world->Runtime().fatalDataMessage, "Data\\World1\\TerrainHeight.OZB file not found.");
    EXPECT_EQ(m_world->Map().objectRecords.size(), records.Value().size());
    EXPECT_EQ(m_world->Map().objects.size(), records.Value().size());
    EXPECT_TRUE(m_world->Map().terrain.mapping);
    EXPECT_TRUE(m_world->Map().terrain.attributes);
    EXPECT_FALSE(m_world->Map().terrain.heights);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 65536).State());
}

TEST_F(MapDataFailureTest, MissingMappingWithValidAttributesDoesNotFabricateTerrainOrRequestQuit)
{
    const Client::Tests::TemporaryFolder folder("missing-map-data");
    ASSERT_NO_FATAL_FAILURE(CopyWorldOne(folder.Path()));
    std::filesystem::remove(folder.Path() / "World1" / "EncTerrain1.map");

    const auto loaded = Load(0, folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("EncTerrain1.map"), std::string::npos);
    EXPECT_FALSE(m_world->Runtime().fatalDataMessage);
    EXPECT_FALSE(m_world->Map().terrain.mapping);
    EXPECT_TRUE(m_world->Map().terrain.attributes);
    EXPECT_TRUE(m_world->Map().objects.empty());
}

TEST_F(MapDataFailureTest, AShortAttributeFileHasNoNativeQuitRequest)
{
    const Client::Tests::TemporaryFolder folder("short-map-data");
    ASSERT_NO_FATAL_FAILURE(CopyWorldOne(folder.Path()));
    const std::vector<std::byte> shortFile(5);
    ASSERT_NO_FATAL_FAILURE(Write(folder.Path() / "World1" / "EncTerrain1.att", shortFile));

    const auto loaded = Load(0, folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("EncTerrain1.att"), std::string::npos);
    EXPECT_FALSE(m_world->Runtime().fatalDataMessage);
    EXPECT_TRUE(m_world->Map().terrain.mapping);
    EXPECT_FALSE(m_world->Map().terrain.attributes);
}

TEST_F(MapDataFailureTest, ShortHeightsHaveNoNativeQuitRequestAndKeepTheObjectPrefix)
{
    const Client::Tests::TemporaryFolder folder("short-map-data");
    ASSERT_NO_FATAL_FAILURE(CopyWorldOne(folder.Path()));
    const std::vector<std::byte> shortFile(5);
    ASSERT_NO_FATAL_FAILURE(Write(folder.Path() / "World1" / "TerrainHeight.OZB", shortFile));

    const auto loaded = Load(0, folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("TerrainHeight.OZB"), std::string::npos);
    EXPECT_FALSE(m_world->Runtime().fatalDataMessage);
    EXPECT_FALSE(m_world->Map().objects.empty());
    EXPECT_FALSE(m_world->Map().terrain.heights);
}

TEST_F(MapDataFailureTest, ARejectedAttributeHeaderRequestsTheNativeDataError)
{
    const Client::Tests::TemporaryFolder folder("corrupt-map-data");
    ASSERT_NO_FATAL_FAILURE(CopyWorldOne(folder.Path()));
    auto plain = Client::Tests::PlainTerrainAttributes(1, Client::Tests::AttributeCells::Bytes);
    plain[0] = std::byte{1};
    const auto encrypted = Client::Tests::EncryptTerrainAttributes(plain);
    ASSERT_NO_FATAL_FAILURE(Write(folder.Path() / "World1" / "EncTerrain1.att", encrypted));

    const auto loaded = Load(0, folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(m_world->Runtime().fatalDataMessage, "Data error");
    EXPECT_NE(loaded.Error().find("EncTerrain1.att"), std::string::npos);
}

TEST_F(MapDataFailureTest, AWrongWorldInTheMappingRequestsTheNativeCorruptionMessage)
{
    const Client::Tests::TemporaryFolder folder("corrupt-map-data");
    ASSERT_NO_FATAL_FAILURE(CopyWorldOne(folder.Path()));
    const auto encrypted = Client::Tests::EncryptK16(Client::Tests::PlainTerrainMapping(2));
    ASSERT_NO_FATAL_FAILURE(Write(folder.Path() / "World1" / "EncTerrain1.map", encrypted));

    const auto loaded = Load(0, folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(m_world->Runtime().fatalDataMessage, "Data\\World1\\EncTerrain1.map file corrupted.");
    EXPECT_NE(loaded.Error().find("es de otro mundo"), std::string::npos);
}

TEST_F(MapDataFailureTest, AWrongWorldInTheAttributesRequestsTheNativeCorruptionMessage)
{
    const Client::Tests::TemporaryFolder folder("corrupt-map-data");
    ASSERT_NO_FATAL_FAILURE(CopyWorldOne(folder.Path()));
    const auto encrypted = Client::Tests::EncryptTerrainAttributes(
        Client::Tests::PlainTerrainAttributes(2, Client::Tests::AttributeCells::Bytes));
    ASSERT_NO_FATAL_FAILURE(Write(folder.Path() / "World1" / "EncTerrain1.att", encrypted));

    const auto loaded = Load(0, folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(m_world->Runtime().fatalDataMessage, "Data\\World1\\EncTerrain1.att file corrupted.");
    EXPECT_NE(loaded.Error().find("es de otro mundo"), std::string::npos);
}

TEST_F(MapDataFailureTest, MissingLightRequestsQuitAfterPublishingHeightsAndObjects)
{
    const Client::Tests::TemporaryFolder folder("missing-map-light");
    ASSERT_NO_FATAL_FAILURE(CopyWorldOne(folder.Path()));
    std::filesystem::remove(folder.Path() / "World1" / "TerrainLight.OZJ");

    const auto loaded = Load(0, folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(m_world->Runtime().fatalDataMessage, "Data\\World1\\TerrainLight.OZJ - File not exist.");
    EXPECT_TRUE(m_world->Map().terrain.heights);
    EXPECT_FALSE(m_world->Map().objects.empty());
    EXPECT_FALSE(m_world->Map().terrain.lighting);
}

TEST_F(MapDataFailureTest, UnsupportedMapErrorsDoNotRequestQuit)
{
    for (const std::uint8_t map : {std::uint8_t{34}, std::uint8_t{55}})
    {
        const auto loaded = Load(map, m_dataRoot);
        ASSERT_FALSE(loaded.HasValue());
        EXPECT_FALSE(m_world->Runtime().fatalDataMessage);
    }
}

TEST_F(MapDataFailureTest, TheFirstFatalMessageSurvivesAnotherLoadAndAnotherFailure)
{
    ASSERT_FALSE(Load(5, m_dataRoot).HasValue());
    const auto first = m_world->Runtime().fatalDataMessage;
    ASSERT_TRUE(first);
    ASSERT_FALSE(Load(35, m_dataRoot).HasValue());
    EXPECT_EQ(m_world->Runtime().fatalDataMessage, first);
    ASSERT_FALSE(Load(55, m_dataRoot).HasValue());
    EXPECT_EQ(m_world->Runtime().fatalDataMessage, first);
    ASSERT_TRUE(Load(0, m_dataRoot).HasValue());
    EXPECT_EQ(m_world->Runtime().fatalDataMessage, first);
}
TEST_F(MapDataFailureTest, MissingObjectsRequestQuitBeforeTheHeightsAreRead)
{
    const Client::Tests::TemporaryFolder folder("missing-map-objects");
    ASSERT_NO_FATAL_FAILURE(CopyWorldOne(folder.Path()));
    std::filesystem::remove(folder.Path() / "World1" / "EncTerrain1.obj");

    const auto loaded = Load(0, folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(m_world->Runtime().fatalDataMessage, "Data\\World1\\EncTerrain1.obj file not found.");
    EXPECT_TRUE(m_world->Map().terrain.attributes);
    EXPECT_FALSE(m_world->Map().terrain.heights);
}

// La recepción real falla antes de Step; el pedido sobrevive a ApplicationFailed y a manager.Poll.
// FakeSocket/FakeTextureStore no acreditan el modal nativo de Window ni su observación en Windows.
class MapDataSessionFailureTest : public Client::Tests::T16::SceneTest, public ::testing::WithParamInterface<int>
{
};

TEST_P(MapDataSessionFailureTest, TheFatalDataRequestSurvivesSessionAndManagerPollFailure)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    const auto reply = Client::Tests::Bytes({0xc1, 8, 0x1c, 1, map, 135, 125, 1});
    m_online->socket.reads = {Client::Tests::FromTheGameServer(reply, m_incoming)};
    Client::Scenes::SceneManager manager({}, m_target, m_clock, [this] { return m_online->session.Poll(); }, m_log);

    manager.Poll();

    ASSERT_TRUE(manager.Failure());
    const std::string path = map == 5 ? "Data\\World6\\TerrainHeight.OZB" : "Data\\World36\\EncTerrain36.att";
    EXPECT_NE(manager.Failure()->find(path), std::string::npos);
    ASSERT_TRUE(m_online->session.Failure());
    EXPECT_EQ(*m_online->session.Failure(), Client::App::SessionFailure::ApplicationFailed);
    EXPECT_EQ(m_world->Runtime().fatalDataMessage, path + " file not found.");
    const auto first = m_world->Runtime().fatalDataMessage;
    manager.Poll();
    EXPECT_EQ(m_world->Runtime().fatalDataMessage, first);
}

INSTANTIATE_TEST_SUITE_P(RealMissingData, MapDataSessionFailureTest, ::testing::Values(5, 35),
                         [](const ::testing::TestParamInfo<int>& value)
                         { return "Map" + std::to_string(value.param); });
} // namespace
