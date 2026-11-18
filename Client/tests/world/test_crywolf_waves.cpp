#include "World/GameModelEffects.h"
#include "Render/Models/Cloth.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include <gtest/gtest.h>
#include <memory>

namespace
{
TEST(CrywolfWavesTest, TheConstructorsKeepTheSignedKeyAndNativePrefixStores)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    const auto wave = effects->CreateWaveEffect({.type = 0x114,
                                                 .position = {100, 200, 300},
                                                 .angles = {10, 20, 30},
                                                 .light = {1, 2, 3},
                                                 .subtype = 6,
                                                 .key = 0xffff});
    ASSERT_EQ(wave, 0u);
    const auto& first = effects->Effects()[0];
    EXPECT_EQ(first.life, 10);
    EXPECT_EQ(first.transform.scale, -0.05f);
    EXPECT_EQ(first.disturbanceState, 0xffff);
    EXPECT_EQ(first.transform.angles, (glm::vec3{-90, 0, 30}));
    EXPECT_EQ(first.objectState.blendMesh, -2);
    EXPECT_EQ(first.objectState.light, 1);
    EXPECT_EQ(first.transform.position, (glm::vec3{100, 200, 300}));
    EXPECT_EQ(first.start, first.transform.position);
    EXPECT_EQ(first.light, (glm::vec3{1, 2, 3}));
    const auto piercing = effects->CreatePiercingEffect(
        {.type = 0x115, .position = {100, 200, 300}, .angles = {10, 20, 30}, .light = {1, 0, 0.5f}, .subtype = 1});
    ASSERT_EQ(piercing, 1u);
    const auto& second = effects->Effects()[1];
    EXPECT_EQ(second.life, 6);
    EXPECT_EQ(second.transform.scale, 2);
    EXPECT_EQ(second.velocity, (glm::vec3{0, -60, 0}));
    EXPECT_EQ(second.start, (glm::vec3{100, 200, 300}));
    EXPECT_EQ(second.transform.position, (glm::vec3{100, 200, 430}));
}

TEST(CrywolfWavesTest, BothNativeWaveSubtypesKeepTheirOwnGrowthFadeAndCommonTail)
{
    for (const int subtype : {2, 6})
    {
        auto particles = std::make_unique<Client::World::Particles>();
        Client::World::ModelEffect wave;
        wave.alive = true;
        wave.type = 0x114;
        wave.subtype = subtype;
        wave.life = 1;
        wave.transform.scale = subtype == 2 ? 1.49f : 2.49f;
        wave.turnDirection = subtype == 2 ? 0.01f : 0.08f;
        wave.velocity = {1, 2, 3};
        Client::Render::Models::CrtRandom random(7);
        const auto result = Client::World::StepGameModelEffect(wave, nullptr, *particles, 0, random);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_FALSE(wave.alive);
        EXPECT_EQ(wave.life, 0);
        EXPECT_EQ(wave.transform.scale, subtype == 2 ? 1.5f : 2.5f);
        EXPECT_FLOAT_EQ(wave.turnDirection, subtype == 2 ? 0.02f : 0.095f);
        EXPECT_FLOAT_EQ(wave.objectState.light, subtype == 2 ? 0.6666666865348816f : 0.7692307829856873f);
        EXPECT_EQ(wave.transform.angles.y, subtype == 2 ? 0 : 45);
        EXPECT_EQ(random.State(), 0x003d7aaeu);
    }
}

TEST(CrywolfWavesTest, ThePiercingMovesTwiceAndCreatesTheWaveAtItsIntermediatePosition)
{
    for (const bool full : {false, true})
    {
        auto particles = std::make_unique<Client::World::Particles>();
        auto effects = std::make_unique<Client::World::ModelEffects>();
        if (full)
            for (auto& e : effects->Effects())
                e.alive = true;
        Client::World::ModelEffect piercing;
        piercing.alive = true;
        piercing.type = 0x115;
        piercing.subtype = 1;
        piercing.life = 6;
        piercing.transform.position = {1000, 2000, 430};
        piercing.velocity = {0, -60, 0};
        piercing.light = {1, 0, 0.5f};
        Client::Render::Models::CrtRandom random(7);
        const auto result = Client::World::StepGameModelEffect(piercing, nullptr, *particles, 0, random, nullptr,
                                                               {.effects = effects.get()});
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(piercing.life, 5);
        EXPECT_EQ(piercing.velocity, (glm::vec3{0, -48, 0}));
        EXPECT_EQ(piercing.transform.position, (glm::vec3{1000, 1892, 430}));
        EXPECT_EQ(piercing.objectState.light, 0.625f);
        EXPECT_EQ(random.State(), 0x003d7aaeu);
        if (!full)
        {
            const auto& wave = effects->Effects()[0];
            EXPECT_EQ(wave.type, 0x114);
            EXPECT_EQ(wave.subtype, 2);
            EXPECT_EQ(wave.life, 15);
            EXPECT_FLOAT_EQ(wave.transform.scale, 0.3f);
            EXPECT_EQ(wave.transform.position, (glm::vec3{1000, 1940, 430}));
            EXPECT_EQ(wave.transform.angles, (glm::vec3{90, 0, 0}));
            EXPECT_EQ(wave.light, piercing.light);
        }
    }
}
} // namespace
