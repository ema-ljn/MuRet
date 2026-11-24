#include "Crt/CrtRandom.h"
#include "World/GameModelEffects.h"
#include "Render/Models/OriginalRotation.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"
#include "support/ParticleDrawRecorder.h"
#include "support/PlayerActors.h"
#include <glm/ext/vector_float2.hpp>
#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace
{
TEST(CrywolfTideEffectsTest, TheConstructorPreservesThePrefixOwnerAndTheUntouchedStartVector)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    effects->Effects()[0].start = {7, 8, 9};
    int owner = 0;
    const auto made = effects->CreateCrywolfTideEffect({.type = 0x145,
                                                        .position = {100, 200, 300},
                                                        .angles = {10, 20, 30},
                                                        .light = {1, 2, 3},
                                                        .subtype = 2,
                                                        .owner = &owner,
                                                        .scale = 99});
    ASSERT_EQ(made, 0u);
    const auto& effect = effects->Effects()[0];
    EXPECT_EQ(effect.life, 30);
    EXPECT_EQ(effect.transform.scale, 0);
    EXPECT_EQ(effect.objectState.blendMesh, -2);
    EXPECT_EQ(effect.velocity, (glm::vec3{0, -40, 0}));
    EXPECT_EQ(effect.start, (glm::vec3{7, 8, 9}));
    EXPECT_EQ(effect.transform.position, (glm::vec3{100, 200, 300}));
    EXPECT_EQ(effect.transform.angles, (glm::vec3{10, 20, 30}));
    EXPECT_EQ(effect.ownerContext, &owner);
    EXPECT_EQ(effect.light, (glm::vec3{1, 2, 3}));
}

TEST(CrywolfTideEffectsTest, TheTideOscillatesAndEmitsSixSmokeChildrenBeforeTheCommonTail)
{
    for (const bool full : {false, true})
        for (const float scale : {0.0f, 0.8f})
        {
            auto particles = std::make_unique<Client::World::Particles>();
            if (full)
                for (auto& p : particles->All())
                    p.alive = true;
            auto lighting = std::make_unique<Client::World::TerrainLighting>();
            Client::World::ModelEffect effect;
            effect.alive = true;
            effect.type = 0x145;
            effect.subtype = 2;
            effect.life = 30;
            effect.transform.position = {1000, 2000, 300};
            effect.transform.angles = {0, 0, -7};
            effect.transform.scale = scale;
            effect.velocity = {0, -40, 0};
            effect.light = {0.1f, 0.2f, 0.3f};
            Client::Crt::CrtRandom random(7);
            const auto stepped = Client::World::StepGameModelEffect(effect, nullptr, *particles, 0, random, nullptr,
                                                                    {.lighting = lighting.get()});
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, full ? 1 : 19).State());
            EXPECT_EQ(effect.life, 29);
            EXPECT_FLOAT_EQ(effect.transform.scale, scale == 0 ? 0.2f : 0.4f);
            EXPECT_EQ(effect.objectState.light, 3);
            EXPECT_EQ(effect.objectState.uv.x, -6);
            const auto motion = Client::Render::Models::OriginalVectorRotate(
                {0, -40, 0}, Client::Render::Models::OriginalAngleMatrix({0, 0, -7}));
            EXPECT_EQ(effect.transform.position, glm::vec3(1000, 2000, 300) + motion);
            if (!full)
            {
                for (int index = 0; index < 6; ++index)
                {
                    const auto& child = particles->All()[static_cast<std::size_t>(index)];
                    EXPECT_TRUE(child.alive);
                    EXPECT_EQ(child.type, 0x5f2);
                    EXPECT_EQ(child.subtype, 25);
                    EXPECT_EQ(child.life, 25);
                    EXPECT_EQ(child.position, (glm::vec3{1000, 1500, 300}));
                    EXPECT_EQ(child.angles, glm::vec3(0));
                    EXPECT_EQ(child.light, effect.light);
                }
                EXPECT_FALSE(particles->All()[6].alive);
            }
            auto expected = std::make_unique<Client::World::TerrainLighting>();
            Client::World::AddTerrainLight(*expected, 1000, 1500, {0.24000000953674316f, 0.48000001907348633f, 0.8f},
                                           3);
            EXPECT_EQ(lighting->cells, expected->cells);
        }
}
TEST(CrywolfTideEffectsTest, TheTideSmokeKeepsThreeConstructorRollsAndItsGrayGrowth)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Crt::CrtRandom random(7);
    const auto made = particles->CreateChecked(0x5f2, {100, 200, 300}, {}, {1, 2, 3}, 25, 1, 0, random);
    ASSERT_TRUE(made.HasValue());
    ASSERT_EQ(made.Value(), 0u);
    auto& p = particles->All()[0];
    EXPECT_EQ(p.life, 25);
    EXPECT_EQ(p.texture, 0x5f2);
    EXPECT_EQ(p.scale, 1.25f);
    EXPECT_EQ(p.rotation, 142);
    EXPECT_EQ(p.verticalSpeed, 7.5f);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, 3).State());
    ASSERT_TRUE(Client::World::StepParticlesChecked(*particles, random).HasValue());
    EXPECT_EQ(p.life, 24);
    EXPECT_FLOAT_EQ(p.scale, 1.34f);
    EXPECT_EQ(p.light, glm::vec3(1.2f));
    const auto drawn = Client::Tests::DrawOneParticle(p, glm::vec2(32), std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value());
    EXPECT_EQ(drawn.Value()->texture, 0x5f2);
}
} // namespace
