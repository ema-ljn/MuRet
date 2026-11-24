#include "World/Particles.h"

#include "Crt/CrtRandom.h"
#include "World/ParticleDrawing.h"
#include "World/PortableParticleTrigonometry.h"

#include "support/ParticleDrawRecorder.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
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
    Client::Crt::CrtRandom random(7);
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
// 0x54271f..0x542878: el alta no separa la 5f3 de la 5f6 (0x542761 da dos ramas iguales). La 5f3/1 del freno de +0x3e5
// (0x506a73) nace como la 5f6/1 de arriba, con sus seis rand(); la 5f6/0 de las piedras de Chaos Castle (0x405f24),
// como la 5f3/0, con cuatro y el empuje (0; 3; 0). Con los 3.000 llenos, ninguna saca rand(). Los bits, aparte (x87).
TEST(CrywolfArrivalParticlesTest, TheDustTypesShareTheirBirthBySubtype)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Crt::CrtRandom random(7);
    auto created = particles->CreateChecked(0x5f3, {100, 200, 300}, {}, {2, 3, 4}, 1, 5, 0, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_EQ(created.Value(), 0u);
    const auto& brake = particles->All()[0];
    EXPECT_EQ(brake.type, 0x5f3);
    EXPECT_EQ(brake.texture, 0x5f3);
    EXPECT_EQ(brake.life, 40);
    EXPECT_EQ(brake.position, (glm::vec3{105, 206, 296}));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(brake.scale), 0x40166666u); // 2,35f
    EXPECT_EQ(brake.velocity, glm::vec3(0));
    EXPECT_EQ(brake.angles, (glm::vec3{41, 0, 200}));
    EXPECT_EQ(random.State(), 0x9974714du);

    random = Client::Crt::CrtRandom(7);
    created = particles->CreateChecked(0x5f6, {100, 200, 300}, {}, {1, 1, 1}, 0, 1.5f, 0, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_EQ(created.Value(), 1u);
    const auto& stone = particles->All()[1];
    EXPECT_EQ(stone.texture, 0x5f6);
    EXPECT_EQ(stone.life, 32);
    EXPECT_EQ(stone.position, (glm::vec3{105, 206, 300}));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(stone.scale), 0x3f347ae1u); // 47 · 1,5 · 0,01f
    EXPECT_EQ(stone.angles, (glm::vec3{200, 0, 0}));
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, 4).State());
    // La 5f3/0 con el mismo CRT en un pool gemelo: los mismos campos salvo el tipo.
    auto twin = std::make_unique<Client::World::Particles>();
    Client::Crt::CrtRandom oracle(7);
    ASSERT_TRUE(twin->CreateChecked(0x5f3, {100, 200, 300}, {}, {1, 1, 1}, 0, 1.5f, 0, oracle).HasValue());
    const auto& dust = twin->All()[0];
    EXPECT_EQ(stone.velocity, dust.velocity);
    EXPECT_EQ(stone.position, dust.position);
    EXPECT_EQ(stone.angles, dust.angles);

    for (auto& particle : particles->All())
        particle.alive = true;
    random = Client::Crt::CrtRandom(7);
    for (const std::int32_t type : {0x5f3, 0x5f6})
        for (const std::int32_t subtype : {0, 1})
        {
            const auto full = particles->CreateChecked(type, {}, {}, {}, subtype, 1, 0, random);
            ASSERT_TRUE(full.HasValue());
            EXPECT_FALSE(full.Value().has_value());
        }
    EXPECT_EQ(random.State(), Client::Crt::CrtRandom(7).State());
    particles->All()[5].alive = false;
    EXPECT_FALSE(particles->CreateChecked(0x5f3, {}, {}, {}, 2, 1, 0, random).HasValue());
}
// 0x54308a: tres rand(), textura 6ad, z−45; 0x547149: escala+0,19 y z+5 tras el paso común.
TEST(CrywolfArrivalParticlesTest, ThePoundingBallKeepsItsCreationAndLastStep)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Crt::CrtRandom random(7);
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
    Client::Crt::CrtRandom random(7);
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
