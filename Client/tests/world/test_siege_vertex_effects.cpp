#include "CastleSiege/SiegeVertexEffects.h"

#include "Crt/CrtRandom.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace
{
using Client::Crt::CrtRandom;

// La primera semilla desde 1 con la que `wanted` (que saca los rand() que mira) da true.
std::uint32_t SeedWhere(const std::function<bool(CrtRandom&)>& wanted)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom random(seed);
        if (wanted(random))
            return seed;
    }
}

std::size_t LiveParticles(const Client::World::Particles& particles)
{
    std::size_t live = 0;
    for (const auto& particle : particles.All())
        live += particle.alive ? 1u : 0u;
    return live;
}

Client::World::ModelEffect Fragment(std::int16_t type, std::int32_t subtype)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = type;
    effect.subtype = subtype;
    effect.life = 20;
    effect.transform.position = {0, 0, 100};
    effect.transform.scale = 1;
    effect.turnDirection = 8;
    return effect;
}

// 0x51d32c y 0x51d3b1: el 0x13c y las piedritas 0x12d/2 y 0x12e/2 empujan ((r & 127) + 32) · 0,1f y caen r % 5 + 2;
// la escala, ((r & 1) + 10) · 0,25 el 0x13c y ((r & 1) + 4) · 0,12f + 0,6f las piedritas. Después la cola común de
// 0x51d50d: vida, yaw del empuje, tres ángulos y la altura. Ocho rand().
TEST(SiegeVertexEffectsTest, TheGolemStoneAndTheShowerTakeTheirSmallPush)
{
    for (const auto& [type, subtype] :
         {std::pair{std::int16_t{0x13c}, std::int32_t{0}}, std::pair{std::int16_t{0x12d}, std::int32_t{2}},
          std::pair{std::int16_t{0x12e}, std::int32_t{2}}})
    {
        SCOPED_TRACE(type);
        auto effects = std::make_unique<Client::World::ModelEffects>();
        CrtRandom random(5), expected(5);
        const auto slot = Client::World::CreateSiegeVertexEffect(
            {.type = type, .position = {2, 3, 4}, .angles = {7, 8, 9}, .subtype = subtype}, *effects, random);
        ASSERT_TRUE(slot.has_value());
        const auto& effect = effects->Effects()[*slot];
        const auto push =
            static_cast<float>(static_cast<double>((expected.Next() & 127) + 32) * static_cast<double>(0.1f));
        const int scaleDraw = expected.Next() & 1;
        const float scale = type == 0x13c
                                ? static_cast<float>((scaleDraw + 10) * 0.25)
                                : static_cast<float>(static_cast<double>(scaleDraw + 4) * static_cast<double>(0.12f) +
                                                     static_cast<double>(0.6f));
        const auto fall = static_cast<float>(expected.Next() % 5 + 2);
        const int life = (expected.Next() & 15) + 32;
        static_cast<void>(expected.Next());
        const glm::vec3 angles{static_cast<float>(expected.Next() % 360), static_cast<float>(expected.Next() % 360),
                               static_cast<float>(expected.Next() % 360)};
        EXPECT_EQ(effect.transform.scale, scale);
        EXPECT_EQ(effect.turnDirection, fall);
        EXPECT_EQ(effect.life, life);
        EXPECT_EQ(effect.transform.angles, angles);
        EXPECT_EQ(effect.transform.position, (glm::vec3{2, 3, 54}));
        EXPECT_NEAR(glm::length(effect.velocity), push, 1e-3);
        EXPECT_EQ(random.State(), expected.State());
    }
}

// 0x51d438..0x51d4a4: el 0x12e/3 solo saca la vida, la escala ((r % 3 + 2) · 0,12f + 1,2f) y la caída (−20 − r % 5)
// y pone +0xd8 en 1,8f: conserva los ángulos y la altura del llamador.
TEST(SiegeVertexEffectsTest, TheThrownStoneKeepsItsAnglesAndHeight)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    CrtRandom random(9), expected(9);
    const auto slot = Client::World::CreateSiegeVertexEffect(
        {.type = 0x12e, .position = {2, 3, 4}, .angles = {-55, 0, 90}, .subtype = 3}, *effects, random);
    ASSERT_TRUE(slot.has_value());
    const auto& effect = effects->Effects()[*slot];
    EXPECT_EQ(effect.life, (expected.Next() & 15) + 32);
    EXPECT_EQ(effect.transform.scale,
              static_cast<float>(static_cast<double>(expected.Next() % 3 + 2) * static_cast<double>(0.12f) +
                                 static_cast<double>(1.2f)));
    EXPECT_EQ(effect.velocity, glm::vec3(0, 0, static_cast<float>(-20 - expected.Next() % 5)));
    EXPECT_EQ(effect.speed, std::bit_cast<float>(0x3fe66666u));
    EXPECT_EQ(effect.transform.angles, (glm::vec3{-55, 0, 90}));
    EXPECT_EQ(effect.transform.position, (glm::vec3{2, 3, 4}));
    EXPECT_EQ(random.State(), expected.State());
}

