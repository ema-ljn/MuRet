#include "Crt/CrtRandom.h"
#include "Particles/Particles.h"
#include "Particles/ParticleOwner.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Particles/ParticleDrawing.h"
#include "Particles/IParticleDrawingServices.h"
#include "Render/ParticleBillboardRequest.h"
#include "Render/TextureEnvMode.h"
#include "Core/Expected.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>
namespace
{
using namespace Client;
// Explicit CPU profile: fixed metadata and D2 boundary log, without GL or real resources.
class TraceServices final : public Particles::IParticleDrawingServices
{
public:
    std::vector<int> trace;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> lights;
    Core::Expected<glm::vec2, Particles::ParticleError> Dimensions(std::int32_t) override
    {
        trace.push_back(1);
        return glm::vec2(8, 16);
    }
    Core::Expected<std::uint8_t, Particles::ParticleError> MaterialFlag(std::int32_t) override
    {
        trace.push_back(2);
        return std::uint8_t{3};
    }
    Result Material(std::uint8_t flag) override
    {
        EXPECT_EQ(flag, 3);
        trace.push_back(3);
        return {};
    }
    Result Subtractive() override
    {
        trace.push_back(4);
        return {};
    }
    Result Blended() override
    {
        trace.push_back(7);
        return {};
    }
    Result Type620850() override
    {
        trace.push_back(5);
        return {};
    }
    Result Type620870() override
    {
        trace.push_back(6);
        return {};
    }
    Result TextureEnv(Render::TextureEnvMode) override
    {
        trace.push_back(8);
        return {};
    }
    Result Billboard(Render::ParticleBillboardRequest request) override
    {
        trace.push_back(7);
        positions.push_back(request.position.get());
        lights.push_back(request.light.get());
        return {};
    }
};
TEST(ParticleLifecycleCompositionTest, RealBackendOrbitAndMap8SharePoolAndDispatch)
{
    Particles::Particles particles;
    Crt::CrtRandom random(7);
    Particles::PortableParticleTrigonometry trig;
    const float clock = 0;
    const auto orbit =
        particles.CreateChecked(0x60f, {2, 3, 4}, {}, {1, 2, 3}, 3, 2, std::cref(clock), random, 91, trig);
    ASSERT_TRUE(orbit.HasValue());
    ASSERT_EQ(orbit.Value(), 0u);
    const auto smoke =
        particles.CreateChecked(0x5f2, {5, 6, 7}, {}, {1, 1, 1}, 6, 1, std::cref(clock), random, 92, trig);
    ASSERT_TRUE(smoke.HasValue());
    ASSERT_EQ(smoke.Value(), 1u);
    auto& p = particles.All()[0];
    auto& s = particles.All()[1];
    EXPECT_EQ(p.life, 60);
    EXPECT_EQ(p.velocity.x, -128);
    EXPECT_EQ(p.rotation, 200);
    // Prepared legal state: after the decrement u=(59-59)*f32(0.1)=0, sine 0 and cosine 1.
    p.velocity.x = -59;
    s.verticalSpeed = 0;
    ASSERT_TRUE(Particles::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trig).HasValue());
    EXPECT_EQ(p.life, 59);
    EXPECT_EQ(p.position.x, 2);
    EXPECT_EQ(p.position.y, -37);
    EXPECT_EQ(s.life, 10);
    EXPECT_EQ(s.position.z, s.rotation);
    EXPECT_EQ(s.scale, 1.7999999523162842f);
    TraceServices services;
    ASSERT_TRUE(Particles::DrawParticlesChecked(particles, 0, std::cref(clock), services).HasValue());
    ASSERT_EQ(services.positions.size(), 2u);
    EXPECT_EQ(services.positions[0], p.position);
    EXPECT_EQ(services.positions[1], s.position);
    EXPECT_EQ(services.trace.front(), 1);
    EXPECT_EQ(services.trace.back(), 7);
}
TEST(ParticleLifecycleCompositionTest, LegacyStillDeniesBothNewFamilies)
{
    Particles::Particles particles;
    Crt::CrtRandom random(7);
    EXPECT_FALSE(particles.CreateChecked(0x60f, {}, {}, {}, 3, 1, 0, random).HasValue());
    EXPECT_FALSE(particles.CreateChecked(0x5f2, {}, {}, {}, 6, 1, 0, random).HasValue());
    EXPECT_EQ(random.State(), 7u);
}
} // namespace

TEST(ParticleLifecycleCompositionTest, PiercingSparkRetainsItsPhysicalParentAndMovesThroughDeath)
{
    Particles::Particles particles;
    auto& retained = particles.All()[0];
    retained.rotationFactor = 7;
    retained.field64 = {8, 9};
    retained.field38 = 0x12345678;
    const Particles::ParticleOwner owner{Particles::ParticleOwnerKind::ModelEffect, 37};
    const auto slot = particles.CreatePiercingSpark({10, 20, 30}, 0.4f, owner, {110, 220, 330});
    ASSERT_TRUE(slot);
    auto& particle = particles.All()[*slot];
    EXPECT_EQ(particle.velocity, (glm::vec3{10, 20, 30}));
    EXPECT_EQ(particle.field38, 0x12345678u);
    EXPECT_FLOAT_EQ(particle.rotationFactor, 7);
    EXPECT_EQ(particle.field64, (glm::vec2{8, 9}));
    float clock = 0;
    Particles::PortableParticleTrigonometry trig;
    Crt::CrtRandom random(3);
    int calls = 0;
    bool parentAlive = true;
    const Particles::ParticleOwnerBones parent = [&](const Particles::Particle& child) -> std::optional<glm::vec3>
    {
        ++calls;
        EXPECT_EQ(child.typedOwner, owner);
        return parentAlive ? std::optional(glm::vec3{999, 888, 777}) : std::nullopt;
    };
    const auto step = [&]
    {
        return Particles::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trig, {}, {}, {}, {},
                                               nullptr, {}, parent);
    };
    ASSERT_TRUE(step().HasValue());
    EXPECT_EQ(particle.life, 14);
    EXPECT_EQ(particle.position, (glm::vec3{20, 40, 60}));
    EXPECT_EQ(particle.start, particle.position);
    TraceServices draw;
    ASSERT_TRUE(Particles::DrawParticlesChecked(particles, 0, std::cref(clock), draw).HasValue());
    ASSERT_EQ(draw.positions.size(), 1u);
    EXPECT_EQ(draw.positions.front(), particle.position);
    ASSERT_EQ(draw.lights.size(), 1u);
    EXPECT_EQ(draw.lights.front(), particle.light);
    parentAlive = false;
    ASSERT_TRUE(step().HasValue());
    EXPECT_EQ(particle.life, 5);
    EXPECT_EQ(particle.light, glm::vec3(1));
    EXPECT_EQ(particle.position, (glm::vec3{30, 60, 90}));
    parentAlive = true;
    for (int tick = 0; tick < 5; ++tick)
        ASSERT_TRUE(step().HasValue());
    EXPECT_EQ(calls, 2);
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.life, 0);
    EXPECT_EQ(particle.position, (glm::vec3{80, 160, 240}));
    EXPECT_EQ(particle.start, particle.position);
    EXPECT_EQ(particle.light, glm::vec3(0));
    EXPECT_FLOAT_EQ(particle.scale, 0.75f);
}
