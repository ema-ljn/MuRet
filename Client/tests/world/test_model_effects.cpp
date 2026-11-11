#include "World/ModelEffects.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
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
