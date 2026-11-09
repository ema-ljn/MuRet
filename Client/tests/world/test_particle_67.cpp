#include "World/Particles.h"

#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <initializer_list>
#include <optional>

// Contratos 53fe70/5446e0/548550_ext_67; predicado 4303c0 histórico íntegro reutilizado.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;
constexpr std::int32_t Type67 = 0x67;

TEST(Particle67Test, CreationUsesOneRandomValueAndLeavesTheCopiedVectorsUntouched)
{
    Particles particles;
    CrtRandom random(7); // R1=61; (8+61%6)*floatBits3dcccccd se almacena como3f666667.
    const auto slot = particles.Create(Type67, {100, 200, 300}, {10, 20, 30}, {2, 3, 4}, 0, 9, 999, random);
    ASSERT_EQ(slot, 0u);
    const Particle& particle = particles.All()[0];
    EXPECT_EQ(particle.life, 20);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.scale), 0x3f666667u);
    EXPECT_EQ(particle.position, glm::vec3(100, 200, 300));
    EXPECT_EQ(particle.start, particle.position);
    EXPECT_EQ(particle.angles, glm::vec3(10, 20, 30));
    EXPECT_EQ(particle.light, glm::vec3(2, 3, 4));
    EXPECT_EQ(particle.rotation, 0);
    EXPECT_EQ(particle.verticalSpeed, 0);
    EXPECT_EQ(particle.velocity, glm::vec3(0));
    EXPECT_EQ(random.State(), 0x003d7aaeu);
}
TEST(Particle67Test, ExpiryStillMovesGrowsAndUsesTheMap34Color)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = Type67,
                        .subtype = 0,
                        .scale = 1,
                        .position = {1, 2, 3},
                        .light = {2, 3, 4},
                        .life = 1,
                        .moves = true,
                        .velocity = {4, 5, 6}};
    CrtRandom random(7);
    const std::int32_t map = 34;
    const auto stepped = Client::World::StepParticlesChecked(particles, random, &map);
    ASSERT_TRUE(stepped.HasValue());
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.life, 0);
    EXPECT_EQ(particle.position, glm::vec3(5, 7, 9));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.scale), 0x3f83d70au);
    EXPECT_EQ(particle.light.x, 1);
    EXPECT_EQ(particle.light.y, 1);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.light.z), 0x3f333333u);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}

TEST(Particle67Test, TheMapPredicateReadsAllThirtyTwoBitsAndOtherMapsSubtractWithoutClamping)
{
    for (const std::int32_t map : {0, 35, 0x122, -1})
    {
        Particles particles;
        Particle& particle = particles.All()[0];
        particle = Particle{.alive = true, .type = Type67, .subtype = 0, .scale = 1, .light = {0, -1, 1}, .life = 2};
        CrtRandom random(7);
        const auto stepped = Client::World::StepParticlesChecked(particles, random, &map);
        ASSERT_TRUE(stepped.HasValue());
        EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.light.x), 0xbd4ccccdu) << map;
        EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.light.y), 0xbf866666u) << map;
        EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.light.z), 0x3f733333u) << map;
        EXPECT_EQ(random.State(), 0xc40e66b9u);
    }
}

TEST(Particle67Test, MissingMapReportsTheFrontierAfterTheNativePrefixAndScaleStore)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true, .type = Type67, .subtype = 0, .scale = 1, .light = {2, 3, 4}, .life = 1};
    CrtRandom random(7);
    const auto stepped = Client::World::StepParticlesChecked(particles, random);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_EQ(stepped.Error(), Client::World::ParticleError::MissingMap);
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.life, 0);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.scale), 0x3f83d70au);
    EXPECT_EQ(particle.light, glm::vec3(2, 3, 4));
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}

