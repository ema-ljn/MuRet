#include "Network/Xor32Packet.h"
#include "Scenes/SessionEventApplier.h"

#include "App/CharacterEvent.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WorldEvent.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Protocol/CastleSiege/CastleSiege.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Protocol/Items/InventoryList.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Chat/Messages.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Pets/PetOrders.h"
#include "Protocol/Skills/Skill.h"
#include "Protocol/Movement/World.h"
#include "Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Scenes/ViewportPacketRing.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"

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
// El aplicador con lo suyo: sin datos (dataRoot vacío) y con Player.bmd.
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
                                                      .scene = [this] { return scene; }})
    {
        models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
    }

    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Models::ClothWorld cloth;
    Client::Scenes::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    std::optional<Client::Scenes::SceneId> scene;
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
    EXPECT_EQ(tested->world.Runtime().lastSoundId, 0x98);
    EXPECT_EQ(tested->world.DrawTail().joints.front().ownerContext, &tested->world.Actors()[7]);
    EXPECT_EQ(online.session.State(), Client::App::SessionState::LoggedIn);
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

// El F3 10 va a 0x4c55a0: con la cantidad en 0, la inicialización común vacía el equipo del registro y las
// dos grillas y cierra el registro con sus 3.848 rand(), sin tocar la sesión ni la selección.

