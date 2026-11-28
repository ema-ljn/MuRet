#include "Network/PacketSerials.h"
#include "Network/Xor32Packet.h"
#include "Session/SessionEventApplier.h"

#include "Session/CharacterEvent.h"
#include "Session/ServerSession.h"
#include "Session/SessionHandlers.h"
#include "Session/ModuleStates.h"
#include "Session/SessionState.h"
#include "Session/WorldEvent.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Protocol/Items/InventoryList.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Movement/World.h"
#include "Models/Cloth.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "FrontEnd/LoginModels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/SceneId.h"
#include "FrontEnd/SelectionState.h"
#include "Actors/ActorsState.h"
#include "Actors/ViewportPacketRing.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Messenger/LetterModel.h"
#include "Camera/GameCamera.h"
#include "Audio/SoundId.h"

#include "support/Bytes.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/Packets.h"
#include "support/PlayerActors.h"
#include "support/RecordingSessionEventSink.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SessionHarness.h"
#include "support/SessionPackets.h"
#include "support/TemporaryFolder.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
// The applier with what it needs: no data (empty dataRoot) and Player.bmd.
struct Applier
{
    explicit Applier(std::filesystem::path dataRoot = {})
        : applier(Client::Scenes::WorldEntryResources{.world = world,
                                                      .selection = selection,
                                                      .dataRoot = std::move(dataRoot),
                                                      .cloth = cloth,
                                                      .camera = camera,
                                                      .cameraInput = input,
                                                      .models = models,
                                                      .log = log,
                                                      .scene = [this] { return scene; },
                                                      .states = &states})
    {
        models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
    }

    Client::Kernel::WorldState world;
    Client::Session::ModuleStates states;
    Client::FrontEnd::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth;
    Client::FrontEnd::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    std::optional<Client::Kernel::SceneId> scene;
    Client::Scenes::SessionEventApplier applier;
};

TEST(SessionEventApplierTest, TheLevelUpWireReachesTheWorldThroughTheRealSession)
{
    auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online(&tested->applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    tested->world.Runtime().hero = 7;
    auto& own = tested->world.OwnCharacter();
    own.bp = 81;
    own.experience = 123;
    online.socket.reads = {Client::Tests::Bytes({0xc1, 18, 0xf3, 5, 0, 1, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0})};
    static_cast<void>(online.session.Poll());
    EXPECT_EQ(own.level, 256);
    EXPECT_EQ(own.levelUpPoints, 2);
    EXPECT_EQ(own.life, 3);
    EXPECT_EQ(own.mana, 4);
    EXPECT_EQ(own.maxBp, 5);
    EXPECT_EQ(own.bp, 81);
    EXPECT_EQ(own.experience, 123u);
    EXPECT_EQ(own.nextExperience, 173680400u);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x98});
    EXPECT_EQ(tested->world.DrawTail().joints.front().ownerContext, &tested->world.Actors()[7]);
    EXPECT_EQ(online.session.State(), Client::Session::SessionState::LoggedIn);
}

TEST(SessionEventApplierTest, AppliesAnEmptyCharacterListImmediately)
{
    auto owned = std::make_unique<Applier>();
    Applier& tested = *owned;
    Client::Tests::LoggedInSession online;
    const Client::App::CharacterEvent event = Client::App::CharacterListReceived{};
    ASSERT_TRUE(tested.applier.ApplyCharacter(event, online.session).HasValue());
    EXPECT_TRUE(tested.selection.HasReceivedList());
}

TEST(SessionEventApplierTest, PropagatesTheActualSelectionConstructionFailure)
{
    auto owned = std::make_unique<Applier>();
    Applier& tested = *owned;
    Client::Tests::LoggedInSession online;
    const Client::App::CharacterEvent event = Client::App::CharacterCreated{};
    const auto result = tested.applier.ApplyCharacter(event, online.session);
    ASSERT_FALSE(result.HasValue());
    ASSERT_TRUE(tested.selection.Failure().has_value());
    EXPECT_EQ(result.Error(), *tested.selection.Failure());
    EXPECT_EQ(tested.selection.Slots()[0].revision, 0u);
}

