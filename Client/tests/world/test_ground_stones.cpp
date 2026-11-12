#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/OriginalRotation.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectStepServices.h"

#include "support/ParticleDrawRecorder.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>

// El pisotón del caballo oscuro (0x4453e0) en la capa World: las piedras 0x118/0x119 (0x51cc6b, 0x51cd1b y su paso
// 0x5275ca), lo que sueltan (el polvo 5f2/11 de 0x541d81/0x546111 y la piedrita 0xc8/0xc9 con el subtipo 10 de
// 0x51b432 y 0x5238be) y la onda 0x6eb/0 (0x51f4ad y 0x52c5ca). Los números esperados repiten las cuentas del ASM con
// otro CrtRandom de la misma semilla.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::ModelEffect;
using Client::World::ModelEffects;

constexpr double Hundredth = static_cast<double>(std::bit_cast<float>(0x3c23d70au)); // 0x6b86c0.

float F32(double value)
{
    return static_cast<float>(value);
}

std::unique_ptr<Client::Data::Terrain::TerrainAttributes> OpenGround(std::uint16_t attribute = 0)
{
    auto attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    attributes->cells.fill(attribute);
    return attributes;
}

// 0x5f8290: y · 256 + x sin máscara, sobre el ftol truncado de cada eje por 0,01f.
TEST(GroundStonesTest, TheCellAttributeIsTheRowTimes256PlusTheColumnWithoutAMask)
{
    auto attributes = OpenGround();
    attributes->cells[Client::Data::Terrain::CellIndex(12, 34)] = 0x1234;
    attributes->cells[256] = 0x66;
    attributes->cells[257] = 0x77;
    attributes->cells[255] = 0x55;

    const auto inside = Client::World::UnmaskedCellAttribute(attributes.get(), {1250.5f, 3499.9f, 0.0f}, "test");
    ASSERT_TRUE(inside.HasValue()) << inside.Error();
    EXPECT_EQ(inside.Value(), 0x1234);
    // La columna 257 de la fila 0 es la celda (1, 1): 0x5f82a0 la habría enmascarado a (1, 0).
    const auto wide = Client::World::UnmaskedCellAttribute(attributes.get(), {25750.0f, 50.0f, 0.0f}, "test");
    ASSERT_TRUE(wide.HasValue());
    EXPECT_EQ(wide.Value(), 0x77);
    // 0,01f es un poco menos que 0,01: 25.700 · 0,01f es 256,99999… y el ftol lo deja en la columna 256.
    const auto under = Client::World::UnmaskedCellAttribute(attributes.get(), {25700.0f, 50.0f, 0.0f}, "test");
    ASSERT_TRUE(under.HasValue());
    EXPECT_EQ(under.Value(), 0x66);
    // ftol(−1,5) es −1: la fila 1 menos uno es la última columna de la fila 0.
    const auto behind = Client::World::UnmaskedCellAttribute(attributes.get(), {-150.0f, 150.0f, 0.0f}, "test");
    ASSERT_TRUE(behind.HasValue());
    EXPECT_EQ(behind.Value(), 0x55);

    for (const glm::vec3 outside : {glm::vec3(0.0f, 25650.0f, 0.0f), glm::vec3(-150.0f, 0.0f, 0.0f),
                                    glm::vec3(std::numeric_limits<float>::infinity(), 0.0f, 0.0f)})
    {
        const auto cut = Client::World::UnmaskedCellAttribute(attributes.get(), outside, "0xdead");
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0xdead"), std::string::npos) << cut.Error();
    }
    const auto blind = Client::World::UnmaskedCellAttribute(nullptr, {}, "0xbeef");
    ASSERT_FALSE(blind.HasValue());
    EXPECT_NE(blind.Error().find("0xbeef"), std::string::npos) << blind.Error();
}

