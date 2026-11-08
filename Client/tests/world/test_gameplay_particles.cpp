#include "World/Particles.h"

#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <limits>

// Oráculos independientes del contrato final-runtime 53fe70/5446e0/548550, no del port.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;
constexpr std::int32_t Fire = 0x5d9;
constexpr std::int32_t Smoke = 0x5f2;

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

TEST(GameplayParticlesTest, FireCreationKeepsTheFourDistinctScaleAndRandomPaths)
{
    constexpr std::array<std::uint32_t, 4> Scales{0x3fb5c28fu, 0x3df5c28fu, 0x3f400000u, 0x3f400000u};
    for (std::int32_t subtype = 0; subtype < 4; ++subtype)
    {
        Particles particles;
        CrtRandom random(7); // R: 61,17422,15215; estados c40e66b9/bb6f8e98.
        const auto slot = particles.Create(Fire, {100, 200, 300}, {10, 20, 30}, {2, 3, 4}, subtype, 0.75f, 999, random);
        ASSERT_EQ(slot, 0u) << subtype;
        const Particle& particle = particles.All()[0];
        EXPECT_EQ(particle.life, 24);
        EXPECT_EQ(Bits(particle.scale), Scales[static_cast<std::size_t>(subtype)]);
        EXPECT_EQ(particle.velocity, glm::vec3(0, -4.5f, 0));
        EXPECT_EQ(particle.rotation, subtype < 2 ? 95.0f : 142.0f);
        EXPECT_EQ(random.State(), subtype < 2 ? 0xbb6f8e98u : 0xc40e66b9u);
        EXPECT_EQ(particle.angles, glm::vec3(10, 20, 30));
        EXPECT_EQ(particle.light, glm::vec3(2, 3, 4));
        EXPECT_EQ(particle.start, glm::vec3(100, 200, 300));
    }
}

TEST(GameplayParticlesTest, SmokeTwoDoesNotUseTheLoginCreation)
{
    Particles particles;
    CrtRandom random(7);
    const auto slot = particles.Create(Smoke, {100, 200, 300}, {10, 20, 30}, {2, 3, 4}, 2, 9, 999, random);
    ASSERT_EQ(slot, 0u);
    const Particle& particle = particles.All()[0];
    EXPECT_EQ(particle.life, 50);
    EXPECT_EQ(Bits(particle.scale), 0x3fa00000u);
    EXPECT_EQ(particle.rotation, 142.0f);
    EXPECT_EQ(particle.verticalSpeed, 7.5f);
    EXPECT_EQ(particle.angles, glm::vec3(10, 20, 30));
    EXPECT_EQ(particle.velocity, glm::vec3(0));
    EXPECT_EQ(random.State(), 0xbb6f8e98u);
}

TEST(GameplayParticlesTest, ConstructorReadsAliasedAnglesAfterWritingThePrefix)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle.light = {2, 3, 4};
    particle.rotationFactor = -7.25f;
    CrtRandom random(7);
    // A apunta a RGB del slot: su copia observa C ya escrito, no un snapshot de entrada.
    ASSERT_EQ(particles.Create(Fire, {100, 200, 300}, particle.light, {8, 9, 10}, 2, 0.75f, 0, random), 0u);
    EXPECT_EQ(particle.angles, glm::vec3(8, 9, 10));
    EXPECT_EQ(particle.rotationFactor, -7.25f);
}

TEST(GameplayParticlesTest, ExpiringFireStillMovesShrinksRisesAndWritesAnIntegerFrame)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = Fire,
                        .subtype = 0,
                        .scale = 1,
                        .position = {1, 2, 3},
                        .light = {2, 3, 4},
                        .life = 1,
                        .moves = true,
                        .velocity = {4, 5, 6}};
    CrtRandom random(7);
    Client::World::StepParticles(particles, random);
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.life, 0);
    EXPECT_EQ(particle.position.x, 5);
    EXPECT_EQ(particle.position.y, 7);
    EXPECT_EQ(Bits(particle.position.z), 0x4110a3d7u);
    EXPECT_EQ(Bits(particle.scale), 0x3f75c28fu);
    EXPECT_EQ(Bits(particle.field48), 3u); // Palabra int32 cruda, no float(3).
    EXPECT_EQ(particle.light, glm::vec3(2, 3, 4));
    EXPECT_EQ(random.State(), 0xc40e66b9u); // Sólo los dos draws de viento.
}

