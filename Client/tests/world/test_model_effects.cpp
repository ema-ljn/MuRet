#include "World/ModelEffects.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "World/Sprites.h"
#include "World/WorldObjects.h"

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
#include <set>
#include <string>
#include <utility>

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

// Q-C278: 0x518cf0 no escribe +0x16c. El efecto nuevo hereda la visibilidad del lugar, aun después de
// 0x6070a0 (Clear), y no la de otro lugar de la tabla.
TEST(ModelEffectsTest, ReusingASlotRetainsItsLastDrawVisibility)
{
    ModelEffects effects;
    const auto first = effects.Create(DragonEffect, {}, {}, {}, 1.0f);
    ASSERT_EQ(first, 0u);
    effects.Effects()[0].seen = true;
    const auto second = effects.Create(Client::World::ArrivalEffect, {}, {}, {}, 1.0f);
    ASSERT_EQ(second, 1u);
    EXPECT_FALSE(effects.Effects()[1].seen);
    effects.Clear();
    EXPECT_TRUE(effects.Effects()[0].seen);
    ASSERT_EQ(effects.Create(Client::World::ArrivalEffect, {}, {}, {}, 1.0f), 0u);
    EXPECT_TRUE(effects.Effects()[0].seen);
    EXPECT_FALSE(effects.Effects()[1].seen);
    effects.Effects()[0].alive = false;
    ASSERT_EQ(effects.Create(DragonEffect, {}, {}, {}, 1.0f), 0u);
    EXPECT_TRUE(effects.Effects()[0].seen);
}

TEST(ModelEffectsTest, AnUnseenSlotDoesNotBecomeSeenWhenReused)
{
    ModelEffects effects;
    ASSERT_EQ(effects.Create(DragonEffect, {}, {}, {}, 1.0f), 0u);
    EXPECT_FALSE(effects.Effects()[0].seen);
    effects.Effects()[0].alive = false;
    ASSERT_EQ(effects.Create(Client::World::ArrivalEffect, {}, {}, {}, 1.0f), 0u);
    EXPECT_FALSE(effects.Effects()[0].seen);
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
// 0x518cf0(0xae, …, subtipo, dueño, −1, 0, 0, 0, 0) → 0x51913a: oculta en la escena 4, vida 250, animación
// a 0,3, sin luz por vértice, el estado con rand() & 1, la velocidad (0, escala del dueño · −5, 0) y la
// escala 0,9 con el /3 (0,25 con los demás). Con el pool lleno no saca rand().
TEST(ModelEffectsTest, TheButterflyIsCreatedHiddenInTheSelectionWithOneDraw)
{
    for (const bool selection : {true, false})
    {
        for (const std::int32_t subtype : {3, 1})
        {
            ModelEffects effects;
            Client::Render::Models::CrtRandom random(9);
            Client::Render::Models::CrtRandom draws(9);
            const auto slot = effects.CreateButterflyEffect({10.0f, 20.0f, 30.0f}, {0.0f, 0.0f, 45.0f}, glm::vec3(1.0f),
                                                            subtype, 1.25f, selection, random);
            ASSERT_EQ(slot, std::optional<std::size_t>(0));
            const ModelEffect& effect = effects.Effects()[0];
            EXPECT_TRUE(effect.alive);
            EXPECT_EQ(effect.type, Client::World::ButterflyEffect);
            EXPECT_EQ(effect.subtype, subtype);
            EXPECT_EQ(effect.transform.position, glm::vec3(10.0f, 20.0f, 30.0f));
            EXPECT_EQ(effect.transform.angles, glm::vec3(0.0f, 0.0f, 45.0f));
            EXPECT_EQ(effect.objectState.hiddenMesh,
                      selection ? Client::World::HiddenObject : ModelEffect{}.objectState.hiddenMesh);
            EXPECT_EQ(effect.life, 250);
            EXPECT_EQ(effect.speed, std::bit_cast<float>(0x3e99999au));
            EXPECT_FALSE(effect.vertexLight);
            EXPECT_EQ(effect.disturbanceState, 0xffffu);
            EXPECT_EQ(effect.state, static_cast<std::uint8_t>(draws.Next() & 1));
            EXPECT_EQ(effect.velocity, glm::vec3(0.0f, static_cast<float>(1.25 * -5.0), 0.0f));
            EXPECT_EQ(effect.transform.scale, subtype == 3 ? std::bit_cast<float>(0x3f666666u) : 0.25f);
            EXPECT_EQ(random.State(), draws.State());
        }
    }
    ModelEffects full;
    for (auto& effect : full.Effects())
        effect.alive = true;
    Client::Render::Models::CrtRandom random(9);
    EXPECT_FALSE(full.CreateButterflyEffect({}, {}, glm::vec3(1.0f), 3, 1.0f, true, random).has_value());
    EXPECT_EQ(random.State(), 9u);
}

// 0x518cf0(0xda, …, −1, …) → 0x51b80a: el prefijo común sin dueño y seis rand(): vida (& 15) + 24, escala
// ((& 7) + 6) · 0,1f, caída −(& 3) (−0 con 0), y el corrimiento ((& 63) − 32; −((& 31) + 50); (& 127) +
// 200), girado (con los ángulos en 0 queda igual) y sumado a la posición en float.
TEST(ModelEffectsTest, TheFallingStoneDrawsSixTimesAndStartsAboveItsDrip)
{
    for (const std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u})
    {
        SCOPED_TRACE(seed);
        ModelEffects effects;
        Client::Render::Models::CrtRandom random(seed);
        Client::Render::Models::CrtRandom draws(seed);
        const auto slot =
            effects.CreateFallingStone({100.0f, 200.0f, 300.0f}, glm::vec3(0.0f), {0.25f, 0.5f, 0.75f}, random);
        ASSERT_EQ(slot, std::optional<std::size_t>(0));
        const ModelEffect& effect = effects.Effects()[0];
        const int life = (draws.Next() & 15) + 24;
        const auto scale =
            static_cast<float>(((draws.Next() & 7) + 6) * static_cast<double>(std::bit_cast<float>(0x3dcccccdu)));
        const int fall = draws.Next() & 3;
        const auto x = static_cast<float>((draws.Next() & 63) - 32);
        const auto y = -static_cast<float>((draws.Next() & 31) + 50);
        const auto z = static_cast<float>((draws.Next() & 127) + 200);
        EXPECT_TRUE(effect.alive);
        EXPECT_EQ(effect.type, Client::World::FallingStoneEffect);
        EXPECT_EQ(effect.subtype, 0);
        EXPECT_EQ(effect.life, life);
        EXPECT_EQ(effect.transform.scale, scale);
        EXPECT_EQ(Bits(effect.turnDirection), Bits(-static_cast<float>(fall)));
        EXPECT_EQ(effect.transform.position, glm::vec3(static_cast<float>(static_cast<double>(x) + 100.0),
                                                       static_cast<float>(static_cast<double>(y) + 200.0),
                                                       static_cast<float>(static_cast<double>(z) + 300.0)));
        EXPECT_EQ(effect.light, glm::vec3(0.25f, 0.5f, 0.75f));
        EXPECT_EQ(effect.disturbanceState, 0xffffu);
        EXPECT_EQ(effect.itemType, nullptr);
        EXPECT_EQ(random.State(), draws.State());
    }
    ModelEffects full;
    for (auto& effect : full.Effects())
        effect.alive = true;
    Client::Render::Models::CrtRandom random(9);
    EXPECT_FALSE(full.CreateFallingStone({}, {}, glm::vec3(1.0f), random).has_value());
    EXPECT_EQ(random.State(), 9u);
}

