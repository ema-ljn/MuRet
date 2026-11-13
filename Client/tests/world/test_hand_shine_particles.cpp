#include "World/Particles.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "World/IParticleTrigonometry.h"
#include "World/ParticleOwner.h"

#include "support/ParticleDrawRecorder.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// El brillo de la mano 5fd (CL9 hito 2, el 1A): su alta (0x542b0e), su paso (0x546cd4) y su dibujo (0x5488f6). Las
// posiciones y los dueños son inventados; los valores esperados salen del ASM.
namespace
{
using Client::World::HandShineParticle;
using Client::World::Particle;
using Client::World::ParticleError;
using Client::World::ParticleOwner;
using Client::World::ParticleOwnerKind;
using Client::World::Particles;

constexpr std::uint64_t OwnerSlot = 7;
constexpr ParticleOwner Owner{ParticleOwnerKind::Actor, OwnerSlot};
// 0x6ba368: 5° en float.
constexpr float PulseStep = std::bit_cast<float>(0x3db2b8c2u);

// Un seno de mentira: anota el argumento y el factor, y devuelve `product`.
class ScriptedSine final : public Client::World::IParticleTrigonometry
{
public:
    mutable std::vector<double> arguments;
    mutable std::vector<float> factors;
    double product = 0.0;
    Client::Core::Expected<double, ParticleError> SineProductPC53(double argument, float factor) const override
    {
        arguments.push_back(argument);
        factors.push_back(factor);
        return product;
    }
    Client::Core::Expected<double, ParticleError> CosineProductPC53(double, float) const override
    {
        ADD_FAILURE() << "la 5fd no pide el coseno";
        return 0.0;
    }
};

class HandShineTest : public ::testing::Test
{
protected:
    std::size_t Create(std::int32_t subtype)
    {
        const auto created =
            m_particles->CreateOwnedChecked(HandShineParticle, {10.0f, 20.0f, 30.0f}, {1.0f, 2.0f, 3.0f},
                                            {1.0f, 0.5f, 0.2f}, subtype, 0.0f, 0.0f, m_random, 0, Owner);
        EXPECT_TRUE(created.HasValue());
        EXPECT_TRUE(created.HasValue() && created.Value().has_value());
        return created.HasValue() ? created.Value().value_or(0) : 0;
    }

    Client::Core::Expected<void, ParticleError> Step(const Client::World::ParticleActorHands& hands)
    {
        const std::int32_t map = 0;
        return Client::World::StepParticlesChecked(*m_particles, m_random, std::cref(m_clock), &map, m_sine, {}, {}, {},
                                                   {}, nullptr, hands);
    }

