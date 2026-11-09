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
#include "Render/Models/Cloth.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SessionHarness.h"

#include <gtest/gtest.h>

#include <string>

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

// El F3 10 (0x4c55a0) sigue sin aplicador (T9): error explícito, sin tocar la sesión ni la selección.
TEST(SessionEventApplierTest, TheInventoryIsStillExplicitlyUnavailable)
{
    Applier tested;
    Client::Tests::LoggedInSession online;
    const Client::App::WorldEvent event = Client::Protocol::Character::InventoryList{};

    const auto result = tested.applier.ApplyWorld(event, online.session);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("F3/10"), std::string::npos);
    EXPECT_EQ(online.session.State(), Client::App::SessionState::Idle);
    EXPECT_FALSE(tested.selection.HasReceivedList());
}

// El F3 03 va a 0x4c3940: sin la carpeta de datos, la carga del mapa 0 corta nombrando el .map de
// World1, después de los stats en el registro propio.
TEST(SessionEventApplierTest, TheWorldEntryGoesToTheNativeApplier)
{
    Applier tested;
    Client::Tests::LoggedInSession online;
    Client::Protocol::Character::CharacterSelectResult reply;
    reply.zen = 1234;
    const Client::App::WorldEvent event = reply;

    const auto result = tested.applier.ApplyWorld(event, online.session);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("EncTerrain1.map"), std::string::npos) << result.Error();
    EXPECT_EQ(tested.world.OwnCharacter().zen, 1234u);
    EXPECT_EQ(tested.world.Runtime().map, 0);
    EXPECT_EQ(online.session.State(), Client::App::SessionState::Idle);
}
} // namespace
