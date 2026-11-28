#include "Session/WorldTransitions.h"

#include "Session/SessionState.h"
#include "Audio/Playback.h"
#include "Core/ILogSink.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Network/Packet.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Network/PacketSerials.h"
#include "Protocol/Movement/World.h"
#include "Protocol/Outgoing.h"
#include "Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Session/IWorldTransitionPublisher.h"
#include "FrontEnd/LoginModels.h"
#include "Hud/MapBanner.h"
#include "Actors/PlayerActionSpeeds.h"
#include "Kernel/SceneId.h"
#include "FrontEnd/SelectionInventory.h"
#include "FrontEnd/SelectionState.h"
#include "Session/SessionEventApplier.h"
#include "Session/ModuleStates.h"
#include "Session/WorldEntry.h"
#include "Session/WorldTransitionSupport.h"
#include "support/LetterMeasure.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindows.h"
#include "Camera/GameCamera.h"

#include "support/Bytes.h"
#include "support/FakeClock.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/ItemFiles.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/ItemState.h"
#include "Effects/ModelEffects.h"
#include "Audio/SoundId.h"
#include "Sound/SoundRequests.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"
#include "Chat/ChatRuntime.h"
#include "Npc/NpcWindowState.h"
#include "Ui/PanelRegistry.h"
#include "Movement/GameMovementState.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct Transition
{
    Transition()
    {
        models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.Runtime().hero = 7;
        world.Runtime().heroSlot = 7;
        world.Runtime().map = 0;
        states.terrain.current.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        states.terrain.current.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        Hero() = Client::Tests::UnarmedPlayer();
        world.OwnCharacter().ClearEquipmentForEntry();
        Hero().alive = true;
        Hero().key = PlayerKey;
        Hero().type = Client::Tables::PlayerSkeletonModel;
        Hero().pet.model = Client::Kernel::NoItemModel;
    }
    Client::Kernel::ActorState& Hero()
    {
        return world.Hero();
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
                .log = log,
                .textMeasure = &letters,
                .texts = &texts,
                .states = &states};
    }
    static constexpr std::uint16_t PlayerKey = 33;
    // The welcome line of the F3 03 (0x4c4b60) for the tests that enter through the session.
    Client::Tests::LetterMeasure letters;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({});
    std::filesystem::path dataRoot;
    Client::Kernel::WorldState world;
    Client::Session::ModuleStates states;
    Client::FrontEnd::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth;
    Client::FrontEnd::SingleModelLibrary models;
    Client::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Tables::ItemSetTables itemSets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize));
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

class TransitionPlayback final : public Client::Audio::IPlayback
{
public:
    void PlaySound(std::int32_t id, std::optional<glm::vec3>, bool loop) override
    {
        requests.push_back(loop ? "play-loop:" + std::to_string(id) : "play:" + std::to_string(id));
    }
    void StopSound(std::int32_t id, bool rewind) override
    {
        requests.push_back(rewind ? "stop-rewind:" + std::to_string(id) : "stop:" + std::to_string(id));
    }
    void PlayMusic(std::string_view, bool) override {}
    void StopMusic() override {}
    void SetEffectsVolume(std::uint8_t) override {}
    void SetListener(glm::vec3) override {}