// 0x51cc6b y 0x51cd1b en una celda abierta (los bits 0x01, 0x02 y 0x20 y el byte alto no cuentan): el prefijo común
// sin dueño, vida 40, la escala rand() % 30 · 0,01f más 1,2f o 1, +0xd8 en 0,3f y el yaw rand() % 360. Dos rand().
TEST(GroundStonesTest, TheGroundStonesAreBornWithTwoRandomsOnAnOpenCell)
{
    auto attributes = OpenGround(0xff23);
    for (const std::int16_t type : {Client::World::FirstGroundStoneEffect, Client::World::SecondGroundStoneEffect})
    {
        SCOPED_TRACE(type);
        ModelEffects effects;
        CrtRandom random(7);
        const auto created = effects.CreateGroundStone(type, {1000.5f, 2000.25f, 300.0f}, {10.0f, 20.0f, 30.0f},
                                                       {0.25f, 0.5f, 0.75f}, attributes.get(), random);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        ASSERT_EQ(created.Value(), std::optional<std::size_t>(0));
        const ModelEffect& stone = effects.Effects()[0];
        CrtRandom draws(7);
        const double base = type == Client::World::FirstGroundStoneEffect
                                ? static_cast<double>(std::bit_cast<float>(0x3f99999au))
                                : 1.0;
        const float scale = F32(static_cast<double>(draws.Next() % 30) * Hundredth + base);
        const auto yaw = static_cast<float>(draws.Next() % 360);
        EXPECT_TRUE(stone.alive);
        EXPECT_EQ(stone.type, type);
        EXPECT_EQ(stone.subtype, 0);
        EXPECT_EQ(stone.life, 40);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(stone.transform.scale), std::bit_cast<std::uint32_t>(scale));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(stone.speed), 0x3e99999au);
        EXPECT_EQ(stone.transform.angles, glm::vec3(10.0f, 20.0f, yaw));
        EXPECT_EQ(stone.transform.position, glm::vec3(1000.5f, 2000.25f, 300.0f));
        EXPECT_EQ(stone.light, glm::vec3(0.25f, 0.5f, 0.75f));
        EXPECT_EQ(stone.velocity, glm::vec3(0.0f));
        EXPECT_EQ(stone.alpha, 1.0f);
        EXPECT_EQ(stone.objectState, Client::Render::Models::ObjectState{});
        EXPECT_EQ(stone.animation.action, 0);
        EXPECT_EQ(stone.disturbanceState, 0xffff);
        EXPECT_FALSE(stone.hasOwner);
        EXPECT_EQ(random.State(), draws.State());
    }
}

// 0x51cc9b..0x51cdc0: con el bit 0x04, el 0x08 o el 0x10 del byte bajo, el lugar queda con el prefijo escrito y el
// byte vivo en 0, y sin rand(). Sin la tabla o fuera de ella, corta después del prefijo; otro tipo o la tabla de
// efectos llena, nada.
TEST(GroundStonesTest, ABlockedCellLeavesTheStoneWrittenButDeadWithoutRandoms)
{
    for (const std::uint16_t bit : {std::uint16_t{0x04}, std::uint16_t{0x08}, std::uint16_t{0x10}})
    {
        SCOPED_TRACE(bit);
        auto attributes = OpenGround(bit);
        ModelEffects effects;
        effects.Effects()[0].life = 3;
        CrtRandom random(5);
        const auto created = effects.CreateGroundStone(Client::World::SecondGroundStoneEffect, {100.0f, 200.0f, 5.0f},
                                                       {}, glm::vec3(1.0f), attributes.get(), random);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_FALSE(created.Value().has_value());
        const ModelEffect& slot = effects.Effects()[0];
        EXPECT_FALSE(slot.alive);
        EXPECT_EQ(slot.type, Client::World::SecondGroundStoneEffect);
        EXPECT_EQ(slot.transform.position, glm::vec3(100.0f, 200.0f, 5.0f));
        EXPECT_EQ(slot.life, 3);
        EXPECT_EQ(random.State(), 5u);
    }

    ModelEffects effects;
    CrtRandom random(5);
    auto cut =
        effects.CreateGroundStone(Client::World::FirstGroundStoneEffect, {}, {}, glm::vec3(1.0f), nullptr, random);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x51cc8b"), std::string::npos) << cut.Error();
    EXPECT_TRUE(effects.Effects()[0].alive);
    EXPECT_EQ(random.State(), 5u);
    auto attributes = OpenGround();
    cut = effects.CreateGroundStone(Client::World::SecondGroundStoneEffect, {0.0f, -150.0f, 0.0f}, {}, glm::vec3(1.0f),
                                    attributes.get(), random);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x51cd3b"), std::string::npos) << cut.Error();

    ModelEffects other;
    const auto none = other.CreateGroundStone(0x11a, {}, {}, glm::vec3(1.0f), attributes.get(), random);
    ASSERT_TRUE(none.HasValue());
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_FALSE(other.Effects()[0].alive);
    ModelEffects full;
    for (ModelEffect& effect : full.Effects())
        effect.alive = true;
    const auto crowded = full.CreateGroundStone(Client::World::FirstGroundStoneEffect, {}, {}, glm::vec3(1.0f),
                                                attributes.get(), random);
    ASSERT_TRUE(crowded.HasValue());
    EXPECT_FALSE(crowded.Value().has_value());
    EXPECT_EQ(random.State(), 5u);
}

