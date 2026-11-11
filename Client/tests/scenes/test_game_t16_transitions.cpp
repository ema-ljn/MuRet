#include "test_game_t16_support.h"
#include "App/SessionState.h"
#include "Core/Crash/Crc32.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Data/Terrain/TerrainHeights.h"

#include "Scenes/ActorState.h"
#include "Scenes/WorldMap.h"
#include "World/KalimaWorld.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <numeric>
#include <span>
#include <cstdint>
#include <string>
#include <string_view>

namespace
{
using Client::Tests::T16::TeleportBoundary;

class GameT16Transitions : public Client::Tests::T16::SceneTest, public ::testing::WithParamInterface<int>
{
protected:
    void ExpectKalimaResources(int map)
    {
        constexpr std::array<int, 7> KalimaMaps{24, 25, 26, 27, 28, 29, 36};
        const bool expected = std::ranges::find(KalimaMaps, map) != KalimaMaps.end();
        ASSERT_EQ(static_cast<bool>(m_world->Map().kalima), expected);
        if (!expected)
            return;
        EXPECT_EQ(m_world->Map().kalima->Map(), map);
        EXPECT_EQ(m_world->Map().kalima->Waves().planes.size(), 2u);
        EXPECT_EQ(m_world->Map().kalima->Waves().planes[0].size(), 65536u);
        EXPECT_TRUE(m_world->Map().kalima->Height(1000, 1000).has_value());
        EXPECT_TRUE(
            std::ranges::all_of(m_world->Map().kalima->Waves().planes[0], [](auto value) { return value == 0; }));
    }

    void ExpectAliasWorldTen()
    {
        const auto& loaded = m_world->Map();
        ASSERT_NE(loaded.terrain.mapping, nullptr);
        ASSERT_NE(loaded.terrain.heights, nullptr);
        Client::Core::Crash::Crc32 first;
        Client::Core::Crash::Crc32 second;
        first.Update(std::as_bytes(std::span(loaded.terrain.mapping->layer1)));
        second.Update(std::as_bytes(std::span(loaded.terrain.mapping->layer2)));
        // Firma independiente de World10: inventario Data de map-sweep-2026-11-10.
        EXPECT_EQ(first.Value(), 0xd7978eebu);
        EXPECT_EQ(second.Value(), 0xdeab7e4eu);
        EXPECT_EQ(std::accumulate(loaded.terrain.heights->cells.begin(), loaded.terrain.heights->cells.end(), 0.0),
                  9888340.0 * 1.5);
        EXPECT_EQ(loaded.objectRecords.size(), 564u);
    }

    void ExpectDataMessage(int map)
    {
        if (map == 5)
        {
            EXPECT_EQ(m_world->Runtime().fatalDataMessage, "Data\\World6\\TerrainHeight.OZB file not found.");
        }
        else if (map == 35)
        {
            EXPECT_EQ(m_world->Runtime().fatalDataMessage, "Data\\World36\\EncTerrain36.att file not found.");
        }
        else
        {
            EXPECT_FALSE(m_world->Runtime().fatalDataMessage.has_value());
        }
    }
};

TEST_P(GameT16Transitions, EncryptedTeleportEntersAndReturnsOrNamesItsExactBoundary)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    const auto hero = m_world->Runtime().hero;
    const auto boundary = TeleportBoundary(map);
    Teleport(map);
    const int effective = map == 32 ? 9 : map;
    EXPECT_EQ(m_world->Runtime().map, effective);
    EXPECT_EQ(m_world->Runtime().hero, hero);
    const auto sent = Sent();
    ASSERT_NO_FATAL_FAILURE(ExpectDataMessage(map));
    ASSERT_NO_FATAL_FAILURE(ExpectKalimaResources(effective));
    if (map == 32)
    {
        ASSERT_NO_FATAL_FAILURE(ExpectAliasWorldTen());
    }
    if (!boundary.empty())
    {
        ASSERT_NE(m_online->session.State(), Client::App::SessionState::InWorld);
        ASSERT_TRUE(m_pollFailure.has_value());
        EXPECT_NE(m_pollFailure->find(boundary), std::string::npos) << *m_pollFailure;
        EXPECT_TRUE(sent.packets.empty());
        RecordProperty("scene_coverage", "blocked before scene: " + std::string(boundary));
        return;
    }
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    ASSERT_EQ(sent.packets.size(), 1u);
    EXPECT_EQ(sent.packets[0].Bytes().size(), 4u);
    EXPECT_EQ(sent.packets[0].Opcode(), 0xf3);
    EXPECT_EQ(sent.packets[0].Bytes()[3], std::byte{0x12});
    EXPECT_EQ(m_world->Runtime().warpCounter, 30);
    EXPECT_EQ(Hero().cellX, Client::Tests::T16::HomeX);
    EXPECT_EQ(Hero().cellY, Client::Tests::T16::HomeY);
    ASSERT_NE(m_world->Map().terrain.heights, nullptr);
    ASSERT_NE(m_world->Map().terrain.attributes, nullptr);
    Teleport(0);
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    EXPECT_EQ(m_world->Runtime().map, 0);
    ASSERT_NO_FATAL_FAILURE(ExpectKalimaResources(0));
    EXPECT_EQ(m_world->Runtime().hero, hero);
    const auto back = Sent();
    ASSERT_EQ(back.packets.size(), 1u);
    EXPECT_EQ(back.packets[0].Bytes()[3], std::byte{0x12});
    RecordProperty("scene_coverage", "1C round trip; walking and drawing checked separately");
}