// The F3 10 goes to 0x4c55a0: with the count at 0, the common initialization empties the record's equipment and the
// two grids and closes the record with its 3848 rand(), without touching the session or the selection.

TEST(SessionEventApplierTest, AnEmptyInventoryGoesToTheNativeApplier)
{
    auto owned = std::make_unique<Applier>();
    Applier& tested = *owned;
    Client::Tests::LoggedInSession online;
    tested.world.OwnCharacter().equipment[3].id = 7;
    tested.world.Inventory().cells[5].id = 7;
    tested.world.Inventory().secondCells[2].id = 7;
    const Client::App::WorldEvent event = Client::Protocol::Items::InventoryList{};

    const auto result = tested.applier.ApplyWorld(event, online.session);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[3].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(tested.world.Inventory().cells[5].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(tested.world.Inventory().secondCells[2].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
    EXPECT_EQ(online.session.State(), Client::Session::SessionState::Idle);
    EXPECT_FALSE(tested.selection.HasReceivedList());
}

// Unencrypted, 0x4c55a0 does the same before checking the flag (0x4c56e2), and nothing after it in the world.

TEST(SessionEventApplierTest, AClearInventoryRunsOnlyTheCommonInitialization)
{
    auto owned = std::make_unique<Applier>();
    Applier& tested = *owned;
    Client::Tests::LoggedInSession online;
    tested.world.OwnCharacter().equipment[3].id = 7;
    tested.world.Inventory().cells[5].id = 7;
    const Client::App::WorldEvent event = Client::App::ClearInventoryReceived{};

    const auto result = tested.applier.ApplyWorld(event, online.session);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[3].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(tested.world.Inventory().cells[5].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
}

// The F3 03 goes to 0x4c3940: without data, the MAP does not open and the load reaches World1's ATT (Q-C180), after
// the stats in the own record.

TEST(SessionEventApplierTest, TheWorldEntryGoesToTheNativeApplier)
{
    auto owned = std::make_unique<Applier>();
    Applier& tested = *owned;
    Client::Tests::LoggedInSession online;
    Client::Protocol::FrontEnd::CharacterSelectResult reply;
    reply.zen = 1234;
    const Client::App::WorldEvent event = reply;

    const auto result = tested.applier.ApplyWorld(event, online.session);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("EncTerrain1.att"), std::string::npos) << result.Error();
    EXPECT_EQ(tested.world.OwnCharacter().zen, 1234u);
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_EQ(online.session.State(), Client::Session::SessionState::Idle);
}

// The encrypted F1 02 with 1 goes to 0x4c2710's return: it leaves the scene 4 order and the world at −1, and closes
// the own record with its 3848 rand(), without touching the session or the selection.

TEST(SessionEventApplierTest, TheReturnToSelectionGoesToTheNativeApplier)
{
    auto owned = std::make_unique<Applier>();
    Applier& tested = *owned;
    Client::Tests::LoggedInSession online;
    ASSERT_TRUE(tested.applier.ApplyCharacter(Client::App::CharacterListReceived{}, online.session).HasValue());
    ASSERT_TRUE(tested.selection.HasReceivedList());
    tested.world.Runtime().map = 0;
    tested.world.Runtime().drawingEnabled = true;
    const Client::App::WorldEvent event = Client::App::ReturnToSelectionReceived{};

    const auto result = tested.applier.ApplyWorld(event, online.session);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.world.Runtime().orderedScene, Client::Kernel::SceneId::CharacterSelect);
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_TRUE(tested.world.Runtime().drawingEnabled);
    EXPECT_EQ(tested.cloth.random.State(), 1u);
    EXPECT_TRUE(tested.selection.HasReceivedList());
    const auto completed = tested.applier.ApplyWorld(Client::App::SelectionWorldReset{}, online.session);
    ASSERT_TRUE(completed.HasValue()) << completed.Error();
    EXPECT_EQ(tested.world.Runtime().map, -1);
    EXPECT_FALSE(tested.world.Runtime().drawingEnabled);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
    EXPECT_EQ(online.session.State(), Client::Session::SessionState::Idle);
    EXPECT_FALSE(tested.selection.HasReceivedList());
}

TEST(SessionEventApplierTest, TheDamageWireReachesItsActorThroughTheRealSession)
{
    auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online(&tested->applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    auto& other = tested->world.Actors()[3];
    other.alive = true;
    other.key = 42;
    online.socket.reads = {Client::Tests::Bytes({0xc1, 8, 0xd7, 0, 42, 0, 50, 0})};
    ASSERT_TRUE(online.session.Poll().HasValue());
    EXPECT_EQ(other.damage, 50);
    EXPECT_EQ(online.session.State(), Client::Session::SessionState::LoggedIn);
}

// The routed rows (Session/Route.h) take their parts from the applier's world: the 14 through the real session lets go
// of the other player, and the orders the chat windows left go out after it, as after every reception.
TEST(SessionEventApplierTest, TheLeaveWireLetsGoOfAnotherThroughTheRealSession)
{
    auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online(&tested->applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    EXPECT_EQ(&tested->applier.RoutedWorld()->world, &tested->world);
    auto& other = tested->world.Actors()[8];
    other.alive = true;
    other.key = 0x45;
    tested->world.Runtime().oldWindows.chatRooms.orders.push_back(Client::Messenger::ChatRoomOrder{
        .kind = Client::Messenger::ChatRoomOrder::Kind::Ping, .room = 0x0304, .text = {}});
    online.socket.reads = {Client::Tests::Bytes({0xc1, 6, 0x14, 1, 0x80, 0x45})};
    ASSERT_TRUE(online.session.Poll().HasValue());
    EXPECT_FALSE(other.alive);
    EXPECT_TRUE(tested->world.Runtime().oldWindows.chatRooms.orders.empty());
    EXPECT_NE(online.session.State(), Client::Session::SessionState::Failed);
}
// The skills reach their appliers through the real session, in wire order: the F3 11 without Skill.bmd stops at the
// count and fails the session.
TEST(SessionEventApplierTest, TheSkillsReachTheirAppliers)
{
    auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online(&tested->applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    Client::Network::PacketSerials serverSerials;
    online.socket.reads = {Client::Tests::Join(
        {Client::Tests::Bytes({0xc1, 5, 0xf3, 0x20, 0x42}), Client::Tests::Bytes({0xc1, 6, 0x1b, 0x1b, 0x07, 0x77}),
         Client::Tests::FromTheGameServer(Client::Tests::Bytes({0xc1, 8, 0x19, 0x13, 0x07, 0x77, 0x08, 0x88}),
                                          serverSerials),
         // The A8 without the owner's raven does nothing.
         Client::Tests::Bytes({0xc1, 9, 0xa8, 0, 1, 0x07, 0x77, 0x08, 0x88}),
         Client::Tests::Bytes({0xc1, 8, 0xf3, 0x11, 0xfe, 0, 0, 0x11})})};

    const auto polled = online.session.Poll();

    ASSERT_FALSE(polled.HasValue());
    EXPECT_NE(polled.Error().find("0x4c54ed"), std::string::npos) << polled.Error();
    EXPECT_EQ(tested->world.Runtime().hud.summonLife, 0x42);
    EXPECT_TRUE(std::ranges::none_of(online.sink.lines, [](const std::string& line)
                                     { return line.find("no llegó cifrada") != std::string::npos; }));
}

// 0x4cb750: another's 18 reaches ApplyActionReply, and the sitting C1 18 it triggers (0x4f2250) goes out through the
// session.

TEST(SessionEventApplierTest, TheActionOfAnotherSitsThroughTheSession)
{
    auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online(&tested->applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    Client::Kernel::ActorState& other = tested->world.Actors()[12];
    other = Client::Tests::UnarmedPlayer();
    other.alive = true;
    other.key = 0x1234;
    other.flags7c = 0x200000;
    const std::size_t sent = online.socket.sent.size();
    online.socket.reads = {Client::Tests::Bytes({0xc1, 7, 0x18, 0x12, 0x34, 3, 0x7a})};

    ASSERT_TRUE(online.session.Poll().HasValue());
    EXPECT_EQ(other.animation.action, 0xca);
    const std::vector<std::byte> action(online.socket.sent.begin() + static_cast<std::ptrdiff_t>(sent),
                                        online.socket.sent.end());
    const auto plain = Client::Network::Xor32Decode(Client::Tests::PacketOf(action));
    EXPECT_TRUE(Client::Tests::SameBytes(plain.Bytes(), Client::Tests::Bytes({0xc1, 5, 0x18, 3, 0x82})));
}
// 0x4dd92c: a 94 of type 0 without the Muto numbers (the original reads them from the receive buffer) is dropped with
// a warning in the log, after what the original did before, and the session goes on (CL9 milestone 11, like Q-C778).
TEST(SessionEventApplierTest, ADiscardedEventPacketIsLoggedAndTheSessionGoesOn)
{
    auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    const Client::Protocol::Events::EventPacket event{Client::Protocol::Events::EventChipInfo{.type = 0, .count = 3}};

    const auto result = Client::Session::ReceiveEventPacket(event, online.session, *tested->applier.RoutedWorld());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested->world.Runtime().panels.field78abb00, 1);
    ASSERT_EQ(tested->sink.lines.size(), 1u);
    EXPECT_EQ(tested->sink.levels[0], Client::Core::LogLevel::Warning);
    EXPECT_NE(tested->sink.lines[0].find("0x4dd92c"), std::string::npos) << tested->sink.lines[0];
}

// 0x4c6098: in scene 2, the 00 asks for the list through the GameServer without looking at the packet (CL8 milestone
// 3).

TEST(SessionEventApplierTest, The00InTheLoginAsksForTheListThroughTheGameServer)
{
    auto owned = std::make_unique<Applier>();
    Applier& tested = *owned;
    Client::Scenes::SessionEventApplier inTheLogin{Client::Scenes::WorldEntryResources{
        .world = tested.world,
        .selection = tested.selection,
        .dataRoot = {},
        .cloth = tested.cloth,
        .camera = tested.camera,
        .cameraInput = tested.input,
        .models = tested.models,
        .log = tested.log,
        .scene = [] { return std::optional<Client::Kernel::SceneId>(Client::Kernel::SceneId::Login); }}};
    Client::Tests::LoggedInSession online(&inTheLogin);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    const std::size_t sent = online.socket.sent.size();
    online.socket.reads = {Client::Tests::Bytes({0xc1, 3, 0x00})};

    ASSERT_TRUE(online.session.Poll().HasValue());
    const std::vector<std::byte> request(online.socket.sent.begin() + static_cast<std::ptrdiff_t>(sent),
                                         online.socket.sent.end());
    EXPECT_EQ(request, Client::Tests::ListRequest());
    EXPECT_EQ(tested.cloth.random.State(), Client::Crt::CrtRandom{}.State());
}
// CL9 milestone 12: the castle packets reach their applier, and what the B2 02 with 1 sends goes out through the
// session's GameServer before the message, which stops without a meter.
TEST(SessionEventApplierTest, TheCastlePacketsReachTheirApplierAndSendThroughTheSession)
{
    const auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online(&tested->applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    const std::size_t sent = online.socket.sent.size();
    online.socket.reads = {Client::Tests::Join(
        {Client::Tests::Bytes({0xc1, 5, 0xb8, 1, 6}), Client::Tests::Bytes({0xc1, 5, 0xb2, 2, 1})})};

    const auto polled = online.session.Poll();

    EXPECT_EQ(tested->world.Hero().byte372, 6);
    ASSERT_FALSE(polled.HasValue());
    EXPECT_NE(polled.Error().find("0x555190"), std::string::npos) << polled.Error();
    const std::vector<std::byte> requests(online.socket.sent.begin() + static_cast<std::ptrdiff_t>(sent),
                                          online.socket.sent.end());
    EXPECT_EQ(requests, (std::vector<std::byte>{std::byte{0xc1}, std::byte{4}, std::byte{0xb2}, std::byte{0x38},
                                                std::byte{0xc1}, std::byte{3}, std::byte{0xb4}}));
}

TEST(SessionEventApplierTest, TeleportUsesTheCursorGuardBeforeTouchingTheHero)
{
    auto owned = std::make_unique<Applier>();
    Applier& tested = *owned;
    Client::Tests::LoggedInSession online;
    tested.world.Runtime().cursorItemCount = 1;
    const auto result = Client::Session::ReceiveTeleport(Client::Protocol::Movement::Teleport{23, 0, 30, 31, 0},
                                                         online.session, *tested.applier.RoutedWorld());
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x5be150"), std::string::npos);
}

// 0x4e3e98..0x4e3f1d: the dispatcher copies the 1C to the queue at 0x77f27b4, the same as the 12's (Q-C801), before
// checking the encryption flag. With the real session and this applier, an unencrypted 1C, rejected without reading
// it, takes a slot.

TEST(SessionEventApplierTest, ATeleportTakesOneSlotOfThePacketRing)
{
    const auto tested = std::make_unique<Applier>();
    const auto online = std::make_unique<Client::Tests::LoggedInSession>(&tested->applier);
    ASSERT_NO_FATAL_FAILURE(online->LogIn());
    const auto teleport = Client::Tests::Bytes({0xc1, 8, 0x1c, 23, 0, 30, 31, 0});
    online->socket.reads = {teleport};
    ASSERT_TRUE(online->session.Poll().HasValue());
    const Client::Actors::ViewportPacketRing& ring = tested->states.actors.viewportPackets;
    ASSERT_EQ(ring.count, 1);
    EXPECT_EQ(ring.sizes[0], 8);
    EXPECT_TRUE(Client::Tests::SameBytes(std::span(ring.slots[0]).first(teleport.size()), teleport));
    EXPECT_EQ(online->session.State(), Client::Session::SessionState::LoggedIn);
}

TEST(SessionEventApplierTest, ThePacketRingNeedsTheModulesStates)
{
    const auto tested = std::make_unique<Applier>();
    Client::Scenes::WorldEntryResources resources = *tested->applier.RoutedWorld();
    resources.states = nullptr;
    Client::Scenes::SessionEventApplier stateless(resources);
    const auto copied = stateless.CopyToPacketRing(Client::Tests::Bytes({0xc1, 8, 0x1c, 23, 0, 30, 31, 0}));
    ASSERT_FALSE(copied.HasValue());
    EXPECT_NE(copied.Error().find("0x4e2b10"), std::string::npos) << copied.Error();
}

TEST(SessionEventApplierTest, ALogoutResponseReleasesOnlyThePendingRequest)
{
    auto tested = std::make_unique<Applier>();
    tested->world.Runtime().ui.logoutRequested = true;
    tested->world.Runtime().drawingEnabled = true;
    tested->world.Runtime().map = 2;

    tested->applier.ClearLogoutRequest();

    EXPECT_FALSE(tested->world.Runtime().ui.logoutRequested);
    EXPECT_TRUE(tested->world.Runtime().drawingEnabled);
    EXPECT_EQ(tested->world.Runtime().map, 2);
}

TEST(SessionEventApplierTest, TheServerListCleanupRunsOnlyInTheGameAndDoesNotResetTheSceneYet)
{
    auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online;
    tested->world.Runtime().map = 0;
    tested->world.Runtime().drawingEnabled = true;
    tested->world.Actors()[5].alive = true;
    tested->world.Actors()[5].key = 5;
    const auto random = tested->cloth.random.State();
    const Client::App::WorldEvent event = Client::App::ServerListLogoutCleanup{};
    tested->scene = Client::Kernel::SceneId::CharacterSelect;
    ASSERT_TRUE(tested->applier.ApplyWorld(event, online.session).HasValue());
    EXPECT_TRUE(tested->world.Actors()[5].alive);

    tested->scene = Client::Kernel::SceneId::Game;
    ASSERT_TRUE(tested->applier.ApplyWorld(event, online.session).HasValue());

    EXPECT_FALSE(tested->world.Actors()[5].alive);
    EXPECT_FALSE(tested->world.Runtime().orderedScene.has_value());
    EXPECT_EQ(tested->world.Runtime().map, 0);
    EXPECT_TRUE(tested->world.Runtime().drawingEnabled);
    EXPECT_EQ(tested->cloth.random.State(), random);
}

TEST(SessionEventApplierTest, TheServerListReturnResetsTheWorldAndOrdersTheLogin)
{
    auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online;
    ASSERT_TRUE(tested->applier.ApplyCharacter(Client::App::CharacterListReceived{}, online.session).HasValue());
    tested->world.Runtime().map = 0;
    tested->world.Runtime().drawingEnabled = true;
    tested->world.OwnCharacter().word3e = 9;
    const Client::App::WorldEvent event = Client::App::ReturnToServerListReceived{};

    ASSERT_TRUE(tested->applier.ApplyWorld(event, online.session).HasValue());

    EXPECT_EQ(tested->world.Runtime().orderedScene, Client::Kernel::SceneId::Login);
    EXPECT_EQ(tested->world.Runtime().map, -1);
    EXPECT_FALSE(tested->world.Runtime().drawingEnabled);
    EXPECT_FALSE(tested->selection.HasReceivedList());
    EXPECT_EQ(tested->world.OwnCharacter().word3e, 0);
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
}

TEST(SessionEventApplierTest, TheServerListReturnAlsoKillsSelectionActorsWithoutClearingFloorItems)
{
    auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online;
    tested->scene = Client::Kernel::SceneId::CharacterSelect;
    tested->world.Actors()[3].alive = true;
    tested->world.Actors()[3].key = 3;
    tested->world.Actors()[4].alive = true;
    tested->world.Actors()[4].key = 0xffff;
    tested->world.DrawTail().floorItems[0].alive = true;
    ASSERT_TRUE(tested->applier.ApplyWorld(Client::App::ServerListLogoutCleanup{}, online.session).HasValue());
    ASSERT_TRUE(tested->world.Actors()[3].alive);

    ASSERT_TRUE(tested->applier.ApplyWorld(Client::App::ReturnToServerListReceived{}, online.session).HasValue());

    EXPECT_FALSE(tested->world.Actors()[3].alive);
    EXPECT_TRUE(tested->world.Actors()[4].alive);
    EXPECT_TRUE(tested->world.DrawTail().floorItems[0].alive);
}

TEST(SessionEventApplierTest, ALogoutDoesNotSkipACursorReturnFailureBeforeReleasingTheWorld)
{
    for (const bool serverList : {false, true})
    {
        SCOPED_TRACE(serverList);
        auto tested = std::make_unique<Applier>();
        Client::Tests::LoggedInSession online;
        tested->scene = Client::Kernel::SceneId::Game;
        tested->world.Runtime().map = 2;
        tested->world.Actors()[3].alive = true;
        tested->world.Actors()[3].key = 3;
        tested->world.Runtime().cursorItemCount = 1;
        tested->world.Runtime().cursorItem.id = 0;
        const Client::App::WorldEvent event = serverList
                                                  ? Client::App::WorldEvent{Client::App::ServerListLogoutCleanup{}}
                                                  : Client::App::WorldEvent{Client::App::ReturnToSelectionReceived{}};

        const auto applied = tested->applier.ApplyWorld(event, online.session);

        EXPECT_FALSE(applied.HasValue());
        EXPECT_TRUE(tested->world.Actors()[3].alive);
        EXPECT_EQ(tested->world.Runtime().cursorItemCount, 1);
        EXPECT_EQ(tested->world.Runtime().map, 2);
        EXPECT_FALSE(tested->world.Runtime().orderedScene.has_value());
    }
}

// 0x4cb750: another's 18 reaches ApplyActionReply, and the sitting C1 18 it triggers (0x4f2250) goes out through the
// session.
TEST(SessionEventApplierTest, EveryPostLoginHelloClearsOnlyGuildKeysAndTheCount)
{
    auto owned = std::make_unique<Applier>();
    auto& tested = *owned;
    Client::Tests::LoggedInSession online;
    auto& guild = tested.world.Runtime().guild;
    guild.tableCount = 2;
    guild.table[0].key = 42;
    guild.table[1999].key = 77;
    guild.table[0].mark[0] = 15;
    const auto before = tested.cloth.random.State();
    const auto applied = tested.applier.ApplyWorld(Client::App::GuildTableReset{}, online.session);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(guild.tableCount, 0);
    for (const auto& entry : guild.table)
        EXPECT_EQ(entry.key, -1);
    EXPECT_EQ(guild.table[0].mark[0], 15);
    EXPECT_EQ(tested.cloth.random.State(), before);
}

TEST(SessionEventApplierTest, TheMapSwitchReconnectsWhenTheMacrosCannotBeSaved)
{
    Client::Tests::TemporaryFolder data{"map-switch-macro-error"};
    ASSERT_TRUE(std::filesystem::create_directory(data.Path() / "Macro.txt"));
    const auto tested = std::make_unique<Applier>(data.Path());
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    online.session.RequestCharacterList();
    online.ReceiveTheList({{0, "char0001"}});
    online.session.ChooseCharacter(std::uint8_t{0});
    ASSERT_NE(online.session.ChosenCharacter(), nullptr);
    online.eventSink.onWorld = [&tested](const Client::App::WorldEvent& event, Client::App::ServerSession& session)
    { return tested->applier.ApplyWorld(event, session); };

    auto wire = Client::Tests::Bytes({0xc1, 0x28, 0xb1, 0, '1', '0', '.', '0', '.', '0', '.', '3'});
    wire.resize(0x28);
    wire[20] = std::byte{0x6f};
    wire[21] = std::byte{0xda};
    const auto previous = online.socket.connects.size();
    const auto before = tested->cloth.random.State();
    online.socket.reads = {std::move(wire)};
    static_cast<void>(online.session.Poll());

    ASSERT_EQ(online.session.State(), Client::Session::SessionState::ConnectingGameServer);
    ASSERT_EQ(online.socket.connects.size(), previous + 1u);
    EXPECT_EQ(online.socket.connects.back().host, "10.0.0.3");
    EXPECT_EQ(online.socket.connects.back().port, 55919);
    ASSERT_FALSE(online.clock.Sleeps().empty());
    EXPECT_EQ(online.clock.Sleeps().back(), std::chrono::milliseconds(20));
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(before, 0xf08).State());
    EXPECT_TRUE(std::ranges::any_of(tested->sink.lines, [](const std::string& line)
                                    { return line.find("Macro.txt") != std::string::npos; }));
}

TEST(SessionEventApplierTest, TheMapJoinResetsTheWorldBeforeTheAuthenticationRandoms)
{
    auto owned = std::make_unique<Applier>();
    auto& tested = *owned;
    Client::Tests::LoggedInSession online;
    auto& runtime = tested.world.Runtime();
    runtime.map = 30;
    runtime.events.soccer.active = true;
    runtime.castleSiege.siegeBattleUi.clock.hours = 3;
    runtime.castleSiege.siegeBattleUi.clock.minutes = 7;
    runtime.castleSiege.siegeBattleUi.clock.tick = 99;
    tested.world.Actors()[1].alive = true;
    tested.world.Actors()[1].key = 123;
    tested.world.Actors()[2].alive = true;
    tested.world.Actors()[2].key = 0xffff;
    auto applied = tested.applier.ApplyWorld(Client::App::MapServerJoined{}, online.session);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_FALSE(tested.world.Actors()[1].alive);
    EXPECT_TRUE(tested.world.Actors()[2].alive);
    EXPECT_EQ(runtime.map, -1);
    EXPECT_FALSE(runtime.events.soccer.active);
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.clock.blink, 1);
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.clock.hours, 3u);
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.clock.minutes, 7u);
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.clock.tick, 99u);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
    applied = tested.applier.ApplyWorld(Client::App::MapServerAuthenticationPrepared{}, online.session);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08 + 2).State());
}

} // namespace
