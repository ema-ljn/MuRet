#include "Scenes/WorldTransitions.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Protocol/World.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/IWorldTransitionPublisher.h"
#include "Scenes/LoginModels.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"

#include "support/FakeClock.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
struct Transition
{
    Transition()
    {
        models.Set(Client::Data::Models::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.Runtime().hero = 7;
        world.Runtime().heroSlot = 7;
        world.Runtime().map = 0;
        world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        world.Map().terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        Hero() = Client::Tests::UnarmedPlayer();
        world.OwnCharacter().ClearEquipmentForEntry();
        Hero().alive = true;
        Hero().key = PlayerKey;
        Hero().type = Client::Data::Models::PlayerSkeletonModel;
        Hero().pet.model = Client::Scenes::NoItemModel;
    }
    Client::Scenes::ActorState& Hero()
    {
        return world.Actors()[world.Runtime().hero];
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = world,
                .selection = selection,
                .dataRoot = dataRoot,
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .items = &items,
                .itemSetTables = &itemSets,
                .log = log};
    }
    static constexpr std::uint16_t PlayerKey = 33;
    std::filesystem::path dataRoot;
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth;
    Client::Scenes::SingleModelLibrary models;
    Client::Data::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Data::Tables::ItemSetTables itemSets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

class Publisher final : public Client::Scenes::IWorldTransitionPublisher
{
public:
    void ConfirmTeleport() override
    {
        ++confirmations;
        if (onConfirm)
            onConfirm();
    }
    void RequestCrywolfState() override
    {
        ++crywolf;
    }
    std::function<void()> onConfirm;
    int confirmations = 0;
    int crywolf = 0;
};
TEST(WorldTransitionsTest, SameMapGateClearsOthersAndItemsButPreservesTheHeroAndCamera)
{
    Transition tested;
    Publisher publisher;
    tested.world.Actors()[8].alive = true;
    tested.world.Actors()[8].key = 44;
    tested.world.Actors()[400].alive = true;
    tested.world.Actors()[400].key = 44;
    tested.world.DrawTail().floorItems[3].alive = true;
    tested.world.Runtime().warpCounter = 9999999;
    tested.world.Runtime().storm = 3;
    tested.world.Runtime().selectedTargets.fill(42);
    tested.camera.level = 9;
    tested.Hero().alpha = 1.0f;
    publisher.onConfirm = [&]
    {
        EXPECT_FALSE(tested.world.Actors()[8].alive);
        EXPECT_EQ(tested.Hero().cellX, 30);
        EXPECT_EQ(tested.world.Runtime().warpCounter, 9999999);
        EXPECT_EQ(tested.Hero().alpha, 1.0f);
    };
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::World::Teleport{23, 0, 30, 31, 0},
                                                      Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(publisher.confirmations, 1);
    EXPECT_FALSE(tested.world.DrawTail().floorItems[3].alive);
    EXPECT_FALSE(tested.world.Actors()[8].alive);
    EXPECT_TRUE(tested.world.Actors()[400].alive);
    EXPECT_TRUE(tested.Hero().alive);
    EXPECT_EQ(tested.Hero().cellX, 30);
    EXPECT_EQ(tested.Hero().position.x, 3050.0f);
    EXPECT_EQ(tested.Hero().angles.z, -45.0f);
    EXPECT_EQ(tested.Hero().alpha, 0.0f);
    EXPECT_EQ(tested.world.Runtime().warpCounter, 30);
    EXPECT_EQ(tested.camera.level, 9);
    EXPECT_EQ(tested.world.Runtime().movement.mouseUpdateTimeMax, 6);
    EXPECT_EQ(tested.world.Runtime().storm, 0);
    EXPECT_EQ(tested.world.Runtime().selectedTargets[0], -1);
}
TEST(WorldTransitionsTest, AnotherMapFailureRetainsThePrefixBeforeConfirmation)
{
    Transition tested;
    Publisher publisher;
    tested.world.Actors()[8].alive = true;
    tested.world.Actors()[8].key = 44;
    tested.world.Runtime().warpCounter = 9999999;
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::World::Teleport{23, 3, 30, 31, 2},
                                                      Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("EncTerrain4.map"), std::string::npos) << result.Error();
    EXPECT_EQ(tested.world.Runtime().map, 3);
    EXPECT_FALSE(tested.world.Actors()[8].alive);
    EXPECT_EQ(tested.Hero().position.x, 3050.0f);
    EXPECT_EQ(tested.world.Runtime().warpCounter, 9999999);
    EXPECT_EQ(publisher.confirmations, 0);
}
TEST(WorldTransitionsTest, RegenKeepsItsOwnFieldsAndSlotWithoutTouchingTheWarpCounter)
{
    Transition tested;
    auto& hero = tested.Hero();
    hero.guildStatus = 5;
    hero.byte369 = 6;
    hero.byte370 = 7;
    hero.pk = 8;
    hero.word1f2 = 0x1234;
    tested.world.Runtime().warpCounter = 9999999;
    tested.world.OwnCharacter().strength = 123;
    const auto result = Client::Scenes::ApplyRegen(Client::Protocol::World::Regen{30, 31, 0, 0, 70, 80, 90, 100, 110},
                                                   Transition::PlayerKey, tested.Resources());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(hero.guildStatus, 5);
    EXPECT_EQ(hero.byte369, 6);
    EXPECT_EQ(hero.byte370, 7);
    EXPECT_EQ(hero.pk, 8);
    EXPECT_EQ(hero.word1f2, 0x1234);
    EXPECT_EQ(hero.key, Transition::PlayerKey);
    EXPECT_EQ(hero.cellX, 30);
    EXPECT_EQ(hero.cellY, 31);
    EXPECT_EQ(hero.angles.z, -45.0f);
    EXPECT_EQ(tested.world.Runtime().warpCounter, 9999999);
    EXPECT_EQ(tested.world.Runtime().heroSlot, 7);
    EXPECT_EQ(tested.world.OwnCharacter().life, 70);
    EXPECT_EQ(tested.world.OwnCharacter().mana, 80);
    EXPECT_EQ(tested.world.OwnCharacter().experience, 100u);
    EXPECT_EQ(tested.world.OwnCharacter().zen, 110u);
    EXPECT_EQ(tested.world.OwnCharacter().strength, 123);
    EXPECT_EQ(hero.byte374, 1);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 3 * 0xf08 + 35).State());
}
TEST(WorldTransitionsTest, MagicTeleportRetainsTheCounterAndOtherActorsWithoutAcknowledgement)
{
    Transition tested;
    Publisher publisher;
    tested.world.Runtime().warpCounter = 42;
    tested.world.Runtime().movement.gateRequestBlocked = 1;
    tested.world.OwnCharacter().attackSpeed = 100;
    tested.world.OwnCharacter().magicSpeed = 200;
    tested.world.Actors()[8].alive = true;
    tested.world.Actors()[8].key = 44;
    tested.world.DrawTail().floorItems[0].alive = true;
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::World::Teleport{0, 3, 30, 31, 2},
                                                      Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(publisher.confirmations, 0);
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_EQ(tested.world.Runtime().warpCounter, 42);
    EXPECT_EQ(tested.world.Runtime().movement.gateRequestBlocked, 0);
    EXPECT_TRUE(tested.world.Actors()[8].alive);
    EXPECT_TRUE(tested.world.DrawTail().floorItems[0].alive);
    EXPECT_EQ(tested.Hero().byte80, 3);
    EXPECT_EQ(tested.Hero().animation.frame, 0.0f);
    EXPECT_EQ(tested.world.Map().effects.Effects()[0].type, 0x5c6);
    EXPECT_EQ(tested.world.Map().effects.Effects()[0].life, 10);
    EXPECT_FLOAT_EQ(tested.world.ActionSpeeds().Speed(146, 0.0f), 1.05f);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 16 + 0xf08 + 1).State());
}
class WorldTransitionsMapsTest : public Client::Tests::GoldenDataTest
{
};
TEST_F(WorldTransitionsMapsTest, ReentersLorenciaAndNoriaWithTheSameDurableState)
{
    Transition tested;
    Publisher publisher;
    tested.dataRoot = m_dataRoot;
    const auto first = Client::Scenes::ApplyTeleport(Client::Protocol::World::Teleport{23, 3, 30, 31, 2},
                                                     Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_TRUE(first.HasValue()) << first.Error();
    EXPECT_EQ(tested.world.Runtime().map, 3);
    EXPECT_EQ(tested.Hero().key, Transition::PlayerKey);
    EXPECT_EQ(publisher.confirmations, 1);
    EXPECT_EQ(tested.world.Map().effects.Effects()[0].type, 322);
    const auto back = Client::Scenes::ApplyTeleport(Client::Protocol::World::Teleport{25, 0, 133, 127, 4},
                                                    Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_TRUE(back.HasValue()) << back.Error();
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_EQ(tested.Hero().cellX, 133);
    EXPECT_EQ(publisher.confirmations, 2);
    const auto again = Client::Scenes::ApplyTeleport(Client::Protocol::World::Teleport{23, 3, 32, 33, 0},
                                                     Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_TRUE(again.HasValue()) << again.Error();
    EXPECT_EQ(tested.world.Runtime().map, 3);
    EXPECT_EQ(publisher.confirmations, 3);
}

TEST(WorldTransitionsTest, TeleportTickCountWrapsAndRetainsLogicalAccessDepths)
{
    Transition tested;
    Publisher publisher;
    Client::Tests::FakeClock clock;
    clock.Advance(std::chrono::milliseconds{4294967296LL + 123});
    auto resources = tested.Resources();
    resources.clock = &clock;
    tested.Hero().cellXAccesses = 1;
    tested.Hero().cellYAccesses = 1;
    tested.world.Runtime().warpDepth = 1;
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::World::Teleport{23, 0, 30, 31, 2},
                                                      Transition::PlayerKey, resources, publisher);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.world.Runtime().movement.lastTeleportMs, 123u);
    EXPECT_EQ(tested.Hero().cellXAccesses, 1);
    EXPECT_EQ(tested.Hero().cellYAccesses, 1);
    EXPECT_EQ(tested.world.Runtime().warpDepth, 1);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 3).State());
}

