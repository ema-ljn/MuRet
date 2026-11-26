#include "Scenes/CastleSiegeMap.h"

#include "Data/Terrain/TerrainAttributes.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/WorldTerrain.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace
{
TEST(CastleSiegeMapTest, OtherMapsKeepTheirMusicAndOnlyRememberBothSiegeStates)
{
    auto world = std::make_unique<Client::Scenes::WorldState>();
    for (const std::int32_t map : {-1, 0, 2})
    {
        auto& runtime = world->Runtime();
        runtime.map = map;
        runtime.castleSiege.siegeActive = false;
        runtime.castleSiege.previousSiegeActive = true;
        runtime.castleSiege.siegeMusicTime = 123.0f;
        ASSERT_TRUE(Client::Scenes::SetTheSiegeActive(255, *world, {}).HasValue());
        EXPECT_TRUE(runtime.castleSiege.siegeActive);
        EXPECT_FALSE(runtime.castleSiege.previousSiegeActive);
        EXPECT_EQ(runtime.castleSiege.siegeMusicTime, 123.0f);
        ASSERT_TRUE(Client::Scenes::SetTheSiegeActive(0, *world, {}).HasValue());
        EXPECT_FALSE(runtime.castleSiege.siegeActive);
        EXPECT_TRUE(runtime.castleSiege.previousSiegeActive);
        EXPECT_FALSE(runtime.fatalDataMessage.has_value());
    }
}

class CastleSiegeMapFilesTest : public Client::Tests::GoldenDataTest
{
};

// La carga y los paquetes repiten ATT/luz incluso con el mismo estado; los objetos y elMAP quedan.
TEST_F(CastleSiegeMapFilesTest, RepeatedWarPacketsReloadTheFilesAndPeaceRestoresThem)
{
    auto world = std::make_unique<Client::Scenes::WorldState>();
    auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, 30);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    world->Map().terrain = std::move(loaded).Value();
    world->Runtime().map = 30;
    const auto* mapping = world->Map().terrain.mapping.get();
    const auto* heights = world->Map().terrain.heights.get();
    for (const std::uint8_t value : std::array<std::uint8_t, 3>{1, 255, 0})
    {
        world->Map().terrain.attributes->cells[0] = 0xabcd;
        world->Runtime().castleSiege.siegeMusicTime = 999.0f;
        const auto changed = Client::Scenes::SetTheSiegeActive(value, *world, m_dataRoot);
        ASSERT_TRUE(changed.HasValue()) << changed.Error();
        const auto number = value == 0 ? "31" : "312";
        const auto bytes =
            Client::Tests::ReadFileBytes(m_dataRoot / "world31" / (std::string("EncTerrain") + number + ".att"));
        ASSERT_TRUE(bytes.has_value());
        const auto attributes =
            Client::Data::Terrain::LoadTerrainAttributesOfAnyWorld(std::as_bytes(std::span(*bytes)));
        ASSERT_TRUE(attributes.HasValue());
        EXPECT_EQ(world->Map().terrain.attributes->cells, attributes.Value()->cells);
        EXPECT_EQ(world->Runtime().castleSiege.siegeMusicTime, 0.0f);
        EXPECT_EQ(world->Map().terrain.mapping.get(), mapping);
        EXPECT_EQ(world->Map().terrain.heights.get(), heights);
    }
    EXPECT_TRUE(world->Runtime().castleSiege.previousSiegeActive);
    EXPECT_FALSE(world->Runtime().castleSiege.siegeActive);
}
} // namespace
