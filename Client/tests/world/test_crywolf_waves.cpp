#include "Crt/CrtRandom.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace
{
TEST(CrywolfWavesTest, TheConstructorsKeepTheSignedKeyAndNativePrefixStores)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    const auto wave = effects->CreateWaveEffect({.type = 0x114,
                                                 .position = {100, 200, 300},
                                                 .angles = {10, 20, 30},
                                                 .light = {1, 2, 3},
                                                 .subtype = 6,
                                                 .key = 0xffff});
    ASSERT_EQ(wave, 0u);
    const auto& first = effects->Effects()[0];
    EXPECT_EQ(first.life, 10);
    EXPECT_EQ(first.transform.scale, -0.05f);
    EXPECT_EQ(first.disturbanceState, 0xffff);
    EXPECT_EQ(first.transform.angles, (glm::vec3{-90, 0, 30}));
    EXPECT_EQ(first.objectState.blendMesh, -2);
    EXPECT_EQ(first.objectState.light, 1);
    EXPECT_EQ(first.transform.position, (glm::vec3{100, 200, 300}));
    EXPECT_EQ(first.start, first.transform.position);
    EXPECT_EQ(first.light, (glm::vec3{1, 2, 3}));
    const auto piercing = effects->CreatePiercingEffect(
        {.type = 0x115, .position = {100, 200, 300}, .angles = {10, 20, 30}, .light = {1, 0, 0.5f}, .subtype = 1});
    ASSERT_EQ(piercing, 1u);
    const auto& second = effects->Effects()[1];
    EXPECT_EQ(second.life, 6);
    EXPECT_EQ(second.transform.scale, 2);
    EXPECT_EQ(second.velocity, (glm::vec3{0, -60, 0}));
    EXPECT_EQ(second.start, (glm::vec3{100, 200, 300}));
    EXPECT_EQ(second.transform.position, (glm::vec3{100, 200, 430}));
}

TEST(CrywolfWavesTest, BothNativeWaveSubtypesKeepTheirOwnGrowthFadeAndCommonTail)
{
    for (const int subtype : {2, 6})
    {
        auto particles = std::make_unique<Client::World::Particles>();
        Client::World::ModelEffect wave;
        wave.alive = true;
        wave.type = 0x114;
        wave.subtype = subtype;
        wave.life = 1;
        // 1,495f + 0,01f y 2,45f + 0,08f pasan el tope (1,5 y 2,5, 0x526e7d y 0x526f61), que los deja ahí.
        wave.transform.scale = subtype == 2 ? 1.495f : 2.45f;
        wave.turnDirection = subtype == 2 ? 0.01f : 0.08f;
        wave.velocity = {1, 2, 3};
        Client::Crt::CrtRandom random(7);
        const auto result = Client::World::StepGameModelEffect(wave, nullptr, *particles, 0, random);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_FALSE(wave.alive);
        EXPECT_EQ(wave.life, 0);
        EXPECT_EQ(wave.transform.scale, subtype == 2 ? 1.5f : 2.5f);
        EXPECT_FLOAT_EQ(wave.turnDirection, subtype == 2 ? 0.02f : 0.095f);
        EXPECT_FLOAT_EQ(wave.objectState.light, subtype == 2 ? 0.6666666865348816f : 0.7692307829856873f);
        EXPECT_EQ(wave.transform.angles.y, subtype == 2 ? 0 : 45);
        EXPECT_EQ(random.State(), 0x003d7aaeu);
    }
}

