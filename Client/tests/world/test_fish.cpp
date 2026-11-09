#include "World/Fish.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{
using Client::World::FishCopiesWorldPosition;
using Client::World::FishDirectSpawnAllowed;
using Client::World::FishHasModel;
using Client::World::FishMovement;
using Client::World::FishMovementFor;
using Client::World::FishMovementMode;
using Client::World::FishNeedsOscillation;
using Client::World::FishOpenWaterTile;
using Client::World::FishSpawnEventGate;
using Client::World::FishStepCount;

// 447030-contract § Recorrido y § Candidatos; oráculos estáticos, sin captura runtime fabricada.
TEST(FishTest, AllMapFamiliesSelectTheirPersistentPrefix)
{
    constexpr std::array<std::size_t, 37> Counts{3, 3, 3, 3, 3, 3,  3,  10, 10, 3,  3,  3, 3, 3, 3, 3,  3, 3, 3,
                                                 3, 3, 3, 3, 3, 10, 10, 10, 10, 10, 10, 3, 3, 3, 3, 10, 3, 10};
    for (std::size_t map = 0; map < Counts.size(); ++map)
        EXPECT_EQ(FishStepCount(static_cast<std::int32_t>(map)), Counts[map]) << map;
    for (const std::int32_t map : {-1, 54, 55, 263})
        EXPECT_EQ(FishStepCount(map), 3u) << map;
    EXPECT_EQ(FishStepCount(33), 3u) << "33 comparte spawn con8 pero no el límite";
}

TEST(FishTest, LorenciaRequiresWaterSurfaceAndIgnoresTheAttributeWord)
{
    EXPECT_TRUE(FishDirectSpawnAllowed(0, 5, 0xffff));
    EXPECT_TRUE(FishDirectSpawnAllowed(0, 5, 1));
    EXPECT_FALSE(FishDirectSpawnAllowed(0, 4, 0));
    EXPECT_FALSE(FishDirectSpawnAllowed(0, 6, 2));
}

TEST(FishTest, DungeonAndArenaUseTheUnsignedWordThreshold)
{
    for (const std::int32_t map : {1, 6})
    {
        EXPECT_TRUE(FishDirectSpawnAllowed(map, 0, 0));
        EXPECT_TRUE(FishDirectSpawnAllowed(map, 5, 1));
        EXPECT_TRUE(FishDirectSpawnAllowed(map, 0xff, 7));
        EXPECT_FALSE(FishDirectSpawnAllowed(map, 5, 8));
        EXPECT_FALSE(FishDirectSpawnAllowed(map, 5, 0xffff));
    }
}

TEST(FishTest, WaterGateIsExactlyZeroOrTwo)
{
    EXPECT_TRUE(FishOpenWaterTile(0));
    EXPECT_TRUE(FishOpenWaterTile(2));
    for (const std::uint16_t attribute : std::array<std::uint16_t, 5>{1, 3, 7, 8, 0xffff})
        EXPECT_FALSE(FishOpenWaterTile(attribute)) << attribute;
}

TEST(FishTest, OnlyEventMapsPassTheSpawnEventPredicate)
{
    for (const std::int32_t map : {33, 54})
        EXPECT_TRUE(FishSpawnEventGate(map)) << map;
    for (const std::int32_t map : {-1, 0, 8, 24, 34, 55, 263})
        EXPECT_FALSE(FishSpawnEventGate(map)) << map;
}

TEST(FishTest, DirectWaterAdmissionDoesNotCollapseFallbackMaps)
{
    for (const std::int32_t map : {8, 33, 34, 54})
        EXPECT_FALSE(FishDirectSpawnAllowed(map, 0, 0)) << map;
    EXPECT_TRUE(FishDirectSpawnAllowed(7, 0, 0));
    EXPECT_TRUE(FishDirectSpawnAllowed(24, 0, 2));
    EXPECT_TRUE(FishDirectSpawnAllowed(36, 0, 2));
}

TEST(FishTest, AllFishTypesUseSignedRangeAndTimerMovementDispatch)
{
    for (std::int16_t type = 0xb2; type <= 0xbc; ++type)
    {
        EXPECT_EQ(FishMovementFor(type, std::numeric_limits<std::int32_t>::min()), FishMovement::Complete) << type;
        EXPECT_EQ(FishMovementFor(type, 0), FishMovement::Complete) << type;
    }
    EXPECT_EQ(FishMovementFor(0xb1, 1), FishMovement::Complete);
    EXPECT_EQ(FishMovementFor(0xb1, 0), FishMovement::Reduced);
    EXPECT_EQ(FishMovementFor(0x148, 0), FishMovement::Reduced);
    EXPECT_EQ(FishMovementFor(0x148, 1), FishMovement::Complete);
    EXPECT_EQ(FishMovementFor(-1, -1), FishMovement::Reduced);
    EXPECT_EQ(FishMovementFor(0x7fff, 1), FishMovement::Complete);
}

TEST(FishTest, CompleteMovementModeAndOscillationUseTheReadType)
{
    EXPECT_EQ(FishMovementMode(0xb1), 1);
    EXPECT_EQ(FishMovementMode(0x148), 1);
    EXPECT_EQ(FishMovementMode(0xb2), 0);
    EXPECT_TRUE(FishNeedsOscillation(0xb9));
    EXPECT_TRUE(FishNeedsOscillation(0xba));
    EXPECT_FALSE(FishNeedsOscillation(0xb8));
    EXPECT_FALSE(FishNeedsOscillation(-1));
}

TEST(FishTest, PositionCopyUsesPostCollisionTypesOnly)
{
    EXPECT_TRUE(FishCopiesWorldPosition(0xb2));
    EXPECT_TRUE(FishCopiesWorldPosition(0x148));
    EXPECT_FALSE(FishCopiesWorldPosition(0xb1));
    EXPECT_FALSE(FishCopiesWorldPosition(0xba));
    EXPECT_FALSE(FishCopiesWorldPosition(-1));
}

TEST(FishTest, SignedTypeSentinelControlsOnlyModelDispatch)
{
    EXPECT_FALSE(FishHasModel(-1));
    EXPECT_FALSE(FishHasModel(static_cast<std::int16_t>(0xffffu)));
    EXPECT_TRUE(FishHasModel(0xb2));
}
} // namespace
