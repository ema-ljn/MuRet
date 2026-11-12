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
#include "Protocol/Movement.h"
#include "Protocol/World.h"
#include "Render/Models/Cloth.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SessionHarness.h"
#include "support/SessionPackets.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
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
} // namespace