TEST(WorldTransitionsTest, UnportedOpenWindowsKeepTheConfirmedWaitPrefixAndStopBeforeArrival)
{
    Transition tested;
    Publisher publisher;
    tested.world.Runtime().movement.windowsMask = 0x8000;
    tested.world.Runtime().warpCounter = 9999999;
    tested.Hero().alpha = 1.0f;
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::World::Teleport{23, 0, 30, 31, 0},
                                                      Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x657a90"), std::string::npos);
    EXPECT_EQ(publisher.confirmations, 1);
    EXPECT_EQ(tested.world.Runtime().warpCounter, 30);
    EXPECT_EQ(tested.Hero().alpha, 1.0f);
    EXPECT_FALSE(tested.world.Map().effects.Effects()[0].alive);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 20).State());
}
TEST_F(WorldTransitionsMapsTest, LeavingChaosCastleStopsAtTheUiEvenWithEquipmentAvailable)
{
    Transition tested;
    Publisher publisher;
    tested.dataRoot = m_dataRoot;
    tested.world.Runtime().map = 18;
    tested.world.Runtime().warpCounter = 9999999;
    auto resources = tested.Resources();
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::World::Teleport{23, 0, 133, 127, 0},
                                                      Transition::PlayerKey, resources, publisher);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x4906a0"), std::string::npos);
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_EQ(publisher.confirmations, 0);
    EXPECT_EQ(tested.world.Runtime().warpCounter, 9999999);
}
TEST_F(WorldTransitionsMapsTest, LeavingChaosCastleKeepsTheUiPrefixBeforeAnyEquipmentFailure)
{
    Transition tested;
    Publisher publisher;
    tested.dataRoot = m_dataRoot;
    tested.world.Runtime().map = 18;
    tested.world.Runtime().warpCounter = 9999999;
    auto resources = tested.Resources();
    resources.itemSetTables = nullptr;
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::World::Teleport{23, 0, 133, 127, 0},
                                                      Transition::PlayerKey, resources, publisher);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x4906a0"), std::string::npos);
    EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x9e);
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_EQ(publisher.confirmations, 0);
    EXPECT_EQ(tested.world.Runtime().warpCounter, 9999999);
}
TEST(WorldTransitionsTest, RegenRetainsThePublishedAndRebuiltSlotsAsDistinctNativeSources)
{
    Transition tested;
    tested.Hero().pk = 8;
    tested.Hero().word1f2 = 0x1234;
    tested.world.Runtime().heroSlot = 9;
    auto& rebuilt = tested.world.Actors()[9];
    rebuilt.guildStatus = 5;
    rebuilt.byte369 = 6;
    rebuilt.byte370 = 7;
    rebuilt.pk = 99;
    rebuilt.word1f2 = 0x5678;
    tested.world.Actors()[400].alive = true;
    const auto result = Client::Scenes::ApplyRegen(Client::Protocol::World::Regen{30, 31, 0, 0, 70, 80, 90, 100, 110},
                                                   Transition::PlayerKey, tested.Resources());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.world.Runtime().hero, 7u);
    EXPECT_EQ(tested.world.Runtime().heroSlot, 9);
    EXPECT_FALSE(tested.Hero().alive);
    EXPECT_TRUE(rebuilt.alive);
    EXPECT_TRUE(tested.world.Actors()[400].alive);
    EXPECT_EQ(rebuilt.guildStatus, 5);
    EXPECT_EQ(rebuilt.byte369, 6);
    EXPECT_EQ(rebuilt.byte370, 7);
    EXPECT_EQ(rebuilt.pk, 8);
    EXPECT_EQ(rebuilt.word1f2, 0x1234);
    EXPECT_EQ(rebuilt.key, Transition::PlayerKey);
    EXPECT_EQ(rebuilt.byte374, 1);
    EXPECT_EQ(rebuilt.cellX, 30);
    EXPECT_EQ(rebuilt.position.x, 3050.0f);
}
} // namespace