// 0x5291ce..0x529385: el 0x13c cae de a 3 (no de a 6 como la puerta) y, después de la vida · 0,1f, con rand() & 3 en
// 0 suelta la piedra 0x6ec/5 y el humo 0x5f2/0x15; con rand() % 10 en 0, el polvo 0x5f3/0. Con la tabla de partículas
// llena, solo esos dos rand().
TEST(SiegeVertexEffectsTest, TheGolemStoneFallsLikeAStoneAndCrumbles)
{
    const Client::World::ModelEffectStepServices services{.terrainHeight = [](float, float) { return 0; }};
    const std::uint32_t crumbling = SeedWhere(
        [](CrtRandom& draw)
        {
            static_cast<void>(draw.Next());
            return (draw.Next() & 3) == 0;
        });
    Client::World::Particles particles;
    auto effect = Fragment(0x13c, 0);
    CrtRandom random(crumbling);
    const auto crumbled = Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
    ASSERT_TRUE(crumbled.HasValue()) << crumbled.Error();
    EXPECT_EQ(effect.turnDirection, 5.0f);
    EXPECT_EQ(effect.transform.position.z, 105.0f);
    EXPECT_EQ(effect.alpha, 2.0f);
    EXPECT_EQ(effect.life, 19);
    ASSERT_GE(LiveParticles(particles), 2u);
    EXPECT_EQ(particles.All()[0].type, 0x6ec);
    EXPECT_EQ(particles.All()[0].subtype, 5);
    EXPECT_EQ(particles.All()[1].type, 0x5f2);
    EXPECT_EQ(particles.All()[1].subtype, 0x15);

    for (auto& particle : particles.All())
        particle.alive = true;
    effect = Fragment(0x13c, 0);
    random = CrtRandom(crumbling);
    const auto full = Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_EQ(effect.life, 19);
    CrtRandom expected(crumbling);
    expected.Discard(3);
    EXPECT_EQ(random.State(), expected.State());

    // Sin la piedra, el polvo 0x5f3/0 sale con rand() % 10 en 0.
    const std::uint32_t dusty = SeedWhere(
        [](CrtRandom& draw)
        {
            static_cast<void>(draw.Next());
            return (draw.Next() & 3) != 0 && draw.Next() % 10 == 0;
        });
    Client::World::Particles room;
    effect = Fragment(0x13c, 0);
    random = CrtRandom(dusty);
    const auto dust = Client::World::StepGameModelEffect(effect, nullptr, room, 0, random, nullptr, services);
    ASSERT_TRUE(dust.HasValue()) << dust.Error();
    ASSERT_EQ(LiveParticles(room), 1u);
    EXPECT_EQ(room.All()[0].type, 0x5f3);
    EXPECT_EQ(room.All()[0].subtype, 0);
}

// 0x52938a..0x5293ea: el 0x12d/2, con rand() % 10 en 0, baja su z a la altura del terreno de este paso y suelta el
// humo 0x5f2/0xb; el 0x12e/2 no.
TEST(SiegeVertexEffectsTest, TheShowerStoneSmokesOnTheGround)
{
    const Client::World::ModelEffectStepServices services{.terrainHeight = [](float, float) { return 40; }};
    const std::uint32_t smoky = SeedWhere(
        [](CrtRandom& draw)
        {
            static_cast<void>(draw.Next());
            return draw.Next() % 10 == 0;
        });
    for (const std::int16_t type : {std::int16_t{0x12d}, std::int16_t{0x12e}})
    {
        SCOPED_TRACE(type);
        Client::World::Particles particles;
        auto effect = Fragment(type, 2);
        CrtRandom random(smoky);
        const auto stepped =
            Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        if (type == 0x12d)
        {
            ASSERT_EQ(LiveParticles(particles), 1u);
            EXPECT_EQ(particles.All()[0].type, 0x5f2);
            EXPECT_EQ(particles.All()[0].subtype, 0xb);
            EXPECT_EQ(effect.transform.position.z, 40.0f);
        }
        else
        {
            EXPECT_EQ(LiveParticles(particles), 0u);
            EXPECT_EQ(effect.transform.position.z, 105.0f);
        }
    }
}