TEST(CrywolfWavesTest, ThePiercingMovesTwiceAndCreatesTheWaveAtItsIntermediatePosition)
{
    for (const bool full : {false, true})
    {
        auto particles = std::make_unique<Client::World::Particles>();
        auto effects = std::make_unique<Client::World::ModelEffects>();
        if (full)
            for (auto& e : effects->Effects())
                e.alive = true;
        Client::World::ModelEffect piercing;
        piercing.alive = true;
        piercing.type = 0x115;
        piercing.subtype = 1;
        piercing.life = 6;
        piercing.transform.position = {1000, 2000, 430};
        piercing.velocity = {0, -60, 0};
        piercing.light = {1, 0, 0.5f};
        Client::Crt::CrtRandom random(7);
        const auto result = Client::World::StepGameModelEffect(piercing, nullptr, *particles, 0, random, nullptr,
                                                               {.effects = effects.get()});
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(piercing.life, 5);
        EXPECT_EQ(piercing.velocity, (glm::vec3{0, -48, 0}));
        EXPECT_EQ(piercing.transform.position, (glm::vec3{1000, 1892, 430}));
        EXPECT_EQ(piercing.objectState.light, 0.625f);
        EXPECT_EQ(random.State(), 0x003d7aaeu);
        if (!full)
        {
            const auto& wave = effects->Effects()[0];
            EXPECT_EQ(wave.type, 0x114);
            EXPECT_EQ(wave.subtype, 2);
            EXPECT_EQ(wave.life, 15);
            EXPECT_FLOAT_EQ(wave.transform.scale, 0.3f);
            EXPECT_EQ(wave.transform.position, (glm::vec3{1000, 1940, 430}));
            EXPECT_EQ(wave.transform.angles, (glm::vec3{90, 0, 0}));
            EXPECT_EQ(wave.light, piercing.light);
        }
    }
}
// 0x51c2b3..0x51c36d: la /2, /3 y /4 viven 15 con +0xe4 en 0,01f y la x en 90; la /5 y la /6 viven 10 con +0xe4 en
// 0,08f y la x en −90. La /0, la /1 y las demás no son de World.
TEST(CrywolfWavesTest, EveryWorldWaveSubtypeTakesItsLifeGrowthAndPitch)
{
    for (const int subtype : {2, 3, 4, 5, 6})
    {
        SCOPED_TRACE(subtype);
        auto effects = std::make_unique<Client::World::ModelEffects>();
        const auto made = effects->CreateWaveEffect(
            {.type = 0x114, .position = {100, 200, 300}, .angles = {10, 20, 30}, .subtype = subtype, .key = 20});
        ASSERT_EQ(made, 0u);
        const auto& wave = effects->Effects()[0];
        const bool slow = subtype <= 4;
        EXPECT_EQ(wave.life, slow ? 15 : 10);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(wave.turnDirection), slow ? 0x3c23d70au : 0x3da3d70au);
        EXPECT_EQ(wave.transform.angles, (glm::vec3{slow ? 90.0f : -90.0f, 0, 30}));
        EXPECT_EQ(wave.transform.scale, static_cast<float>(20.0 * static_cast<double>(0.05f)));
    }
    for (const int subtype : {0, 1, 7})
    {
        auto effects = std::make_unique<Client::World::ModelEffects>();
        EXPECT_EQ(effects->CreateWaveEffect({.type = 0x114, .subtype = subtype}), std::nullopt);
    }
}

