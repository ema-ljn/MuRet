#include "World/Birds.h"

#include "Render/Models/Cloth.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::BirdGlowLight;
using Client::World::BirdStepCount;

TEST(BirdsTest, AllSupportedMapsSelectTheirStepPrefix)
{
    constexpr std::array<std::size_t, 37> Counts{5, 5, 5, 5, 5, 5, 5, 40, 5, 5, 13, 5, 5, 5, 5, 5, 5, 5, 5,
                                                 5, 5, 5, 5, 5, 2, 2, 2,  2, 2, 2,  5, 5, 5, 5, 5, 5, 2};
    for (std::size_t map = 0; map < Counts.size(); ++map)
        EXPECT_EQ(BirdStepCount(static_cast<std::int32_t>(map)), Counts[map]) << map;
    for (const std::int32_t map : {-1, 54, 55, 263})
        EXPECT_EQ(BirdStepCount(map), 5u) << map;
}

TEST(BirdsTest, TheAeGlowUsesOneDrawAndTheRetainedProducts)
{
    CrtRandom random(74);
    const auto light = BirdGlowLight(0xae, random);
    ASSERT_TRUE(light);
    // 446b60-contract: q=88*0.01f; (q*0.2f,q*0.4f,q*0.4f), F32 en cada store.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light->x), 0x3e343958u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light->y), 0x3eb43958u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light->z), 0x3eb43958u);
    EXPECT_EQ(random.State(), 0x011845e5u);
}

TEST(BirdsTest, TheBcGlowSharesItsSingleDrawForBothSides)
{
    CrtRandom random(74);
    const auto light = BirdGlowLight(0xbc, random);
    ASSERT_TRUE(light);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light->x), 0x3fc28f5cu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light->y), 0x3e9ba5e3u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light->z), 0u);
    EXPECT_EQ(random.State(), 0x011845e5u);
}

TEST(BirdsTest, TheBcGreenDoesNotReloadTheRoundedRed)
{
    CrtRandom random(0);
    const auto light = BirdGlowLight(0xbc, random);
    ASSERT_TRUE(light);
    // q=134*0.01f; recargar F32(q) produce0x3e89374b, un ULP menos.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light->x), 0x3fab851eu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light->y), 0x3e89374cu);
    EXPECT_EQ(random.State(), 0x00269ec3u);
}

TEST(BirdsTest, OtherTypesDoNotConsumeTheGlowDraw)
{
    for (const std::int16_t type : std::array<std::int16_t, 9>{-1, -2, 0, 0xad, 0xaf, 0x10f, 0x171, 0x173, 0x148})
    {
        CrtRandom random(74);
        EXPECT_FALSE(BirdGlowLight(type, random)) << type;
        EXPECT_EQ(random.State(), 74u);
    }
}
} // namespace