    std::vector<std::string> requests{};
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
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{23, 0, 30, 31, 0},
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
    EXPECT_EQ(tested.world.Runtime().mouse.updateTimeMax, 6);
    EXPECT_EQ(tested.world.Runtime().storm, 0);
    EXPECT_EQ(tested.world.Runtime().selectedTargets[0], -1);
}
// 0x4c830c..0x4c831c: the 1C with a gate (pkt[3]) triggers 0x65dc30 after 0x4f2250, with the map of 0x6c9dd0;
// without a gate (the magic teleport), it does not.
TEST(WorldTransitionsTest, AGateTeleportShowsTheMapBannerAndAMagicOneDoesNot)
{
    auto gate = std::make_unique<Transition>();
    Publisher publisher;
    const auto gated = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{23, 0, 30, 31, 0},
                                                     Transition::PlayerKey, gate->Resources(), publisher);
    ASSERT_TRUE(gated.HasValue()) << gated.Error();
    const Client::Hud::MapBanner& shown = gate->world.Runtime().hud.mapBanner;
    EXPECT_EQ(shown.state, Client::Hud::MapBannerState::FadingIn);
    EXPECT_EQ(shown.shownMap, 0);
    ASSERT_TRUE(shown.pendingImage.has_value());
    EXPECT_EQ(*shown.pendingImage, "Interface\\lorencia.tga");

    auto magic = std::make_unique<Transition>();
    const auto moved = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{0, 0, 30, 31, 0},
                                                     Transition::PlayerKey, magic->Resources(), publisher);
    ASSERT_TRUE(moved.HasValue()) << moved.Error();
    const Client::Hud::MapBanner& quiet = magic->world.Runtime().hud.mapBanner;
    EXPECT_EQ(quiet.state, Client::Hud::MapBannerState::Off);
    EXPECT_FALSE(quiet.pendingImage.has_value());
}

// 0x4c5048..0x4c504e: the F3 04 triggers 0x65dc30 before closing the windows (0x657a90).
TEST(WorldTransitionsTest, RegenShowsTheMapBanner)
{
    auto tested = std::make_unique<Transition>();
    const auto result =
        Client::Scenes::ApplyRegen(Client::Protocol::Movement::Regen{30, 31, 0, 0, 70, 80, 90, 100, 110},
                                   Transition::PlayerKey, tested->Resources());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    const Client::Hud::MapBanner& banner = tested->world.Runtime().hud.mapBanner;
    EXPECT_EQ(banner.state, Client::Hud::MapBannerState::FadingIn);
    EXPECT_EQ(banner.shownMap, 0);
    ASSERT_TRUE(banner.pendingImage.has_value());
    EXPECT_EQ(*banner.pendingImage, "Interface\\lorencia.tga");
}
TEST(WorldTransitionsTest, TeleportReturnsTheCursorToItsInventoryFootprintBeforeMovingTheHero)
{
    auto tested = std::make_unique<Transition>();
    Publisher publisher;
    tested->items = Client::Tests::ItemTableOf({{0x1c00, 1, 1, 0}});
    auto& runtime = tested->world.Runtime();
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Kernel::CursorGrid::Inventory;
    runtime.slot78a6758 = 12 + 18;
    runtime.cursorItem.id = 0x1c00;
    runtime.cursorItem.occupied = 1;
    runtime.cursorItem.durability = 5;
    runtime.cursorItem.gridColumn = 2;
    runtime.cursorItem.gridRow = 2;
    tested->Hero().cellX = 10;
    tested->Hero().cellY = 11;

    auto result = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{0, 0, 30, 31, 2},
                                                Transition::PlayerKey, tested->Resources(), publisher);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(runtime.cursorItem.occupied, 0);
    EXPECT_EQ(tested->world.Inventory().cells[18].id, 0x1c00);
    EXPECT_EQ(tested->world.Inventory().cells[18].occupied, 1);
    EXPECT_EQ(tested->world.Inventory().cells[18].durability, 5);
    EXPECT_EQ(tested->Hero().cellX, 30);
    EXPECT_EQ(tested->Hero().cellY, 31);
    EXPECT_EQ(tested->Hero().byte80, 3);
    EXPECT_EQ(publisher.confirmations, 0); // Gate 0 is a Teleport, not the map change with F3 12.
}

