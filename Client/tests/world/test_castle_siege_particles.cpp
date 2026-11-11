#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "World/KalimaParticleKernels.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"

#include "support/ParticleDrawRecorder.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

// Las partículas que sueltan los objetos de Castle Siege (el mapa 30) y que no comparten todo con las de Kalima y
// Crywolf: la caída 6f4/6 de los braseros 0x34 y el dibujo de la chispa 6ec/0 de las antorchas (06 § Los objetos de
// Castle Siege). El alta y el paso de la 6ec/0 son los de la 6ed/0 (CrywolfParticlesTest).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Tests::RecordedParticleDraw;
using Client::World::Particle;
using Client::World::Particles;

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

CrtRandom After(std::uint32_t seed, int draws)
{
    CrtRandom random(seed);
    for (int draw = 0; draw < draws; ++draw)
        static_cast<void>(random.Next());
    return random;
}
} // namespace

// 0x543f04..0x543f47: la 6f4/6 nace con vida 30, el giro rand() % 360, la escala del argumento más 0,6f (0x6b8a20),
// la caída −12 − rand() % 5 y la luz en 0,2f; la 6f4/0 de Kalima, en cambio, tiene la escala 1,6f y la caída −7 −
// rand() % 5.
TEST(CastleSiegeParticlesTest, TheBrazierFallAddsSixTenthsToItsScaleAndFallsFaster)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(29);
    CrtRandom draws(29);
    const glm::vec3 position(6799.0f, 13792.0f, 60.0f);
    const float scale = std::bit_cast<float>(0x3fc7ae14u);
    const auto created =
        particles->CreateChecked(Client::World::KalimaWaterfallLast, position, {0.0f, 0.0f, 45.0f}, glm::vec3(1.0f),
                                 Client::World::BrazierFallSubtype, scale, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const Particle& fall = particles->All()[*created.Value()];
    EXPECT_EQ(fall.life, 30);
    EXPECT_EQ(fall.rotation, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(fall.scale, F32(D(scale) + D(std::bit_cast<float>(0x3f19999au))));
    EXPECT_EQ(fall.velocity, glm::vec3(0.0f, 0.0f, static_cast<float>(-12 - draws.Next() % 5)));
    EXPECT_EQ(fall.light, glm::vec3(std::bit_cast<float>(0x3e4ccccdu)));
    EXPECT_EQ(fall.position, position);
    EXPECT_TRUE(fall.moves);
    EXPECT_EQ(random.State(), draws.State());

    // Los demás subtipos del 0x6f4 que no salen de los mapas portados siguen sin alta.
    EXPECT_FALSE(
        particles
            ->CreateChecked(Client::World::KalimaWaterfallLast, position, {}, glm::vec3(1.0f), 5, scale, 0.0f, random)
            .HasValue());
    EXPECT_EQ(random.State(), draws.State());
}

// 0x547e7e..0x547fa7: la /6 no es la /0 ni la /5, que restan 0,005f a la escala y suman 0,1f a la caída; salta a
// 0x547fc1, que solo cambia la luz: por 1,1f (0x6b8830) con la vida en más de 20 y por 0,8333f (0x6b8b98) con menos
// de 8. El movimiento común (0x4e98d0) la baja con su caída girada por los ángulos. No saca rand() además del viento.
TEST(CastleSiegeParticlesTest, TheBrazierFallKeepsItsScaleAndSpeedAndOnlyChangesItsLight)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(31);
    const auto created =
        particles->CreateChecked(Client::World::KalimaWaterfallLast, {6799.0f, 13792.0f, 60.0f}, {0.0f, 0.0f, 45.0f},
                                 glm::vec3(1.0f), Client::World::BrazierFallSubtype, 1.5f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const Particle& fall = particles->All()[*created.Value()];
    Particle expected = fall;
    const std::uint32_t born = random.State();
    constexpr double Brighten = static_cast<double>(std::bit_cast<float>(0x3f8ccccdu));
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f555555u));
    const float clock = 2500.0f;
    Client::World::PortableParticleTrigonometry trigonometry;
    constexpr int Steps = 30;
    for (int step = 1; step <= Steps; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::World::StepParticlesChecked(*particles, random, std::cref(clock), nullptr, trigonometry)
                        .HasValue());
        expected.life -= 1;
        const glm::vec3 turned = Client::Render::Models::OriginalVectorRotate(
            expected.velocity, Client::Render::Models::OriginalAngleMatrix(expected.angles));
        for (int axis = 0; axis < 3; ++axis)
            expected.position[axis] = F32(D(turned[axis]) + D(expected.position[axis]));
        const double factor = expected.life < 8 ? Fade : expected.life > 20 ? Brighten : 1.0;
        if (factor != 1.0)
        {
            for (int axis = 0; axis < 3; ++axis)
                expected.light[axis] = F32(D(expected.light[axis]) * factor);
        }
        EXPECT_EQ(fall.alive, expected.life > 0);
        EXPECT_EQ(fall.life, expected.life);
        EXPECT_EQ(fall.position, expected.position);
        EXPECT_EQ(fall.light, expected.light);
        EXPECT_EQ(fall.scale, expected.scale);
        EXPECT_EQ(fall.velocity, expected.velocity);
        EXPECT_EQ(fall.rotation, expected.rotation);
    }
    EXPECT_EQ(random.State(), After(born, 2 * Steps).State());
}

// 0x548550: el 0x6ec y el 0x6f4 caen al default (0x54889a), el billboard común con su textura, el ancho y el alto de
// la imagen por la escala, la luz y el giro de +0x44, sin leer el subtipo ni pedir otra mezcla.
TEST(CastleSiegeParticlesTest, TheSparkAndTheFallDrawTheCommonBillboard)
{
    for (const auto& [type, subtype] :
         {std::pair<std::int32_t, std::int32_t>{Client::World::CrywolfFanta, 0},
          std::pair<std::int32_t, std::int32_t>{Client::World::KalimaWaterfallLast, Client::World::BrazierFallSubtype}})
    {
        SCOPED_TRACE(type);
        Particle particle;
        particle.alive = true;
        particle.type = particle.texture = type;
        particle.subtype = subtype;
        particle.scale = 1.25f;
        particle.position = {6799.0f, 13792.0f, 60.0f};
        particle.light = glm::vec3(0.75f, 0.5f, 0.25f);
        particle.rotation = 123.0f;
        const auto drawn = Client::Tests::DrawOneParticle(particle, glm::vec2(64.0f, 128.0f), std::uint8_t{3});
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        const RecordedParticleDraw& draw = *drawn.Value();
        EXPECT_EQ(draw.texture, type);
        EXPECT_EQ(draw.blend, RecordedParticleDraw::Blend::Additive);
        EXPECT_EQ(draw.position, particle.position);
        EXPECT_EQ(draw.width, 80.0f);
        EXPECT_EQ(draw.height, 160.0f);
        EXPECT_EQ(draw.light, particle.light);
        EXPECT_EQ(draw.rotation, 123.0f);
        EXPECT_EQ(draw.uv, glm::vec2(0.0f));
        EXPECT_EQ(draw.uvSize, glm::vec2(1.0f));
    }
}
