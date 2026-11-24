#include "World/CastleSiegeObjectStep.h"

#include "Crt/CrtRandom.h"
#include "World/WorldObjects.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>

// 42c967..42c996: oculta antes de mirar el asedio, distingue la máscara entera y siempre correV durante guerra.
TEST(CastleSiegeMarkerTest, WarUsesTheWholeCrownFlagsAndScrollsTheMarkerWithoutRandom)
{
    bool war = true;
    std::uint32_t flags = 0;
    Client::World::ObjectStepGlobals globals;
    globals.siegeActive = &war;
    globals.siegeCrownFlags = &flags;
    globals.worldClock = 12345.5f;
    Client::Crt::CrtRandom random(7);
    Client::World::WorldObject object;
    object.type = 0x42;
    for (const std::uint32_t value : {0u, 0x20000u, 0x20001u})
    {
        flags = value;
        const auto stepped = Client::World::StepCastleSiegeObject(object, random, globals);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(object.state.hiddenMesh, value == 0x20000 ? -2 : -1);
        EXPECT_EQ(object.state.uv.y, static_cast<float>(static_cast<double>(globals.worldClock) *
                                                        static_cast<double>(std::bit_cast<float>(0x3a83126fu))));
        EXPECT_EQ(random.State(), 7u);
    }
    war = false;
    object.state.uv.y = 12.0f;
    ASSERT_TRUE(Client::World::StepCastleSiegeObject(object, random, globals).HasValue());
    EXPECT_EQ(object.state.hiddenMesh, -2);
    EXPECT_EQ(object.state.uv.y, 12.0f);
}