TEST(WorldTransitionsTest, CursorDressFailureRetainsTheReturnedItemAndStopsBeforeHeroCellWrites)
{
    auto tested = std::make_unique<Transition>();
    Publisher publisher;
    tested->items = Client::Tests::ItemTableOf({{0x1c00, 1, 1, 0}});
    tested->models = Client::FrontEnd::SingleModelLibrary{}; // Player.bmd is missing right at the return's 0x512a30.
    auto& runtime = tested->world.Runtime();
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Kernel::CursorGrid::Inventory;
    runtime.slot78a6758 = 12 + 18;
    runtime.cursorItem.id = 0x1c00;
    runtime.cursorItem.occupied = 1;
    runtime.cursorItem.gridColumn = 2;
    runtime.cursorItem.gridRow = 2;
    tested->Hero().cellX = 10;
    tested->Hero().cellY = 11;
    const auto before = tested->cloth.random.State();

    auto result = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{23, 0, 30, 31, 2},
                                                Transition::PlayerKey, tested->Resources(), publisher);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("Player.bmd"), std::string::npos) << result.Error();
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(tested->world.Inventory().cells[18].id, 0x1c00);
    EXPECT_EQ(tested->Hero().cellX, 10);
    EXPECT_EQ(tested->Hero().cellY, 11);
    EXPECT_EQ(tested->cloth.random.State(), before);
    EXPECT_EQ(publisher.confirmations, 0);
}