// 0x5290df..0x5291c9: el 0x12e/3 baja su velocidad z en +0xd8, que sube 0,3f. En el aire, con rand() & 1 en 0, el
// humo 0x5f2/0x18 de escala 0,2f (0x5291c1) con sus rand(). Con el terreno por encima, la z en el terreno y la
// velocidad, los ángulos y +0x58 en 0; con rand() % 10 en 0, el mismo humo.
TEST(SiegeVertexEffectsTest, TheThrownStoneFallsFasterAndLands)
{
    const std::uint32_t even = SeedWhere(
        [](CrtRandom& draw)
        {
            static_cast<void>(draw.Next());
            return (draw.Next() & 1) == 0;
        });
    const Client::World::ModelEffectStepServices low{.terrainHeight = [](float, float) { return 0; }};
    Client::World::Particles particles;
    auto effect = Fragment(0x12e, 3);
    effect.velocity = {0, 0, -20};
    effect.speed = std::bit_cast<float>(0x3fe66666u);
    CrtRandom random(even);
    const auto smoked = Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, low);
    ASSERT_TRUE(smoked.HasValue()) << smoked.Error();
    ASSERT_EQ(LiveParticles(particles), 1u);
    EXPECT_EQ(particles.All()[0].type, 0x5f2);
    EXPECT_EQ(particles.All()[0].subtype, 0x18);
    CrtRandom smoke(even);
    smoke.Discard(2);
    auto twin = std::make_unique<Client::World::Particles>();
    ASSERT_TRUE(twin->CreateChecked(0x5f2, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 0x18,
                                    std::bit_cast<float>(0x3e4ccccdu), 0.0f, smoke)
                    .HasValue());
    EXPECT_EQ(random.State(), smoke.State());
    particles.All()[0].alive = false;
    EXPECT_EQ(effect.velocity.z, static_cast<float>(-20.0 - static_cast<double>(std::bit_cast<float>(0x3fe66666u))));
    EXPECT_EQ(effect.speed,
              static_cast<float>(static_cast<double>(std::bit_cast<float>(0x3fe66666u)) + static_cast<double>(0.3f)));

    const std::uint32_t quiet = SeedWhere(
        [](CrtRandom& draw)
        {
            static_cast<void>(draw.Next());
            return draw.Next() % 10 != 0;
        });
    const Client::World::ModelEffectStepServices high{.terrainHeight = [](float, float) { return 150; }};
    effect = Fragment(0x12e, 3);
    effect.velocity = {1, 2, -20};
    effect.transform.angles = {-55, 3, 90};
    effect.objectState.hiddenMesh = 4;
    random = CrtRandom(quiet);
    const auto landed = Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, high);
    ASSERT_TRUE(landed.HasValue()) << landed.Error();
    EXPECT_EQ(effect.transform.position.z, 150.0f);
    EXPECT_EQ(effect.velocity, glm::vec3(0));
    EXPECT_EQ(effect.transform.angles, glm::vec3(0));
    EXPECT_EQ(effect.objectState.hiddenMesh, 0);
    EXPECT_EQ(effect.life, 19);
    CrtRandom expected(quiet);
    expected.Discard(2);
    EXPECT_EQ(random.State(), expected.State());
}

TEST(SiegeVertexEffectsTest, TheGateConstructorConsumesEightDrawsInNativeOrder)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    Client::Crt::CrtRandom random(1), expected(1);
    const auto slot = Client::World::CreateSiegeVertexEffect({.type = 0x12f, .position = {2, 3, 4}}, *effects, random);
    ASSERT_TRUE(slot.has_value());
    const auto& effect = effects->Effects()[*slot];
    EXPECT_EQ(effect.life, 36);
    EXPECT_FLOAT_EQ(effect.transform.scale, 0.8f);
    EXPECT_EQ(effect.turnDirection, 10);
    EXPECT_EQ(effect.transform.position, (glm::vec3{2, 3, 54}));
    EXPECT_EQ(effect.transform.angles, (glm::vec3{244, 318, 198}));
    expected.Discard(8);
    EXPECT_EQ(random.State(), expected.State());
}