TEST_P(GameT16Transitions, RegenUsesRealMapDataWithoutSendingTeleportDone)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    m_world->Runtime().warpCounter = 42;
    const auto slot = m_world->Runtime().hero;
    Hero().guildStatus = 5;
    Hero().byte369 = 6;
    Hero().byte370 = 7;
    Hero().pk = 8;
    Hero().word1f2 = 0x1234;
    Regen(map);
    EXPECT_EQ(m_world->Runtime().map, map == 32 ? 9 : map);
    EXPECT_EQ(m_world->Runtime().hero, slot);
    EXPECT_EQ(m_world->Runtime().warpCounter, 42);
    EXPECT_TRUE(Sent().packets.empty());
    const bool loadBoundary = map == 5 || map == 30 || map == 34 || map == 35;
    ASSERT_NO_FATAL_FAILURE(ExpectDataMessage(map));
    ASSERT_NO_FATAL_FAILURE(ExpectKalimaResources(map == 32 ? 9 : map));
    if (map == 32)
    {
        ASSERT_NO_FATAL_FAILURE(ExpectAliasWorldTen());
    }
    if (loadBoundary)
    {
        ASSERT_EQ(m_online->session.State(), Client::App::SessionState::Failed);
        ASSERT_TRUE(m_pollFailure.has_value());
        EXPECT_EQ(*m_pollFailure, TeleportBoundary(map));
        EXPECT_EQ(m_online->session.Failure(), Client::App::SessionFailure::ApplicationFailed);
        RecordProperty("scene_coverage", "F304 blocked at " + std::string(TeleportBoundary(map)));
        return;
    }
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    EXPECT_EQ(Hero().guildStatus, 5);
    EXPECT_EQ(Hero().byte369, 6);
    EXPECT_EQ(Hero().byte370, 7);
    EXPECT_EQ(Hero().pk, 8);
    EXPECT_EQ(Hero().word1f2, 0x1234);
    EXPECT_EQ(m_world->OwnCharacter().life, 70);
    EXPECT_EQ(m_world->OwnCharacter().mana, 80);
    EXPECT_EQ(m_world->OwnCharacter().bp, 90);
    Regen(0);
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    EXPECT_EQ(m_world->Runtime().map, 0);
    ASSERT_NO_FATAL_FAILURE(ExpectKalimaResources(0));
    EXPECT_EQ(m_world->Runtime().warpCounter, 42);
    EXPECT_TRUE(Sent().packets.empty());
}

INSTANTIATE_TEST_SUITE_P(AllMaps0Through36, GameT16Transitions, ::testing::Range(0, 37),
                         [](const ::testing::TestParamInfo<int>& mapParameter)
                         { return "Map" + std::to_string(mapParameter.param); });
} // namespace