// 0x51b432..0x51b4ff, la piedrita con el subtipo 10: el empuje ((& 255) + 64) · 0,2 (el double 0x6ba1e8) en y, la vida
// (& 15) + 32, la escala ((& 3) + 15) · 0,05f, el yaw rand() % 360, la velocidad (el empuje girado con ese yaw) y la
// caída (& 15) + 28. Otro tipo, nada y sin rand().
TEST(GroundStonesTest, TheGroundStoneChipIsBornWithFiveRandoms)
{
    constexpr double Fifth = std::bit_cast<double>(0x3fc999999999999aull);
    constexpr double Twentieth = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
    for (const std::int16_t type : {Client::World::FirstStoneChipEffect, Client::World::SecondStoneChipEffect})
    {
        SCOPED_TRACE(type);
        ModelEffects effects;
        CrtRandom random(11);
        const auto slot =
            effects.CreateGroundStoneChip(type, {10.0f, 20.0f, 30.0f}, {5.0f, 0.0f, 90.0f}, {0.5f, 0.5f, 0.5f}, random);
        ASSERT_EQ(slot, std::optional<std::size_t>(0));
        const ModelEffect& chip = effects.Effects()[0];
        CrtRandom draws(11);
        const float push = F32(static_cast<double>((draws.Next() & 255) + 64) * Fifth);
        const int life = (draws.Next() & 15) + 32;
        const float scale = F32(static_cast<double>((draws.Next() & 3) + 15) * Twentieth);
        const auto yaw = static_cast<float>(draws.Next() % 360);
        const auto fall = static_cast<float>((draws.Next() & 15) + 28);
        EXPECT_EQ(chip.type, type);
        EXPECT_EQ(chip.subtype, Client::World::GroundStoneChipSubtype);
        EXPECT_EQ(chip.life, life);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(chip.transform.scale), std::bit_cast<std::uint32_t>(scale));
        EXPECT_EQ(chip.transform.angles, glm::vec3(5.0f, 0.0f, yaw));
        EXPECT_EQ(chip.velocity,
                  Client::Render::Models::OriginalVectorRotate(
                      {0.0f, push, 0.0f}, Client::Render::Models::OriginalAngleMatrix({5.0f, 0.0f, yaw})));
        EXPECT_EQ(chip.turnDirection, fall);
        EXPECT_EQ(chip.transform.position, glm::vec3(10.0f, 20.0f, 30.0f));
        EXPECT_EQ(random.State(), draws.State());
    }
    ModelEffects effects;
    CrtRandom random(11);
    EXPECT_FALSE(effects.CreateGroundStoneChip(0xca, {}, {}, glm::vec3(1.0f), random).has_value());
    EXPECT_EQ(random.State(), 11u);
}

// 0x51f4ad con el subtipo 0: vida 30 y escala 20, sin rand(). Lleno, ninguna.
TEST(GroundStonesTest, TheStompShockWaveIsBornWithoutRandoms)
{
    ModelEffects effects;
    const auto slot = effects.CreateStompShockWave({100.5f, 200.25f, 300.0f}, {0.0f, 0.0f, 45.0f}, glm::vec3(0.5f));
    ASSERT_EQ(slot, std::optional<std::size_t>(0));
    const ModelEffect& wave = effects.Effects()[0];
    EXPECT_TRUE(wave.alive);
    EXPECT_EQ(wave.type, Client::World::ShockWaveEffect);
    EXPECT_EQ(wave.subtype, Client::World::StompShockWaveSubtype);
    EXPECT_EQ(wave.life, 30);
    EXPECT_EQ(wave.transform.scale, 20.0f);
    EXPECT_EQ(wave.transform.position, glm::vec3(100.5f, 200.25f, 300.0f));
    EXPECT_EQ(wave.transform.angles, glm::vec3(0.0f, 0.0f, 45.0f));
    EXPECT_EQ(wave.light, glm::vec3(0.5f));
    EXPECT_FALSE(wave.hasOwner);

    ModelEffects full;
    for (ModelEffect& effect : full.Effects())
        effect.alive = true;
    EXPECT_FALSE(full.CreateStompShockWave({}, {}, glm::vec3(1.0f)).has_value());
}