TEST(GameplayParticlesTest, FireDefaultsGrowFromTheRoundedAccumulatorThenDampVelocity)
{
    for (const std::int32_t subtype : {1, 2, 3})
    {
        Particles particles;
        Particle& particle = particles.All()[0];
        particle = Particle{.alive = true,
                            .type = Fire,
                            .subtype = subtype,
                            .scale = 0.5f,
                            .life = 24,
                            .verticalSpeed = 0.25f,
                            .velocity = {1, -2, 3}};
        CrtRandom random(7);
        Client::World::StepParticles(particles, random);
        EXPECT_EQ(Bits(particle.scale), 0x3f410625u);
        EXPECT_EQ(Bits(particle.velocity.x), 0x3f7ae148u);
        EXPECT_EQ(Bits(particle.velocity.y), 0xbffae148u);
        EXPECT_EQ(Bits(particle.velocity.z), 0x403c28f6u);
        EXPECT_EQ(Bits(particle.field48), 0u);
    }
}

TEST(GameplayParticlesTest, SmokeTwoUsesTheUnroundedSpeedForBothPositionStores)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = Smoke,
                        .subtype = 2,
                        .scale = 1.25f,
                        .position = {100, 200, 300},
                        .light = {2, 3, 4},
                        .life = 50,
                        .verticalSpeed = 7.5f};
    CrtRandom random(7);
    Client::World::StepParticles(particles, random);
    EXPECT_EQ(Bits(particle.light.x), 0x3f7ae147u);
    EXPECT_EQ(particle.light.y, particle.light.x);
    EXPECT_EQ(particle.light.z, particle.light.x);
    EXPECT_EQ(Bits(particle.verticalSpeed), 0x40eccccdu);
    EXPECT_EQ(Bits(particle.position.x), 0x42c50a3du);
    EXPECT_EQ(particle.position.y, 200);
    EXPECT_EQ(Bits(particle.position.z), 0x4399b333u);
    EXPECT_EQ(Bits(particle.scale), 0x3f9eb852u);
}

TEST(GameplayParticlesTest, LifeDecrementWrapsBeforeTheSignedExpiryTest)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true, .type = Fire, .subtype = 0, .life = std::numeric_limits<std::int32_t>::min()};
    CrtRandom random(7);
    Client::World::StepParticles(particles, random);
    EXPECT_TRUE(particle.alive);
    EXPECT_EQ(particle.life, std::numeric_limits<std::int32_t>::max());
    // wrap(23-INT_MAX)/6 = -357913937, truncado hacia cero.
    EXPECT_EQ(std::bit_cast<std::int32_t>(particle.field48), -357913937);
}

TEST(GameplayParticlesTest, FireDrawUsesSignedIntegerAtlasAndAnglesX)
{
    Particle particle{.alive = true,
                      .type = Fire,
                      .texture = 17,
                      .subtype = 3,
                      .scale = 1.5f,
                      .angles = {17, 22, 33},
                      .rotation = 95,
                      .field48 = std::bit_cast<float>(-5)};
    const auto draw = Client::World::ParticleDrawOf(particle, {128, 64}, 4);
    ASSERT_TRUE(draw.has_value());
    EXPECT_EQ(draw->texture, 17);
    EXPECT_EQ(draw->width, 48);
    EXPECT_EQ(draw->height, 96);
    EXPECT_EQ(draw->rotation, 17);
    EXPECT_EQ(draw->uv, glm::vec2(-0.25f, 0));
    EXPECT_EQ(draw->uvSize, glm::vec2(0.25f, 1));
    EXPECT_EQ(draw->blend, Client::World::ParticleDraw::Blend::Alpha);
}