TEST(Particle67Test, ThePostMovementSubtypeOneContinuationUsesTheStoredAccumulator)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true, .type = Type67, .subtype = 1, .scale = 1, .life = 2, .verticalSpeed = 0.25f};
    CrtRandom random(7);
    const std::int32_t map = 34;
    const auto stepped = Client::World::StepParticlesChecked(particles, random, &map);
    ASSERT_TRUE(stepped.HasValue());
    EXPECT_EQ(particle.scale, 1.25f);
    EXPECT_EQ(particle.verticalSpeed, 0.25f);
}
TEST(Particle67Test, GrowthUsesTheBinary32ConstantRatherThanAnIdealDecimal)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle =
        Particle{.alive = true, .type = Type67, .subtype = 0, .scale = std::bit_cast<float>(0xbcf5c28fu), .life = 2};
    CrtRandom random(7);
    const std::int32_t map = 34;
    const auto stepped = Client::World::StepParticlesChecked(particles, random, &map);
    ASSERT_TRUE(stepped.HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.scale), 0u);
}

TEST(Particle67Test, TheLegacyStepExposesItsMissingMapErrorInsteadOfReportingSuccess)
{
    Particles particles;
    particles.All()[0] = Particle{.alive = true, .type = Type67, .subtype = 0, .life = 2};
    CrtRandom random(7);
    const auto error = Client::World::StepParticles(particles, random);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(*error, Client::World::ParticleError::MissingMap);
}

TEST(Particle67Test, AnEmptyCheckedStepNeedsNoMapAndStillConsumesTheWindDraws)
{
    Particles particles;
    CrtRandom random(7);
    const auto stepped = Client::World::StepParticlesChecked(particles, random);
    EXPECT_TRUE(stepped.HasValue());
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}
TEST(Particle67Test, DrawUsesFullDimensionsRotation44AndWholeTextureWithoutAnAtlas)
{
    Particle particle{.alive = true,
                      .type = Type67,
                      .texture = 17,
                      .subtype = 0,
                      .scale = 1.5f,
                      .position = {10, 20, 30},
                      .angles = {17, 22, 33},
                      .light = {2, 3, 4},
                      .rotation = 95,
                      .field48 = std::bit_cast<float>(-5)};
    const auto draw = Client::World::ParticleDrawOf(particle, {128, 64}, 4);
    ASSERT_TRUE(draw.has_value());
    EXPECT_EQ(draw->texture, 17);
    EXPECT_EQ(draw->width, 192);
    EXPECT_EQ(draw->height, 96);
    EXPECT_EQ(draw->rotation, 95);
    EXPECT_EQ(draw->uv, glm::vec2(0));
    EXPECT_EQ(draw->uvSize, glm::vec2(1));
    EXPECT_EQ(draw->position, glm::vec3(10, 20, 30));
    EXPECT_EQ(draw->light, glm::vec3(2, 3, 4));
    EXPECT_EQ(draw->blend, Client::World::ParticleDraw::Blend::Alpha);
}

TEST(Particle67Test, CheckedDrawKeepsTheHeightOverlapAndReportsMissingMetadataForItsActualBinding)
{
    Particle particle{
        .alive = true, .type = Type67, .texture = 17, .subtype = 0, .scale = 1, .position = {10, 20, 325}};
    for (const std::uint8_t mode : {std::uint8_t{1}, std::uint8_t{2}})
    {
        const auto draw = Client::World::ParticleDrawChecked(particle, glm::vec2(128, 64), std::uint8_t{3}, 0, 0, mode);
        ASSERT_TRUE(draw.HasValue());
        ASSERT_TRUE(draw.Value().has_value());
        EXPECT_EQ(draw.Value()->blend, Client::World::ParticleDraw::Blend::Additive);
    }
    const auto missing = Client::World::ParticleDrawChecked(particle, std::nullopt, std::uint8_t{3});
    ASSERT_FALSE(missing.HasValue());
    EXPECT_EQ(missing.Error(), Client::World::ParticleError::MissingTextureSize);
    particle.position.z = 351;
    const auto hidden = Client::World::ParticleDrawChecked(particle, std::nullopt, std::nullopt, 0, 0, 1);
    ASSERT_TRUE(hidden.HasValue());
    EXPECT_FALSE(hidden.Value().has_value());
}
} // namespace
