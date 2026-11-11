#include "World/GameModelEffects.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/SelectionParticleKernels.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectStepServices.h"

#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace
{
TEST(GameModelEffectsTest, NoriaUpdatesLightAndPitchThenTheCommonLifeWithOneRandom)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 322;
    effect.life = 100;
    effect.turnDirection = 3.0f;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    const auto result = Client::World::StepGameModelEffect(effect, nullptr, particles, 0.0f, random);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(effect.light, glm::vec3(0.01f));
    EXPECT_EQ(effect.transform.angles.y, 7.0f);
    EXPECT_EQ(effect.life, 99);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
}
TEST(GameModelEffectsTest, MagicalTeleportCreatesEighteenSparksWithThreeAnglesEach)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::World::TeleportEffect;
    effect.life = 10;
    effect.transform.position = {100.0f, 200.0f, 300.0f};
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    const auto result = Client::World::StepGameModelEffect(effect, nullptr, particles, 0.0f, random);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(effect.life, 9);
    ASSERT_TRUE(particles.All()[0].alive);
    EXPECT_EQ(particles.All()[0].position.z, 324.0f);
    EXPECT_EQ(particles.All()[0].scale, 12.0f);
    EXPECT_EQ(particles.All()[0].subtype, 1);
    EXPECT_EQ(particles.All()[0].life, 2);
    EXPECT_EQ(particles.All()[17].position.z, 732.0f);
    EXPECT_EQ(particles.All()[17].scale, 6.0f);
    EXPECT_FALSE(particles.All()[18].alive);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 55).State());
}
TEST(GameModelEffectsTest, AFullSparkPoolConsumesOnlyTheModelEffectPrefix)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::World::TeleportEffect;
    effect.life = 10;
    Client::World::Particles particles;
    for (auto& particle : particles.All())
        particle.alive = true;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(Client::World::StepGameModelEffect(effect, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(effect.life, 9);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
}
TEST(GameModelEffectsTest, TeleportSparksUseTheirOwnShrinkRatherThanTheRavenFade)
{
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    const auto created =
        particles.CreateChecked(Client::World::SparkParticle, {}, {}, {1.0f, 0.5f, 0.25f}, 1, 12.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 3).State());
    const auto step = Client::World::StepParticlesChecked(particles, random);
    ASSERT_TRUE(step.HasValue());
    EXPECT_EQ(particles.All()[0].scale, 10.0f);
    EXPECT_EQ(particles.All()[0].light, glm::vec3(1.0f, 0.5f, 0.25f));
}
TEST(GameModelEffectsTest, NoriaUsesTheStoredBlueArgumentAtANonzeroClock)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 324;
    effect.life = 100;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(Client::World::StepGameModelEffect(effect, nullptr, particles, 1000.0f, random).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.x), 0x3e40c25fu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.y), 0x3e55553du);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.z), 0x3e4f93a4u);
}
TEST(GameModelEffectsTest, TextureEffectsSkipTheNativeModelAnimationRange)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::World::ArrivalEffect;
    effect.life = 100;
    effect.speed = 1.0f;
    effect.animation.frame = 1.0f;
    Client::Render::Models::Skeleton skeleton;
    skeleton.actions.resize(1);
    skeleton.actions[0].keyCount = 4;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(Client::World::StepGameModelEffect(effect, &skeleton, particles, 1000.0f, random).HasValue());
    EXPECT_EQ(effect.animation.frame, 1.0f);
    EXPECT_EQ(effect.life, 99);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
}
TEST(GameModelEffectsTest, SparseParticleSlotsPreserveTheirPayloadAndConsumeOnlyTheirThreeAngles)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::World::TeleportEffect;
    effect.life = 10;
    Client::World::Particles particles;
    for (auto& particle : particles.All())
        particle.alive = true;
    particles.All()[5].alive = false;
    particles.All()[7].alive = false;
    particles.All()[5].field38 = 0x12345678;
    particles.All()[5].rotationFactor = 23.0f;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(Client::World::StepGameModelEffect(effect, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(particles.All()[5].scale, 12.0f);
    EXPECT_EQ(particles.All()[7].scale, 6.0f);
    EXPECT_EQ(particles.All()[5].position.z, 24.0f);
    EXPECT_EQ(particles.All()[7].position.z, 48.0f);
    EXPECT_EQ(particles.All()[5].field38, 0x12345678u);
    EXPECT_EQ(particles.All()[5].rotationFactor, 23.0f);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 7).State());
}
// 0x520d11..0x520fd1, la mariposa 0xae del 0x3c de la selección: el giro según +0x88, la deriva y el aleteo
// de la velocidad z, el freno contra la altura del terreno (más 50 sube, más 150 baja), el salto de z, la
// luz del subtipo y el sprite 0x5ac de escala 1 en su lugar, antes del paso común.
struct Butterfly
{
    Client::World::ModelEffect effect;
    Client::World::Particles particles;
    Client::World::FrameSprites sprites;
    double height = 1000.0;
    Client::World::ModelEffectStepServices services;