// 0x52c5d1 y 0x52c74e..0x52c7a6: la /0 pierde 1 de escala por paso, sin rand() más que el común; una escala negativa
// vuelve a 0 y la luz es (40 − vida) · 0,05f pasados los 20, o la vida · 0,05f, con la vida antes de la cola.
TEST(GroundStonesTest, TheStompShockWaveShrinksAndFadesWithItsLife)
{
    constexpr double LightPerLife = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
    ModelEffects effects;
    ASSERT_TRUE(effects.CreateStompShockWave({100.5f, 200.25f, 300.0f}, {}, glm::vec3(1.0f)).has_value());
    ModelEffect& wave = effects.Effects()[0];
    Client::World::Particles particles;
    CrtRandom random(3);
    for (int step = 1; step <= 30; ++step)
    {
        SCOPED_TRACE(step);
        const std::int32_t life = wave.life;
        const auto stepped = Client::World::StepGameModelEffect(wave, nullptr, particles, 0.0f, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(wave.transform.scale, step <= 20 ? static_cast<float>(20 - step) : 0.0f);
        const std::int32_t fade = life > 20 ? 40 - life : life;
        EXPECT_EQ(wave.light, glm::vec3(F32(static_cast<double>(fade) * LightPerLife)));
        EXPECT_EQ(wave.transform.position, glm::vec3(100.5f, 200.25f, 300.0f));
        EXPECT_EQ(wave.alive, step < 30);
    }
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(3, 30).State());
}

// Una piedra 0x118 de prueba, sin esqueleto (la animación común queda fuera).
ModelEffect GroundStone(std::int32_t life)
{
    ModelEffect stone;
    stone.alive = true;
    stone.type = Client::World::FirstGroundStoneEffect;
    stone.life = life;
    stone.transform.scale = 1.2f;
    stone.transform.position = {1000.0f, 2000.0f, 300.0f};
    stone.transform.angles = {0.0f, 0.0f, 45.0f};
    stone.light = {0.25f, 0.5f, 0.75f};
    return stone;
}

// 0x52769b..0x5276b2 → 0x52c4e5: la luz dinámica (0,79f; 0,72f; 0,49f) de radio 2 en cada paso. Con la vida bajo 8, el
// alpha por 0,7692308f (0x6ba240) y la luz de las mallas por 0,5f, cada uno guardado; con 8, nada. Fuera de 33..36 no
// suelta nada y sólo saca el rand() común.
TEST(GroundStonesTest, TheGroundStoneLightsTheGroundAndFadesAtTheEnd)
{
    constexpr glm::vec3 Glow{std::bit_cast<float>(0x3f4a3d71u), std::bit_cast<float>(0x3f3851ecu),
                             std::bit_cast<float>(0x3efae148u)};
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu));
    for (const std::int32_t life : {40, 37, 32, 8, 7, 1})
    {
        SCOPED_TRACE(life);
        ModelEffect stone = GroundStone(life);
        stone.alpha = 0.5f;
        stone.objectState.light = 0.75f;
        Client::World::Particles particles;
        ModelEffects effects;
        auto lighting = std::make_unique<Client::World::TerrainLighting>();
        Client::World::ModelEffectStepServices services;
        services.effects = &effects;
        services.lighting = lighting.get();
        CrtRandom random(9);
        const auto stepped =
            Client::World::StepGameModelEffect(stone, nullptr, particles, 0.0f, random, nullptr, services);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(stone.alpha, life < 8 ? F32(0.5 * Fade) : 0.5f);
        EXPECT_EQ(stone.objectState.light, life < 8 ? 0.375f : 0.75f);
        EXPECT_FALSE(particles.Any());
        EXPECT_FALSE(effects.Effects()[0].alive);
        auto expected = std::make_unique<Client::World::TerrainLighting>();
        ASSERT_TRUE(
            Client::World::AddDynamicLight(expected.get(), {1000.0f, 2000.0f, 300.0f}, Glow, 2, "test").HasValue());
        EXPECT_EQ(lighting->cells, expected->cells);
        EXPECT_EQ(stone.transform.position, glm::vec3(1000.0f, 2000.0f, 300.0f));
        EXPECT_EQ(stone.life, life - 1);
        EXPECT_EQ(stone.alive, life > 1);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(9, 1).State());
    }
}

