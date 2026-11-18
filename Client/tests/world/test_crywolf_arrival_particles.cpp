#include "World/Particles.h"

#include "Render/Models/Cloth.h"
#include "World/ParticleDrawing.h"
#include "World/PortableParticleTrigonometry.h"

#include "support/ParticleDrawRecorder.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace
{
// 0x54271f..0x542897: seis rand() para 5f6/1; 0x546b66 comparte polvo, incluso al expirar.
TEST(CrywolfArrivalParticlesTest, TheArrivalDustCompletesCreationStepAndDrawing)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Render::Models::CrtRandom random(7);
    const auto created = particles->CreateChecked(0x5f6, {100, 200, 300}, {}, {2, 3, 4}, 1, 5, 0, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_EQ(created.Value(), 0u);
    auto& particle = particles->All()[0];
    EXPECT_EQ(particle.life, 40);
    EXPECT_EQ(particle.position, (glm::vec3{105, 206, 296}));
    EXPECT_FLOAT_EQ(particle.scale, 2.35f);
    EXPECT_EQ(particle.velocity, glm::vec3(0));
    EXPECT_EQ(particle.angles, (glm::vec3{41, 0, 200}));
    EXPECT_EQ(random.State(), 0x9974714du);
    particle.life = 1;
    const float clock = 0;
    Client::World::PortableParticleTrigonometry trigonometry;
    const auto stepped = Client::World::StepParticlesChecked(
        *particles, random, std::cref(clock), nullptr, trigonometry, [](float, float) { return 20.0; }, {}, {},
        [](std::int32_t type) -> std::optional<float> { return type == 0x5f6 ? std::optional(32.0f) : std::nullopt; });
    ASSERT_TRUE(stepped.HasValue());
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.life, 0);
    EXPECT_EQ(particle.light, glm::vec3(0));
    EXPECT_FLOAT_EQ(particle.position.z, 58.88f);
    particle.alive = true;
    const auto drawn = Client::Tests::DrawOneParticle(particle, glm::vec2(32), std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(drawn.Value()->texture, 0x5f6);
}
// 0x54308a: tres rand(), textura 6ad, z−45; 0x547149: escala+0,19 y z+5 tras el paso común.
TEST(CrywolfArrivalParticlesTest, ThePoundingBallKeepsItsCreationAndLastStep)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Render::Models::CrtRandom random(7);
    const auto created = particles->CreateChecked(0x640, {100, 200, 300}, {}, {2, 3, 4}, 1, 7, 0, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_EQ(created.Value(), 0u);
    auto& particle = particles->All()[0];
    EXPECT_EQ(particle.life, 30);
    EXPECT_EQ(particle.position, (glm::vec3{100, 200, 255}));
    EXPECT_EQ(particle.texture, 0x6ad);
    EXPECT_FLOAT_EQ(particle.scale, 1.25f);
    EXPECT_EQ(particle.rotation, 142);
    EXPECT_FLOAT_EQ(particle.verticalSpeed, 7.5f);
    EXPECT_EQ(random.State(), 0xbb6f8e98u);
    particle.life = 1;
    particle.velocity = {1, 2, 3};
    const float clock = 0;
    Client::World::PortableParticleTrigonometry trig;
    const auto stepped = Client::World::StepParticlesChecked(*particles, random, std::cref(clock), nullptr, trig);
    ASSERT_TRUE(stepped.HasValue());
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.position, (glm::vec3{101, 202, 263}));
    EXPECT_FLOAT_EQ(particle.scale, 1.44f);
    EXPECT_EQ(particle.light, glm::vec3(0));
    particle.alive = true;
    const auto drawn = Client::Tests::DrawOneParticle(particle, glm::vec2(32), std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(drawn.Value()->texture, 0x6ad);
}
TEST(CrywolfArrivalParticlesTest, TheFireworkParticleDrawsItsPhysicalSlotTrail)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Render::Models::CrtRandom random(7);
    const auto created =
        particles->CreateChecked(0x60d, {100, 200, 300}, {10, 20, 30}, {1, 0.6f, 0.2f}, -1, 7, 0, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_EQ(created.Value(), 0u);
    auto& particle = particles->All()[0];
    EXPECT_EQ(particle.life, 2);
    EXPECT_EQ(particle.angles, glm::vec3(0));
    EXPECT_EQ(particle.scale, 0.25f);
    EXPECT_EQ(particle.rotation, 61);
    EXPECT_EQ(random.State(), 0x003d7aaeu);
    particle.velocity = {1, 2, 3};
    Client::Tests::ParticleDrawRecorder draw;
    draw.dimensions[0x60d] = glm::vec2(32);
    draw.materialFlags[0x60d] = 3;
    const float clock = 0;
    const auto drawn = Client::World::DrawParticlesChecked(*particles, 0, std::cref(clock), draw);
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_EQ(draw.draws.size(), 23u);
    EXPECT_EQ(draw.draws.front().light, glm::vec3(0));
    EXPECT_FLOAT_EQ(draw.draws.front().position.x, 97.8f);
    EXPECT_EQ(draw.draws.back().position, particle.position);
    EXPECT_FLOAT_EQ(draw.draws.back().light.r, 0.208f);
    EXPECT_FLOAT_EQ(draw.draws.back().light.g, 0.128f);
    EXPECT_FLOAT_EQ(draw.draws.back().light.b, 0.024f);
    particles->All()[7] = particle;
    particle.alive = false;
    draw.draws.clear();
    ASSERT_TRUE(Client::World::DrawParticlesChecked(*particles, 0, std::cref(clock), draw).HasValue());
    EXPECT_EQ(draw.draws.size(), 30u);
    particle.alive = true;
    particles->All()[7].alive = false;
    const auto stepped = Client::World::StepParticlesChecked(*particles, random);
    ASSERT_TRUE(stepped.HasValue());
    EXPECT_EQ(particle.life, 1);
    EXPECT_EQ(particle.position, (glm::vec3{101, 202, 303}));
    EXPECT_EQ(particle.velocity.z, 2.5f);
}
} // namespace