// 0x518cf0(0x5de, …) → 0x51e7d8 con el subtipo 0: vida 40 y el registro propio abierto y cerrado; con la
// cuenta en 0, el cierre gasta los 3.848 rand() del registro.
TEST(ModelEffectsTest, TheFlameJetLivesFortyAndClosesTheOwnRecord)
{
    for (const std::uint8_t start : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(static_cast<int>(start));
        ModelEffects effects;
        std::uint8_t depth = start;
        Client::Render::Models::CrtRandom random(9);
        const auto slot =
            effects.CreateFlameJet({1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 90.0f}, glm::vec3(1.0f), depth, random);
        ASSERT_EQ(slot, std::optional<std::size_t>(0));
        const ModelEffect& effect = effects.Effects()[0];
        EXPECT_EQ(effect.type, Client::World::FlameJetEffect);
        EXPECT_EQ(effect.subtype, 0);
        EXPECT_EQ(effect.life, 40);
        EXPECT_EQ(effect.transform.position, glm::vec3(1.0f, 2.0f, 3.0f));
        EXPECT_EQ(depth, start);
        Client::Render::Models::CrtRandom expected(9);
        if (start == 0)
        {
            for (int draw = 0; draw < Client::Render::Models::OwnRecordCloseRandoms; ++draw)
                static_cast<void>(expected.Next());
        }
        EXPECT_EQ(random.State(), expected.State());
    }
}

