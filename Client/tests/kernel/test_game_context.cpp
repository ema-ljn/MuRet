#include "Kernel/GameContext.h"

#include "Kernel/WorldState.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <utility>

namespace
{
using Client::Kernel::GameContext;

TEST(GameContextTest, TheHeroIsTheFirstActorUntilOneIsNamed)
{
    GameContext context;
    EXPECT_EQ(context.HeroIndex(), std::size_t{0});
    EXPECT_EQ(&context.Hero(), &context.Actors()[0]);
}

TEST(GameContextTest, SetHeroMakesThatActorTheHero)
{
    GameContext context;
    context.SetHero(5);
    EXPECT_EQ(&context.Hero(), &context.Actors()[5]);
    EXPECT_EQ(context.Runtime().hero, std::size_t{5});
}

TEST(GameContextTest, ThePoolHoldsTheOriginalsFourHundredAndOneActors)
{
    const GameContext context;
    EXPECT_EQ(context.Actors().size(), std::size_t{401});
}

TEST(GameContextTest, TheWorldStateForwardsTheSharedCoreToItsContext)
{
    Client::Kernel::WorldState world;
    GameContext& context = world.Context();
    EXPECT_EQ(world.Actors().data(), context.Actors().data());
    EXPECT_EQ(&world.OwnCharacter(), &context.OwnCharacter());
    EXPECT_EQ(&world.Inventory(), &context.Inventory());
    EXPECT_EQ(&world.Map(), &context.Map());
    EXPECT_EQ(&world.Runtime(), &context.Runtime());
}

TEST(GameContextTest, TheWorldStatesHeroIsItsContextsHero)
{
    Client::Kernel::WorldState world;
    world.Context().SetHero(3);
    EXPECT_EQ(&world.Hero(), &world.Context().Hero());
    EXPECT_EQ(&std::as_const(world).Hero(), &world.Actors()[3]);
}
} // namespace
