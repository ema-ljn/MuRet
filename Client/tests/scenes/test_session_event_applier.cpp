#include "Scenes/SessionEventApplier.h"

#include "App/CharacterEvent.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WorldEvent.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "support/SessionHarness.h"

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace
{
TEST(SessionEventApplierTest, AppliesAnEmptyCharacterListImmediately)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Scenes::SessionEventApplier applier(selection, camera, input);
    Client::Tests::LoggedInSession online;
    const Client::App::CharacterEvent event = Client::App::CharacterListReceived{};
    ASSERT_TRUE(applier.ApplyCharacter(event, online.session).HasValue());
    EXPECT_TRUE(selection.HasReceivedList());
}

TEST(SessionEventApplierTest, PropagatesTheActualSelectionConstructionFailure)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Scenes::SessionEventApplier applier(selection, camera, input);
    Client::Tests::LoggedInSession online;
    const Client::App::CharacterEvent event = Client::App::CharacterCreated{};
    const auto result = applier.ApplyCharacter(event, online.session);
    ASSERT_FALSE(result.HasValue());
    ASSERT_TRUE(selection.Failure().has_value());
    EXPECT_EQ(result.Error(), *selection.Failure());
    EXPECT_EQ(selection.Slots()[0].revision, 0u);
}

TEST(SessionEventApplierTest, WorldApplicationIsExplicitlyUnavailableForEntryAndEmptyInventory)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Scenes::SessionEventApplier applier(selection, camera, input);
    Client::Tests::LoggedInSession online;
    const std::array<Client::App::WorldEvent, 2> events{Client::Protocol::Character::CharacterSelectResult{},
                                                        Client::Protocol::Character::InventoryList{}};
    for (const Client::App::WorldEvent& event : events)
    {
        const auto result = applier.ApplyWorld(event, online.session);
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("aplicador de mundo"), std::string::npos);
        EXPECT_EQ(online.session.State(), Client::App::SessionState::Idle);
        EXPECT_FALSE(selection.HasReceivedList());
    }
}
} // namespace