// 0x518cf0(0xc8 o 0xc9, …) → 0x51b72b con el subtipo 0: el empuje ((& 255) + 64) · 0,1f en y, la vida (& 15)
// + 32, la escala ((& 3) + 8) · 0,1f, el yaw rand() % 360, la velocidad (el empuje girado con ese yaw) y la
// caída (& 15) + 8. Otro tipo no crea nada ni saca rand().
TEST(ModelEffectsTest, TheStoneChipsArePushedAlongTheirDrawnYaw)
{
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    for (const std::int16_t type : {Client::World::FirstStoneChipEffect, Client::World::SecondStoneChipEffect})
    {
        SCOPED_TRACE(type);
        ModelEffects effects;
        Client::Render::Models::CrtRandom random(4);
        Client::Render::Models::CrtRandom draws(4);
        const auto slot =
            effects.CreateStoneChip(type, {10.0f, 20.0f, 30.0f}, glm::vec3(0.0f), {0.5f, 0.5f, 0.5f}, random);
        ASSERT_EQ(slot, std::optional<std::size_t>(0));
        const ModelEffect& effect = effects.Effects()[0];
        const auto push = static_cast<float>(((draws.Next() & 255) + 64) * Tenth);
        const int life = (draws.Next() & 15) + 32;
        const auto scale = static_cast<float>(((draws.Next() & 3) + 8) * Tenth);
        const auto yaw = static_cast<float>(draws.Next() % 360);
        const auto fall = static_cast<float>((draws.Next() & 15) + 8);
        EXPECT_EQ(effect.type, type);
        EXPECT_EQ(effect.life, life);
        EXPECT_EQ(effect.transform.scale, scale);
        EXPECT_EQ(effect.transform.angles, glm::vec3(0.0f, 0.0f, yaw));
        EXPECT_EQ(effect.velocity,
                  Client::Render::Models::OriginalVectorRotate(
                      {0.0f, push, 0.0f}, Client::Render::Models::OriginalAngleMatrix({0.0f, 0.0f, yaw})));
        EXPECT_EQ(effect.turnDirection, fall);
        EXPECT_EQ(effect.transform.position, glm::vec3(10.0f, 20.0f, 30.0f));
        EXPECT_EQ(random.State(), draws.State());
    }
    ModelEffects effects;
    Client::Render::Models::CrtRandom random(4);
    EXPECT_FALSE(effects.CreateStoneChip(0xca, {}, {}, glm::vec3(1.0f), random).has_value());
    EXPECT_FALSE(effects.Effects()[0].alive);
    EXPECT_EQ(random.State(), 4u);
}
} // namespace

// 0x518cf0(0xbb, …) → 0x51b968: la nube de Icarus vive 2, sin blendMesh, escala 10, 200 más en y y 190 menos
// en z, sin luz por vértice y sin rand(). 0x518cf0(0x5ac, …) → 0x51e3ec: luz 0,3f, s = rand() % 5 + 9 y la
// velocidad sin girar (sen 30° · cos 70°; cos 30° · cos 70°; sen 70°) · s · 0,5 en la x87, escala 3 y vida
// 400. Otro tipo, ninguno.
TEST(ModelEffectsTest, TheIcarusCloudAndFlareAreCreatedWithTheirOwnFields)
{
    ModelEffects effects;
    Client::Render::Models::CrtRandom random(1);
    const auto cloud = effects.CreateIcarusEffect(Client::World::IcarusCloudEffect, {100.0f, 200.0f, 300.0f},
                                                  glm::vec3(0.0f), {0.1f, 0.2f, 0.3f}, random);
    ASSERT_EQ(cloud, std::optional<std::size_t>(0));
    const ModelEffect& sky = effects.Effects()[0];
    EXPECT_EQ(sky.type, Client::World::IcarusCloudEffect);
    EXPECT_EQ(sky.transform.position, glm::vec3(100.0f, 400.0f, 110.0f));
    EXPECT_EQ(sky.previousPosition, sky.transform.position);
    EXPECT_EQ(sky.light, glm::vec3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(sky.transform.scale, 10.0f);
    EXPECT_EQ(sky.life, 2);
    EXPECT_EQ(sky.objectState.blendMesh, 0);
    EXPECT_FALSE(sky.vertexLight);
    EXPECT_EQ(random.State(), 1u);

    std::uint32_t seed = 1;
    while (Client::Render::Models::CrtRandom(seed).Next() % 5 != 0)
        ++seed;
    Client::Render::Models::CrtRandom draws(seed);
    const auto flare = effects.CreateIcarusEffect(Client::World::IcarusFlareEffect, {1.0f, 2.0f, 3.0f}, glm::vec3(0.0f),
                                                  glm::vec3(1.0f), draws);
    ASSERT_EQ(flare, std::optional<std::size_t>(1));
    const ModelEffect& falling = effects.Effects()[1];
    EXPECT_EQ(falling.type, Client::World::IcarusFlareEffect);
    EXPECT_EQ(falling.light, glm::vec3(std::bit_cast<float>(0x3e99999au)));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(falling.velocity.x), 0x3f4500edu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(falling.velocity.y), 0x3faa9c31u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(falling.velocity.z), 0x408750d4u);
    EXPECT_EQ(falling.transform.scale, 3.0f);
    EXPECT_EQ(falling.life, 400);
    Client::Render::Models::CrtRandom expected(seed);
    static_cast<void>(expected.Next());
    EXPECT_EQ(draws.State(), expected.State());

    EXPECT_FALSE(effects.CreateIcarusEffect(0xb4, {}, {}, glm::vec3(1.0f), draws).has_value());
}

