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

// The first seed from 1 for which `wanted` (which takes the rand() it checks) gives true.
std::uint32_t SeedWhere(const std::function<bool(CrtRandom&)>& wanted)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom random(seed);
        if (wanted(random))
            return seed;
    }
}

std::size_t LiveParticles(const Client::Particles::Particles& particles)
{
    std::size_t live = 0;
    for (const auto& particle : particles.All())
        live += particle.alive ? 1u : 0u;
    return live;
}

Client::Effects::ModelEffect Fragment(std::int16_t type, std::int32_t subtype)
{
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = type;
    effect.subtype = subtype;
    effect.life = 20;
    effect.transform.position = {0, 0, 100};
    effect.transform.scale = 1;
    effect.turnDirection = 8;
    return effect;
}

// 0x51d32c and 0x51d3b1: the 0x13c and the pebbles 0x12d/2 and 0x12e/2 push ((r & 127) + 32) · 0.1f and fall
// r % 5 + 2; the scale, ((r & 1) + 10) · 0.25 for the 0x13c and ((r & 1) + 4) · 0.12f + 0.6f for the pebbles. Then
// the common tail of 0x51d50d: life, push yaw, three angles and the height. Eight rand().
TEST(SiegeVertexEffectsTest, TheGolemStoneAndTheShowerTakeTheirSmallPush)
{
    for (const auto& [type, subtype] :
         {std::pair{std::int16_t{0x13c}, std::int32_t{0}}, std::pair{std::int16_t{0x12d}, std::int32_t{2}},
          std::pair{std::int16_t{0x12e}, std::int32_t{2}}})
    {
        SCOPED_TRACE(type);
        auto effects = std::make_unique<Client::Effects::ModelEffects>();
        CrtRandom random(5), expected(5);
        const auto slot = Client::CastleSiege::CreateSiegeVertexEffect(
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

// 0x51d438..0x51d4a4: the 0x12e/3 only takes the life, the scale ((r % 3 + 2) · 0.12f + 1.2f) and the fall
// (−20 − r % 5) and sets +0xd8 to 1.8f: it keeps the caller's angles and height.
TEST(SiegeVertexEffectsTest, TheThrownStoneKeepsItsAnglesAndHeight)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    CrtRandom random(9), expected(9);
    const auto slot = Client::CastleSiege::CreateSiegeVertexEffect(
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

// 0x5291ce..0x529385: the 0x13c falls by 3 (not by 6 like the gate) and, after the life · 0.1f, with rand() & 3 at
// 0 drops stone 0x6ec/5 and smoke 0x5f2/0x15; with rand() % 10 at 0, dust 0x5f3/0. With the particle table full,
// only those two rand().
TEST(SiegeVertexEffectsTest, TheGolemStoneFallsLikeAStoneAndCrumbles)
{
    const Client::Effects::ModelEffectStepServices services{.terrainHeight = [](float, float) { return 0; }};
    const std::uint32_t crumbling = SeedWhere(
        [](CrtRandom& draw)
        {
            static_cast<void>(draw.Next());
            return (draw.Next() & 3) == 0;
        });
    Client::Particles::Particles particles;
    auto effect = Fragment(0x13c, 0);
    CrtRandom random(crumbling);
    const auto crumbled =
        Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
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
    const auto full = Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_EQ(effect.life, 19);
    CrtRandom expected(crumbling);
    expected.Discard(3);
    EXPECT_EQ(random.State(), expected.State());

    // Without the stone, the dust 0x5f3/0 comes out with rand() % 10 at 0.
    const std::uint32_t dusty = SeedWhere(
        [](CrtRandom& draw)
        {
            static_cast<void>(draw.Next());
            return (draw.Next() & 3) != 0 && draw.Next() % 10 == 0;
        });
    Client::Particles::Particles room;
    effect = Fragment(0x13c, 0);
    random = CrtRandom(dusty);
    const auto dust = Client::Effects::StepGameModelEffect(effect, nullptr, room, 0, random, nullptr, services);
    ASSERT_TRUE(dust.HasValue()) << dust.Error();
    ASSERT_EQ(LiveParticles(room), 1u);
    EXPECT_EQ(room.All()[0].type, 0x5f3);
    EXPECT_EQ(room.All()[0].subtype, 0);
}

// 0x52938a..0x5293ea: the 0x12d/2, with rand() % 10 at 0, lowers its z to this step's terrain height and drops smoke
// 0x5f2/0xb; the 0x12e/2 does not.
TEST(SiegeVertexEffectsTest, TheShowerStoneSmokesOnTheGround)
{
    const Client::Effects::ModelEffectStepServices services{.terrainHeight = [](float, float) { return 40; }};
    const std::uint32_t smoky = SeedWhere(
        [](CrtRandom& draw)
        {
            static_cast<void>(draw.Next());
            return draw.Next() % 10 == 0;
        });
    for (const std::int16_t type : {std::int16_t{0x12d}, std::int16_t{0x12e}})
    {
        SCOPED_TRACE(type);
        Client::Particles::Particles particles;
        auto effect = Fragment(type, 2);
        CrtRandom random(smoky);
        const auto stepped =
            Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
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

// 0x5290df..0x5291c9: the 0x12e/3 lowers its z speed by +0xd8, which goes up by 0.3f. In the air, with rand() & 1 at
// 0, smoke 0x5f2/0x18 of scale 0.2f (0x5291c1) with its rand(). With the terrain above, z at the terrain and the
// speed, the angles and +0x58 at 0; with rand() % 10 at 0, the same smoke.
TEST(SiegeVertexEffectsTest, TheThrownStoneFallsFasterAndLands)
{
    const std::uint32_t even = SeedWhere(
        [](CrtRandom& draw)
        {
            static_cast<void>(draw.Next());
            return (draw.Next() & 1) == 0;
        });
    const Client::Effects::ModelEffectStepServices low{.terrainHeight = [](float, float) { return 0; }};
    Client::Particles::Particles particles;
    auto effect = Fragment(0x12e, 3);
    effect.velocity = {0, 0, -20};
    effect.speed = std::bit_cast<float>(0x3fe66666u);
    CrtRandom random(even);
    const auto smoked = Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, low);
    ASSERT_TRUE(smoked.HasValue()) << smoked.Error();
    ASSERT_EQ(LiveParticles(particles), 1u);
    EXPECT_EQ(particles.All()[0].type, 0x5f2);
    EXPECT_EQ(particles.All()[0].subtype, 0x18);
    CrtRandom smoke(even);
    smoke.Discard(2);
    auto twin = std::make_unique<Client::Particles::Particles>();
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
    const Client::Effects::ModelEffectStepServices high{.terrainHeight = [](float, float) { return 150; }};
    effect = Fragment(0x12e, 3);
    effect.velocity = {1, 2, -20};
    effect.transform.angles = {-55, 3, 90};
    effect.objectState.hiddenMesh = 4;
    random = CrtRandom(quiet);
    const auto landed = Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, high);
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
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    Client::Crt::CrtRandom random(1), expected(1);
    const auto slot =
        Client::CastleSiege::CreateSiegeVertexEffect({.type = 0x12f, .position = {2, 3, 4}}, *effects, random);
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
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    Client::Crt::CrtRandom random(1), expected(1);
    const auto slot = Client::CastleSiege::CreateSiegeVertexEffect({.type = 0x131}, *effects, random);
    ASSERT_TRUE(slot.has_value());
    EXPECT_EQ(effects->Effects()[*slot].subtype, 1);
    expected.Discard(9);
    EXPECT_EQ(random.State(), expected.State());
    for (auto& effect : effects->Effects())
        effect.alive = true;
    EXPECT_FALSE(Client::CastleSiege::CreateSiegeVertexEffect({.type = 0x12f}, *effects, random).has_value());
    EXPECT_EQ(random.State(), expected.State());
}

TEST(SiegeVertexEffectsTest, TheWholeStepPreservesTheSecondRotatedMovementAndTheCommonLightDraw)
{
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = 0x12f;
    effect.life = 20;
    effect.transform.position = {0, 0, 100};
    effect.transform.scale = 0;
    effect.velocity = {10, 0, 0};
    effect.turnDirection = 8;
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1), expected(1);
    const Client::Effects::ModelEffectStepServices services{.terrainHeight = [](float, float) { return 0; }};
    const auto result = Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
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
    Client::Effects::ModelEffect initial;
    initial.alive = true;
    initial.type = 0x131;
    initial.life = 20;
    initial.transform.position.z = 100;
    initial.transform.scale = 0;
    initial.velocity.x = 10;
    initial.turnDirection = 8;
    Client::Particles::Particles particles;
    const Client::Effects::ModelEffectStepServices services{.terrainHeight = [](float, float) { return 0; }};
    Client::Crt::CrtRandom random(1);
    // The second draw of seed 1 is 18467; seed 2 gives 29216. Find the first state with dust.
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        auto effect = initial;
        const auto before = random.State();
        const auto result =
            Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
        if (result.HasValue())
            continue;
        EXPECT_NE(result.Error().find("0x529520"), std::string::npos);
        EXPECT_EQ(effect.life, 20);
        EXPECT_EQ(effect.transform.position.x, 10);
        for (auto& particle : particles.All())
            particle.alive = true;
        random = Client::Crt::CrtRandom(before);
        effect = initial;
        const auto full =
            Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
        ASSERT_TRUE(full.HasValue()) << full.Error();
        EXPECT_EQ(effect.life, 19);
        EXPECT_EQ(effect.transform.position.x, 19);
        return;
    }
    FAIL() << "la secuencia no alcanzó el polvo";
}

TEST(SiegeVertexEffectsTest, TheGuardDebrisAlsoCompletesItsTailWhenTheDustPoolIsFull)
{
    Client::Effects::ModelEffect initial;
    initial.alive = true;
    initial.type = 0x10a;
    initial.life = 20;
    initial.transform.position.z = 100;
    initial.transform.scale = 0;
    initial.velocity.x = 10;
    initial.turnDirection = 8;
    Client::Particles::Particles particles;
    const Client::Effects::ModelEffectStepServices services{.terrainHeight = [](float, float) { return 0; }};
    Client::Crt::CrtRandom random(8);
    auto effect = initial;
    const auto room = Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
    ASSERT_FALSE(room.HasValue());
    EXPECT_NE(room.Error().find("0x5252e6"), std::string::npos);
    EXPECT_EQ(effect.life, 20);
    for (auto& particle : particles.All())
        particle.alive = true;
    effect = initial;
    random = Client::Crt::CrtRandom(8);
    const auto full = Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_EQ(effect.life, 19);
    EXPECT_EQ(effect.transform.position.x, 19);
}
} // namespace