// 0x5275df..0x52766f: con la vida entre 33 y 36, en (x + 60; y − 60; z + 50) el polvo 5f2/11 con los ángulos de la
// piedra, la luz (1; 0,8f; 0,6f) y la escala 2, y después la piedrita 0xc8 + (rand() & 1) con el subtipo 10, los mismos
// ángulos y la luz de la piedra, en ese orden de rand().
TEST(GroundStonesTest, TheGroundStoneDropsDustAndAChipBetweenThirtyThreeAndThirtySix)
{
    const glm::vec3 point{1060.0f, 1940.0f, 350.0f};
    const glm::vec3 dustLight{1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f19999au)};
    for (const std::int32_t life : {33, 34, 35, 36})
    {
        for (const std::uint32_t seed : {1u, 2u, 3u, 4u})
        {
            SCOPED_TRACE(life);
            SCOPED_TRACE(seed);
            ModelEffect stone = GroundStone(life);
            Client::World::Particles particles;
            ModelEffects effects;
            auto lighting = std::make_unique<Client::World::TerrainLighting>();
            Client::World::ModelEffectStepServices services;
            services.effects = &effects;
            services.lighting = lighting.get();
            CrtRandom random(seed);
            const auto stepped =
                Client::World::StepGameModelEffect(stone, nullptr, particles, 0.0f, random, nullptr, services);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();

            CrtRandom draws(seed);
            static_cast<void>(draws.Next());
            Client::World::Particles expectedParticles;
            ASSERT_TRUE(expectedParticles
                            .CreateChecked(Client::World::BreathParticle, point, stone.transform.angles, dustLight,
                                           Client::World::StoneDustSubtype, 2.0f, 0.0f, draws)
                            .HasValue());
            const auto type = static_cast<std::int16_t>(0xc8 + (draws.Next() & 1));
            ModelEffects expectedEffects;
            ASSERT_TRUE(expectedEffects.CreateGroundStoneChip(type, point, stone.transform.angles, stone.light, draws)
                            .has_value());

            const Client::World::Particle& dust = particles.All()[0];
            const Client::World::Particle& wanted = expectedParticles.All()[0];
            ASSERT_TRUE(dust.alive);
            EXPECT_EQ(dust.type, 0x5f2);
            EXPECT_EQ(dust.subtype, 11);
            EXPECT_EQ(dust.position, wanted.position);
            EXPECT_EQ(dust.angles, wanted.angles);
            EXPECT_EQ(dust.light, dustLight);
            EXPECT_EQ(dust.velocity, wanted.velocity);
            EXPECT_EQ(dust.rotation, wanted.rotation);
            EXPECT_EQ(dust.start, point);
            EXPECT_FALSE(particles.All()[1].alive);
            const ModelEffect& chip = effects.Effects()[0];
            ASSERT_TRUE(chip.alive);
            EXPECT_EQ(chip.type, type);
            EXPECT_EQ(chip.subtype, Client::World::GroundStoneChipSubtype);
            EXPECT_EQ(chip.transform.position, point);
            EXPECT_EQ(chip.light, stone.light);
            EXPECT_EQ(chip.velocity, expectedEffects.Effects()[0].velocity);
            EXPECT_EQ(chip.life, expectedEffects.Effects()[0].life);
            EXPECT_FALSE(effects.Effects()[1].alive);
            EXPECT_EQ(random.State(), draws.State());
            EXPECT_EQ(stone.life, life - 1);
        }
    }
}

