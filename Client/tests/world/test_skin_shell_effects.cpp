#include "Effects/SkinShellEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>

// The 0x137 shells (Effects/SkinShellEffects.h): creation 0x51d8c4..0x51da45 and step 0x529926..0x529a4a, with
// 0x5208a0's common prologue and epilogue. The pinned bits come from repeating the ASM's computations in float32
// separately.
namespace
{
using Client::Crt::CrtRandom;
using Client::Effects::ModelEffect;
using Client::Effects::ModelEffects;

// The seed with which the fourth rand() gives 0 in % 360: the creation's speed stays unrotated.
constexpr std::uint32_t StraightSeed = 298;

float Bits(std::uint32_t value)
{
    return std::bit_cast<float>(value);
}

// What 0x4ee770 (0x4eee6b) asks for: the corner, zero angles, light (1, 1, 1) and the joint's subtype.
Client::Kernel::ModelEffectArguments ShellArguments(std::int32_t subtype)
{
    return {.type = Client::Effects::SkinShellEffect,
            .position = {1000.0f, 2000.0f, 300.0f},
            .angles = glm::vec3(0.0f),
            .light = glm::vec3(1.0f),
            .subtype = subtype};
}

// A shell by hand for the step: speed (10, 20, 0), fall 6, scale 0.4f, angles' x 100, life 40.
ModelEffect PlacedShell()
{
    ModelEffect shell{};
    shell.alive = true;
    shell.type = Client::Effects::SkinShellEffect;
    shell.transform.position = {1000.0f, 2000.0f, 300.0f};
    shell.velocity = {10.0f, 20.0f, 0.0f};
    shell.turnDirection = 6.0f;
    shell.transform.scale = Bits(0x3ecccccdu);
    shell.transform.angles = {100.0f, 0.0f, 0.0f};
    shell.life = 40;
    shell.light = glm::vec3(1.0f);
    return shell;
}
} // namespace

// 0x51d8c4..0x51da45 with seed 298: push ((1011 & 127) + 32) · 0.1f = 14.7f, z + 50, life (32248 & 15) + 32 = 40,
// scale (3577 % 3 + 3) · 0.1f = 0.4f, alpha 0.5, the angles' z 29880 % 360 = 0 and the speed (0; 14.7f; 0) unrotated;
// the fall 2406 % 5 + 2 + 2314 % 5 = 7 and the angles (11, 45, 224). The light: 0.5 for the /0, (0.1f; 0.6f; 1) for
// the /1 and the request's for the /2. Nine rand().
TEST(SkinShellEffectsTest, TheShellIsBornWithNineRandoms)
{
    for (const std::int32_t subtype : {0, 1, 2})
    {
        SCOPED_TRACE(subtype);
        auto effects = std::make_unique<ModelEffects>();
        CrtRandom random(StraightSeed);

        const auto created = Client::Effects::CreateSkinShell(ShellArguments(subtype), *effects, random);

        ASSERT_EQ(created, std::optional<std::size_t>(0));
        const ModelEffect& shell = effects->Effects()[0];
        EXPECT_TRUE(shell.alive);
        EXPECT_EQ(shell.type, Client::Effects::SkinShellEffect);
        EXPECT_EQ(shell.subtype, subtype);
        EXPECT_EQ(shell.transform.position, (glm::vec3{1000.0f, 2000.0f, 350.0f}));
        EXPECT_EQ(shell.previousPosition, shell.transform.position);
        EXPECT_EQ(shell.life, 40);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(shell.transform.scale), 0x3ecccccdu);
        EXPECT_EQ(shell.alpha, 0.5f);
        EXPECT_EQ(shell.velocity, (glm::vec3{0.0f, Bits(0x416b3333u), 0.0f}));
        EXPECT_EQ(shell.turnDirection, 7.0f);
        EXPECT_EQ(shell.transform.angles, (glm::vec3{11.0f, 45.0f, 224.0f}));
        const glm::vec3 light = subtype == 0   ? glm::vec3(0.5f)
                                : subtype == 1 ? glm::vec3(Bits(0x3dcccccdu), Bits(0x3f19999au), 1.0f)
                                               : glm::vec3(1.0f);
        EXPECT_EQ(shell.light, light);
        CrtRandom expected(StraightSeed);
        expected.Discard(9);
        EXPECT_EQ(random.State(), expected.State());
    }
}

// With seed 7 (rand() 61, 17422, 15215, 7040, 15521, 6516, 30152, 11794, 27727): push (61 & 127) + 32 = 93 · 0.1f =
// 0x4114cccd, life (17422 & 15) + 32 = 46, scale (15215 % 3 + 3) · 0.1f = 0.5 and the angles' z 7040 % 360 = 200.
// 0x5fc8f0 with (0, 0, 200): sin(200 · 0x3c8efa35) stored as float and the cosine on the x87 stack; 0x5fcbd0 rotates
// (0, push, 0) to (0x404b9205, 0xc10bd386, 0). Then the fall 15521 % 5 + 2 + 6516 % 5 = 4 and the angles (272, 274,
// 7). The bits come from repeating the ASM's computations in a separate x87 (exact Fraction with rounding to 53 and 24
// bits).
TEST(SkinShellEffectsTest, ThePushTurnsWithTheFirstYaw)
{
    auto effects = std::make_unique<ModelEffects>();
    CrtRandom random(7);
    ASSERT_TRUE(Client::Effects::CreateSkinShell(ShellArguments(0), *effects, random).has_value());

    const ModelEffect& shell = effects->Effects()[0];
    EXPECT_EQ(std::bit_cast<std::uint32_t>(shell.velocity.x), 0x404b9205u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(shell.velocity.y), 0xc10bd386u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(shell.velocity.z), 0x00000000u);
    EXPECT_EQ(shell.turnDirection, 4.0f);
    EXPECT_EQ(shell.transform.angles, (glm::vec3{272.0f, 274.0f, 7.0f}));
    EXPECT_EQ(shell.life, 46);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(shell.transform.scale), 0x3f000000u);
    EXPECT_EQ(shell.transform.position, (glm::vec3{1000.0f, 2000.0f, 350.0f}));
    CrtRandom expected(7);
    expected.Discard(9);
    EXPECT_EQ(random.State(), expected.State());
}

