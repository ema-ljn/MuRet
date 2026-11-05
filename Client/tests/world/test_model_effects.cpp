#include "World/ModelEffects.h"

#include "Render/Models/Skeleton.h"

#include "support/DragonVectors.h"
#include "support/WorldObjectVectors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

// Client::World::ModelEffects con el dragón del login (06 § El dragón de fondo), contra lo que hizo el
// código real del main.exe: 0x518cf0 con el 0xb4 y 1.500 pasos de 0x5208a0 (tests/vectors/world55,
// make_dragon_vectors.py), bit a bit.
namespace
{
using Client::Tests::DragonVectors;
using Client::Tests::DragonVectorState;
using Client::World::DragonEffect;
using Client::World::ModelEffect;
using Client::World::ModelEffects;

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

// El esqueleto del dragón para el paso: la acción de Dragon.bmd, 41 keys que repiten (el +0 en 0, como
// la del generador).
Client::Render::Models::Skeleton DragonSkeleton()
{
    Client::Render::Models::Skeleton skeleton;
    skeleton.actions = {
        Client::Render::Models::ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 41, .lockPositions = false}};
    return skeleton;
}

void ExpectSameState(const ModelEffect& effect, const DragonVectorState& expected, const std::string& where)
{
    EXPECT_EQ(effect.alive, expected.alive) << where << ": vivo";
    EXPECT_EQ(effect.type, expected.type) << where << ": el tipo";
    EXPECT_EQ(Bits(effect.transform.scale), Bits(expected.scale)) << where << ": la escala";
    for (int k = 0; k < 3; ++k)
    {
        EXPECT_EQ(Bits(effect.transform.position[k]), Bits(expected.position[k])) << where << ": la posición " << k;
        EXPECT_EQ(Bits(effect.transform.angles[k]), Bits(expected.angles[k])) << where << ": el ángulo " << k;
        EXPECT_EQ(Bits(effect.velocity[k]), Bits(expected.velocity[k])) << where << ": la velocidad " << k;
        EXPECT_EQ(Bits(effect.light[k]), Bits(expected.light[k])) << where << ": la luz " << k;
        EXPECT_EQ(Bits(effect.start[k]), Bits(expected.start[k])) << where << ": la partida " << k;
    }
    EXPECT_EQ(effect.life, expected.life) << where << ": la vida";
    EXPECT_EQ(Bits(effect.speed), Bits(expected.speed)) << where << ": la velocidad de la animación";
    EXPECT_EQ(Bits(effect.turnPending), Bits(expected.turnPending)) << where << ": +0xdc";
    EXPECT_EQ(Bits(effect.turnDirection), Bits(expected.turnDirection)) << where << ": +0xe4";
    EXPECT_EQ(Bits(effect.flagEc), Bits(expected.flagEc)) << where << ": +0xec";
    EXPECT_EQ(effect.state, expected.state) << where << ": el estado";
    EXPECT_EQ(Bits(effect.reference), Bits(expected.reference)) << where << ": +0x84";
    EXPECT_EQ(Bits(effect.animation.frame), Bits(expected.frame)) << where << ": el frame";
    EXPECT_EQ(Bits(static_cast<float>(effect.animation.previousKey)), Bits(expected.previousFrame))
        << where << ": el frame anterior";
    EXPECT_EQ(effect.animation.action, expected.action) << where << ": la acción";
    EXPECT_EQ(Bits(effect.alpha), Bits(expected.alpha)) << where << ": el alpha";
    EXPECT_EQ(effect.objectState.hiddenMesh, expected.hiddenMesh) << where << ": la malla oculta";
    EXPECT_EQ(effect.objectState.blendMesh, expected.blendMesh) << where << ": el blendMesh";
    EXPECT_EQ(Bits(effect.objectState.light), Bits(expected.blendMeshLight)) << where << ": la luz del blendMesh";
    EXPECT_EQ(effect.vertexLight, expected.vertexLight) << where << ": la luz por vértice";
}

class DragonTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto error =
            Client::Tests::ReadDragonVectors(Client::Tests::World55VectorsDir() / "dragon.txt", m_vectors);
        ASSERT_FALSE(error.has_value()) << *error;
    }

    // 0x606660 con el dueño del dragón (el 0xb4 de la carga del mapa, en (5.600; 23.000; 0)): 0x518cf0
    // con su posición, ángulos 0, luz blanca y escala 1,6.
    std::optional<std::size_t> CreateTheDragon(ModelEffects& effects) const
    {
        return effects.Create(DragonEffect, {5600.0f, 23000.0f, 0.0f}, glm::vec3(0.0f), glm::vec3(1.0f),
                              Client::World::DragonScale);
    }

    DragonVectors m_vectors;
};

TEST_F(DragonTest, ItIsCreatedAsTheBinary)
{
    ModelEffects effects;
    const auto index = CreateTheDragon(effects);
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(*index, 0u) << "el primer lugar libre";
    ExpectSameState(effects.Effects()[*index], m_vectors.created, "al crearse");
}