// 0x51900b: el 0xb3 de Aida. El prefijo común con la escala 0 (pasa a 0,9) y seis rand(): la z, la y y la x
// corridas ± 100, el yaw, la y de la velocidad y la escala; la partida en la posición ya corrida.
TEST(ModelEffectsTest, TheAidaWispIsCreatedWithSixDrawsAroundItsObject)
{
    ModelEffects effects;
    effects.Effects()[0].alive = true;
    effects.Effects()[1].turnPending = 7.0f;
    Client::Render::Models::CrtRandom random(11);
    Client::Render::Models::CrtRandom draws(11);
    const glm::vec3 at{1000.5f, 2000.25f, 300.0f};
    const glm::vec3 angles{0.0f, 0.0f, 45.0f};
    const auto created = effects.CreateAidaWisp(at, angles, glm::vec3(0.1f), random);
    ASSERT_EQ(created, std::optional<std::size_t>(1));
    const ModelEffect& wisp = effects.Effects()[1];
    const int z = draws.Next() % 200;
    const int y = draws.Next() % 200;
    const int x = draws.Next() % 200;
    const int yaw = draws.Next() % 360;
    const int push = draws.Next() & 7;
    const int grow = draws.Next() % 5;
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_EQ(wisp.type, Client::World::AidaWispEffect);
    EXPECT_EQ(wisp.subtype, 0);
    const glm::vec3 moved{static_cast<float>(static_cast<double>(x) - 100.0 + static_cast<double>(at.x)),
                          static_cast<float>(static_cast<double>(y) - 100.0 + static_cast<double>(at.y)),
                          static_cast<float>(static_cast<double>(z) - 100.0 + static_cast<double>(at.z))};
    EXPECT_EQ(wisp.transform.position, moved);
    EXPECT_EQ(wisp.start, moved);
    EXPECT_EQ(wisp.previousPosition, moved);
    EXPECT_EQ(wisp.transform.angles, glm::vec3(0.0f, 0.0f, static_cast<float>(yaw + 45)));
    EXPECT_EQ(wisp.velocity,
              glm::vec3(0.0f, static_cast<float>(static_cast<double>(push) * static_cast<double>(0.1f) - 5.0), 0.0f));
    EXPECT_EQ(wisp.transform.scale,
              static_cast<float>(static_cast<double>(grow) * static_cast<double>(std::bit_cast<float>(0x3ccccccdu)) +
                                 static_cast<double>(std::bit_cast<float>(0x3f666666u))));
    EXPECT_EQ(wisp.life, 1000);
    EXPECT_EQ(wisp.state, 0);
    EXPECT_EQ(wisp.speed, 1.0f);
    EXPECT_EQ(wisp.turnDirection, 1.0f);
    EXPECT_EQ(wisp.flagEc, 1.0f);
    EXPECT_EQ(wisp.turnPending, 7.0f); // +0xdc no lo escribe nadie: queda lo del lugar.
    EXPECT_EQ(wisp.objectState.light, 1.0f);
    EXPECT_EQ(wisp.objectState.blendMesh, -1);
    EXPECT_TRUE(wisp.vertexLight);
    EXPECT_EQ(wisp.disturbanceState, 0xffff);
    EXPECT_EQ(wisp.light, glm::vec3(0.01f, 0.01f, std::bit_cast<float>(0x3cf5c28fu)));

    for (auto& effect : effects.Effects())
        effect.alive = true;
    Client::Render::Models::CrtRandom full(11);
    EXPECT_FALSE(effects.CreateAidaWisp(at, angles, glm::vec3(0.1f), full).has_value());
    EXPECT_EQ(full.State(), 11u);
}

// 0x518cf0(0x6eb, …) → 0x51f4ad con el subtipo 1, la onda del galope en Icarus: sin dueño, vida 20 y la escala
// ((rand() % 10) + 10) · 0,1f en la x87. Con la semilla 1, 41: 1,1f. Lleno, ninguna y sin rand().
TEST(ModelEffectsTest, TheGallopShockWaveIsBornWithOneRandom)
{
    ModelEffects effects;
    Client::Render::Models::CrtRandom random(1);
    const auto slot =
        effects.CreateGallopShockWave({100.5f, 200.25f, 300.0f}, {0.0f, 0.0f, 45.0f}, glm::vec3(1.0f), random);
    ASSERT_EQ(slot, std::optional<std::size_t>(0));
    const ModelEffect& wave = effects.Effects()[0];
    EXPECT_TRUE(wave.alive);
    EXPECT_EQ(wave.type, Client::World::ShockWaveEffect);
    EXPECT_EQ(wave.subtype, Client::World::GallopShockWaveSubtype);
    EXPECT_EQ(wave.life, 20);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(wave.transform.scale), 0x3f8ccccdu);
    EXPECT_EQ(wave.transform.position, glm::vec3(100.5f, 200.25f, 300.0f));
    EXPECT_EQ(wave.transform.angles, glm::vec3(0.0f, 0.0f, 45.0f));
    EXPECT_EQ(wave.light, glm::vec3(1.0f));
    EXPECT_EQ(wave.itemType, nullptr);
    Client::Render::Models::CrtRandom expected(1);
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());

    ModelEffects full;
    for (auto& effect : full.Effects())
        effect.alive = true;
    Client::Render::Models::CrtRandom untouched(1);
    EXPECT_FALSE(full.CreateGallopShockWave({}, {}, glm::vec3(1.0f), untouched).has_value());
    EXPECT_EQ(untouched.State(), 1u);
}