    explicit Butterfly(std::int32_t subtype)
    {
        effect.alive = true;
        effect.type = Client::World::ButterflyEffect;
        effect.subtype = subtype;
        effect.life = 250;
        effect.state = 1;
        effect.transform.position = {100.0f, 200.0f, 300.0f};
        effect.transform.angles = {0.0f, 0.0f, 10.0f};
        effect.light = {0.5f, 0.5f, 0.5f};
        services.terrainHeight = [this](float x, float y)
        {
            EXPECT_EQ(x, 100.0f);
            EXPECT_EQ(y, 200.0f);
            return height;
        };
        services.sprites = &sprites;
    }
};

TEST(GameModelEffectsTest, TheButterflyFlutterKeepsItsHeightBandAndGlowsWithItsSubtype)
{
    for (const double height : {1000.0, 0.0})
    {
        for (const std::uint8_t state : {std::uint8_t{0}, std::uint8_t{1}})
        {
            Butterfly butterfly(0);
            butterfly.height = height;
            butterfly.effect.state = state;
            Client::Render::Models::CrtRandom random(5);
            Client::Render::Models::CrtRandom draws(5);
            static_cast<void>(draws.Next()); // 0x5208c5.
            const int turn = draws.Next() % 10;
            float velocity = 0.0f;
            if ((draws.Next() & 31) == 0)
                velocity = static_cast<float>(draws.Next() % 15 - 7);
            velocity = static_cast<float>((draws.Next() % 15 - 7) * static_cast<double>(0.2f) + velocity);
            const double damping = static_cast<double>(0.8f);
            if (height + 50.0 > 300.0)
                velocity = static_cast<float>(velocity * damping + 1.0);
            if (height + 150.0 < 300.0)
                velocity = static_cast<float>(velocity * damping - 1.0);
            const float z = static_cast<float>((draws.Next() % 15 - 7) * static_cast<double>(0.3f) + 300.0);
            const double level = ((draws.Next() & 31) + 64) * static_cast<double>(0.01f);

            const auto stepped = Client::World::StepGameModelEffect(butterfly.effect, nullptr, butterfly.particles,
                                                                    0.0f, random, nullptr, butterfly.services);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(butterfly.effect.transform.angles.z, static_cast<float>(state > 0 ? 10.0 + turn : 10.0 - turn));
            EXPECT_EQ(butterfly.effect.velocity.z, velocity);
            const auto& sprite = butterfly.sprites.Sprites()[0];
            ASSERT_TRUE(sprite.alive);
            EXPECT_EQ(sprite.texture, 0x5ac);
            EXPECT_EQ(sprite.scale, 1.0f);
            EXPECT_EQ(sprite.position, glm::vec3(100.0f, 200.0f, z));
            EXPECT_EQ(sprite.light, glm::vec3(static_cast<float>(level * static_cast<double>(0.4f)),
                                              static_cast<float>(level * static_cast<double>(0.8f)),
                                              static_cast<float>(level * static_cast<double>(0.6f))));
            EXPECT_EQ(butterfly.effect.life, 249);
            EXPECT_EQ(random.State(), draws.State());
        }
    }
}

