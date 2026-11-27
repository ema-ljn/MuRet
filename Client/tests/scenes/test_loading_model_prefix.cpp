#include "Scenes/LoadingScene.h"

#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "Crt/CrtRandom.h"
#include "Input/InputState.h"
#include "Models/Cloth.h"
#include "Assets/FaunaModels.h"
#include "Assets/GameModels.h"
#include "Scenes/SceneContext.h"
#include "Kernel/WorldState.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"
#include "support/TemporaryFolder.h"
#include "Session/ModuleStates.h"

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
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::Session::ModuleStates states;
    Client::Models::ClothWorld cloth{.random = Client::Crt::CrtRandom(1)};
    const Client::Scenes::SceneContext context{.dataRoot = folder.Path(),
                                               .textures = textures,
                                               .log = log,
                                               .cloth = &cloth,
                                               .world = world.get(),
                                               .states = &states};
    Client::Assets::GameModels models;
    Client::Scenes::LoadingScene first(context, &models);
    first.Step(Client::Input::InputState{});
    EXPECT_TRUE(first.WantsToQuit());
    EXPECT_EQ(states.assets.fauna.PrefixOffset(), std::optional<std::uint16_t>{std::uint16_t{41}});
    EXPECT_NE(states.assets.fauna.PrefixModel(), nullptr);
    EXPECT_EQ(cloth.random.State(), 0x0029e2c0u);

    Client::Scenes::LoadingScene second(context, &models);
    second.Step(Client::Input::InputState{});
    EXPECT_TRUE(second.WantsToQuit());
    EXPECT_EQ(states.assets.fauna.PrefixOffset(), std::optional<std::uint16_t>{std::uint16_t{41}});
    EXPECT_EQ(cloth.random.State(), 0x0029e2c0u);
}

TEST(LoadingModelPrefixTest, KeepsTheZeroChoiceWithoutInventingAPrecedingConstructedModel)
{
    Client::Tests::TemporaryFolder folder("loading-model-prefix-zero");
    Client::Tests::FakeTextureStore textures;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::Session::ModuleStates states;
    Client::Models::ClothWorld cloth{.random = Client::Crt::CrtRandom(302)};
    const Client::Scenes::SceneContext context{.dataRoot = folder.Path(),
                                               .textures = textures,
                                               .log = log,
                                               .cloth = &cloth,
                                               .world = world.get(),
                                               .states = &states};
    Client::Assets::GameModels models;
    Client::Scenes::LoadingScene scene(context, &models);
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.WantsToQuit());
    EXPECT_EQ(states.assets.fauna.PrefixOffset(), std::optional<std::uint16_t>{std::uint16_t{0}});
    EXPECT_EQ(states.assets.fauna.PrefixModel(), nullptr);
    const auto afterChoice = cloth.random.State();
    Client::Scenes::LoadingScene reentry(context, &models);
    reentry.Step(Client::Input::InputState{});
    EXPECT_EQ(cloth.random.State(), afterChoice);
}
} // namespace
