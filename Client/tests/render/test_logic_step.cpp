#include "Render/Models/Cloth.h"
#include "Render/Models/ClothWorldStep.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace
{
using Client::Render::Models::ClothWorld;
using Client::Render::Models::ClothWorldStep;
using Client::Render::Models::CrtRandom;

void Consume(CrtRandom& random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
}

TEST(LogicStepTest, KeepsSceneBetweenTheSnapshotAndWindAndIncrementsTheReopenedCurrentValue)
{
    for (const std::uint8_t depth : std::array<std::uint8_t, 3>{0, 1, 255})
    {
        ClothWorld world{.random = CrtRandom(0x12345678), .logicSteps = 370, .openCounterAccesses = depth};
        CrtRandom expected(0x12345678);
        if (depth == 0)
            Consume(expected, 4);
        Consume(expected, 4); // snapshot370 satisface remainder signed313==57.
        {
            ClothWorldStep step(&world);
            EXPECT_EQ(world.logicSteps, 370);
            EXPECT_EQ(world.openCounterAccesses, depth);
            EXPECT_EQ(world.windBase, 0.0f);
            EXPECT_EQ(world.random.State(), expected.State());
            EXPECT_EQ(world.random.Next(), expected.Next()); // consumidor de la escena, en su sitio.
            world.logicSteps = 123456; // un productor intermedio obliga a reabrir el valor actual.
        }
        Consume(expected, 5);
        if (depth == 0)
            Consume(expected, 4);
        EXPECT_EQ(world.logicSteps, 123457);
        EXPECT_EQ(world.openCounterAccesses, depth);
        EXPECT_EQ(world.random.State(), expected.State());
    }
}

TEST(LogicStepTest, RefreshUsesTheSignedPreincrementSnapshotAndOnlyThePhase57)
{
    for (const std::int32_t value : std::array<std::int32_t, 7>{-313, -256, -1, 56, 57, 58, 683})
    {
        ClothWorld world{.random = CrtRandom(98765), .logicSteps = value, .openCounterAccesses = 1};
        CrtRandom expected(98765);
        if (value == 57 || value == 683)
            Consume(expected, 4);
        {
            ClothWorldStep step(&world);
            EXPECT_EQ(world.logicSteps, value);
            EXPECT_EQ(world.random.State(), expected.State());
        }
        Consume(expected, 5);
        EXPECT_EQ(world.logicSteps, value + 1);
        EXPECT_EQ(world.random.State(), expected.State());
    }
}
} // namespace