TEST(SessionEventApplierTest, AnEmptyInventoryGoesToTheNativeApplier)
{
    auto owned = std::make_unique<Applier>();
    Applier& tested = *owned;
    Client::Tests::LoggedInSession online;
    tested.world.OwnCharacter().equipment[3].id = 7;
    tested.world.Inventory().cells[5].id = 7;
    tested.world.Inventory().secondCells[2].id = 7;
    const Client::App::WorldEvent event = Client::Protocol::Character::InventoryList{};

    const auto result = tested.applier.ApplyWorld(event, online.session);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[3].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(tested.world.Inventory().cells[5].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(tested.world.Inventory().secondCells[2].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
    EXPECT_EQ(online.session.State(), Client::App::SessionState::Idle);
    EXPECT_FALSE(tested.selection.HasReceivedList());
}

// Sin cifrar, 0x4c55a0 hace lo mismo antes de mirar el flag (0x4c56e2), y nada después en el mundo.

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
    EXPECT_EQ(tested.world.OwnCharacter().equipment[3].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(tested.world.Inventory().cells[5].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
}

// El F3 03 va a 0x4c3940: sin datos, el MAP no abre y la carga alcanza el ATT de World1
// (Q-C180), después de los stats en el registro propio.

TEST(SessionEventApplierTest, TheWorldEntryGoesToTheNativeApplier)
{
    auto owned = std::make_unique<Applier>();
    Applier& tested = *owned;
    Client::Tests::LoggedInSession online;
    Client::Protocol::Character::CharacterSelectResult reply;
    reply.zen = 1234;
    const Client::App::WorldEvent event = reply;

    const auto result = tested.applier.ApplyWorld(event, online.session);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("EncTerrain1.att"), std::string::npos) << result.Error();
    EXPECT_EQ(tested.world.OwnCharacter().zen, 1234u);
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_EQ(online.session.State(), Client::App::SessionState::Idle);
}

// El F1 02 con 1 cifrado va a la vuelta de 0x4c2710: deja la orden de la escena 4 y el mundo en −1, y
// cierra el registro propio con sus 3.848 rand(), sin tocar la sesión ni la selección.

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
    EXPECT_EQ(tested.world.Runtime().orderedScene, Client::Scenes::SceneId::CharacterSelect);
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_TRUE(tested.world.Runtime().drawingEnabled);
    EXPECT_EQ(tested.cloth.random.State(), 1u);
    EXPECT_TRUE(tested.selection.HasReceivedList());
    const auto completed = tested.applier.ApplyWorld(Client::App::SelectionWorldReset{}, online.session);
    ASSERT_TRUE(completed.HasValue()) << completed.Error();
    EXPECT_EQ(tested.world.Runtime().map, -1);
    EXPECT_FALSE(tested.world.Runtime().drawingEnabled);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
    EXPECT_EQ(online.session.State(), Client::App::SessionState::Idle);
    EXPECT_FALSE(tested.selection.HasReceivedList());
}

TEST(SessionEventApplierTest, WalkAndDamageReachTheirAppliersWithoutFallingThroughToF303)
{
    auto owned = std::make_unique<Applier>();
    Applier& tested = *owned;
    Client::Tests::LoggedInSession online;
    const Client::App::WorldEvent walk = Client::Protocol::Movement::Walk{42, {30, 31}, 15};
    const Client::App::WorldEvent damage = Client::Protocol::Movement::Damage{42, false, 50, 0, false};
    EXPECT_TRUE(tested.applier.ApplyWorld(walk, online.session).HasValue());
    EXPECT_TRUE(tested.applier.ApplyWorld(damage, online.session).HasValue());
}
// 07 Q-C1240: el 22, el 23 y el 2C cortos llegan a lo que sueltan (0x4d10b9, 0x4d13e5 y 0x4deb0e), sin caer en el
// F3 03.

TEST(SessionEventApplierTest, TheSkillsReachTheirAppliers)
{
    auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online;
    const Client::App::WorldEvent summon = Client::Protocol::Skill::SummonLife{0x42};
    const Client::App::WorldEvent cancel = Client::Protocol::Skill::MagicCancel{0x1b, 0x0777};
    const Client::App::WorldEvent attack = Client::Protocol::Skill::MagicAttack{0x13, 0x0777, 0x0888, false};
    EXPECT_TRUE(tested->applier.ApplyWorld(summon, online.session).HasValue());
    EXPECT_EQ(tested->world.Runtime().hud.summonLife, 0x42);
    EXPECT_TRUE(tested->applier.ApplyWorld(cancel, online.session).HasValue());
    EXPECT_TRUE(tested->applier.ApplyWorld(attack, online.session).HasValue());
    // El A8 sin el cuervo del dueño no hace nada.
    const Client::App::WorldEvent raven = Client::Protocol::Pet::PetAttack{1, 0x0777, 0x0888};
    EXPECT_TRUE(tested->applier.ApplyWorld(raven, online.session).HasValue());
    // F3 11 sin Skill.bmd corta en la cuenta.
    const Client::App::WorldEvent list =
        Client::Protocol::Skill::SkillList{.count = 0xfe, .list = 0, .slot = 0, .skill = 0x11, .entries = {}};
    const auto counted = tested->applier.ApplyWorld(list, online.session);
    ASSERT_FALSE(counted.HasValue());
    EXPECT_NE(counted.Error().find("0x4c54ed"), std::string::npos) << counted.Error();
}

// 0x4cb750: el 18 de otro llega a ApplyActionReply, y el C1 18 de sentarse que dispara (0x4f2250) sale por la sesión.

TEST(SessionEventApplierTest, TheActionOfAnotherSitsThroughTheSession)
{
    auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    Client::Scenes::ActorState& other = tested->world.Actors()[12];
    other = Client::Tests::UnarmedPlayer();
    other.alive = true;
    other.key = 0x1234;
    other.flags7c = 0x200000;
    const std::size_t sent = online.socket.sent.size();
    const Client::App::WorldEvent event =
        Client::Protocol::Movement::Action{.key = 0x1234, .direction = 3, .action = 0x7a};

    ASSERT_TRUE(tested->applier.ApplyWorld(event, online.session).HasValue());
    EXPECT_EQ(other.animation.action, 0xca);
    const std::vector<std::byte> action(online.socket.sent.begin() + static_cast<std::ptrdiff_t>(sent),
                                        online.socket.sent.end());
    const auto plain = Client::Network::Xor32Decode(Client::Tests::PacketOf(action));
    EXPECT_TRUE(Client::Tests::SameBytes(plain.Bytes(), Client::Tests::Bytes({0xc1, 5, 0x18, 3, 0x82})));
}
// 0x4dd92c: un 94 de tipo 0 sin los números del Muto (el original los lee del buffer de la recepción) se descarta con
// un aviso en el log, después de lo que el original hizo antes, y la sesión sigue (CL9 hito 11, como Q-C778).
TEST(SessionEventApplierTest, ADiscardedEventPacketIsLoggedAndTheSessionGoesOn)
{
    auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    const Client::App::WorldEvent event =
        Client::Protocol::Events::EventPacket{Client::Protocol::Events::EventChipInfo{.type = 0, .count = 3}};

    const auto result = tested->applier.ApplyWorld(event, online.session);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested->world.Runtime().panels.field78abb00, 1);
    ASSERT_EQ(tested->sink.lines.size(), 1u);
    EXPECT_EQ(tested->sink.levels[0], Client::Core::LogLevel::Warning);
    EXPECT_NE(tested->sink.lines[0].find("0x4dd92c"), std::string::npos) << tested->sink.lines[0];
}