namespace
{
// 0x518cf0(0x14d, …) → 0x51db7a, el rayo de los Fenrir: vida 100, la escala (rand() % 100) · 0,002f + 0,3f, el alpha
// 0,7f, tres ángulos rand() % 360 y el lugar por rand() % 30. Para cada semilla repite las cuentas con otra CRT, con
// el punto de cada hueso en (hueso, 2 · hueso, 3 · hueso), y el sprite 0x5ac de 2 con la luz menos 0,3f.
TEST(ModelEffectsTest, TheLightningPicksItsPlaceWithItsOwnRandoms)
{
    const auto bonePoint = [](std::size_t bone)
    {
        const auto b = static_cast<float>(bone);
        return glm::vec3(b, 2.0f * b, 3.0f * b);
    };
    const glm::vec3 at(1000.5f, 2000.25f, 300.0f);
    const glm::vec3 light(1.0f, 1.0f, 0.2f);
    std::set<int> places;
    for (std::uint32_t seed = 1; seed <= 400 && places.size() < 30; ++seed)
    {
        SCOPED_TRACE(seed);
        ModelEffects effects;
        Client::World::FrameSprites sprites;
        int owner = 0;
        Client::Render::Models::CrtRandom random(seed);
        const auto slot = effects.CreateLightning(at, {0.0f, 0.0f, 45.0f}, light, &owner, random, bonePoint, sprites);
        ASSERT_TRUE(slot.HasValue()) << slot.Error();
        ASSERT_EQ(slot.Value(), std::optional<std::size_t>(0));
        const ModelEffect& bolt = effects.Effects()[0];
        Client::Render::Models::CrtRandom draws(seed);
        float scale = static_cast<float>(static_cast<double>(draws.Next() % 100) * static_cast<double>(0.002f) +
                                         static_cast<double>(0.3f));
        glm::vec3 angles;
        for (int axis = 0; axis < 3; ++axis)
            angles[axis] = static_cast<float>(draws.Next() % 360);
        const int where = draws.Next() % 30;
        places.insert(where);
        const auto shrunk = [&scale]
        { scale = static_cast<float>(static_cast<double>(scale) - static_cast<double>(0.2f)); };
        glm::vec3 place = at;
        bool alive = true;
        if (where == 1 || where == 2)
            place = bonePoint(10);
        else if (where == 3)
        {
            shrunk();
            place = bonePoint(14);
        }
        else if (where == 4 || where == 5)
            place = bonePoint(2);
        else if (where == 6 || where == 7)
        {
            shrunk();
            place = bonePoint((draws.Next() & 1) == 0 ? 50 : 51);
        }
        else if (where == 8)
        {
            shrunk();
            place = bonePoint(53);
        }
        else if (where == 9 || where == 10)
            alive = false;
        else
        {
            scale = static_cast<float>(static_cast<double>(scale) + static_cast<double>(0.1f));
            place.x = static_cast<float>(static_cast<double>((draws.Next() % 240) - 120) + static_cast<double>(at.x));
            place.y = static_cast<float>(static_cast<double>((draws.Next() % 10) - 5) + static_cast<double>(at.y));
            place.z = static_cast<float>(static_cast<double>(at.z) + 110.0);
        }
        EXPECT_EQ(random.State(), draws.State());
        EXPECT_EQ(bolt.alive, alive);
        EXPECT_EQ(bolt.type, Client::World::LightningEffect);
        EXPECT_EQ(bolt.subtype, 0);
        EXPECT_EQ(bolt.life, 100);
        EXPECT_EQ(bolt.alpha, alive ? std::bit_cast<float>(0x3f333333u) : 0.0f);
        EXPECT_EQ(bolt.transform.scale, scale);
        EXPECT_EQ(bolt.transform.angles, angles);
        EXPECT_EQ(bolt.transform.position, place);
        EXPECT_EQ(bolt.light, light);
        EXPECT_TRUE(bolt.hasOwner);
        EXPECT_EQ(bolt.ownerContext, &owner);
        EXPECT_EQ(bolt.disturbanceState, 0xffff);
        const auto& flare = sprites.Sprites()[0];
        ASSERT_TRUE(flare.alive);
        EXPECT_EQ(flare.texture, 0x5ac);
        EXPECT_EQ(flare.position, place);
        EXPECT_EQ(flare.scale, 2.0f);
        EXPECT_EQ(flare.light, glm::vec3(static_cast<float>(1.0 - static_cast<double>(0.3f)),
                                         static_cast<float>(1.0 - static_cast<double>(0.3f)),
                                         static_cast<float>(static_cast<double>(0.2f) - static_cast<double>(0.3f))));
    }
    EXPECT_EQ(places.size(), 30u);

    ModelEffects full;
    for (auto& effect : full.Effects())
        effect.alive = true;
    Client::World::FrameSprites sprites;
    Client::Render::Models::CrtRandom untouched(1);
    const auto none = full.CreateLightning(at, {}, light, nullptr, untouched, bonePoint, sprites);
    ASSERT_TRUE(none.HasValue());
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(untouched.State(), 1u);
    EXPECT_FALSE(sprites.Sprites()[0].alive);
    ModelEffects empty;
    EXPECT_FALSE(empty.CreateLightning(at, {}, light, nullptr, untouched, {}, sprites).HasValue());
    EXPECT_FALSE(empty.Effects()[0].alive);
}

// 0x518cf0(0x14e, …) → 0x51ddfc, el calco de los Fenrir: vida 200, el cuadro en 0, la luz, el alpha en 1, la hora de
// timeGetTime() y la z en 0, con el subtipo del color. Sin rand(); lleno, ninguno.
TEST(ModelEffectsTest, TheLightningDecalKeepsItsHourAndLiesOnZeroHeight)
{
    ModelEffects effects;
    effects.Effects()[0].textureFrame = 3; // lo que dejó el anterior.
    int owner = 0;
    const auto slot =
        effects.CreateLightningDecal({100.5f, 200.25f, 300.0f}, {0.0f, 0.0f, 45.0f}, glm::vec3(1.0f), 2, &owner, 4321u);
    ASSERT_EQ(slot, std::optional<std::size_t>(0));
    const ModelEffect& decal = effects.Effects()[0];
    EXPECT_TRUE(decal.alive);
    EXPECT_EQ(decal.type, Client::World::LightningDecalEffect);
    EXPECT_EQ(decal.subtype, 2);
    EXPECT_EQ(decal.life, 200);
    EXPECT_EQ(decal.textureFrame, 0);
    EXPECT_EQ(decal.textureTime, 4321u);
    EXPECT_EQ(decal.alpha, 1.0f);
    EXPECT_EQ(decal.light, glm::vec3(1.0f));
    EXPECT_EQ(decal.transform.position, glm::vec3(100.5f, 200.25f, 0.0f));
    EXPECT_EQ(decal.transform.angles, glm::vec3(0.0f, 0.0f, 45.0f));
    EXPECT_EQ(decal.ownerContext, &owner);

    ModelEffects full;
    for (auto& effect : full.Effects())
        effect.alive = true;
    EXPECT_FALSE(full.CreateLightningDecal({}, {}, glm::vec3(1.0f), 1, nullptr, 0u).has_value());
}
// 0x518cf0(0x61e, …, 1, héroe, −1, 0, 0, 0, 0) → 0x51eebb, el calco de la mascota: el prefijo común (escala 0,9, +0x8a
// en −1), el dueño y vida 20; el subtipo 1 sale sin caso. Sin rand(); lleno, ninguno.
TEST(ModelEffectsTest, ThePetSummonIsTheColoredDecalWithItsOwnerAndTwentySteps)
{
    ModelEffects effects;
    int owner = 0;
    const auto slot = effects.CreatePetSummon({100.5f, 200.25f, 300.0f}, {0.0f, 0.0f, 45.0f}, glm::vec3(0.5f), &owner);
    ASSERT_EQ(slot, std::optional<std::size_t>(0));
    const ModelEffect& summon = effects.Effects()[0];
    EXPECT_TRUE(summon.alive);
    EXPECT_EQ(summon.type, Client::World::ColoredDecalEffect);
    EXPECT_EQ(summon.subtype, Client::World::PetSummonSubtype);
    EXPECT_EQ(summon.life, 20);
    EXPECT_EQ(summon.transform.scale, 0.9f);
    EXPECT_EQ(summon.transform.position, glm::vec3(100.5f, 200.25f, 300.0f));
    EXPECT_EQ(summon.transform.angles, glm::vec3(0.0f, 0.0f, 45.0f));
    EXPECT_EQ(summon.light, glm::vec3(0.5f));
    EXPECT_EQ(summon.velocity, glm::vec3(0.0f));
    EXPECT_EQ(summon.disturbanceState, 0xffffu);
    EXPECT_TRUE(summon.hasOwner);
    EXPECT_EQ(summon.ownerContext, &owner);
    EXPECT_EQ(summon.itemType, nullptr);

    ModelEffects full;
    for (auto& effect : full.Effects())
        effect.alive = true;
    EXPECT_FALSE(full.CreatePetSummon({}, {}, glm::vec3(1.0f), &owner).has_value());
}
} // namespace

