#include "World/Particles.h"

#include "Render/Models/Cloth.h"

#include "support/ParticleDrawRecorder.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <optional>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;
constexpr std::int32_t EffectType = 0x68;

TEST(Particle68CreateTest, UsesFourOrderedRandomValuesAndRetainsTheNativeWords)
{
    Particles particles;
    Particle& freeSlot = particles.All()[0];
    freeSlot.field38 = 0xa5a5a5a5u;
    freeSlot.rotationFactor = std::bit_cast<float>(0x7fc12345u);
    freeSlot.field64 = {6.0f, -7.0f};
    CrtRandom random(7);

    const auto created =
        particles.CreateChecked(EffectType, {2, 3, 4}, {}, {0.25f, 0.5f, 0.75f}, 0, 1.0f, 0, random, 0);

    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    EXPECT_EQ(*created.Value(), 0u);
    const Particle& particle = particles.All()[0];
    EXPECT_TRUE(particle.alive);
    EXPECT_EQ(particle.type, EffectType);
    EXPECT_EQ(particle.texture, EffectType);
    EXPECT_EQ(particle.subtype, 0);
    EXPECT_EQ(particle.life, 20);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.scale), 0x3f666667u);
    EXPECT_EQ(particle.position, glm::vec3(-1, 3, -1));
    EXPECT_EQ(particle.start, glm::vec3(2, 3, 4));
    EXPECT_EQ(particle.light, glm::vec3(0.25f, 0.5f, 0.75f));
    EXPECT_EQ(particle.field38, 0xa5a5a5a5u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.rotationFactor), 0x7fc12345u);
    EXPECT_EQ(particle.field64, glm::vec2(6.0f, -7.0f));
    EXPECT_EQ(particle.velocity, glm::vec3(0));
    EXPECT_EQ(random.State(), 0x1b8052fbu);
}

TEST(Particle68CreateTest, AFullPoolDoesNotConsumeRandomValues)
{
    Particles particles;
    for (Particle& particle : particles.All())
    {
        particle.alive = true;
    }
    CrtRandom random(7);

    const auto created = particles.CreateChecked(EffectType, {}, {}, {}, 0, 1.0f, 0, random);

    ASSERT_TRUE(created.HasValue());
    EXPECT_FALSE(created.Value().has_value());
    EXPECT_EQ(random.State(), 7u);
}

TEST(Particle68CreateTest, StepRunsAndUnsupportedCreationSubtypeRemainsAnError)
{
    Particles particles;
    CrtRandom random(7);
    const auto created = particles.CreateChecked(EffectType, {}, {}, {}, 0, 1.0f, 0, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());

    const std::int32_t map = 34;
    const auto stepped = Client::World::StepParticlesChecked(particles, random, &map);
    ASSERT_TRUE(stepped.HasValue());
    EXPECT_EQ(particles.All()[*created.Value()].life, 19);

    Particles unsupported;
    CrtRandom untouched(7);
    const auto wrongSubtype = unsupported.CreateChecked(EffectType, {}, {}, {}, 1, 1.0f, 0, untouched);
    ASSERT_FALSE(wrongSubtype.HasValue());
    EXPECT_EQ(wrongSubtype.Error(), Client::World::ParticleError::UnsupportedSubtype);
    EXPECT_EQ(untouched.State(), 7u);
}

TEST(Particle68DrawTest, UsesTheGenericWholeTexturePathAndDoesNotStoreToTheSlot)
{
    Particle particle{.alive = true,
                      .type = EffectType,
                      .texture = EffectType,
                      .subtype = 0,
                      .scale = 2.0f,
                      .position = {4, 5, 6},
                      .light = {0.25f, 0.5f, 0.75f},
                      .field38 = 0xa5a5a5a5u,
                      .life = 20,
                      .owner = 0,
                      .rotation = -35.0f,
                      .field48 = std::bit_cast<float>(0xdeadbeefu),
                      .moves = true,
                      .verticalSpeed = 61.0f,
                      .velocity = {1, 2, 3},
                      .rotationFactor = 4.0f,
                      .field64 = {6, 7},
                      .start = {8, 9, 10}};
    const std::uint32_t qBefore = particle.field38;
    const auto posBefore = particle.position;
    const auto colorBefore = particle.light;
    const auto startBefore = particle.start;

    const auto draw = Client::Tests::DrawOneParticle(particle, glm::vec2(64, 32), std::uint8_t{3});

    ASSERT_TRUE(draw.HasValue());
    ASSERT_TRUE(draw.Value().has_value());
    EXPECT_EQ(draw.Value()->texture, EffectType);
    EXPECT_EQ(draw.Value()->blend, Client::Tests::RecordedParticleDraw::Blend::Additive);
    EXPECT_EQ(draw.Value()->position, posBefore);
    EXPECT_EQ(draw.Value()->width, 128.0f);
    EXPECT_EQ(draw.Value()->height, 64.0f);
    EXPECT_EQ(draw.Value()->light, colorBefore);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(draw.Value()->rotation), 0xc20c0000u);
    EXPECT_EQ(draw.Value()->uv, glm::vec2(0));
    EXPECT_EQ(draw.Value()->uvSize, glm::vec2(1));
    EXPECT_EQ(particle.field38, qBefore);
    EXPECT_EQ(particle.position, posBefore);
    EXPECT_EQ(particle.light, colorBefore);
    EXPECT_EQ(particle.start, startBefore);
    EXPECT_EQ(particle.rotation, -35.0f);
}
} // namespace
