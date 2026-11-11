#include "Scenes/LoadingScene.h"

#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "Input/InputState.h"
#include "Render/Models/Cloth.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameModels.h"
#include "Scenes/SceneContext.h"
#include "Scenes/WorldState.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace
{
TEST(LoadingModelPrefixTest, PublishesTheCrtChoiceBeforeARequiredFileFailsAndKeepsItOnReentry)
{
    Client::Tests::TemporaryFolder folder("loading-model-prefix");
    Client::Tests::FakeTextureStore textures;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    auto world = std::make_unique<Client::Scenes::WorldState>();
    Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom(1)};
    const Client::Scenes::SceneContext context{
        .dataRoot = folder.Path(), .textures = textures, .log = log, .cloth = &cloth, .world = world.get()};
    Client::Scenes::GameModels models;
    Client::Scenes::LoadingScene first(context, &models);
    first.Step(Client::Input::InputState{});
    EXPECT_TRUE(first.WantsToQuit());
    EXPECT_EQ(world->FaunaResources().PrefixOffset(), std::optional<std::uint16_t>{std::uint16_t{41}});
    EXPECT_NE(world->FaunaResources().PrefixModel(), nullptr);
    EXPECT_EQ(cloth.random.State(), 0x0029e2c0u);

    Client::Scenes::LoadingScene second(context, &models);
    second.Step(Client::Input::InputState{});
    EXPECT_TRUE(second.WantsToQuit());
    EXPECT_EQ(world->FaunaResources().PrefixOffset(), std::optional<std::uint16_t>{std::uint16_t{41}});
    EXPECT_EQ(cloth.random.State(), 0x0029e2c0u);
}

TEST(LoadingModelPrefixTest, KeepsTheZeroChoiceWithoutInventingAPrecedingConstructedModel)
{
    Client::Tests::TemporaryFolder folder("loading-model-prefix-zero");
    Client::Tests::FakeTextureStore textures;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    auto world = std::make_unique<Client::Scenes::WorldState>();
    Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom(302)};
    const Client::Scenes::SceneContext context{
        .dataRoot = folder.Path(), .textures = textures, .log = log, .cloth = &cloth, .world = world.get()};
    Client::Scenes::GameModels models;
    Client::Scenes::LoadingScene scene(context, &models);
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.WantsToQuit());
    EXPECT_EQ(world->FaunaResources().PrefixOffset(), std::optional<std::uint16_t>{std::uint16_t{0}});
    EXPECT_EQ(world->FaunaResources().PrefixModel(), nullptr);
    const auto afterChoice = cloth.random.State();
    Client::Scenes::LoadingScene reentry(context, &models);
    reentry.Step(Client::Input::InputState{});
    EXPECT_EQ(cloth.random.State(), afterChoice);
}
} // namespace