TEST(WorldTransitionsTest, AnotherMapFailureRetainsThePrefixBeforeConfirmation)
{
    Transition tested;
    Publisher publisher;
    tested.world.Actors()[8].alive = true;
    tested.world.Actors()[8].key = 44;
    tested.world.Runtime().warpCounter = 9999999;
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{23, 3, 30, 31, 2},
                                                      Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("EncTerrain4.att"), std::string::npos) << result.Error();
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
    const auto result =
        Client::Scenes::ApplyRegen(Client::Protocol::Movement::Regen{30, 31, 0, 0, 70, 80, 90, 100, 110},
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
TEST(WorldTransitionsTest, TransitionArrivalRetainsTheDurableOwnerAndNativeKey)
{
    Transition tested;
    auto& actor = tested.Hero();
    actor.position = {10, 20, 30};
    actor.angles = {1, 2, 3};
    actor.light = {0.25f, 0.5f, 0.75f};
    Client::Scenes::CreateTransitionArrival(actor, tested.Resources());
    const auto& effect = tested.world.Map().effects.Effects()[0];
    ASSERT_TRUE(effect.alive);
    EXPECT_EQ(effect.type, Client::Effects::ArrivalEffect);
    EXPECT_TRUE(effect.hasOwner);
    EXPECT_EQ(effect.ownerContext, &actor);
    EXPECT_EQ(effect.disturbanceState, 0xffffu);
    EXPECT_EQ(effect.transform.position, actor.position);
    EXPECT_EQ(effect.transform.angles, actor.angles);
    EXPECT_EQ(effect.light, actor.light);
    EXPECT_EQ(effect.life, 20);
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
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{0, 3, 30, 31, 2},
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
// 0x4c7c17..0x4c7c4e: the 1C that changes map, after the z with the new heights, writes the system line "%s%s"
// (0x6c8410) with the new map's name (0x5f0b40) and text 484, with the empty name (0x5767988, from the .bss).
// Without a map change, it does not.
TEST_F(WorldTransitionsMapsTest, ATeleportToAnotherMapWelcomesToIt)
{
    Transition tested;
    Publisher publisher;
    tested.dataRoot = m_dataRoot;
    tested.texts = Client::Tests::MakeTextTable({{30, "<mapa 0>"}, {33, "<mapa 3>"}, {484, " <bienvenida>"}});
    const auto& log = tested.world.Runtime().chat.log.entries;

    const auto same = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{23, 0, 30, 31, 2},
                                                    Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_TRUE(same.HasValue()) << same.Error();
    EXPECT_TRUE(log.empty());

    const auto noria = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{23, 3, 30, 31, 2},
                                                     Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_TRUE(noria.HasValue()) << noria.Error();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log.back().name, "");
    EXPECT_EQ(log.back().text, "<mapa 3> <bienvenida>");
    EXPECT_EQ(log.back().type, Client::Ui::SystemMessage);
}

TEST_F(WorldTransitionsMapsTest, ReentersLorenciaAndNoriaWithTheSameDurableState)
{
    Transition tested;
    Publisher publisher;
    tested.dataRoot = m_dataRoot;
    const auto first = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{23, 3, 30, 31, 2},
                                                     Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_TRUE(first.HasValue()) << first.Error();
    EXPECT_EQ(tested.world.Runtime().map, 3);
    EXPECT_EQ(tested.Hero().key, Transition::PlayerKey);
    EXPECT_EQ(publisher.confirmations, 1);
    EXPECT_EQ(tested.world.Map().effects.Effects()[0].type, 322);
    const auto back = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{25, 0, 133, 127, 4},
                                                    Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_TRUE(back.HasValue()) << back.Error();
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_EQ(tested.Hero().cellX, 133);
    EXPECT_EQ(publisher.confirmations, 2);
    const auto again = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{23, 3, 32, 33, 0},
                                                     Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_TRUE(again.HasValue()) << again.Error();
    EXPECT_EQ(tested.world.Runtime().map, 3);
    EXPECT_EQ(publisher.confirmations, 3);
}
std::vector<std::byte> EntryPacket(Client::Network::PacketSerials& serials)
{
    std::vector<std::byte> reply(0x34);
    reply[0] = std::byte{0xc1};
    reply[1] = std::byte{0x34};
    reply[2] = std::byte{0xf3};
    reply[3] = std::byte{3};
    reply[4] = std::byte{133};
    reply[5] = std::byte{127};
    return Client::Tests::FromTheGameServer(reply, serials);
}
TEST_F(WorldTransitionsMapsTest, AGameServerBatchAppliesTheLoadedMapBeforeTheFollowingHeroEcho)
{
    Transition tested;
    tested.dataRoot = m_dataRoot;
    Client::Scenes::SessionEventApplier applier(tested.Resources());
    Client::Tests::LoggedInSession online(&applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    Client::Network::PacketSerials serials;
    online.socket.reads = {EntryPacket(serials)};
    static_cast<void>(online.session.Poll());
    ASSERT_EQ(online.session.State(), Client::Session::SessionState::InWorld);
    online.socket.sent.clear();
    online.socket.reads = {
        Client::Tests::FromTheGameServer(Client::Tests::Bytes({0xc1, 8, 0x1c, 23, 3, 30, 31, 2}), serials),
        Client::Tests::Bytes({0xc1, 8, 0xd3, 0, 5, 40, 41, 0xf0})};
    static_cast<void>(online.session.Poll());
    ASSERT_EQ(online.session.State(), Client::Session::SessionState::InWorld);
    EXPECT_EQ(tested.world.Runtime().map, 3);
    const auto& hero = tested.Hero();
    EXPECT_EQ(hero.position.x, 3050.0f);
    EXPECT_EQ(hero.cellX, 40);
    EXPECT_EQ(hero.cellY, 41);
    EXPECT_EQ(hero.direction, 15);
    Client::Network::PacketSerials receivedSerials;
    const auto received = Client::Tests::AsTheGameServerReceives(online.socket.sent, receivedSerials);
    ASSERT_EQ(received.packets.size(), 1u);
    EXPECT_EQ(received.packets[0].Opcode(), Client::Network::Opcode{0xf3});
    online.socket.sent.clear();
    online.socket.reads = {
        Client::Tests::FromTheGameServer(Client::Tests::Bytes({0xc1, 8, 0x1c, 25, 0, 133, 127, 4}), serials)};
    static_cast<void>(online.session.Poll());
    ASSERT_EQ(online.session.State(), Client::Session::SessionState::InWorld);
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_EQ(tested.Hero().cellX, 133);
    EXPECT_EQ(tested.Hero().position.x, 13350.0f);
    const auto confirmedBack = Client::Tests::AsTheGameServerReceives(online.socket.sent, receivedSerials);
    ASSERT_EQ(confirmedBack.packets.size(), 1u);
    EXPECT_EQ(confirmedBack.packets[0].Opcode(), Client::Network::Opcode{0xf3});
}
TEST_F(WorldTransitionsMapsTest, AGameServerCanReturnToSelectionAndEnterAgain)
{
    Transition tested;
    tested.dataRoot = m_dataRoot;
    Client::Scenes::SessionEventApplier applier(tested.Resources());
    Client::Tests::LoggedInSession online(&applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    Client::Network::PacketSerials serials;
    online.socket.reads = {EntryPacket(serials)};
    static_cast<void>(online.session.Poll());
    ASSERT_EQ(online.session.State(), Client::Session::SessionState::InWorld);
    const auto firstSlot = tested.world.Runtime().hero;
    tested.world.DrawTail().damage[0].alive = true;
    online.socket.reads = {Client::Tests::FromTheGameServer(Client::Tests::Bytes({0xc1, 5, 0xf1, 2, 1}), serials)};
    static_cast<void>(online.session.Poll());
    ASSERT_EQ(online.session.State(), Client::Session::SessionState::WaitingCharacterList);
    EXPECT_EQ(tested.world.Runtime().hero, firstSlot);
    EXPECT_EQ(tested.world.Runtime().map, -1);
    EXPECT_EQ(tested.world.Runtime().orderedScene, Client::Kernel::SceneId::CharacterSelect);
    EXPECT_FALSE(tested.world.DrawTail().damage[0].alive);
    online.socket.reads = {EntryPacket(serials)};
    static_cast<void>(online.session.Poll());
    ASSERT_EQ(online.session.State(), Client::Session::SessionState::InWorld);
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_TRUE(tested.Hero().alive);
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
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{23, 0, 30, 31, 2},
                                                      Transition::PlayerKey, resources, publisher);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.world.Runtime().movement.lastTeleportMs, 123u);
    EXPECT_EQ(tested.Hero().cellXAccesses, 1);
    EXPECT_EQ(tested.Hero().cellYAccesses, 1);
    EXPECT_EQ(tested.world.Runtime().warpDepth, 1);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 3).State());
}
TEST_F(WorldTransitionsMapsTest, RegenChangesMapsWithoutAcknowledgingOrResettingTheWait)
{
    Transition tested;
    tested.dataRoot = m_dataRoot;
    Client::Scenes::SessionEventApplier applier(tested.Resources());
    Client::Tests::LoggedInSession online(&applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    Client::Network::PacketSerials serials;
    online.socket.reads = {EntryPacket(serials)};
    static_cast<void>(online.session.Poll());
    ASSERT_EQ(online.session.State(), Client::Session::SessionState::InWorld);
    tested.world.Runtime().warpCounter = 42;
    online.socket.sent.clear();
    const auto slot = tested.world.Runtime().hero;
    auto& hero = tested.Hero();
    hero.guildStatus = 5;
    hero.byte369 = 6;
    hero.byte370 = 7;
    hero.pk = 8;
    hero.word1f2 = 0x1234;
    for (const std::uint8_t map : std::array<std::uint8_t, 2>{3, 0})
    {
        std::vector<std::byte> regen(24);
        regen[0] = std::byte{0xc1};
        regen[1] = std::byte{24};
        regen[2] = std::byte{0xf3};
        regen[3] = std::byte{4};
        regen[4] = std::byte{30};
        regen[5] = std::byte{31};
        regen[6] = std::byte{map};
        regen[7] = std::byte{0};
        regen[8] = std::byte{70};
        regen[10] = std::byte{80};
        regen[12] = std::byte{90};
        online.socket.reads = {Client::Tests::FromTheGameServer(regen, serials)};
        static_cast<void>(online.session.Poll());
        ASSERT_EQ(online.session.State(), Client::Session::SessionState::InWorld);
        EXPECT_EQ(tested.world.Runtime().hero, slot);
        EXPECT_EQ(tested.world.Runtime().warpCounter, 42);
        EXPECT_EQ(tested.world.Runtime().map, map);
        EXPECT_EQ(tested.Hero().guildStatus, 5);
        EXPECT_EQ(tested.Hero().word1f2, 0x1234);
        EXPECT_EQ(tested.world.OwnCharacter().life, 70);
    }
    EXPECT_TRUE(online.socket.sent.empty());
}
TEST(WorldTransitionsTest, UnportedOpenWindowsKeepTheConfirmedWaitPrefixAndStopBeforeArrival)
{
    Transition tested;
    Publisher publisher;
    // The event NPC (0x1000), which CL9.5 closes.
    tested.world.Runtime().panels.eventNpc = 1;
    tested.world.Runtime().warpCounter = 9999999;
    tested.Hero().alpha = 1.0f;
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{23, 0, 30, 31, 0},
                                                      Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x657ab0(0x1000)"), std::string::npos) << result.Error();
    EXPECT_EQ(publisher.confirmations, 1);
    EXPECT_EQ(tested.world.Runtime().warpCounter, 30);
    EXPECT_EQ(tested.Hero().alpha, 1.0f);
    EXPECT_FALSE(tested.world.Map().effects.Effects()[0].alive);
    // The prefix's 20 and the trade query (0x20, 0x657fc1), which 0x657ab0 makes before the one for 0x1000.
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 20 + 1).State());
}
TEST_F(WorldTransitionsMapsTest, LeavingChaosCastleResetsTheCommandsAndContinuesToArrival)
{
    Transition tested;
    Publisher publisher;
    tested.dataRoot = m_dataRoot;
    tested.world.Runtime().map = 18;
    tested.world.Runtime().warpCounter = 9999999;
    auto& commands = tested.world.Runtime().panels.delegated;
    commands.commands = 1;
    commands.commandsWindow.target = 23;
    commands.commandsWindow.chosen = 7;
    commands.commandsWindow.states.fill(0xff);
    auto resources = tested.Resources();
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{23, 0, 133, 127, 0},
                                                      Transition::PlayerKey, resources, publisher);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(commands.commands, 0);
    EXPECT_EQ(commands.commandsWindow.target, -1);
    EXPECT_EQ(commands.commandsWindow.chosen, -1);
    EXPECT_EQ(commands.commandsWindow.states, (std::array<std::uint8_t, 12>{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}));
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_EQ(publisher.confirmations, 1);
    EXPECT_EQ(tested.world.Runtime().warpCounter, 30);
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
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{23, 0, 133, 127, 0},
                                                      Transition::PlayerKey, resources, publisher);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().find("0x4c7b34"), std::string::npos) << result.Error();
    EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x9e});
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_EQ(publisher.confirmations, 0);
    EXPECT_EQ(tested.world.Runtime().warpCounter, 9999999);
}
class Letters final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};
TEST(WorldTransitionsTest, TransitionClosersKeepSeniorBusyAsALocalWarningWithoutASocket)
{
    auto tested = std::make_unique<Transition>();
    Letters letters;
    auto texts = Client::Tests::MakeTextTable({{1641, "la caja sigue ocupada"}});
    auto resources = tested->Resources();
    resources.textMeasure = &letters;
    resources.texts = &texts;
    Client::Crt::CrtRandom setupRandom(1);
    Client::FrontEnd::ClearSelectionInventory(tested->world, setupRandom);
    auto& runtime = tested->world.Runtime();
    runtime.panels.delegated.castleSiegeList = 1;
    runtime.npcWindows.chaosBox[0].id = 1;
    runtime.npcWindows.chaosBox[0].occupied = 0; // 661b90 mira type, no occupied.

    auto result = Client::Scenes::CloseTransitionWindows(resources);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(runtime.panels.delegated.castleSiegeList, 1);
    ASSERT_FALSE(runtime.chat.log.entries.empty());
    EXPECT_EQ(runtime.chat.log.entries.back().text, "la caja sigue ocupada");
    EXPECT_EQ(runtime.chat.log.entries.back().type, Client::Ui::ErrorMessage); // 0x65cb89: push 2.
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(1, 2).State());
}