TEST(GameplayParticlesTest, SmokeTwoDrawIsSubtractiveAndUsesTheWholeTexture)
{
    Particle particle{.alive = true,
                      .type = Smoke,
                      .texture = 17,
                      .subtype = 2,
                      .scale = 1.5f,
                      .angles = {17, 22, 33},
                      .rotation = 95};
    const auto draw = Client::World::ParticleDrawOf(particle, {128, 64}, 3);
    ASSERT_TRUE(draw.has_value());
    EXPECT_EQ(draw->width, 192);
    EXPECT_EQ(draw->height, 96);
    EXPECT_EQ(draw->rotation, 95);
    EXPECT_EQ(draw->uv, glm::vec2(0));
    EXPECT_EQ(draw->uvSize, glm::vec2(1));
    EXPECT_EQ(draw->blend, Client::World::ParticleDraw::Blend::Subtractive);
}
// Debe fallar si se valida el binding antes de la salida nativa sin slot.
TEST(GameplayParticlesTest, AFullPoolIsSuccessWithoutEmissionEvenBeforeBindingDispatch)
{
    Particles particles;
    for (Particle& particle : particles.All())
    {
        particle.alive = true;
    }
    CrtRandom random(7);
    const auto created = particles.CreateChecked(std::numeric_limits<std::int32_t>::max(), {}, {}, {}, 0, 1, 0, random);
    ASSERT_TRUE(created.HasValue());
    EXPECT_FALSE(created.Value().has_value());
    EXPECT_EQ(random.State(), 7u);
}

TEST(GameplayParticlesTest, CheckedCreationReportsUnsupportedBindingsWithoutMutatingOrDrawingRandom)
{
    Particles particles;
    CrtRandom random(7);
    const auto type = particles.CreateChecked(std::numeric_limits<std::int32_t>::max(), {}, {}, {}, 0, 1, 0, random);
    ASSERT_FALSE(type.HasValue());
    EXPECT_EQ(type.Error(), Client::World::ParticleError::UnsupportedType);
    const auto subtype = particles.CreateChecked(Fire, {}, {}, {}, 5, 1, 0, random);
    ASSERT_FALSE(subtype.HasValue());
    EXPECT_EQ(subtype.Error(), Client::World::ParticleError::UnsupportedSubtype);
    EXPECT_FALSE(particles.Any());
    EXPECT_EQ(random.State(), 7u);
}

TEST(GameplayParticlesTest, ReusePreservesAllRetainedWordsAndCopiesTheOwner)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle.field38 = 0xdeadbeefu;
    particle.rotationFactor = -7.25f;
    particle.field64 = {31, 32};
    CrtRandom random(7);
    const auto created = particles.CreateChecked(Fire, {}, {}, {}, 2, 1, 0, random, 0xfedcba98u);
    ASSERT_TRUE(created.HasValue());
    ASSERT_EQ(created.Value(), 0u);
    EXPECT_EQ(particle.field38, 0xdeadbeefu);
    EXPECT_EQ(particle.rotationFactor, -7.25f);
    EXPECT_EQ(particle.field64, glm::vec2(31, 32));
    EXPECT_EQ(particle.owner, 0xfedcba98u);
}

TEST(GameplayParticlesTest, SmokeTwoRetainsTheX87ValueAfterStoringItsRoundedSpeed)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true, .type = Smoke, .subtype = 2, .life = 1, .verticalSpeed = 1};
    CrtRandom random(7);
    Client::World::StepParticles(particles, random);
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(Bits(particle.position.x), 0xbe3851ecu); // recargar +50 produciría be3851eb.
    EXPECT_EQ(particle.light, glm::vec3(0));
}

