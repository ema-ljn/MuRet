#include "Assets/ConstructedBmdPrefix.h"

#include "Crt/CrtRandom.h"
#include "Models/Skeleton.h"
#include "Fauna/FaunaMotion.h"
#include "Assets/FaunaModels.h"
#include "Fauna/FaunaObject.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace
{
using Client::Assets::ConstructedBmdPrefix;
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
        Client::Fauna::FaunaObject fish;
        fish.model = -1;
        fish.action = 2;
        fish.previousAction = 7;
        fish.frame = 9.25f;
        fish.previousFrame = 8.5f;
        Client::Fauna::SetFaunaAction(fish, *model, 1);
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
    Client::Assets::FaunaModels resources;
    EXPECT_EQ(resources.PrefixModel(), nullptr);
    EXPECT_FALSE(resources.PrefixOffset());
    ASSERT_TRUE(resources.InitializePrefix(9).HasValue());
    const auto* borrowed = resources.PrefixModel();
    ASSERT_NE(borrowed, nullptr);
    EXPECT_EQ(resources.PrefixOffset(), 9);
    EXPECT_EQ(resources.SkeletonOf(Client::Models::ModelId{0xffff}), nullptr);
    EXPECT_EQ(resources.Find(Client::Models::ModelId{0xffff}), nullptr);
    resources.Bind(nullptr);
    resources.Release();
    EXPECT_EQ(resources.PrefixModel(), borrowed);
    EXPECT_EQ(resources.PrefixOffset(), 9);
}

TEST(ConstructedBmdPrefixTest, ZeroIsAReachableNativeCrtChoice)
{
    Client::Crt::CrtRandom random(302);
    const auto offset = static_cast<std::uint16_t>(random.Next() & 1023);
    ASSERT_EQ(offset, 0);
    ConstructedBmdPrefix prefix;
    ASSERT_TRUE(prefix.Initialize(offset).HasValue());
    EXPECT_EQ(prefix.BorrowForSetAction(), nullptr);
}
} // namespace
