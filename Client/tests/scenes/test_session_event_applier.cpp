#include "Scenes/SessionEventApplier.h"

#include "App/CharacterEvent.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WorldEvent.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"
#include "Protocol/Messages.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/Movement.h"
#include "Protocol/Pet/PetOrders.h"
#include "Protocol/Skill.h"
#include "Protocol/World.h"
#include "Render/Models/Cloth.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/SceneId.h"
#include "Scenes/ActorState.h"
#include "Scenes/SelectionState.h"
#include "Scenes/ViewportPacketRing.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"

#include "support/Bytes.h"
#include "support/Packets.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SessionHarness.h"
#include "support/SessionPackets.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
// El aplicador con lo suyo: sin datos (dataRoot vacío) y con Player.bmd.
struct Applier
{
    Applier()
    {
        models.Set(Client::Data::Models::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
    }

    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth;
    Client::Scenes::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Scenes::SessionEventApplier applier{Client::Scenes::WorldEntryResources{.world = world,
                                                                                    .selection = selection,
                                                                                    .dataRoot = {},
                                                                                    .cloth = cloth,
                                                                                    .camera = camera,
                                                                                    .cameraInput = input,
                                                                                    .models = models,
                                                                                    .log = log}};
};

TEST(SessionEventApplierTest, AppliesAnEmptyCharacterListImmediately)
{
    Applier tested;
    Client::Tests::LoggedInSession online;
    const Client::App::CharacterEvent event = Client::App::CharacterListReceived{};
    ASSERT_TRUE(tested.applier.ApplyCharacter(event, online.session).HasValue());
    EXPECT_TRUE(tested.selection.HasReceivedList());
}

TEST(SessionEventApplierTest, PropagatesTheActualSelectionConstructionFailure)
{
    Applier tested;
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
    Applier tested;
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
    Applier tested;
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
    Applier tested;
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
    Applier tested;
    Client::Tests::LoggedInSession online;
    tested.world.Runtime().map = 0;
    tested.world.Runtime().drawingEnabled = true;
    const Client::App::WorldEvent event = Client::App::ReturnToSelectionReceived{};

    const auto result = tested.applier.ApplyWorld(event, online.session);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.world.Runtime().orderedScene, Client::Scenes::SceneId::CharacterSelect);
    EXPECT_EQ(tested.world.Runtime().map, -1);
    EXPECT_FALSE(tested.world.Runtime().drawingEnabled);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
    EXPECT_EQ(online.session.State(), Client::App::SessionState::Idle);
    EXPECT_FALSE(tested.selection.HasReceivedList());
}
TEST(SessionEventApplierTest, WalkAndDamageReachTheirAppliersWithoutFallingThroughToF303)
{
    Applier tested;
    Client::Tests::LoggedInSession online;
    const Client::App::WorldEvent walk = Client::Protocol::Movement::Walk{42, {30, 31}, 15};
    const Client::App::WorldEvent damage = Client::Protocol::Movement::Damage{42, false, 50, 0, false};
    EXPECT_TRUE(tested.applier.ApplyWorld(walk, online.session).HasValue());
    EXPECT_TRUE(tested.applier.ApplyWorld(damage, online.session).HasValue());
}
// CL9 hito 4: las skills van a SkillReplies, sin caer en el F3 03.
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
    const auto plain = Client::Network::Crypto::Xor32Decode(Client::Tests::PacketOf(action));
    EXPECT_TRUE(Client::Tests::SameBytes(plain.Bytes(), Client::Tests::Bytes({0xc1, 5, 0x18, 3, 0x82})));
}
// 0x4c6098: en la escena 2, el 00 pide la lista por el GameServer sin mirar el paquete (CL8 hito 3).
TEST(SessionEventApplierTest, The00InTheLoginAsksForTheListThroughTheGameServer)
{
    Applier tested;
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
    EXPECT_EQ(tested.cloth.random.State(), Client::Render::Models::CrtRandom{}.State());
}
TEST(SessionEventApplierTest, TeleportUsesTheCursorGuardBeforeTouchingTheHero)
{
    Applier tested;
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
} // namespace
