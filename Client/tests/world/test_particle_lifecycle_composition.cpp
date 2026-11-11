#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/ParticleDrawing.h"
#include "World/IParticleDrawingServices.h"
#include "Render/Models/Cloth.h"
#include "Render/ParticleBillboardRequest.h"
#include "Core/Expected.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>
#include <cstdint>
#include <functional>
#include <vector>
namespace
{
using namespace Client;
// Perfil CPU explícito: metadata fija y registro de fronteras D2, sin GL ni recursos reales.
class TraceServices final : public World::IParticleDrawingServices
{
public:
    std::vector<int> trace;
    std::vector<glm::vec3> positions;
    Core::Expected<glm::vec2, World::ParticleError> Dimensions(std::int32_t) override
    {
        trace.push_back(1);
        return glm::vec2(8, 16);
    }
    Core::Expected<std::uint8_t, World::ParticleError> MaterialFlag(std::int32_t) override
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
    Result Billboard(Render::ParticleBillboardRequest request) override
    {
        trace.push_back(7);
        positions.push_back(request.position.get());
        return {};
    }
};
TEST(ParticleLifecycleCompositionTest, RealBackendOrbitAndMap8SharePoolAndDispatch)
{
    World::Particles particles;
    Render::Models::CrtRandom random(7);
    World::PortableParticleTrigonometry trig;
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
    // Estado legal preparado: tras decremento u=(59-59)*f32(0.1)=0, seno0 y coseno1.
    p.velocity.x = -59;
    s.verticalSpeed = 0;
    ASSERT_TRUE(World::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trig).HasValue());
    EXPECT_EQ(p.life, 59);
    EXPECT_EQ(p.position.x, 2);
    EXPECT_EQ(p.position.y, -37);
    EXPECT_EQ(s.life, 10);
    EXPECT_EQ(s.position.z, s.rotation);
    EXPECT_EQ(s.scale, 1.7999999523162842f);
    TraceServices services;
    ASSERT_TRUE(World::DrawParticlesChecked(particles, 0, std::cref(clock), services).HasValue());
    ASSERT_EQ(services.positions.size(), 2u);
    EXPECT_EQ(services.positions[0], p.position);
    EXPECT_EQ(services.positions[1], s.position);
    EXPECT_EQ(services.trace.front(), 1);
    EXPECT_EQ(services.trace.back(), 7);
}
TEST(ParticleLifecycleCompositionTest, LegacyStillDeniesBothNewFamilies)
{
    World::Particles particles;
    Render::Models::CrtRandom random(7);
    EXPECT_FALSE(particles.CreateChecked(0x60f, {}, {}, {}, 3, 1, 0, random).HasValue());
    EXPECT_FALSE(particles.CreateChecked(0x5f2, {}, {}, {}, 6, 1, 0, random).HasValue());
    EXPECT_EQ(random.State(), 7u);
}
} // namespace