TEST(SiegeVertexEffectsTest, TheStatueSubtypeAddsItsNinthDrawWhileAFullPoolConsumesNothing)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    Client::Crt::CrtRandom random(1), expected(1);
    const auto slot = Client::World::CreateSiegeVertexEffect({.type = 0x131}, *effects, random);
    ASSERT_TRUE(slot.has_value());
    EXPECT_EQ(effects->Effects()[*slot].subtype, 1);
    expected.Discard(9);
    EXPECT_EQ(random.State(), expected.State());
    for (auto& effect : effects->Effects())
        effect.alive = true;
    EXPECT_FALSE(Client::World::CreateSiegeVertexEffect({.type = 0x12f}, *effects, random).has_value());
    EXPECT_EQ(random.State(), expected.State());
}

TEST(SiegeVertexEffectsTest, TheWholeStepPreservesTheSecondRotatedMovementAndTheCommonLightDraw)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 0x12f;
    effect.life = 20;
    effect.transform.position = {0, 0, 100};
    effect.transform.scale = 0;
    effect.velocity = {10, 0, 0};
    effect.turnDirection = 8;
    Client::World::Particles particles;
    Client::Crt::CrtRandom random(1), expected(1);
    const Client::World::ModelEffectStepServices services{.terrainHeight = [](float, float) { return 0; }};
    const auto result = Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(effect.transform.position, (glm::vec3{19, 0, 102}));
    EXPECT_EQ(effect.previousPosition, (glm::vec3{0, 0, 100}));
    EXPECT_EQ(effect.velocity, (glm::vec3{9, 0, 0}));
    EXPECT_EQ(effect.life, 19);
    EXPECT_EQ(effect.alpha, 2);
    expected.Discard(1);
    EXPECT_EQ(random.State(), expected.State());
}

TEST(SiegeVertexEffectsTest, IndeterminateDustCutsAfterThePrefixUnlessTheParticlePoolIsFull)
{
    Client::World::ModelEffect initial;
    initial.alive = true;
    initial.type = 0x131;
    initial.life = 20;
    initial.transform.position.z = 100;
    initial.transform.scale = 0;
    initial.velocity.x = 10;
    initial.turnDirection = 8;
    Client::World::Particles particles;
    const Client::World::ModelEffectStepServices services{.terrainHeight = [](float, float) { return 0; }};
    Client::Crt::CrtRandom random(1);
    // El segundo sorteo de seed1 es18467; seed2 produce29216. Buscar el primer estado con polvo.
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        auto effect = initial;
        const auto before = random.State();
        const auto result =
            Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
        if (result.HasValue())
            continue;
        EXPECT_NE(result.Error().find("0x529520"), std::string::npos);
        EXPECT_EQ(effect.life, 20);
        EXPECT_EQ(effect.transform.position.x, 10);
        for (auto& particle : particles.All())
            particle.alive = true;
        random = Client::Crt::CrtRandom(before);
        effect = initial;
        const auto full = Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
        ASSERT_TRUE(full.HasValue()) << full.Error();
        EXPECT_EQ(effect.life, 19);
        EXPECT_EQ(effect.transform.position.x, 19);
        return;
    }
    FAIL() << "la secuencia no alcanzó el polvo";
}

TEST(SiegeVertexEffectsTest, TheGuardDebrisAlsoCompletesItsTailWhenTheDustPoolIsFull)
{
    Client::World::ModelEffect initial;
    initial.alive = true;
    initial.type = 0x10a;
    initial.life = 20;
    initial.transform.position.z = 100;
    initial.transform.scale = 0;
    initial.velocity.x = 10;
    initial.turnDirection = 8;
    Client::World::Particles particles;
    const Client::World::ModelEffectStepServices services{.terrainHeight = [](float, float) { return 0; }};
    Client::Crt::CrtRandom random(8);
    auto effect = initial;
    const auto room = Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
    ASSERT_FALSE(room.HasValue());
    EXPECT_NE(room.Error().find("0x5252e6"), std::string::npos);
    EXPECT_EQ(effect.life, 20);
    for (auto& particle : particles.All())
        particle.alive = true;
    effect = initial;
    random = Client::Crt::CrtRandom(8);
    const auto full = Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_EQ(effect.life, 19);
    EXPECT_EQ(effect.transform.position.x, 19);
}
} // namespace