// Sin la tabla de efectos corta en 0x52766a con el polvo ya creado y antes del rand() de la piedrita; sin la luz
// dinámica, en 0x52c4f1 con lo demás hecho.
TEST(GroundStonesTest, TheGroundStoneCutsWithoutItsTables)
{
    ModelEffect stone = GroundStone(35);
    Client::World::Particles particles;
    auto lighting = std::make_unique<Client::World::TerrainLighting>();
    Client::World::ModelEffectStepServices services;
    services.lighting = lighting.get();
    CrtRandom random(1);
    auto cut = Client::World::StepGameModelEffect(stone, nullptr, particles, 0.0f, random, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52766a"), std::string::npos) << cut.Error();
    EXPECT_TRUE(particles.All()[0].alive);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 8).State()) << "el común y los siete del polvo";

    ModelEffect dark = GroundStone(5);
    ModelEffects effects;
    services.effects = &effects;
    services.lighting = nullptr;
    cut = Client::World::StepGameModelEffect(dark, nullptr, particles, 0.0f, random, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52c4f1"), std::string::npos) << cut.Error();
    EXPECT_EQ(dark.objectState.light, 0.5f);
}

// 0x523a2e..0x523ad5 con el subtipo 10: el mismo rebote que el 0, pero sale antes del rand() % 10 del fuego.
TEST(GroundStonesTest, TheGroundStoneChipBouncesWithoutBurning)
{
    std::uint32_t burns = 1;
    while (Client::Tests::AfterDraws(burns, 1).Next() % 10 != 0)
        ++burns;
    ModelEffect chip;
    chip.alive = true;
    chip.type = Client::World::FirstStoneChipEffect;
    chip.subtype = Client::World::GroundStoneChipSubtype;
    chip.life = 40;
    chip.transform.scale = 0.75f;
    chip.transform.position = {1000.0f, 1000.0f, 100.0f};
    chip.velocity = {10.0f, 0.0f, 0.0f};
    chip.turnDirection = 5.0f;
    chip.animation.action = 0xff;
    Client::World::Particles particles;
    Client::World::ModelEffectStepServices services;
    services.terrainHeight = [](float, float) { return 200.0; };
    CrtRandom random(burns);
    const auto stepped = Client::World::StepGameModelEffect(chip, nullptr, particles, 0.0f, random, nullptr, services);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(chip.transform.position.z, 200.0f);
    EXPECT_EQ(chip.velocity.x, F32(10.0 * static_cast<double>(std::bit_cast<float>(0x3f666666u))));
    EXPECT_EQ(chip.turnDirection, -1.0f);
    EXPECT_EQ(chip.transform.angles.x, -96.0f);
    EXPECT_EQ(chip.life, 35);
    EXPECT_FALSE(particles.Any());
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(burns, 1).State());
}