TEST(GameplayParticlesTest, EmptyStepStillConsumesTheTwoWindDraws)
{
    Particles particles;
    CrtRandom random(7);
    Client::World::StepParticles(particles, random);
    EXPECT_FALSE(particles.Any());
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}

TEST(GameplayParticlesTest, CheckedDrawReportsMissingMetadataWithoutPretendingToDraw)
{
    Particle particle{.alive = true, .type = Fire, .texture = 17, .subtype = 0};
    const auto size = Client::World::ParticleDrawChecked(particle, std::nullopt, std::uint8_t{3});
    ASSERT_FALSE(size.HasValue());
    EXPECT_EQ(size.Error(), Client::World::ParticleError::MissingTextureSize);
    const auto material = Client::World::ParticleDrawChecked(particle, glm::vec2(128, 64), std::nullopt);
    ASSERT_FALSE(material.HasValue());
    EXPECT_EQ(material.Error(), Client::World::ParticleError::MissingMaterialFlag);
    const auto draw = Client::World::ParticleDrawChecked(particle, glm::vec2(128, 64), std::uint8_t{4});
    ASSERT_TRUE(draw.HasValue());
    ASSERT_TRUE(draw.Value().has_value());
    EXPECT_EQ(draw.Value()->texture, 17);
    EXPECT_EQ(draw.Value()->blend, Client::World::ParticleDraw::Blend::Alpha);
    particle.type = std::numeric_limits<std::int32_t>::max();
    const auto unsupported = Client::World::ParticleDrawChecked(particle, glm::vec2(128, 64), std::uint8_t{3});
    ASSERT_FALSE(unsupported.HasValue());
    EXPECT_EQ(unsupported.Error(), Client::World::ParticleError::UnsupportedType);
}

TEST(GameplayParticlesTest, HeightPassesOverlapAndKeepTheNativeUnorderedBehavior)
{
    struct Case
    {
        float z;
        bool lower;
        bool upper;
    };
    const std::array cases{Case{300, true, false},
                           Case{325, true, true},
                           Case{350, true, true},
                           Case{351, false, true},
                           Case{std::numeric_limits<float>::quiet_NaN(), true, false},
                           Case{std::numeric_limits<float>::infinity(), false, true},
                           Case{-std::numeric_limits<float>::infinity(), true, false}};
    for (const Case& value : cases)
    {
        Particle particle{.alive = true, .type = Fire, .subtype = 0, .position = {0, 0, value.z}};
        for (std::uint8_t mode = 0; mode < 4; ++mode)
        {
            const auto draw =
                Client::World::ParticleDrawChecked(particle, glm::vec2(128, 64), std::uint8_t{3}, 0, 0, mode);
            ASSERT_TRUE(draw.HasValue());
            const bool visible = mode == 1 ? value.lower : mode == 2 ? value.upper : true;
            EXPECT_EQ(draw.Value().has_value(), visible) << value.z << ": " << static_cast<int>(mode);
        }
    }
    Particle dead;
    const auto draw = Client::World::ParticleDrawChecked(dead, std::nullopt, std::nullopt);
    ASSERT_TRUE(draw.HasValue());
    EXPECT_FALSE(draw.Value().has_value());
}
TEST(GameplayParticlesTest, AtlasWidthRoundsTheTextureProductBeforeDividingByFour)
{
    Particle particle{.alive = true, .type = Fire, .subtype = 2, .scale = 0.5f};
    // Cinco unidades subnormales: primer store 2,5 -> 2; segundo store 0,5 -> 0.
    // Fusionar los productos daría 0,625 -> 1 unidad, alterando el ancho.
    const auto draw = Client::World::ParticleDrawOf(particle, {std::bit_cast<float>(5u), 64}, 3);
    ASSERT_TRUE(draw.has_value());
    EXPECT_EQ(Bits(draw->width), 0u);
}
} // namespace