namespace
{
using Client::World::ModelEffectOwner;

float EarthquakeSum(float value, double addend)
{
    return static_cast<float>(static_cast<double>(value) + addend);
}

// 0x518cf0(0xfa, posición, ángulos, luz, 0, caballo, −1, 0, 2, 0, 0) → 0x51a66d: el prefijo común con el estado 2
// y el dueño, +0x28..+0x30 con los ángulos (x 80 más, z 180 más), la partida, vida 20, el subtipo rand() % 100, la
// z de los ángulos 330 más y +0xe4 en 50. 0x51a8d8: abre el registro, sube su +0xf04 (envuelve) y lo cierra; con la
// cuenta de accesos en 0, el cierre gasta sus 3.848 rand(). Lleno, ninguno: sin rand() ni registro.
TEST(ModelEffectsTest, TheEarthquakeIsBornWithItsSubtypeAndClosesTheOwnRecord)
{
    for (const std::uint8_t start : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(static_cast<int>(start));
        ModelEffects effects;
        const std::uint16_t horseType = 0x1b1 + 1;
        const glm::vec3 horseAngles(0.0f, 0.0f, 33.0f);
        const ModelEffectOwner owner{.type = &horseType, .effect = nullptr, .angles = &horseAngles};
        std::uint8_t depth = start;
        std::uint8_t count = 0xff;
        Client::Render::Models::CrtRandom random(7);
        const auto slot = effects.CreateEarthquake({100.5f, 200.25f, 300.0f}, {10.0f, 20.0f, 45.3f}, glm::vec3(0.5f), 2,
                                                   owner, depth, count, random);
        ASSERT_EQ(slot, std::optional<std::size_t>(0));
        const ModelEffect& quake = effects.Effects()[0];
        Client::Render::Models::CrtRandom expected(7);
        const int subtype = expected.Next() % 100;
        EXPECT_TRUE(quake.alive);
        EXPECT_EQ(quake.type, Client::World::EarthquakeEffect);
        EXPECT_EQ(quake.subtype, subtype);
        EXPECT_EQ(quake.state, 2);
        EXPECT_EQ(quake.life, 20);
        EXPECT_EQ(quake.transform.scale, 0.9f);
        EXPECT_EQ(quake.transform.position, glm::vec3(100.5f, 200.25f, 300.0f));
        EXPECT_EQ(quake.start, glm::vec3(100.5f, 200.25f, 300.0f));
        EXPECT_EQ(quake.transform.angles, glm::vec3(10.0f, 20.0f, EarthquakeSum(45.3f, 330.0)));
        EXPECT_EQ(quake.copiedAngles, glm::vec3(EarthquakeSum(10.0f, 80.0), 20.0f, EarthquakeSum(45.3f, 180.0)));
        EXPECT_EQ(quake.turnDirection, 50.0f);
        EXPECT_EQ(quake.light, glm::vec3(0.5f));
        EXPECT_EQ(quake.disturbanceState, 0xffffu);
        EXPECT_TRUE(quake.hasOwner);
        EXPECT_EQ(quake.owner.type, &horseType);
        EXPECT_EQ(quake.owner.angles, &horseAngles);
        EXPECT_EQ(quake.renderFlags, 0u);
        EXPECT_EQ(Client::World::OwnerTypeOf(quake.owner), std::optional<std::uint16_t>(std::uint16_t{0x1b2}));
        // La cuenta +0xf04 es un byte: 0xff más uno vuelve a 0.
        EXPECT_EQ(count, 0);
        EXPECT_EQ(depth, start);
        if (start == 0)
        {
            for (int draw = 0; draw < Client::Render::Models::OwnRecordCloseRandoms; ++draw)
                static_cast<void>(expected.Next());
        }
        EXPECT_EQ(random.State(), expected.State());
    }

    ModelEffects full;
    for (auto& effect : full.Effects())
        effect.alive = true;
    std::uint8_t depth = 0;
    std::uint8_t count = 4;
    Client::Render::Models::CrtRandom untouched(7);
    EXPECT_FALSE(full.CreateEarthquake({}, {}, glm::vec3(1.0f), 2, {}, depth, count, untouched).has_value());
    EXPECT_EQ(untouched.State(), 7u);
    EXPECT_EQ(count, 4);
}

struct CrackLifeCase
{
    std::int16_t type;
    std::int32_t subtype;
    std::int32_t life;
};

// 0x51a6e4..0x51a85c: la vida de cada grieta con sus subtipos, la escala size · 0,01f en la x87, +0x8a con el tamaño
// y +0x64 en 0 salvo en 0xfd y 0x100. Sin rand().
TEST(ModelEffectsTest, TheEarthquakeCracksTakeTheirLifeAndScaleFromTheirSize)
{
    constexpr CrackLifeCase Cases[] = {
        {0xfb, 0, 35},  {0xfb, 1, 60},  {0xfc, 0, 20},  {0xfc, 1, 20},  {0xfc, 2, 50},
        {0xfd, 0, 35},  {0xfd, 7, 35},  {0xfe, 0, 35},  {0xff, 0, 40},  {0xff, 1, 40},
        {0x100, 0, 35}, {0x101, 0, 40}, {0x101, 3, 40}, {0x102, 0, 40}, {0x102, 1, 40},
    };
    const std::uint16_t horseType = 0x10;
    const ModelEffectOwner owner{.type = &horseType, .effect = nullptr, .angles = nullptr};
    for (const CrackLifeCase& crack : Cases)
    {
        for (const std::int16_t size : {std::int16_t{0x96}, std::int16_t{47}})
        {
            SCOPED_TRACE(std::to_string(crack.type) + "/" + std::to_string(crack.subtype) + " " + std::to_string(size));
            ModelEffects effects;
            const auto slot = effects.CreateEarthquakeCrack(crack.type, {1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 72.5f},
                                                            glm::vec3(0.25f), crack.subtype, owner, size);
            ASSERT_EQ(slot, std::optional<std::size_t>(0));
            const ModelEffect& effect = effects.Effects()[0];
            EXPECT_TRUE(effect.alive);
            EXPECT_EQ(effect.type, crack.type);
            EXPECT_EQ(effect.subtype, crack.subtype);
            EXPECT_EQ(effect.life, crack.life);
            const float scale =
                static_cast<float>(static_cast<double>(size) * static_cast<double>(std::bit_cast<float>(0x3c23d70au)));
            EXPECT_EQ(Bits(effect.transform.scale), Bits(scale));
            EXPECT_EQ(effect.disturbanceState, static_cast<std::uint16_t>(size));
            const bool keepsItsBlend = crack.type == 0xfd || crack.type == 0x100;
            EXPECT_EQ(effect.objectState.blendMesh, keepsItsBlend ? Client::Render::Models::NoBlendMesh : 0);
            const bool flagged =
                crack.subtype == 1 && (crack.type == 0xfc || crack.type == 0xff || crack.type == 0x102);
            EXPECT_EQ(effect.renderFlags, flagged ? Client::World::CrackRenderFlags : 0u);
            EXPECT_EQ(effect.transform.position, glm::vec3(1.0f, 2.0f, 3.0f));
            EXPECT_EQ(effect.transform.angles, glm::vec3(0.0f, 0.0f, 72.5f));
            EXPECT_EQ(effect.light, glm::vec3(0.25f));
            EXPECT_TRUE(effect.hasOwner);
            EXPECT_EQ(effect.owner.type, &horseType);
        }
    }
}

// Q-C423: 0xfb con un subtipo que no es 0 ni 1 y 0xfc con uno fuera de 0..2 salen por 0x51db73 sin caso: la escala
// 0,9 del prefijo y la vida que el lugar tenía de su uso anterior. Otro tipo o la tabla llena, ninguno.
TEST(ModelEffectsTest, AnEarthquakeCrackWithoutItsCaseKeepsTheLifeOfThePreviousEffect)
{
    for (const auto& [type, subtype] : {std::pair<std::int16_t, std::int32_t>{std::int16_t{0xfb}, 2},
                                        std::pair<std::int16_t, std::int32_t>{std::int16_t{0xfc}, 3},
                                        std::pair<std::int16_t, std::int32_t>{std::int16_t{0xfb}, -1}})
    {
        SCOPED_TRACE(std::to_string(type) + "/" + std::to_string(subtype));
        ModelEffects effects;
        effects.Effects()[0].life = 7;
        const auto slot = effects.CreateEarthquakeCrack(type, {}, {}, glm::vec3(1.0f), subtype, {}, 0x96);
        ASSERT_EQ(slot, std::optional<std::size_t>(0));
        const ModelEffect& effect = effects.Effects()[0];
        EXPECT_TRUE(effect.alive);
        EXPECT_EQ(effect.life, 7);
        EXPECT_EQ(effect.transform.scale, 0.9f);
        EXPECT_EQ(effect.disturbanceState, 0x96u);
        EXPECT_EQ(effect.objectState.blendMesh, Client::Render::Models::NoBlendMesh);
        EXPECT_FALSE(effect.hasOwner);
    }

    ModelEffects effects;
    EXPECT_FALSE(effects.CreateEarthquakeCrack(0xfa, {}, {}, glm::vec3(1.0f), 0, {}, 0x96).has_value());
    EXPECT_FALSE(effects.CreateEarthquakeCrack(0x103, {}, {}, glm::vec3(1.0f), 0, {}, 0x96).has_value());
    EXPECT_FALSE(effects.Effects()[0].alive);
    for (auto& effect : effects.Effects())
        effect.alive = true;
    EXPECT_FALSE(effects.CreateEarthquakeCrack(0xfb, {}, {}, glm::vec3(1.0f), 0, {}, 0x96).has_value());
}

// El tipo del dueño se lee en vivo: un efecto (su +0x2 con signo, como WORD) antes que el tipo de un helper.
TEST(ModelEffectsTest, TheOwnerTypeIsReadFromTheLiveOwner)
{
    ModelEffect owner;
    owner.type = Client::World::EarthquakeEffect;
    std::uint16_t helperType = 0x1b1;
    EXPECT_EQ(Client::World::OwnerTypeOf({.type = &helperType, .effect = &owner, .angles = nullptr}),
              std::optional<std::uint16_t>(std::uint16_t{0xfa}));
    EXPECT_EQ(Client::World::OwnerTypeOf({.type = &helperType, .effect = nullptr, .angles = nullptr}),
              std::optional<std::uint16_t>(std::uint16_t{0x1b1}));
    helperType = 0x1b2;
    EXPECT_EQ(Client::World::OwnerTypeOf({.type = &helperType, .effect = nullptr, .angles = nullptr}),
              std::optional<std::uint16_t>(std::uint16_t{0x1b2}));
    EXPECT_EQ(Client::World::OwnerTypeOf({}), std::nullopt);
}
} // namespace