TEST(WorldTransitionsTest, TransitionClosersPreserveTheNpcTransportsFailureBeforeClearingItsPanel)
{
    auto tested = std::make_unique<Transition>();
    auto resources = tested->Resources();
    auto& runtime = tested->world.Runtime();
    runtime.panels.warehouse = 1;
    runtime.npcWindows.grid[0].id = 1;
    int npcPackets = 0;
    int genericPackets = 0;
    resources.sendGamePacket = [&](Client::Protocol::Outgoing) { ++genericPackets; };
    resources.sendNpcWindowPacket = [&](Client::Protocol::Outgoing) -> Client::Core::Expected<void, std::string>
    {
        ++npcPackets;
        return Client::Core::Unexpected(std::string("falló el transporte del NPC"));
    };

    auto result = Client::Scenes::CloseTransitionWindows(resources);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("falló el transporte del NPC"), std::string::npos) << result.Error();
    EXPECT_EQ(npcPackets, 1);
    EXPECT_EQ(genericPackets, 0);
    EXPECT_EQ(runtime.npcWindows.grid[0].id, 0xffff);
    EXPECT_EQ(runtime.panels.warehouse, 1);
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(1, 1).State());
}

// 0x4c7af1..0x4c7b2f: entering or leaving Chaos Castle, the sound, the old windows hidden without noting them
// (0x4906a0 with 1 and 0, Q-C572), no focus and the keyboard in the game, and the friends window released
// (0x4914e0); then it stops at the commands object. The box is opened by hand: in CL8 nobody opens it.
TEST_F(WorldTransitionsMapsTest, TheChaosCastlePrefixHidesTheOldWindowsWithoutNotingThem)
{
    struct Pass
    {
        std::uint8_t from;
        std::uint8_t to;
    };
    for (const Pass pass : {Pass{18, 0}, Pass{23, 2}, Pass{0, 20}, Pass{0, 2}})
    {
        SCOPED_TRACE(pass.to);
        Transition tested;
        TransitionPlayback playback;
        Publisher publisher;
        tested.dataRoot = m_dataRoot;
        auto& runtime = tested.world.Runtime();
        runtime.audio = &playback;
        Client::Sound::SoundRequests playbackRequests(&playback);
        runtime.sounds = &playbackRequests;
        runtime.map = pass.from;
        runtime.warpCounter = 9999999;
        const Letters letters;
        const auto box = Client::Ui::CreateTheOldWindow(
            runtime.oldWindows,
            Client::Ui::OldWindowRequest{
                .kind = Client::Ui::QuestionCase, .x = -1, .y = -1, .data = "Seguro?", .aux = 0, .flags = 0},
            Client::Ui::OldWindowTexts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No"},
            Client::Ui::OldWindowMeasure{
                .measure = letters, .dcFont = Client::Render::Text::FontKind::Normal, .screen = {640, 480}});
        ASSERT_TRUE(box.HasValue()) << box.Error();
        runtime.oldWindows.focus = box.Value();
        runtime.chatInput.focus = Client::Ui::ChatFocus::Message;
        const auto result =
            Client::Scenes::ApplyTeleport(Client::Protocol::Movement::Teleport{23, pass.to, 133, 127, 0},
                                          Transition::PlayerKey, tested.Resources(), publisher);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(runtime.map, pass.to);
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x9e});
        const bool fromChaos = pass.from >= 18 && pass.from <= 23;
        const bool toChaos = pass.to >= 18 && pass.to <= 23;
        const bool crossesChaos = fromChaos || toChaos;
        EXPECT_EQ(runtime.oldWindows.windows.at(box.Value()).base.state, crossesChaos ? Client::Ui::HiddenWindow : 0);
        EXPECT_TRUE(runtime.oldWindows.hidden.empty());
        EXPECT_EQ(runtime.oldWindows.focus, crossesChaos ? 0 : box.Value());
        EXPECT_EQ(runtime.chatInput.focus, crossesChaos ? Client::Ui::ChatFocus::Game : Client::Ui::ChatFocus::Message);
        std::vector<std::string> expected;
        if (crossesChaos)
            expected.push_back("play-loop:158");
        if (!toChaos)
        {
            expected.push_back("stop-rewind:153");
            expected.push_back("stop-rewind:158");
        }
        EXPECT_EQ(playback.requests, expected);
    }
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
    const auto result =
        Client::Scenes::ApplyRegen(Client::Protocol::Movement::Regen{30, 31, 0, 0, 70, 80, 90, 100, 110},
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
