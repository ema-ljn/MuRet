#include "Render/Models/Cloth.h"
#include "support/ParticleDrawRecorder.h"
#include "World/Particles.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>
#include <bit>
#include <cstdint>
#include <memory>

namespace
{
TEST(NoriaParticlesTest, ShinySubtypesStartWithoutRandomAndKeepTheCommonPrefix)
{
    for (int subtype : {0, 1})
    {
        auto particles = std::make_unique<Client::World::Particles>();
        Client::Render::Models::CrtRandom random(7);
        const auto created =
            particles->CreateChecked(0x5fc, {1, 2, 3}, {10, 20, 30}, {0.2f, 0.3f, 0.4f}, subtype, 9.0f, 0.0f, random);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        const auto& particle = particles->All()[*created.Value()];
        EXPECT_EQ(particle.life, 18);
        EXPECT_EQ(particle.angles, glm::vec3(45, 20, 30));
        EXPECT_EQ(particle.scale, 9.0f);
        EXPECT_EQ(particle.velocity, glm::vec3(0));
        EXPECT_EQ(random.State(), 7u);
    }
}

TEST(NoriaParticlesTest, ShinyStepUsesLifeSineAndSubtypeOneRotatesTwelveDegrees)
{
    for (int subtype : {0, 1})
    {
        auto particles = std::make_unique<Client::World::Particles>();
        auto& particle = particles->All().front();
        particle.alive = true;
        particle.type = particle.texture = 0x5fc;
        particle.subtype = subtype;
        particle.life = 18;
        particle.scale = 9.0f;
        particle.rotation = 30.0f;
        Client::Render::Models::CrtRandom random(7);
        const auto stepped = Client::World::StepParticlesChecked(*particles, random);
        ASSERT_TRUE(stepped.HasValue());
        EXPECT_EQ(particle.life, 17);
        EXPECT_NEAR(particle.scale, subtype == 0 ? 0.17364827f : 0.13023620f, 0.0000001f);
        EXPECT_EQ(particle.rotation, subtype == 0 ? 30.0f : 18.0f);
        EXPECT_EQ(random.State(), 0xc40e66b9u); // Sólo dos rand del viento.
    }
}

TEST(NoriaParticlesTest, TheFairySparkKeepsFiveRandomDrawsAndItsStoredMotion)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Render::Models::CrtRandom random(7);
    const auto created = particles->CreateChecked(0x5c5, {100, 200, 300}, {0, 0, 0}, {1, 1, 1}, 0, 9.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const auto& particle = particles->All()[*created.Value()];
    EXPECT_FLOAT_EQ(particle.scale, 0.5f);
    EXPECT_EQ(particle.life, 38);
    EXPECT_EQ(particle.angles.z, 95.0f);
    EXPECT_EQ(particle.verticalSpeed, 6.0f);
    EXPECT_NEAR(particle.velocity.x, -2.0920088f, 0.000001f);
    EXPECT_NEAR(particle.velocity.y, -0.1830271f, 0.000001f);
    EXPECT_EQ(particle.velocity.z, 0.0f);
    EXPECT_EQ(random.State(), 0xbca151d2u);
}
TEST(NoriaParticlesTest, TheFairySparkKeepsThePrefixBeforeMissingTerrainAndContinuesAfterCollision)
{
    auto particles = std::make_unique<Client::World::Particles>();
    auto& particle = particles->All().front();
    particle.alive = true;
    particle.type = particle.texture = 0x5c5;
    particle.life = 10;
    particle.position = {10, 20, 100};
    particle.verticalSpeed = 4;
    particle.velocity = {1, 2, 3};
    const auto initial = particle;
    Client::Render::Models::CrtRandom random(7);
    const auto missing = Client::World::StepParticlesChecked(*particles, random);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_EQ(missing.Error(), Client::World::ParticleError::MissingTerrainHeight);
    EXPECT_EQ(particle.position, glm::vec3(10, 20, 104));
    EXPECT_EQ(particle.verticalSpeed, 2.0f);
    EXPECT_EQ(particle.life, 9);
    EXPECT_EQ(particle.light, glm::vec3(0.5625f));
    particle = initial;
    int calls = 0;
    const auto stepped = Client::World::StepParticlesChecked(*particles, random, nullptr,
                                                             [&](float x, float y)
                                                             {
                                                                 EXPECT_EQ(x, 10.0f);
                                                                 EXPECT_EQ(y, 20.0f);
                                                                 EXPECT_EQ(particle.position.z, 104.0f);
                                                                 ++calls;
                                                                 return 120.0;
                                                             });
    ASSERT_TRUE(stepped.HasValue());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(particle.position, glm::vec3(11, 22, 123));
    EXPECT_FLOAT_EQ(particle.verticalSpeed, -1.2f);
    EXPECT_EQ(particle.life, 5);
}
TEST(NoriaParticlesTest, NoriaParticleBillboardsUseTheWholeTextureAndTheStoredRotation)
{
    for (const auto type : {0x5fc, 0x5c5})
    {
        Client::World::Particle particle;
        particle.alive = true;
        particle.type = particle.texture = type;
        particle.scale = 2.0f;
        particle.rotation = 27.0f;
        particle.light = {0.2f, 0.4f, 0.6f};
        const auto drawn = Client::Tests::DrawOneParticle(particle, glm::vec2(128, 64), std::uint8_t{3});
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        EXPECT_EQ(drawn.Value()->texture, type);
        EXPECT_EQ(drawn.Value()->rotation, 27.0f);
        EXPECT_EQ(drawn.Value()->uv, glm::vec2(0));
        EXPECT_EQ(drawn.Value()->uvSize, glm::vec2(1));
        EXPECT_EQ(drawn.Value()->light, particle.light);
    }
}
// 0x54038c..0x5403a7: la 5c5/7 (las chispas de la mariposa de la selección) sortea su escala como la /0 y
// después sólo su vida (12..23): sin giro, sin velocidad y sin la escala del argumento.
TEST(NoriaParticlesTest, TheButterflySparkDrawsOnlyItsScaleAndItsLife)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Render::Models::CrtRandom random(7);
    Client::Render::Models::CrtRandom draws(7);
    const auto created =
        particles->CreateChecked(0x5c5, {100, 200, 300}, {0, 0, 30}, {0.5f, 0.5f, 0.5f}, 7, 1.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const auto& particle = particles->All()[*created.Value()];
    EXPECT_EQ(particle.scale, static_cast<float>(((draws.Next() & 3) + 4) * static_cast<double>(0.1f)));
    EXPECT_EQ(particle.life, draws.Next() % 12 + 12);
    EXPECT_EQ(particle.angles, glm::vec3(0, 0, 30));
    EXPECT_EQ(particle.verticalSpeed, 0.0f);
    EXPECT_EQ(particle.velocity, glm::vec3(0));
    EXPECT_EQ(random.State(), draws.State());
}

// 0x5403eb..0x540406: la 5c5/1 (las chispas del bastón 0xc18, 0x50f5a8) sortea su escala como la /0 y después sólo su
// vida, (rand() & 7) + 8: sin giro, sin velocidad y sin la escala del argumento.
TEST(NoriaParticlesTest, TheStaffSparkDrawsOnlyItsScaleAndItsLife)
{
    for (const std::uint32_t seed : {1u, 7u, 99u})
    {
        SCOPED_TRACE(seed);
        auto particles = std::make_unique<Client::World::Particles>();
        Client::Render::Models::CrtRandom random(seed);
        Client::Render::Models::CrtRandom draws(seed);
        const auto created =
            particles->CreateChecked(0x5c5, {100, 200, 300}, {0, 0, 30}, {0.8f, 0.48f, 0.32f}, 1, 1.0f, 0.0f, random);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        const auto& particle = particles->All()[*created.Value()];
        EXPECT_EQ(particle.scale, static_cast<float>(((draws.Next() & 3) + 4) * static_cast<double>(0.1f)));
        EXPECT_EQ(particle.life, (draws.Next() & 7) + 8);
        EXPECT_EQ(particle.angles, glm::vec3(0, 0, 30));
        EXPECT_EQ(particle.light, glm::vec3(0.8f, 0.48f, 0.32f));
        EXPECT_EQ(particle.verticalSpeed, 0.0f);
        EXPECT_EQ(particle.velocity, glm::vec3(0));
        EXPECT_EQ(random.State(), draws.State());
    }
}

// 0x544f7b: la /7 cae 0,6 (0x6b8a20) por paso; la /0 y la /1, 2 (0x6b8818).
TEST(NoriaParticlesTest, TheButterflySparkFallsSlowerThanTheFairySpark)
{
    for (const int subtype : {0, 1, 7})
    {
        auto particles = std::make_unique<Client::World::Particles>();
        auto& particle = particles->All().front();
        particle.alive = true;
        particle.type = particle.texture = 0x5c5;
        particle.subtype = subtype;
        particle.life = 10;
        particle.position = {10, 20, 100};
        particle.verticalSpeed = 4;
        Client::Render::Models::CrtRandom random(7);
        const auto stepped =
            Client::World::StepParticlesChecked(*particles, random, nullptr, [](float, float) { return 0.0; });
        ASSERT_TRUE(stepped.HasValue());
        EXPECT_EQ(particle.position.z, 104.0f);
        EXPECT_EQ(
            particle.verticalSpeed,
            static_cast<float>(4.0 - (subtype == 7 ? static_cast<double>(std::bit_cast<float>(0x3f19999au)) : 2.0)));
        EXPECT_EQ(particle.light, glm::vec3(0.5625f));
    }
}
} // namespace