// 0x520ec6..0x520fb1: la /3 brilla (0,7; 0,9; 0,5) y, con rand() & 1 en 0, suelta una 5c5/7 con la luz del
// efecto, tres desplazamientos de (rand() & 15) − 8 sorteados antes de sumarse.
TEST(GameModelEffectsTest, TheLargeButterflyDropsASparkOnEvenDraws)
{
    int sparks = 0;
    for (const std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u})
    {
        Butterfly butterfly(3);
        Client::Render::Models::CrtRandom random(seed);
        Client::Render::Models::CrtRandom draws(seed);
        static_cast<void>(draws.Next()); // 0x5208c5.
        static_cast<void>(draws.Next()); // El giro.
        if ((draws.Next() & 31) == 0)
            static_cast<void>(draws.Next()); // La deriva.
        static_cast<void>(draws.Next());     // El aleteo.
        static_cast<void>(draws.Next());     // El salto.
        const double level = ((draws.Next() & 31) + 64) * static_cast<double>(0.01f);
        const bool spark = (draws.Next() & 1) == 0;
        glm::vec3 offset{};
        if (spark)
        {
            for (int axis = 0; axis < 3; ++axis)
                offset[axis] = static_cast<float>((draws.Next() & 15) - 8);
        }
        const auto stepped = Client::World::StepGameModelEffect(butterfly.effect, nullptr, butterfly.particles, 0.0f,
                                                                random, nullptr, butterfly.services);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const auto& sprite = butterfly.sprites.Sprites()[0];
        EXPECT_EQ(sprite.light, glm::vec3(static_cast<float>(level * static_cast<double>(0.7f)),
                                          static_cast<float>(level * static_cast<double>(0.9f)),
                                          static_cast<float>(level * static_cast<double>(0.5f))));
        const auto& particle = butterfly.particles.All()[0];
        EXPECT_EQ(particle.alive, spark) << seed;
        if (!spark)
            continue;
        ++sparks;
        EXPECT_EQ(particle.type, Client::World::FairySparkParticle);
        EXPECT_EQ(particle.subtype, 7);
        EXPECT_EQ(particle.light, glm::vec3(0.5f));
        EXPECT_EQ(particle.position.x, 100.0f + offset.x);
        EXPECT_EQ(particle.position.y, 200.0f + offset.y);
        EXPECT_EQ(particle.position.z, sprite.position.z + offset.z);
    }
    EXPECT_GT(sparks, 0);
}

// Sin la altura del terreno corta en 0x520dad; sin la tabla de sprites, en 0x520fd1.
TEST(GameModelEffectsTest, TheButterflyNeedsTheTerrainHeightAndTheSpriteTable)
{
    Butterfly heightless(0);
    heightless.services.terrainHeight = {};
    Client::Render::Models::CrtRandom random(5);
    auto cut = Client::World::StepGameModelEffect(heightless.effect, nullptr, heightless.particles, 0.0f, random,
                                                  nullptr, heightless.services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x520dad"), std::string::npos);

    Butterfly spriteless(0);
    spriteless.services.sprites = nullptr;
    cut = Client::World::StepGameModelEffect(spriteless.effect, nullptr, spriteless.particles, 0.0f, random, nullptr,
                                             spriteless.services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x520fd1"), std::string::npos);
}

// Los efectos del Dungeon y de Lost Tower, sin dueño y con la animación común fuera (sin esqueleto).
Client::World::ModelEffect DungeonEffect(std::int16_t type, std::int32_t life)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = type;
    effect.life = life;
    effect.transform.scale = 1.0f;
    effect.transform.position = {1000.0f, 1000.0f, 100.0f};
    effect.light = {0.25f, 0.5f, 0.75f};
    effect.animation.action = 0xff;
    return effect;
}

// La primera semilla cuya sorteo número `draw` (desde 0) cumple la condición.
std::uint32_t SeedWhereDraw(int draw, bool (*condition)(int))
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        Client::Render::Models::CrtRandom random(seed);
        for (int skipped = 0; skipped < draw; ++skipped)
            static_cast<void>(random.Next());
        if (condition(random.Next()))
            return seed;
    }
}

