#include "CastleSiege/CastleSiegeMap.h"

#include "Data/Terrain/TerrainAttributes.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Terrain/WorldTerrain.h"

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
    auto world = std::make_unique<Client::Kernel::WorldState>();
    for (const std::int32_t map : {-1, 0, 2})
    {
        auto& runtime = world->Runtime();
        runtime.map = map;
        runtime.castleSiege.siegeActive = false;
        runtime.castleSiege.previousSiegeActive = true;
        runtime.castleSiege.siegeMusicTime = 123.0f;
        ASSERT_TRUE(Client::CastleSiege::SetTheSiegeActive(255, *world, nullptr, {}).HasValue());
        EXPECT_TRUE(runtime.castleSiege.siegeActive);
        EXPECT_FALSE(runtime.castleSiege.previousSiegeActive);
        EXPECT_EQ(runtime.castleSiege.siegeMusicTime, 123.0f);
        ASSERT_TRUE(Client::CastleSiege::SetTheSiegeActive(0, *world, nullptr, {}).HasValue());
        EXPECT_FALSE(runtime.castleSiege.siegeActive);
        EXPECT_TRUE(runtime.castleSiege.previousSiegeActive);
        EXPECT_FALSE(runtime.fatalDataMessage.has_value());
    }
}

TEST(CastleSiegeMapTest, TheSiegeMapWithoutATerrainStopsAfterRememberingTheState)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    auto& runtime = world->Runtime();
    runtime.map = 30;
    runtime.castleSiege.siegeActive = false;

    const auto changed = Client::CastleSiege::SetTheSiegeActive(255, *world, nullptr, {});

    ASSERT_FALSE(changed.HasValue());
    EXPECT_NE(changed.Error().find("0x4294c0"), std::string::npos) << changed.Error();
    EXPECT_TRUE(runtime.castleSiege.siegeActive);
}

class CastleSiegeMapFilesTest : public Client::Tests::GoldenDataTest
{
};

// The load and the packets repeat ATT/light even with the same state; the objects and the MAP stay.
TEST_F(CastleSiegeMapFilesTest, RepeatedWarPacketsReloadTheFilesAndPeaceRestoresThem)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    auto loaded = Client::Terrain::LoadWorldTerrain(m_dataRoot, 30);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    Client::Terrain::WorldTerrain terrain = std::move(loaded).Value();
    world->Runtime().map = 30;
    const auto* mapping = terrain.mapping.get();
    const auto* heights = terrain.heights.get();
    for (const std::uint8_t value : std::array<std::uint8_t, 3>{1, 255, 0})
    {
        terrain.attributes->cells[0] = 0xabcd;
        world->Runtime().castleSiege.siegeMusicTime = 999.0f;
        const auto changed = Client::CastleSiege::SetTheSiegeActive(value, *world, &terrain, m_dataRoot);
        ASSERT_TRUE(changed.HasValue()) << changed.Error();
        const auto number = value == 0 ? "31" : "312";
        const auto bytes =
            Client::Tests::ReadFileBytes(m_dataRoot / "world31" / (std::string("EncTerrain") + number + ".att"));
        ASSERT_TRUE(bytes.has_value());
        const auto attributes =
            Client::Data::Terrain::LoadTerrainAttributesOfAnyWorld(std::as_bytes(std::span(*bytes)));
        ASSERT_TRUE(attributes.HasValue());
        EXPECT_EQ(terrain.attributes->cells, attributes.Value()->cells);
        EXPECT_EQ(world->Runtime().castleSiege.siegeMusicTime, 0.0f);
        EXPECT_EQ(terrain.mapping.get(), mapping);
        EXPECT_EQ(terrain.heights.get(), heights);
    }
    EXPECT_TRUE(world->Runtime().castleSiege.previousSiegeActive);
    EXPECT_FALSE(world->Runtime().castleSiege.siegeActive);
}
} // namespace
