#include "World/SkinShellEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>

// Las cáscaras 0x137 (World/SkinShellEffects.h): el alta 0x51d8c4..0x51da45 y el paso 0x529926..0x529a4a, con el
// prólogo y el epílogo comunes de 0x5208a0. Los bits fijados salen de repetir las cuentas del ASM en float32 aparte.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::ModelEffect;
using Client::World::ModelEffects;

// La semilla con la que el cuarto rand() da 0 en % 360: la velocidad del alta queda sin girar.
constexpr std::uint32_t StraightSeed = 298;

float Bits(std::uint32_t value)
{
    return std::bit_cast<float>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

// Lo que pide 0x4ee770 (0x4eee6b): la esquina, los ángulos en 0, la luz (1, 1, 1) y el subtipo del joint.
Client::World::ModelEffectArguments ShellArguments(std::int32_t subtype)
{
    return {.type = Client::World::SkinShellEffect,
            .position = {1000.0f, 2000.0f, 300.0f},
            .angles = glm::vec3(0.0f),
            .light = glm::vec3(1.0f),
            .subtype = subtype};
}

// Una cáscara a mano para el paso: velocidad (10, 20, 0), caída 6, escala 0,4f, x de los ángulos 100, vida 40.
ModelEffect PlacedShell()
{
    ModelEffect shell{};
    shell.alive = true;
    shell.type = Client::World::SkinShellEffect;
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

// 0x51d8c4..0x51da45 con la semilla 298: empuje ((1011 & 127) + 32) · 0,1f = 14,7f, la z + 50, vida (32248 & 15) + 32 =
// 40, escala (3577 % 3 + 3) · 0,1f = 0,4f, alpha 0,5, la z de los ángulos 29880 % 360 = 0 y la velocidad (0; 14,7f; 0)
// sin girar; la caída 2406 % 5 + 2 + 2314 % 5 = 7 y los ángulos (11, 45, 224). La luz: 0,5 el /0, (0,1f; 0,6f; 1) el
// /1 y la del pedido el /2. Nueve rand().
TEST(SkinShellEffectsTest, TheShellIsBornWithNineRandoms)
{
    for (const std::int32_t subtype : {0, 1, 2})
    {
        SCOPED_TRACE(subtype);
        auto effects = std::make_unique<ModelEffects>();
        CrtRandom random(StraightSeed);

        const auto created = Client::World::CreateSkinShell(ShellArguments(subtype), *effects, random);

        ASSERT_EQ(created, std::optional<std::size_t>(0));
        const ModelEffect& shell = effects->Effects()[0];
        EXPECT_TRUE(shell.alive);
        EXPECT_EQ(shell.type, Client::World::SkinShellEffect);
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

// Con otra semilla, el empuje girado por la z de los ángulos (0x5fc8f0, 0x5fcbd0) antes de sortear los tres ángulos.
TEST(SkinShellEffectsTest, ThePushTurnsWithTheFirstYaw)
{
    auto effects = std::make_unique<ModelEffects>();
    CrtRandom random(7);
    ASSERT_TRUE(Client::World::CreateSkinShell(ShellArguments(0), *effects, random).has_value());

    CrtRandom replay(7);
    const float push = F32(static_cast<double>((replay.Next() & 127) + 32) * D(Bits(0x3dcccccdu)));
    static_cast<void>(replay.Next());
    static_cast<void>(replay.Next());
    const auto yaw = static_cast<float>(replay.Next() % 360);
    const glm::vec3 velocity = Client::Render::Models::OriginalVectorRotate(
        {0.0f, push, 0.0f}, Client::Render::Models::OriginalAngleMatrix({0.0f, 0.0f, yaw}));
    EXPECT_EQ(effects->Effects()[0].velocity, velocity);
}

// 0x518cf0 con la tabla llena: nada, ni rand().
TEST(SkinShellEffectsTest, TheFullTableSkipsTheShellWithoutRandoms)
{
    auto effects = std::make_unique<ModelEffects>();
    for (ModelEffect& effect : effects->Effects())
        effect.alive = true;
    CrtRandom random(StraightSeed);
    EXPECT_EQ(Client::World::CreateSkinShell(ShellArguments(0), *effects, random), std::nullopt);
    EXPECT_EQ(random.State(), CrtRandom(StraightSeed).State());
}

// 0x529926..0x529a4a: la posición más la velocidad, la velocidad por 0,9f, la z más la caída, la velocidad por 1,1f, la
// caída menos 3. En el aire (el terreno debajo, o NaN), la x de los ángulos menos 0,4f · 16; en el suelo (500 por
// encima de 306), la z ahí, la caída por −0,2f, cinco vidas menos y la x menos 0,4f · 128. El alpha, la vida · 0,1f.
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
        Client::World::ModelEffectStepServices services;
        services.terrainHeight = [&probe](float, float) { return probe.ground; };

        ASSERT_TRUE(Client::World::StepSkinShell(shell, services).HasValue());

        EXPECT_EQ(shell.transform.position, (glm::vec3{1010.0f, 2020.0f, Bits(probe.z)}));
        EXPECT_EQ(shell.velocity, (glm::vec3{Bits(0x411e6667u), Bits(0x419e6667u), 0.0f}));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(shell.turnDirection), probe.fall);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(shell.transform.angles.x), probe.tilt);
        EXPECT_EQ(shell.life, probe.life);
        EXPECT_EQ(shell.alpha, probe.alpha);
    }
    ModelEffect blind = PlacedShell();
    const auto cut = Client::World::StepSkinShell(blind, {});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x5299dc")) << cut.Error();
}

// 0x5208a0 con el 0x137: el rand() de la luz local (0x5208c5), el cuerpo, la animación (sin esqueleto, nada), el
// movimiento común 0x51fc80(efecto, 1) con la velocidad nueva girada por los ángulos nuevos y la vida menos uno; la
// posición de antes del paso queda como la anterior.
TEST(SkinShellEffectsTest, TheGameStepAddsTheCommonMove)
{
    ModelEffect shell = PlacedShell();
    ModelEffect body = PlacedShell();
    Client::World::ModelEffectStepServices services;
    services.terrainHeight = [](float, float) { return -1000.0; };
    auto particles = std::make_unique<Client::World::Particles>();
    CrtRandom random(5);

    const auto stepped =
        Client::World::StepGameModelEffect(shell, nullptr, *particles, 0.0f, random, nullptr, services);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_TRUE(Client::World::StepSkinShell(body, services).HasValue());
    const glm::vec3 move = Client::Render::Models::OriginalVectorRotate(
        body.velocity, Client::Render::Models::OriginalAngleMatrix(body.transform.angles));
    EXPECT_EQ(shell.transform.position,
              (glm::vec3{F32(D(move.x) + D(body.transform.position.x)), F32(D(move.y) + D(body.transform.position.y)),
                         F32(D(move.z) + D(body.transform.position.z))}));
    EXPECT_EQ(shell.previousPosition, (glm::vec3{1000.0f, 2000.0f, 300.0f}));
    EXPECT_EQ(shell.life, 39);
    EXPECT_EQ(shell.alpha, 4.0f);
    EXPECT_TRUE(shell.alive);
    CrtRandom expected(5);
    expected.Discard(1);
    EXPECT_EQ(random.State(), expected.State());
}