// 0x526dd0..0x526f9b: la escala suma +0xe4 (la /0 sólo con más de 4 de vida), +0xe4 crece, la escala topa, +0x68 baja
// y la /3..6 giran 45 en y; con otro subtipo, sólo la cola. Sin rand() salvo el de la luz local.
TEST(CrywolfWavesTest, EveryWaveSubtypeStepsWithItsGrowthLimitAndFade)
{
    struct Case
    {
        int subtype;
        int life;
        float scale;
        float growth;
        float light;
        float yaw;
        float start = 1.0f;
    };
    const auto f = [](double value) { return static_cast<float>(value); };
    const auto d = [](std::uint32_t bits) { return static_cast<double>(std::bit_cast<float>(bits)); };
    const std::array cases{
        Case{0, 6, f(1.0 + 0.5), f(0.5 + d(0x3dcccccdu)), f(d(0x3f36db6eu)), 0},
        Case{0, 4, 1.0f, 0.5f, f(d(0x3f36db6eu)), 0},
        Case{1, 6, 1.5f, f(0.5 + d(0x3d8f5c29u)), f(d(0x3f2aaaabu)), 0},
        Case{1, 6, 2.0f, f(0.5 + d(0x3d8f5c29u)), f(d(0x3f2aaaabu)), 0, 1.6f},
        Case{3, 6, 1.5f, f(0.5 + d(0x3ba3d70au)), f(d(0x3f44ec4fu)), 45},
        Case{4, 6, 1.5f, f(0.5 + d(0x3b03126fu)), f(d(0x3f555555u)), 45},
        Case{5, 6, 1.5f, f(0.5 + d(0x3c75c28fu)), f(d(0x3f44ec4fu)), 45},
        // Los topes (0x526ec8, 0x526f0b y 0x526f61): 1,4f + 0,5 pasa el 1,5 y 2,4f + 0,5 pasa el 2,5.
        Case{3, 6, 1.5f, f(0.5 + d(0x3ba3d70au)), f(d(0x3f44ec4fu)), 45, 1.4f},
        Case{4, 6, std::bit_cast<float>(0x40200000u), f(0.5 + d(0x3b03126fu)), f(d(0x3f555555u)), 45, 2.4f},
        Case{5, 6, std::bit_cast<float>(0x40200000u), f(0.5 + d(0x3c75c28fu)), f(d(0x3f44ec4fu)), 45, 2.4f},
        Case{7, 6, 1.0f, 0.5f, 1.0f, 0},
    };
    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        const Case& probe = cases[index];
        SCOPED_TRACE(index);
        auto particles = std::make_unique<Client::World::Particles>();
        Client::World::ModelEffect wave;
        wave.alive = true;
        wave.type = 0x114;
        wave.subtype = probe.subtype;
        wave.life = probe.life;
        wave.transform.scale = probe.start;
        wave.turnDirection = 0.5f;
        wave.objectState.light = 1.0f;
        Client::Crt::CrtRandom random(7);
        const auto result = Client::World::StepGameModelEffect(wave, nullptr, *particles, 0, random);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(wave.life, probe.life - 1);
        EXPECT_EQ(wave.transform.scale, probe.scale);
        EXPECT_EQ(wave.turnDirection, probe.growth);
        EXPECT_EQ(wave.objectState.light, probe.light);
        EXPECT_EQ(wave.transform.angles.y, probe.yaw);
        EXPECT_EQ(random.State(), 0x003d7aaeu);
    }
}

// 0x51c8e8 y 0x52705a: el 0x115 de otro subtipo que el 1 vive 10 y suelta la onda 0x114/2 sólo con más de 5 de vida.
TEST(CrywolfWavesTest, TheOtherPiercingLivesTenAndWavesAboveFive)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    ASSERT_EQ(effects->CreatePiercingEffect({.type = 0x115, .position = {100, 200, 300}}), 0u);
    EXPECT_EQ(effects->Effects()[0].life, 10);
    for (const int life : {5, 6})
    {
        SCOPED_TRACE(life);
        auto table = std::make_unique<Client::World::ModelEffects>();
        auto particles = std::make_unique<Client::World::Particles>();
        Client::World::ModelEffect piercing;
        piercing.alive = true;
        piercing.type = 0x115;
        piercing.subtype = 0;
        piercing.life = life;
        piercing.objectState.light = 1.0f;
        Client::Crt::CrtRandom random(7);
        const auto result = Client::World::StepGameModelEffect(piercing, nullptr, *particles, 0, random, nullptr,
                                                               {.effects = table.get()});
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(table->Effects()[0].alive, life == 6);
        EXPECT_EQ(piercing.objectState.light, life == 6 ? 0.625f : 1.0f);
        if (life == 6)
        {
            EXPECT_EQ(table->Effects()[0].disturbanceState, 0xffff);
        }
    }
}
} // namespace
