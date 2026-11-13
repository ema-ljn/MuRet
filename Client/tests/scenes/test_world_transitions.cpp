#include "Scenes/WorldTransitions.h"

#include "App/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Network/PacketSerials.h"
#include "Protocol/World.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/IWorldTransitionPublisher.h"
#include "Scenes/LoginModels.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SessionEventApplier.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/OldWindows.h"
#include "World/GameCamera.h"

#include "support/Bytes.h"
#include "support/FakeClock.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
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
    ASSERT_EQ(online.session.State(), Client::App::SessionState::InWorld);
    online.socket.sent.clear();
    online.socket.reads = {
        Client::Tests::FromTheGameServer(Client::Tests::Bytes({0xc1, 8, 0x1c, 23, 3, 30, 31, 2}), serials),
        Client::Tests::Bytes({0xc1, 8, 0xd3, 0, 5, 40, 41, 0xf0})};
    static_cast<void>(online.session.Poll());
    ASSERT_EQ(online.session.State(), Client::App::SessionState::InWorld);
    EXPECT_EQ(tested.world.Runtime().map, 3);
    const auto& hero = tested.Hero();
    EXPECT_EQ(hero.position.x, 3050.0f);
    EXPECT_EQ(hero.cellX, 40);
    EXPECT_EQ(hero.cellY, 41);
    EXPECT_EQ(hero.direction, 15);
    Client::Network::PacketSerials receivedSerials;
    const auto received = Client::Tests::AsTheGameServerReceives(online.socket.sent, receivedSerials);
    ASSERT_EQ(received.packets.size(), 1u);
    EXPECT_EQ(received.packets[0].Opcode(), 0xf3);
    online.socket.sent.clear();
    online.socket.reads = {
        Client::Tests::FromTheGameServer(Client::Tests::Bytes({0xc1, 8, 0x1c, 25, 0, 133, 127, 4}), serials)};
    static_cast<void>(online.session.Poll());
    ASSERT_EQ(online.session.State(), Client::App::SessionState::InWorld);
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_EQ(tested.Hero().cellX, 133);
    EXPECT_EQ(tested.Hero().position.x, 13350.0f);
    const auto confirmedBack = Client::Tests::AsTheGameServerReceives(online.socket.sent, receivedSerials);
    ASSERT_EQ(confirmedBack.packets.size(), 1u);
    EXPECT_EQ(confirmedBack.packets[0].Opcode(), 0xf3);
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
    ASSERT_EQ(online.session.State(), Client::App::SessionState::InWorld);
    const auto firstSlot = tested.world.Runtime().hero;
    tested.world.DrawTail().damage[0].alive = true;
    online.socket.reads = {Client::Tests::FromTheGameServer(Client::Tests::Bytes({0xc1, 5, 0xf1, 2, 1}), serials)};
    static_cast<void>(online.session.Poll());
    ASSERT_EQ(online.session.State(), Client::App::SessionState::WaitingCharacterList);
    EXPECT_EQ(tested.world.Runtime().hero, firstSlot);
    EXPECT_EQ(tested.world.Runtime().map, -1);
    EXPECT_EQ(tested.world.Runtime().orderedScene, Client::Scenes::SceneId::CharacterSelect);
    EXPECT_FALSE(tested.world.DrawTail().damage[0].alive);
    online.socket.reads = {EntryPacket(serials)};
    static_cast<void>(online.session.Poll());
    ASSERT_EQ(online.session.State(), Client::App::SessionState::InWorld);
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
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::World::Teleport{23, 0, 30, 31, 2},
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
    ASSERT_EQ(online.session.State(), Client::App::SessionState::InWorld);
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
        ASSERT_EQ(online.session.State(), Client::App::SessionState::InWorld);
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
    // El NPC de la caja (0x8000), que cierra la CL9.3.
    tested.world.Runtime().panels.delegated.trainer = 1;
    tested.world.Runtime().warpCounter = 9999999;
    tested.Hero().alpha = 1.0f;
    const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::World::Teleport{23, 0, 30, 31, 0},
                                                      Transition::PlayerKey, tested.Resources(), publisher);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x657ab0(0x8000)"), std::string::npos);
    EXPECT_EQ(publisher.confirmations, 1);
    EXPECT_EQ(tested.world.Runtime().warpCounter, 30);
    EXPECT_EQ(tested.Hero().alpha, 1.0f);
    EXPECT_FALSE(tested.world.Map().effects.Effects()[0].alive);
    // Los 20 del prefijo y la consulta del comercio (0x20, 0x657fc1), que 0x657ab0 hace antes que la del 0x8000.
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 20 + 1).State());
}
TEST_F(WorldTransitionsMapsTest, LeavingChaosCastleStopsAtTheCommandsEvenWithEquipmentAvailable)
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
    EXPECT_NE(result.Error().find("0x4c7b34"), std::string::npos);
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
    EXPECT_NE(result.Error().find("0x4c7b34"), std::string::npos);
    EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x9e);
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
// 0x4c7af1..0x4c7b2f: al entrar a Chaos Castle o al salir, el sonido, las ventanas viejas escondidas sin anotarlas
// (0x4906a0 con 1 y 0, Q-C572), sin foco y con el teclado en el juego, y la de amigos suelta (0x4914e0); después corta
// en el objeto de los comandos. La caja se abre a mano: en la CL8 no la abre nadie.
TEST_F(WorldTransitionsMapsTest, TheChaosCastlePrefixHidesTheOldWindowsWithoutNotingThem)
{
    struct Pass
    {
        std::uint8_t from;
        std::uint8_t to;
    };
    for (const Pass pass : {Pass{18, 0}, Pass{23, 2}, Pass{0, 20}})
    {
        SCOPED_TRACE(pass.to);
        Transition tested;
        Publisher publisher;
        tested.dataRoot = m_dataRoot;
        auto& runtime = tested.world.Runtime();
        runtime.map = pass.from;
        runtime.warpCounter = 9999999;
        const Letters letters;
        const auto box = Client::UI::CreateTheOldWindow(
            runtime.oldWindows,
            Client::UI::OldWindowRequest{
                .kind = Client::UI::QuestionCase, .x = -1, .y = -1, .data = "Seguro?", .aux = 0, .flags = 0},
            Client::UI::OldWindowTexts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No"},
            Client::UI::OldWindowMeasure{
                .measure = letters, .dcFont = Client::Render::Text::FontKind::Normal, .screen = {640, 480}});
        ASSERT_TRUE(box.HasValue()) << box.Error();
        runtime.oldWindows.focus = box.Value();
        runtime.chat.input.focus = Client::UI::ChatFocus::Message;
        const auto result = Client::Scenes::ApplyTeleport(Client::Protocol::World::Teleport{23, pass.to, 133, 127, 0},
                                                          Transition::PlayerKey, tested.Resources(), publisher);
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x4c7b34"), std::string::npos) << result.Error();
        EXPECT_EQ(runtime.map, pass.to);
        EXPECT_EQ(runtime.lastSoundId, 0x9e);
        EXPECT_EQ(runtime.oldWindows.windows.at(box.Value()).base.state, Client::UI::HiddenWindow);
        EXPECT_TRUE(runtime.oldWindows.hidden.empty());
        EXPECT_EQ(runtime.oldWindows.focus, 0);
        EXPECT_EQ(runtime.chat.input.focus, Client::UI::ChatFocus::Game);
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