TEST_F(DragonTest, FifteenHundredStepsMatchTheBinary)
{
    ModelEffects effects;
    const auto index = CreateTheDragon(effects);
    ASSERT_TRUE(index.has_value());
    const auto skeleton = DragonSkeleton();
    ASSERT_EQ(m_vectors.steps.size(), 1500u);
    for (std::size_t step = 0; step < m_vectors.steps.size(); ++step)
    {
        ModelEffect& dragon = effects.Effects()[*index];
        Client::World::StepModelEffect(dragon, &skeleton);
        ExpectSameState(dragon, m_vectors.steps[step], "en el paso " + std::to_string(step + 1));
        if (HasFailure())
        {
            break;
        }
    }
}

// 0x518cf0 toma el primer lugar sin el byte vivo, de los 200; 0x6070a0 los apaga todos (Q-C138).
TEST(ModelEffectsTest, TheTableHasTwoHundredPlaces)
{
    ModelEffects effects;
    for (std::size_t k = 0; k < Client::World::ModelEffectCount; ++k)
    {
        const auto index = effects.Create(DragonEffect, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 1.0f);
        ASSERT_TRUE(index.has_value());
        EXPECT_EQ(*index, k);
    }
    EXPECT_FALSE(effects.Create(DragonEffect, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 1.0f).has_value());
    effects.Clear();
    for (const ModelEffect& effect : effects.Effects())
    {
        EXPECT_FALSE(effect.alive);
    }
    EXPECT_EQ(effects.Create(DragonEffect, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 1.0f), 0u);
}

// De los tipos de 0x518cf0, MuRet solo arma el 0xb4: los demás no se crean.
TEST(ModelEffectsTest, OnlyTheDragonIsCreated)
{
    ModelEffects effects;
    EXPECT_FALSE(effects.Create(0xb5, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 1.0f).has_value());
}

// La vida baja uno por paso y se vuelve a llenar en 10 (0x520b3b): el dragón no muere nunca.
TEST_F(DragonTest, TheDragonNeverDies)
{
    ModelEffects effects;
    const auto index = CreateTheDragon(effects);
    ASSERT_TRUE(index.has_value());
    const auto skeleton = DragonSkeleton();
    for (int step = 0; step < 5000; ++step)
    {
        Client::World::StepModelEffect(effects.Effects()[*index], &skeleton);
        ASSERT_TRUE(effects.Effects()[*index].alive) << step;
        ASSERT_GT(effects.Effects()[*index].life, 0) << step;
    }
}

// 0x518cf0 con otras entradas que las del login (emuladas en la revisión de la CL7.6 A): la z es la del
// dueño más 3.400 (0x518fc9), el alabeo va en 0 (0x518fb3), la luz en blanco (la salida 0x51a97b) y una
// escala de 0 o menos pasa a 0,9 (0x518d3c).
TEST(ModelEffectsTest, CreateIsTheBinaryWithOtherInputs)
{
    ModelEffects effects;
    const auto index =
        effects.Create(DragonEffect, {5600.0f, 23000.0f, 100.0f}, {5.0f, 7.0f, 9.0f}, {0.2f, 0.3f, 0.4f}, -1.0f);
    ASSERT_TRUE(index.has_value());
    const ModelEffect& dragon = effects.Effects()[*index];
    EXPECT_EQ(dragon.transform.position.z, 3500.0f);
    EXPECT_EQ(dragon.transform.angles, glm::vec3(5.0f, 0.0f, 9.0f));
    EXPECT_EQ(dragon.light, glm::vec3(1.0f));
    EXPECT_EQ(Bits(dragon.transform.scale), Bits(0.9f));
    EXPECT_EQ(dragon.start, dragon.transform.position);
}

// Las comparaciones de 0x5208a0 sin orden, como la x87: con +0xdc en NaN, `test ah, 0x40` (C3 prendido)
// no da la vuelta, y el dragón gira con +0xe4 en 1 hacia la meta del alabeo de su estado.
TEST(ModelEffectsTest, ANotANumberTurnIsNotATurn)
{
    ModelEffects effects;
    const auto index = effects.Create(DragonEffect, {5600.0f, 23000.0f, 0.0f}, glm::vec3(0.0f), glm::vec3(1.0f),
                                      Client::World::DragonScale);
    ASSERT_TRUE(index.has_value());
    ModelEffect& dragon = effects.Effects()[*index];
    dragon.transform.position.y += 2000.0f;
    dragon.turnPending = std::numeric_limits<float>::quiet_NaN();
    Client::World::StepModelEffect(dragon, nullptr);
    EXPECT_EQ(dragon.transform.angles.z, 1.5f);
    EXPECT_EQ(dragon.turnDirection, 1.0f);
    EXPECT_EQ(dragon.reference, 30.0f);
}

// Y con el frame en NaN, `test ah, 1` (C0 prendido) no lo vuelve a 0.
TEST(ModelEffectsTest, ANotANumberFrameIsNotReset)
{
    ModelEffects effects;
    const auto index = effects.Create(DragonEffect, {5600.0f, 23000.0f, 0.0f}, glm::vec3(0.0f), glm::vec3(1.0f),
                                      Client::World::DragonScale);
    ASSERT_TRUE(index.has_value());
    ModelEffect& dragon = effects.Effects()[*index];
    dragon.animation.frame = std::numeric_limits<float>::quiet_NaN();
    Client::World::StepModelEffect(dragon, nullptr);
    EXPECT_TRUE(std::isnan(dragon.animation.frame));
}
} // namespace