// 0x4c6098: en la escena 2, el 00 pide la lista por el GameServer sin mirar el paquete (CL8 hito 3).

TEST(SessionEventApplierTest, The00InTheLoginAsksForTheListThroughTheGameServer)
{
    auto owned = std::make_unique<Applier>();
    Applier& tested = *owned;
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    const std::size_t sent = online.socket.sent.size();
    Client::Scenes::SessionEventApplier inTheLogin{Client::Scenes::WorldEntryResources{
        .world = tested.world,
        .selection = tested.selection,
        .dataRoot = {},
        .cloth = tested.cloth,
        .camera = tested.camera,
        .cameraInput = tested.input,
        .models = tested.models,
        .log = tested.log,
        .scene = [] { return std::optional<Client::Scenes::SceneId>(Client::Scenes::SceneId::Login); }}};
    const Client::App::WorldEvent event = Client::Protocol::Messages::Chat{};

    ASSERT_TRUE(inTheLogin.ApplyWorld(event, online.session).HasValue());
    const std::vector<std::byte> request(online.socket.sent.begin() + static_cast<std::ptrdiff_t>(sent),
                                         online.socket.sent.end());
    EXPECT_EQ(request, Client::Tests::ListRequest());
    EXPECT_EQ(tested.cloth.random.State(), Client::Crt::CrtRandom{}.State());
}
// CL9 hito 12: los paquetes del castillo llegan a su aplicador, y lo que manda el B2 02 con 1 sale por el GameServer de
// la sesión antes del mensaje, que sin medidor corta.
TEST(SessionEventApplierTest, TheCastlePacketsReachTheirApplierAndSendThroughTheSession)
{
    namespace Castle = Client::Protocol::CastleSiege;
    const auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    const Client::App::WorldEvent count = Castle::CastleSiegePacket{Castle::KillCount{.value = 6}};
    ASSERT_TRUE(tested->applier.ApplyWorld(count, online.session).HasValue());
    EXPECT_EQ(tested->world.Actors()[tested->world.Runtime().hero].byte372, 6);

    const std::size_t sent = online.socket.sent.size();
    const Client::App::WorldEvent giveUp = Castle::CastleSiegePacket{Castle::GiveUpResult{.result = 1}};
    const auto result = tested->applier.ApplyWorld(giveUp, online.session);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x555190"), std::string::npos);
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
    const Client::App::WorldEvent event = Client::Protocol::World::Teleport{23, 0, 30, 31, 0};
    const auto result = tested.applier.ApplyWorld(event, online.session);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x5be150"), std::string::npos);
}

