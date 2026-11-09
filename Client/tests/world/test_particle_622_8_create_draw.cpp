#include "World/Particles.h"

#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <tuple>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;
constexpr std::int32_t CloudType = 0x622;
constexpr std::int32_t CloudSubtype = 8;

TEST(Particle622Subtype8CreateTest, UsesSevenOrderedRandomDrawsAndKeepsTheNativeFieldLayout)
{
    Particles particles;
    particles.All()[0].field64 = {6.0f, 7.0f};
    CrtRandom random(7);

    const auto created =
        particles.CreateChecked(CloudType, {2, 3, 4}, {}, {0.5f, 1.0f, 0.25f}, CloudSubtype, 0.4f, 0.0f, random, 0);

    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    EXPECT_EQ(*created.Value(), 0u);
    const Particle& particle = particles.All()[0];
    EXPECT_TRUE(particle.alive);
    EXPECT_EQ(particle.type, CloudType);
    EXPECT_EQ(particle.texture, CloudType);
    EXPECT_EQ(particle.subtype, CloudSubtype);
    EXPECT_EQ(particle.life, 300);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.field38), 0x3dcccccdu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.scale), 0x3ffae147u);
    EXPECT_EQ(particle.position, glm::vec3(174, -32, -16));
    EXPECT_EQ(particle.start.x, 2.0f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.start.y), 0x3e570a3du);
    EXPECT_EQ(particle.start.z, -16.0f);
    EXPECT_EQ(particle.light, glm::vec3(0.5f, 1.0f, 0.25f));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.rotationFactor), 0x3ed70a3eu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.verticalSpeed), 0x42740000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.start.z), std::bit_cast<std::uint32_t>(particle.position.z));
    EXPECT_EQ(particle.field64, glm::vec2(6.0f, 7.0f));
    EXPECT_EQ(particle.velocity, glm::vec3(0));
    EXPECT_EQ(random.State(), 0x75c8bedcu);
}

TEST(Particle622Subtype8DrawTest, UsesImageDimensionsTypeMaterialFlagAndWholeTexture)
{
    Particle particle{.alive = true,
                      .type = CloudType,
                      .texture = CloudType,
                      .subtype = CloudSubtype,
                      .scale = std::bit_cast<float>(0x3ffae147u),
                      .position = {2, 3, 4},
                      .light = {0.5f, 1.0f, 0.25f},
                      .field38 = 0x3dcccccdu,
                      .rotationFactor = std::bit_cast<float>(0x3ed70a3eu),
                      .start = {2, std::bit_cast<float>(0x3e570a3du), -16}};
    const glm::vec3 originalLight = particle.light;

    const auto even = Client::World::ParticleDrawChecked(particle, glm::vec2(100, 200), std::uint8_t{3}, 1.0f, 2);

    ASSERT_TRUE(even.HasValue());
    ASSERT_TRUE(even.Value().has_value());
    EXPECT_EQ(even.Value()->texture, CloudType);
    EXPECT_EQ(even.Value()->blend, Client::World::ParticleDraw::Blend::Additive);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(even.Value()->width), 0x4343ffffu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(even.Value()->height), 0x43c3ffffu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(even.Value()->rotation), 0x3e5fa43fu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(even.Value()->light.x), 0x3d4ccccdu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(even.Value()->light.y), 0x3dcccccdu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(even.Value()->light.z), 0x3ccccccdu);
    EXPECT_EQ(even.Value()->uv, glm::vec2(0));
    EXPECT_EQ(even.Value()->uvSize, glm::vec2(1));
    EXPECT_EQ(particle.light, originalLight);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.rotation), 0x3e5fa43fu);

    const auto odd = Client::World::ParticleDrawChecked(particle, glm::vec2(100, 200), std::uint8_t{1}, 1.0f, 3);
    ASSERT_TRUE(odd.HasValue());
    ASSERT_TRUE(odd.Value().has_value());
    EXPECT_EQ(odd.Value()->blend, Client::World::ParticleDraw::Blend::Alpha);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(odd.Value()->rotation), 0x3e4e703bu);
}

TEST(Particle622Subtype8CreateTest, AFullPoolReturnsSuccessWithoutReadsOrRandomDraws)
{
    Particles particles;
    for (Particle& particle : particles.All())
    {
        particle.alive = true;
    }
    CrtRandom random(7);

    const auto created = particles.CreateChecked(CloudType, {}, {}, {}, CloudSubtype, 0.4f, 0, random);

    ASSERT_TRUE(created.HasValue());
    EXPECT_FALSE(created.Value().has_value());
    EXPECT_EQ(random.State(), 7u);
}

TEST(Particle622Subtype8DrawTest, PreservesHeightPassBoundariesAndUnorderedComparison)
{
    Particle particle{.alive = true, .type = CloudType, .texture = CloudType, .subtype = CloudSubtype};
    for (const auto& [height, mode, expected] :
         {std::tuple{350.0f, std::uint8_t{1}, true}, std::tuple{351.0f, std::uint8_t{1}, false},
          std::tuple{300.0f, std::uint8_t{2}, false}, std::tuple{301.0f, std::uint8_t{2}, true},
          std::tuple{std::bit_cast<float>(0x7fc12345u), std::uint8_t{1}, true},
          std::tuple{std::bit_cast<float>(0x7fc12345u), std::uint8_t{2}, false}})
    {
        particle.position.z = height;
        const auto draw = Client::World::ParticleDrawChecked(particle, glm::vec2(1), std::uint8_t{3}, 0, 0, mode);
        ASSERT_TRUE(draw.HasValue()) << height << ", " << static_cast<int>(mode);
        EXPECT_EQ(draw.Value().has_value(), expected) << height << ", " << static_cast<int>(mode);
    }
}

TEST(Particle622Subtype8DrawTest, DoesNotClampTheTemporaryRgbProduct)
{
    Particle particle{.alive = true,
                      .type = CloudType,
                      .texture = CloudType,
                      .subtype = CloudSubtype,
                      .scale = 1.0f,
                      .light = {2.0f, -1.0f, 0.5f},
                      .field38 = std::bit_cast<std::uint32_t>(2.0f)};

    const auto draw = Client::World::ParticleDrawChecked(particle, glm::vec2(1), std::uint8_t{0});

    ASSERT_TRUE(draw.HasValue());
    ASSERT_TRUE(draw.Value().has_value());
    EXPECT_EQ(draw.Value()->blend, Client::World::ParticleDraw::Blend::Alpha);
    EXPECT_EQ(draw.Value()->light, glm::vec3(4.0f, -2.0f, 1.0f));
    EXPECT_EQ(particle.light, glm::vec3(2.0f, -1.0f, 0.5f));
}
} // namespace