// 0x541d81..0x541e72: la 5f2/11 vive 50, se corre ((& 63) − 32; (& 63) − 32; (& 63) + 32), se inclina rand() % 90 −
// 45, gira rand() % 360 en yaw y en el billboard, guarda su luz en +0x60..+0x68 y empuja −((& 7) + 40) en y. Siete
// rand().
TEST(GroundStonesTest, TheStoneDustIsBornWithSevenRandoms)
{
    Client::World::Particles particles;
    CrtRandom random(21);
    const glm::vec3 light{1.0f, 0.8f, 0.6f};
    const auto created =
        particles.CreateChecked(Client::World::BreathParticle, {1060.0f, 1940.0f, 350.0f}, {0.0f, 7.0f, 45.0f}, light,
                                Client::World::StoneDustSubtype, 2.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_EQ(created.Value(), std::optional<std::size_t>(0));
    const Client::World::Particle& dust = particles.All()[0];
    CrtRandom draws(21);
    const int x = (draws.Next() & 63) - 32;
    const int y = (draws.Next() & 63) - 32;
    const int z = (draws.Next() & 63) + 32;
    const int tilt = draws.Next() % 90 - 45;
    const int yaw = draws.Next() % 360;
    const int spin = draws.Next() % 360;
    const int push = (draws.Next() & 7) + 40;
    EXPECT_EQ(dust.life, 50);
    EXPECT_EQ(dust.position, glm::vec3(F32(x + 1060.0), F32(y + 1940.0), F32(z + 350.0)));
    EXPECT_EQ(dust.start, glm::vec3(1060.0f, 1940.0f, 350.0f));
    EXPECT_EQ(dust.angles, glm::vec3(static_cast<float>(tilt), 7.0f, static_cast<float>(yaw)));
    EXPECT_EQ(dust.rotation, static_cast<float>(spin));
    EXPECT_EQ(dust.velocity, glm::vec3(0.0f, -static_cast<float>(push), 0.0f));
    EXPECT_EQ(dust.rotationFactor, 1.0f);
    EXPECT_EQ(dust.field64, glm::vec2(0.8f, 0.6f));
    EXPECT_EQ(dust.light, light);
    EXPECT_EQ(dust.scale, 2.0f);
    EXPECT_TRUE(dust.moves);
    EXPECT_EQ(random.State(), draws.State());
}

// 0x546111..0x54616c, después de la vida y del movimiento común (la velocidad girada por los ángulos): la luz es la
// vida por 0,02f por la de la creación, la velocidad por 0,4f, la escala más 0,05f y la z 1 menos. Los dos rand() del
// viento, nada más.
TEST(GroundStonesTest, TheStoneDustDimsBrakesGrowsAndSinks)
{
    constexpr double LightPerLife = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
    constexpr double Brake = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
    Client::World::Particles particles;
    CrtRandom random(21);
    ASSERT_TRUE(particles
                    .CreateChecked(Client::World::BreathParticle, {1060.0f, 1940.0f, 350.0f}, {0.0f, 7.0f, 45.0f},
                                   {1.0f, 0.8f, 0.6f}, Client::World::StoneDustSubtype, 2.0f, 0.0f, random)
                    .HasValue());
    Client::World::Particle before = particles.All()[0];
    const std::uint32_t state = random.State();
    const auto stepped = Client::World::StepParticlesChecked(particles, random);
    ASSERT_TRUE(stepped.HasValue());
    const Client::World::Particle& dust = particles.All()[0];
    const glm::vec3 turned = Client::Render::Models::OriginalVectorRotate(
        before.velocity, Client::Render::Models::OriginalAngleMatrix(before.angles));
    glm::vec3 moved{};
    for (int axis = 0; axis < 3; ++axis)
        moved[axis] = F32(static_cast<double>(turned[axis]) + static_cast<double>(before.position[axis]));
    moved.z = F32(static_cast<double>(moved.z) - 1.0);
    EXPECT_EQ(dust.life, 49);
    EXPECT_EQ(dust.position, moved);
    const double glow = 49.0 * LightPerLife;
    EXPECT_EQ(dust.light,
              glm::vec3(F32(glow * 1.0), F32(glow * static_cast<double>(0.8f)), F32(glow * static_cast<double>(0.6f))));
    EXPECT_EQ(dust.velocity, glm::vec3(0.0f, F32(static_cast<double>(before.velocity.y) * Brake), 0.0f));
    EXPECT_EQ(dust.scale, F32(2.0 + Growth));
    EXPECT_EQ(dust.angles, before.angles);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(state, 2).State());
}

// 0x548707..0x548a64: la 5f2/11 no resta ni es la /6: el billboard común con su textura, el ancho y el alto por la
// escala, su luz y el giro de +0x44.
TEST(GroundStonesTest, TheStoneDustIsDrawnWithTheCommonBillboard)
{
    Client::World::Particles particles;
    CrtRandom random(21);
    ASSERT_TRUE(particles
                    .CreateChecked(Client::World::BreathParticle, {1060.0f, 1940.0f, 350.0f}, {0.0f, 7.0f, 45.0f},
                                   {1.0f, 0.8f, 0.6f}, Client::World::StoneDustSubtype, 2.0f, 0.0f, random)
                    .HasValue());
    Client::World::Particle dust = particles.All()[0];
    const auto drawn = Client::Tests::DrawOneParticle(dust, glm::vec2(64.0f, 32.0f), std::uint8_t{0});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    const Client::Tests::RecordedParticleDraw& draw = *drawn.Value();
    EXPECT_EQ(draw.texture, 0x5f2);
    EXPECT_NE(draw.blend, Client::Tests::RecordedParticleDraw::Blend::Subtractive);
    EXPECT_EQ(draw.position, dust.position);
    EXPECT_EQ(draw.width, 128.0f);
    EXPECT_EQ(draw.height, 64.0f);
    EXPECT_EQ(draw.light, dust.light);
    EXPECT_EQ(draw.rotation, dust.rotation);
    EXPECT_EQ(draw.uv, glm::vec2(0.0f));
    EXPECT_EQ(draw.uvSize, glm::vec2(1.0f));
}
} // namespace