// 0x52444d..0x5244ae: la z suma la caída (+0xe4), que pierde 1; con el suelo por encima, se apoya, la caída
// rebota a −0,4f, pierde 4 de vida y 2 en la y de su velocidad (que 0x51fc80 suma girada en el mismo paso).
// Un solo rand(), el del prefijo. previousPosition guarda la posición de antes del paso entero.
TEST(GameModelEffectsTest, TheFallingStoneFallsAndBouncesOnTheGround)
{
    Client::World::ModelEffect falling = DungeonEffect(Client::World::FallingStoneEffect, 30);
    falling.turnDirection = -2.0f;
    Client::World::Particles particles;
    Client::World::ModelEffectStepServices services;
    services.terrainHeight = [](float, float) { return 50.0; };
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(
        Client::World::StepGameModelEffect(falling, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
    EXPECT_EQ(falling.transform.position, glm::vec3(1000.0f, 1000.0f, 98.0f));
    EXPECT_EQ(falling.turnDirection, -3.0f);
    EXPECT_EQ(falling.life, 29);
    EXPECT_EQ(falling.velocity, glm::vec3(0.0f));
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());

    Client::World::ModelEffect landing = DungeonEffect(Client::World::FallingStoneEffect, 30);
    landing.turnDirection = -2.0f;
    services.terrainHeight = [](float, float) { return 120.25; };
    ASSERT_TRUE(
        Client::World::StepGameModelEffect(landing, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
    EXPECT_EQ(landing.transform.position, glm::vec3(1000.0f, 998.0f, 120.25f));
    EXPECT_EQ(landing.turnDirection, static_cast<float>(-3.0 * static_cast<double>(std::bit_cast<float>(0xbecccccdu))));
    EXPECT_EQ(landing.life, 25);
    EXPECT_EQ(landing.velocity, glm::vec3(0.0f, -2.0f, 0.0f));
    EXPECT_EQ(landing.previousPosition, glm::vec3(1000.0f, 1000.0f, 100.0f));

    services.terrainHeight = {};
    Client::World::ModelEffect blind = DungeonEffect(Client::World::FallingStoneEffect, 30);
    const auto cut = Client::World::StepGameModelEffect(blind, nullptr, particles, 0.0f, random, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x524473"), std::string::npos) << cut.Error();
}

// 0x5239c8..0x523b3a con el subtipo 0: la velocidad sin girar se suma y queda por 0,9f; la caída suma y
// pierde 3; en el aire se inclina escala · 32 y en el suelo se apoya, rebota a −0,5, pierde 4 de vida y se
// inclina escala · 128. Con rand() % 10 en 0, un fuego 5d9 de subtipo rand() % 3 + 1 con su luz.
TEST(GameModelEffectsTest, TheStoneChipsDragTiltBounceAndSometimesBurn)
{
    const std::uint32_t burns = SeedWhereDraw(1, [](int value) { return value % 10 == 0; });
    const std::uint32_t cold = SeedWhereDraw(1, [](int value) { return value % 10 != 0; });
    {
        Client::World::ModelEffect chip = DungeonEffect(Client::World::FirstStoneChipEffect, 40);
        chip.velocity = {10.0f, 0.0f, 0.0f};
        chip.turnDirection = 5.0f;
        Client::World::Particles particles;
        Client::World::ModelEffectStepServices services;
        services.terrainHeight = [](float, float) { return 0.0; };
        Client::Render::Models::CrtRandom random(cold);
        ASSERT_TRUE(
            Client::World::StepGameModelEffect(chip, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
        EXPECT_EQ(chip.velocity.x, static_cast<float>(10.0 * static_cast<double>(std::bit_cast<float>(0x3f666666u))));
        EXPECT_EQ(chip.turnDirection, 2.0f);
        EXPECT_EQ(chip.transform.angles.x, -32.0f);
        EXPECT_EQ(chip.transform.position.z, 105.0f);
        EXPECT_EQ(chip.life, 39);
        EXPECT_FALSE(particles.All()[0].alive);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(cold, 2).State());
    }
    {
        Client::World::ModelEffect chip = DungeonEffect(Client::World::SecondStoneChipEffect, 40);
        chip.turnDirection = 5.0f;
        Client::World::Particles particles;
        Client::World::ModelEffectStepServices services;
        services.terrainHeight = [](float, float) { return 200.0; };
        Client::Render::Models::CrtRandom random(burns);
        ASSERT_TRUE(
            Client::World::StepGameModelEffect(chip, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
        EXPECT_EQ(chip.transform.position.z, 200.0f);
        EXPECT_EQ(chip.turnDirection, -1.0f);
        EXPECT_EQ(chip.transform.angles.x, -128.0f);
        EXPECT_EQ(chip.life, 35);
        const auto& fire = particles.All()[0];
        ASSERT_TRUE(fire.alive);
        EXPECT_EQ(fire.type, 0x5d9);
        Client::Render::Models::CrtRandom draws = Client::Tests::AfterDraws(burns, 2);
        EXPECT_EQ(fire.subtype, draws.Next() % 3 + 1);
    }
    Client::World::ModelEffect owned = DungeonEffect(Client::World::FirstStoneChipEffect, 40);
    owned.subtype = 1;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    const auto cut = Client::World::StepGameModelEffect(owned, nullptr, particles, 0.0f, random);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5238be"), std::string::npos) << cut.Error();
}

// 0x52ad21..0x52ae25 con el subtipo 0: seis llamas 5de/0 a ±25 en x e y (sus dos sorteos antes de cada
// creación), con rand() & 7 en 0 la piedrita 0xc8 + (rand() & 1), y la luz dinámica (L; L · 0,4f; 0) de radio
// 3 con L = ((primer rand() & 3) + 7) · 0,1f. Lo mismo con las mismas llamadas en otro orden daría otros
// números: el esperado repite la secuencia del ASM con otro CrtRandom.
TEST(GameModelEffectsTest, TheFlameJetSpitsSixFlamesASometimesChipAndItsLight)
{
    for (const bool chipped : {true, false})
    {
        SCOPED_TRACE(chipped);
        // Sin el + de los lambdas: en MSVC, con varias convenciones de llamada, es ambiguo.
        const auto chips = [](int value) { return (value & 7) == 0; };
        const auto stays = [](int value) { return (value & 7) != 0; };
        const std::uint32_t seed = chipped ? SeedWhereDraw(25, chips) : SeedWhereDraw(25, stays);
        Client::World::ModelEffect jet = DungeonEffect(Client::World::FlameJetEffect, 30);
        Client::World::Particles particles;
        Client::World::ModelEffects effects;
        auto lighting = std::make_unique<Client::World::TerrainLighting>();
        Client::World::ModelEffectStepServices services;
        services.effects = &effects;
        services.lighting = lighting.get();
        Client::Render::Models::CrtRandom random(seed);
        const auto stepped =
            Client::World::StepGameModelEffect(jet, nullptr, particles, 0.0f, random, nullptr, services);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();

        Client::Render::Models::CrtRandom draws(seed);
        const auto luminosity =
            static_cast<float>(((draws.Next() & 3) + 7) * static_cast<double>(std::bit_cast<float>(0x3dcccccdu)));
        Client::World::Particles expectedParticles;
        for (std::size_t flame = 0; flame < 6; ++flame)
        {
            const auto dx = static_cast<float>(draws.Next() % 50 - 25);
            const auto dy = static_cast<float>(draws.Next() % 50 - 25);
            const glm::vec3 point{static_cast<float>(static_cast<double>(dx) + 1000.0),
                                  static_cast<float>(static_cast<double>(dy) + 1000.0), 100.0f};
            ASSERT_TRUE(expectedParticles
                            .CreateChecked(Client::World::FlameParticle, point, glm::vec3(0.0f), glm::vec3(1.0f), 0,
                                           1.0f, 0.0f, draws)
                            .HasValue());
            EXPECT_EQ(particles.All()[flame].position, point) << flame;
            EXPECT_EQ(particles.All()[flame].subtype, 0);
            EXPECT_EQ(particles.All()[flame].light, glm::vec3(1.0f));
            EXPECT_EQ(particles.All()[flame].scale, expectedParticles.All()[flame].scale);
        }
        EXPECT_FALSE(particles.All()[6].alive);
        Client::World::ModelEffects expectedEffects;
        if ((draws.Next() & 7) == 0)
        {
            const auto type = static_cast<std::int16_t>(0xc8 + (draws.Next() & 1));
            ASSERT_TRUE(
                expectedEffects.CreateStoneChip(type, {1000.0f, 1000.0f, 100.0f}, glm::vec3(0.0f), jet.light, draws)
                    .has_value());
        }
        EXPECT_EQ(effects.Effects()[0].alive, chipped);
        EXPECT_EQ(effects.Effects()[0].type, expectedEffects.Effects()[0].type);
        EXPECT_EQ(effects.Effects()[0].velocity, expectedEffects.Effects()[0].velocity);
        EXPECT_EQ(random.State(), draws.State());
        auto expectedLighting = std::make_unique<Client::World::TerrainLighting>();
        ASSERT_TRUE(
            Client::World::AddDynamicLight(expectedLighting.get(), {1000.0f, 1000.0f, 100.0f},
                                           {luminosity,
                                            static_cast<float>(static_cast<double>(luminosity) *
                                                               static_cast<double>(std::bit_cast<float>(0x3ecccccdu))),
                                            0.0f},
                                           3, "test")
                .HasValue());
        EXPECT_EQ(lighting->cells, expectedLighting->cells);
        EXPECT_EQ(jet.life, 29);
    }
}

// La luz local del primer rand() se apaga con menos de 5 de vida: L − (5 − vida) · 0,2f, en 0 si queda debajo.
// Sin la tabla de efectos corta cuando la piedrita sale; sin la luz dinámica, al final; otro subtipo, antes.
TEST(GameModelEffectsTest, TheFlameJetFadesItsLightAndCutsWithoutItsTables)
{
    Client::World::ModelEffect fading = DungeonEffect(Client::World::FlameJetEffect, 1);
    Client::World::Particles particles;
    Client::World::ModelEffects effects;
    auto lighting = std::make_unique<Client::World::TerrainLighting>();
    Client::World::ModelEffectStepServices services;
    services.effects = &effects;
    services.lighting = lighting.get();
    Client::Render::Models::CrtRandom random(3);
    ASSERT_LT(Client::Tests::AfterDraws(3, 0).Next() & 3, 2);
    ASSERT_TRUE(
        Client::World::StepGameModelEffect(fading, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
    EXPECT_FALSE(fading.alive);
    EXPECT_EQ(lighting->cells, std::make_unique<Client::World::TerrainLighting>()->cells)
        << "con la vida en 1, L − 0,8: el primer rand() de la semilla 3 deja L en 0,7 u 0,8 y la luz negra";

    const std::uint32_t chips = SeedWhereDraw(25, [](int value) { return (value & 7) == 0; });
    Client::World::ModelEffect jet = DungeonEffect(Client::World::FlameJetEffect, 30);
    services.effects = nullptr;
    Client::Render::Models::CrtRandom chipping(chips);
    auto cut = Client::World::StepGameModelEffect(jet, nullptr, particles, 0.0f, chipping, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52adf2"), std::string::npos) << cut.Error();

    const std::uint32_t quiet = SeedWhereDraw(25, [](int value) { return (value & 7) != 0; });
    Client::World::ModelEffect dark = DungeonEffect(Client::World::FlameJetEffect, 30);
    Client::World::Particles more;
    services.lighting = nullptr;
    Client::Render::Models::CrtRandom quietRandom(quiet);
    cut = Client::World::StepGameModelEffect(dark, nullptr, more, 0.0f, quietRandom, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52ae25"), std::string::npos) << cut.Error();

    Client::World::ModelEffect other = DungeonEffect(Client::World::FlameJetEffect, 30);
    other.subtype = 1;
    cut = Client::World::StepGameModelEffect(other, nullptr, more, 0.0f, quietRandom, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52ae7f"), std::string::npos) << cut.Error();
}
} // namespace
