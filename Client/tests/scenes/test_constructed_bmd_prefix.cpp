#include "Scenes/ConstructedBmdPrefix.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/FaunaMotion.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/FaunaObject.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace
{
using Client::Scenes::ConstructedBmdPrefix;
TEST(ConstructedBmdPrefixTest, UninitializedAndZeroOffsetsDoNotInventAPredecessor)
{
    ConstructedBmdPrefix prefix;
    EXPECT_FALSE(prefix.Offset());
    EXPECT_EQ(prefix.BorrowForSetAction(), nullptr);
    ASSERT_TRUE(prefix.Initialize(0).HasValue());
    ASSERT_TRUE(prefix.Offset());
    EXPECT_EQ(*prefix.Offset(), 0);
    EXPECT_EQ(prefix.BorrowForSetAction(), nullptr);
}
TEST(ConstructedBmdPrefixTest, EveryPositiveNativeOffsetProvesTheConstructorActionGuard)
{
    for (std::uint16_t offset = 1; offset <= 1023; ++offset)
    {
        ConstructedBmdPrefix prefix;
        ASSERT_TRUE(prefix.Initialize(offset).HasValue());
        ASSERT_TRUE(prefix.Offset());
        EXPECT_EQ(*prefix.Offset(), offset);
        const auto* model = prefix.BorrowForSetAction();
        ASSERT_NE(model, nullptr);
        EXPECT_TRUE(model->actions.empty());
        Client::Scenes::FaunaObject fish;
        fish.type = -1;
        fish.action = 2;
        fish.previousAction = 7;
        fish.frame = 9.25f;
        fish.previousFrame = 8.5f;
        Client::Scenes::SetFaunaAction(fish, *model, 1);
        EXPECT_EQ(fish.action, 2);
        EXPECT_EQ(fish.previousAction, 7);
        EXPECT_EQ(fish.frame, 9.25f);
        EXPECT_EQ(fish.previousFrame, 8.5f);
    }
}
TEST(ConstructedBmdPrefixTest, RejectsImpossibleOffsetsWithoutChangingAnExistingLoan)
{
    ConstructedBmdPrefix prefix;
    ASSERT_TRUE(prefix.Initialize(1).HasValue());
    const auto* model = prefix.BorrowForSetAction();
    ASSERT_NE(model, nullptr);
    EXPECT_FALSE(prefix.Initialize(1024).HasValue());
    EXPECT_EQ(prefix.BorrowForSetAction(), model);
    EXPECT_EQ(prefix.Offset(), 1);
    EXPECT_FALSE(prefix.Initialize(0).HasValue());
    EXPECT_EQ(prefix.Offset(), 1);
    EXPECT_TRUE(prefix.Initialize(1).HasValue());
    EXPECT_EQ(prefix.BorrowForSetAction(), model);
}
TEST(ConstructedBmdPrefixTest, TheProductionFaunaOwnerKeepsTheLoanAcrossRebindingAndResourceRelease)
{
    Client::Scenes::FaunaModels resources;
    EXPECT_EQ(resources.PrefixModel(), nullptr);
    EXPECT_FALSE(resources.PrefixOffset());
    ASSERT_TRUE(resources.InitializePrefix(9).HasValue());
    const auto* borrowed = resources.PrefixModel();
    ASSERT_NE(borrowed, nullptr);
    EXPECT_EQ(resources.PrefixOffset(), 9);
    EXPECT_EQ(resources.SkeletonOf(0xffff), nullptr);
    EXPECT_EQ(resources.Find(0xffff), nullptr);
    resources.Bind(nullptr);
    resources.Release();
    EXPECT_EQ(resources.PrefixModel(), borrowed);
    EXPECT_EQ(resources.PrefixOffset(), 9);
}

TEST(ConstructedBmdPrefixTest, ZeroIsAReachableNativeCrtChoice)
{
    Client::Render::Models::CrtRandom random(302);
    const auto offset = static_cast<std::uint16_t>(random.Next() & 1023);
    ASSERT_EQ(offset, 0);
    ConstructedBmdPrefix prefix;
    ASSERT_TRUE(prefix.Initialize(offset).HasValue());
    EXPECT_EQ(prefix.BorrowForSetAction(), nullptr);
}
} // namespace