// 0x518cf0 with the table full: nothing, not even rand().
TEST(SkinShellEffectsTest, TheFullTableSkipsTheShellWithoutRandoms)
{
    auto effects = std::make_unique<ModelEffects>();
    for (ModelEffect& effect : effects->Effects())
        effect.alive = true;
    CrtRandom random(StraightSeed);
    EXPECT_EQ(Client::Effects::CreateSkinShell(ShellArguments(0), *effects, random), std::nullopt);
    EXPECT_EQ(random.State(), CrtRandom(StraightSeed).State());
}

// 0x529926..0x529a4a: position plus speed, speed times 0.9f, z plus the fall, speed times 1.1f, the fall minus 3. In
// the air (terrain below, or NaN), the angles' x minus 0.4f · 16; on the ground (500 above 306), z there, the fall
// times −0.2f, five lives less and x minus 0.4f · 128. The alpha, life · 0.1f.
TEST(SkinShellEffectsTest, TheShellFallsAndBouncesOnTheGround)
{
    struct Case
    {
        double ground;
        std::uint32_t z;
        std::uint32_t fall;
        std::uint32_t tilt;
        std::int32_t life;
        float alpha;
    };
    for (const Case& probe :
         {Case{-1000.0, 0x43990000u, 0x40400000u, 0x42bb3333u, 40, 4.0f},
          Case{std::numeric_limits<double>::quiet_NaN(), 0x43990000u, 0x40400000u, 0x42bb3333u, 40, 4.0f},
          Case{500.0, 0x43fa0000u, 0xbf19999au, 0x42433333u, 35, 3.5f}})
    {
        SCOPED_TRACE(probe.ground);
        ModelEffect shell = PlacedShell();
        Client::Effects::ModelEffectStepServices services;
        services.terrainHeight = [&probe](float, float) { return probe.ground; };

        ASSERT_TRUE(Client::Effects::StepSkinShell(shell, services).HasValue());

        EXPECT_EQ(shell.transform.position, (glm::vec3{1010.0f, 2020.0f, Bits(probe.z)}));
        EXPECT_EQ(shell.velocity, (glm::vec3{Bits(0x411e6667u), Bits(0x419e6667u), 0.0f}));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(shell.turnDirection), probe.fall);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(shell.transform.angles.x), probe.tilt);
        EXPECT_EQ(shell.life, probe.life);
        EXPECT_EQ(shell.alpha, probe.alpha);
    }
    ModelEffect blind = PlacedShell();
    const auto cut = Client::Effects::StepSkinShell(blind, {});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x5299dc")) << cut.Error();
}

// 0x5208a0 with the 0x137: the local light's rand() (0x5208c5), the body, the animation (no skeleton, nothing), the
// common move 0x51fc80(effect, 1) with the new speed rotated by the new angles and life minus one; the position before
// the step stays as the previous one. The 0x137's step leaves the position at (1010, 2020, 306), the speed at
// (0x411e6667, 0x419e6667, 0) and the angles' x at 0x42bb3333 (93.6f); 0x5fc8f0 with (93.6f, 0, 0) gives sin and cos
// in float (0x3f7f7eae, 0xbd80984c), and 0x5fcbd0 rotates the speed to (0x411e6667, 0xbf9f22df, 0x419e1663). The
// position's bits come from repeating those computations in a separate x87.
TEST(SkinShellEffectsTest, TheGameStepAddsTheCommonMove)
{
    ModelEffect shell = PlacedShell();
    Client::Effects::ModelEffectStepServices services;
    services.terrainHeight = [](float, float) { return -1000.0; };
    auto particles = std::make_unique<Client::Particles::Particles>();
    CrtRandom random(5);

    const auto stepped =
        Client::Effects::StepGameModelEffect(shell, nullptr, *particles, 0.0f, random, nullptr, services);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(std::bit_cast<std::uint32_t>(shell.transform.position.x), 0x447ef99au);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(shell.transform.position.y), 0x44fc5837u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(shell.transform.position.z), 0x43a2e166u);
    EXPECT_EQ(shell.previousPosition, (glm::vec3{1000.0f, 2000.0f, 300.0f}));
    EXPECT_EQ(shell.life, 39);
    EXPECT_EQ(shell.alpha, 4.0f);
    EXPECT_TRUE(shell.alive);
    CrtRandom expected(5);
    expected.Discard(1);
    EXPECT_EQ(random.State(), expected.State());
}