    std::unique_ptr<Particles> m_particles = std::make_unique<Particles>();
    Client::Render::Models::CrtRandom m_random;
    ScriptedSine m_sine;
    float m_clock = 0.0f;
    std::vector<std::int32_t> m_handSubtypes;
    Client::World::ParticleActorHands m_hands = [this](std::uint64_t slot,
                                                       std::int32_t subtype) -> std::optional<glm::vec3>
    {
        EXPECT_EQ(slot, OwnerSlot);
        m_handSubtypes.push_back(subtype);
        return glm::vec3(100.0f + static_cast<float>(subtype), 200.0f, 300.0f);
    };
};

// 0x53fe9c..0x53ff37 y 0x542b0e..0x542b1b: el prefijo común, la vida 36 y el ángulo x en 45; sin rand() ni reloj. La
// posición de 0x53fdb0 la pone quien la crea: la partícula queda donde se la pidió.
TEST_F(HandShineTest, ItIsBornWithThirtySixStepsTiltedFortyFiveDegrees)
{
    const auto seed = m_random.State();
    const std::size_t index = Create(2);
    const Particle& shine = m_particles->All()[index];
    EXPECT_TRUE(shine.alive);
    EXPECT_EQ(shine.type, HandShineParticle);
    EXPECT_EQ(shine.texture, HandShineParticle);
    EXPECT_EQ(shine.subtype, 2);
    EXPECT_EQ(shine.life, 36);
    EXPECT_EQ(shine.angles, glm::vec3(45.0f, 2.0f, 3.0f));
    EXPECT_EQ(shine.position, glm::vec3(10.0f, 20.0f, 30.0f));
    EXPECT_EQ(shine.start, glm::vec3(10.0f, 20.0f, 30.0f));
    EXPECT_EQ(shine.light, glm::vec3(1.0f, 0.5f, 0.2f));
    EXPECT_EQ(shine.scale, 0.0f);
    EXPECT_EQ(shine.typedOwner, Owner);
    EXPECT_EQ(m_random.State(), seed);
}

// Sólo la /0 y la /2, las de 0x53d810, y con un personaje de dueño: sin él, el paso no tendría la mano.
TEST_F(HandShineTest, OnlyTheTwoSubtypesOfAPersonAreBorn)
{
    const auto odd = m_particles->CreateOwnedChecked(HandShineParticle, {}, {}, {}, 1, 0.0f, 0.0f, m_random, 0, Owner);
    ASSERT_FALSE(odd.HasValue());
    EXPECT_EQ(odd.Error(), ParticleError::UnsupportedSubtype);
    const auto ownerless = m_particles->CreateChecked(HandShineParticle, {}, {}, {}, 0, 0.0f, 0.0f, m_random);
    ASSERT_FALSE(ownerless.HasValue());
    EXPECT_EQ(ownerless.Error(), ParticleError::MissingOwner);
    EXPECT_FALSE(m_particles->Any());
}

// 0x546d08..0x546d4f: la vida ya bajó; la escala es sin(vida · 5°) · 5 y la /0 no gira. Después, 0x53fdb0 con el
// subtipo la pone en la mano. Los dos rand() son los del viento.
TEST_F(HandShineTest, TheFirstOnePulsesWithItsLifeAndFollowsTheHand)
{
    const std::size_t index = Create(0);
    m_sine.product = 4.25;
    const auto seed = m_random.State();
    ASSERT_TRUE(Step(m_hands).HasValue());
    const Particle& shine = m_particles->All()[index];
    EXPECT_EQ(shine.life, 35);
    ASSERT_EQ(m_sine.arguments.size(), 1u);
    EXPECT_EQ(m_sine.arguments[0], 35.0 * static_cast<double>(PulseStep));
    EXPECT_EQ(m_sine.factors[0], 5.0f);
    EXPECT_EQ(shine.scale, 4.25f);
    EXPECT_EQ(shine.rotation, 0.0f);
    EXPECT_EQ(shine.position, glm::vec3(100.0f, 200.0f, 300.0f));
    EXPECT_EQ(m_handSubtypes, (std::vector<std::int32_t>{0}));
    Client::Render::Models::CrtRandom expected(seed);
    static_cast<void>(expected.Next());
    static_cast<void>(expected.Next());
    EXPECT_EQ(m_random.State(), expected.State());
}

// 0x546d21..0x546d34: de la /2 en adelante, el mismo producto de la pila por 0,75 y 12 grados menos de rotación.
TEST_F(HandShineTest, TheSecondOneIsSmallerAndTurns)
{
    const std::size_t index = Create(2);
    m_sine.product = 3.0;
    ASSERT_TRUE(Step(m_hands).HasValue());
    ASSERT_TRUE(Step(m_hands).HasValue());
    const Particle& shine = m_particles->All()[index];
    EXPECT_EQ(shine.life, 34);
    EXPECT_EQ(shine.scale, 2.25f);
    EXPECT_EQ(shine.rotation, -24.0f);
    EXPECT_EQ(shine.position, glm::vec3(102.0f, 200.0f, 300.0f));
    EXPECT_EQ(m_handSubtypes, (std::vector<std::int32_t>{2, 2}));
}

// Sin quién diga dónde está la mano, el paso corta después de la escala y la rotación.
TEST_F(HandShineTest, WithoutTheHandTheStepStopsAfterThePulse)
{
    const std::size_t index = Create(2);
    m_sine.product = 1.0;
    const auto stepped = Step({});
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_EQ(stepped.Error(), ParticleError::MissingOwner);
    const Particle& shine = m_particles->All()[index];
    EXPECT_EQ(shine.scale, 0.75f);
    EXPECT_EQ(shine.rotation, -12.0f);
    EXPECT_EQ(shine.position, glm::vec3(10.0f, 20.0f, 30.0f));
}

// 0x5488f6 → 0x54889a: el billboard común con la textura de la creación (Effect\Shiny02.jpg).
TEST_F(HandShineTest, ItIsDrawnWithTheCommonBillboard)
{
    Particle shine = m_particles->All()[Create(0)];
    shine.scale = 2.0f;
    const auto drawn = Client::Tests::DrawOneParticle(shine, glm::vec2(32.0f), std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(drawn.Value()->texture, HandShineParticle);
    EXPECT_EQ(drawn.Value()->position, glm::vec3(10.0f, 20.0f, 30.0f));
    EXPECT_EQ(drawn.Value()->light, glm::vec3(1.0f, 0.5f, 0.2f));
}
} // namespace