// 0x4e3e98..0x4e3f1d: el dispatcher copia el 1C a la cola de 0x77f27b4, la misma de los 12 (Q-C801), antes de mirar el
// flag de cifrado. Con la sesión de verdad y este aplicador, un 1C sin cifrar, que se rechaza sin leerlo, ocupa una
// ranura.

TEST(SessionEventApplierTest, ATeleportTakesOneSlotOfThePacketRing)
{
    const auto tested = std::make_unique<Applier>();
    const auto online = std::make_unique<Client::Tests::LoggedInSession>(&tested->applier);
    ASSERT_NO_FATAL_FAILURE(online->LogIn());
    const auto teleport = Client::Tests::Bytes({0xc1, 8, 0x1c, 23, 0, 30, 31, 0});
    online->socket.reads = {teleport};
    ASSERT_TRUE(online->session.Poll().HasValue());
    const Client::Scenes::ViewportPacketRing& ring = tested->world.Runtime().viewportPackets;
    ASSERT_EQ(ring.count, 1);
    EXPECT_EQ(ring.sizes[0], 8);
    EXPECT_TRUE(Client::Tests::SameBytes(std::span(ring.slots[0]).first(teleport.size()), teleport));
    EXPECT_EQ(online->session.State(), Client::App::SessionState::LoggedIn);
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
    tested->scene = Client::Scenes::SceneId::CharacterSelect;
    ASSERT_TRUE(tested->applier.ApplyWorld(event, online.session).HasValue());
    EXPECT_TRUE(tested->world.Actors()[5].alive);

    tested->scene = Client::Scenes::SceneId::Game;
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

    EXPECT_EQ(tested->world.Runtime().orderedScene, Client::Scenes::SceneId::Login);
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
    tested->scene = Client::Scenes::SceneId::CharacterSelect;
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
        tested->scene = Client::Scenes::SceneId::Game;
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

TEST(SessionEventApplierTest, TheShortFloorItemRepliesReachTheirReleases)
{
    namespace FloorItems = Client::Protocol::FloorItems;
    const auto tested = std::make_unique<Applier>();
    Client::Tests::LoggedInSession online;
    Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
    runtime.floor.pendingPickUp = 5;
    runtime.floor.pendingDrop = 22;
    runtime.ui.field5767954 = 10;
    const Client::App::WorldEvent picked = FloorItems::ShortPickUpResult{};
    const Client::App::WorldEvent dropped = FloorItems::ShortDropResult{.result = std::uint8_t{0}};
    const Client::App::WorldEvent fruit = FloorItems::ShortStatFruit{};
    EXPECT_TRUE(tested->applier.ApplyWorld(picked, online.session).HasValue());
    EXPECT_TRUE(tested->applier.ApplyWorld(dropped, online.session).HasValue());
    EXPECT_TRUE(tested->applier.ApplyWorld(fruit, online.session).HasValue());
    EXPECT_EQ(runtime.floor.pendingPickUp, -1);
    EXPECT_EQ(runtime.floor.pendingDrop, -1);
    EXPECT_EQ(runtime.ui.field5767954, 0);
}

// 0x4cb750: el 18 de otro llega a ApplyActionReply, y el C1 18 de sentarse que dispara (0x4f2250) sale por la sesión.
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

    ASSERT_EQ(online.session.State(), Client::App::SessionState::ConnectingGameServer);
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
    runtime.siegeBattleUi.clock.hours = 3;
    runtime.siegeBattleUi.clock.minutes = 7;
    runtime.siegeBattleUi.clock.tick = 99;
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
    EXPECT_EQ(runtime.siegeBattleUi.clock.blink, 1);
    EXPECT_EQ(runtime.siegeBattleUi.clock.hours, 3u);
    EXPECT_EQ(runtime.siegeBattleUi.clock.minutes, 7u);
    EXPECT_EQ(runtime.siegeBattleUi.clock.tick, 99u);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
    applied = tested.applier.ApplyWorld(Client::App::MapServerAuthenticationPrepared{}, online.session);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08 + 2).State());
}

} // namespace
