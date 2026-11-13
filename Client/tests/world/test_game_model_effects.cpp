#include "World/GameModelEffects.h"

#include "Data/Terrain/TerrainAttributes.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "World/KalimaWorld.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/SelectionParticleKernels.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectStepServices.h"

#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

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

// La piedra 0x10b del ataque a las estatuas de Blood Castle. El alta (0x51bafb): nueve rand() con el 0x10b, que pasa al
// subtipo 1, y ocho con el 0x10a; la z 50 más, sin dueño. El paso (0x52531d): la velocidad sin girar se suma y queda
// por 0,9f (por 1,1f más con el subtipo 1), la caída suma y pierde 3; en el aire se inclina escala · 16 y en el suelo
// se apoya, la caída por −0,2f, pierde 5 de vida y se inclina escala · 128; +0x174 = vida · 0,1f. Después, rand() % 10
// y, con 0, el polvo 0x5f3/0 (cuatro rand(); su punto es Q-C1176); la cola común resta la vida.
TEST(GameModelEffectsTest, TheStatueStonesJumpFallTiltAndSometimesRaiseDust)
{
    {
        Client::World::ModelEffects effects;
        Client::Render::Models::CrtRandom random(7);
        const auto slot = effects.CreateDebris(Client::World::DebrisEffect, {1000.0f, 1000.0f, 100.0f}, glm::vec3(0.0f),
                                               glm::vec3(1.0f), random);
        ASSERT_TRUE(slot.has_value());
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, 9).State());
        const Client::World::ModelEffect& stone = effects.Effects()[*slot];
        EXPECT_EQ(stone.subtype, 1);
        EXPECT_EQ(stone.transform.position, glm::vec3(1000.0f, 1000.0f, 150.0f));
        EXPECT_GE(stone.life, 32);
        EXPECT_LE(stone.life, 47);
        EXPECT_GE(stone.turnDirection, 2.0f);
        EXPECT_LE(stone.turnDirection, 10.0f);
        EXPECT_FALSE(stone.hasOwner);
        EXPECT_EQ(stone.disturbanceState, 0xffff);
        Client::Render::Models::CrtRandom other(7);
        const auto first = effects.CreateDebris(Client::World::FirstDebrisEffect, glm::vec3(0.0f), glm::vec3(0.0f),
                                                glm::vec3(1.0f), other);
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(other.State(), Client::Tests::AfterDraws(7, 8).State());
        EXPECT_EQ(effects.Effects()[*first].subtype, 0);
    }
    const auto stone = []
    {
        Client::World::ModelEffect effect = DungeonEffect(Client::World::DebrisEffect, 40);
        effect.subtype = 1;
        effect.velocity = {0.0f, 10.0f, 0.0f};
        effect.turnDirection = 5.0f;
        effect.transform.position = {1000.0f, 1000.0f, 500.0f};
        return effect;
    };
    const auto times = [](double a, float b) { return static_cast<float>(a * static_cast<double>(b)); };
    Client::World::Particles particles;
    Client::World::ModelEffectStepServices services;
    const std::uint32_t quiet = SeedWhereDraw(1, [](int draw) { return draw % 10 != 0; });
    {
        Client::World::ModelEffect flying = stone();
        services.terrainHeight = [](float, float) { return 0.0; };
        Client::Render::Models::CrtRandom random(quiet);
        ASSERT_TRUE(
            Client::World::StepGameModelEffect(flying, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
        EXPECT_EQ(flying.velocity.y,
                  times(times(10.0, std::bit_cast<float>(0x3f666666u)), std::bit_cast<float>(0x3f8ccccdu)));
        EXPECT_EQ(flying.turnDirection, 2.0f);
        EXPECT_EQ(flying.transform.angles.x, -16.0f);
        EXPECT_EQ(flying.alpha, times(40.0, std::bit_cast<float>(0x3dcccccdu)));
        EXPECT_EQ(flying.life, 39);
        EXPECT_EQ(flying.previousPosition, glm::vec3(1000.0f, 1000.0f, 500.0f));
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(quiet, 2).State());
        EXPECT_FALSE(particles.Any());
    }
    {
        Client::World::ModelEffect landing = stone();
        services.terrainHeight = [](float, float) { return 1000.0; };
        Client::Render::Models::CrtRandom random(quiet);
        ASSERT_TRUE(Client::World::StepGameModelEffect(landing, nullptr, particles, 0.0f, random, nullptr, services)
                        .HasValue());
        EXPECT_EQ(landing.turnDirection, times(2.0, std::bit_cast<float>(0xbe4ccccdu)));
        EXPECT_EQ(landing.transform.angles.x, -128.0f);
        EXPECT_EQ(landing.alpha, times(35.0, std::bit_cast<float>(0x3dcccccdu)));
        EXPECT_EQ(landing.life, 34);
    }
    {
        const std::uint32_t dusty = SeedWhereDraw(1, [](int draw) { return draw % 10 == 0; });
        Client::World::ModelEffect raising = stone();
        services.terrainHeight = [](float, float) { return 0.0; };
        Client::Render::Models::CrtRandom random(dusty);
        ASSERT_TRUE(Client::World::StepGameModelEffect(raising, nullptr, particles, 0.0f, random, nullptr, services)
                        .HasValue());
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(dusty, 2 + 4).State());
        EXPECT_TRUE(particles.Any());
    }
    services.terrainHeight = {};
    Client::World::ModelEffect blind = stone();
    Client::Render::Models::CrtRandom random(quiet);
    const auto cut = Client::World::StepGameModelEffect(blind, nullptr, particles, 0.0f, random, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5253d9"), std::string::npos) << cut.Error();
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

// 0x52a737..0x52a7af y 0x52cdb3..0x52cdfb: el 0x5ac suma su velocidad, la baja 0,01f en z, suelta una 5ac/1
// con su luz y su escala, y la cola común la vuelve a sumar; con la vida que queda impar da otro paso entero.
// La nube 0xbb sólo tiene la cola común.
TEST(GameModelEffectsTest, TheIcarusFlareStepsTwiceAndLeavesTwoTrails)
{
    Client::World::ModelEffect flare = DungeonEffect(Client::World::IcarusFlareEffect, 400);
    flare.velocity = {1.0f, 2.0f, 3.0f};
    flare.transform.scale = 3.0f;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(Client::World::StepGameModelEffect(flare, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(flare.life, 398);
    const float sink = std::bit_cast<float>(0x3c23d70au);
    const auto vz1 = static_cast<float>(3.0 - static_cast<double>(sink));
    const auto vz2 = static_cast<float>(static_cast<double>(vz1) - static_cast<double>(sink));
    EXPECT_EQ(flare.velocity, glm::vec3(1.0f, 2.0f, vz2));
    float z = 100.0f;
    for (const float vz : {vz1, vz2})
    {
        z = static_cast<float>(static_cast<double>(vz == vz1 ? 3.0f : vz1) + static_cast<double>(z));
        z = static_cast<float>(static_cast<double>(z) + static_cast<double>(vz));
    }
    EXPECT_EQ(flare.transform.position, glm::vec3(1004.0f, 1008.0f, z));
    EXPECT_EQ(flare.previousPosition, glm::vec3(1000.0f, 1000.0f, 100.0f));
    int trails = 0;
    for (const auto& particle : particles.All())
    {
        if (!particle.alive)
            continue;
        ++trails;
        EXPECT_EQ(particle.type, 0x5ac);
        EXPECT_EQ(particle.subtype, 1);
        EXPECT_EQ(particle.life, 50);
        EXPECT_EQ(particle.scale, 3.0f);
        EXPECT_EQ(particle.light, glm::vec3(0.25f, 0.5f, 0.75f));
    }
    EXPECT_EQ(trails, 2);
    // El rand() común de 0x5208a0, uno por paso entero.
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 2).State());

    Client::World::ModelEffect sinking = DungeonEffect(Client::World::IcarusFlareEffect, 400);
    sinking.velocity = {0.0f, 0.0f, -1.995f};
    ASSERT_TRUE(Client::World::StepGameModelEffect(sinking, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_FALSE(sinking.alive);

    Client::World::ModelEffect cloud = DungeonEffect(Client::World::IcarusCloudEffect, 2);
    ASSERT_TRUE(Client::World::StepGameModelEffect(cloud, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(cloud.life, 1);
    EXPECT_EQ(cloud.transform.position, glm::vec3(1000.0f, 1000.0f, 100.0f));
}

namespace
{
// El seno · f de la x87 (FSIN y FMUL), con el signo de f.
double SineTimes(double argument, float multiplier)
{
    const auto product = Client::Render::Models::Detail::SineProductPC53(argument, std::abs(multiplier));
    EXPECT_TRUE(product.has_value());
    return multiplier < 0.0f ? -product.value_or(0.0) : product.value_or(0.0);
}

Client::World::ModelEffect AidaWisp()
{
    Client::World::ModelEffect wisp = DungeonEffect(Client::World::AidaWispEffect, 1000);
    wisp.speed = 1.0f;
    wisp.turnDirection = 1.0f;
    wisp.flagEc = 1.0f;
    wisp.transform.scale = 0.95f;
    wisp.transform.angles = {10.0f, 0.0f, 30.0f};
    wisp.velocity = {0.0f, -4.5f, 0.0f};
    wisp.start = wisp.transform.position;
    return wisp;
}
} // namespace

// 0x520b77..0x520d0c: sin giros sorteados, el yaw suma sen(ftol(T) % 10.000 · 0,0001f) · +0xe4 y el pitch el
// mismo seno · +0xec · 1/3f; la luz es +0x68 · (0,1f; 0,1f; 0,25f) y la 5ac/9 sale en el lugar con los
// ángulos nuevos, antes de que la cola común lo avance por la velocidad girada. Tres rand(): el común y los dos
// % 40.
TEST(GameModelEffectsTest, TheAidaWispTurnsWithTheClockAndLeavesATrailBeforeMoving)
{
    const std::uint32_t seed = SeedWhereDraw(1, [](int value) { return value % 40 != 1; });
    ASSERT_NE(Client::Tests::AfterDraws(seed, 2).Next() % 40, 1);
    Client::World::ModelEffect wisp = AidaWisp();
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(seed);
    const float clock = 12345.75f;
    ASSERT_TRUE(Client::World::StepGameModelEffect(wisp, nullptr, particles, clock, random).HasValue());
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(seed, 3).State());
    const double argument = 2345.0 * static_cast<double>(std::bit_cast<float>(0x38d1b717u));
    const float yaw = static_cast<float>(SineTimes(argument, 1.0f) + 30.0);
    const float pitch =
        static_cast<float>(SineTimes(argument, 1.0f) * static_cast<double>(std::bit_cast<float>(0x3eaaaaabu)) + 10.0);
    EXPECT_EQ(wisp.transform.angles, glm::vec3(pitch, 0.0f, yaw));
    EXPECT_EQ(wisp.light, glm::vec3(0.1f, 0.1f, 0.25f));
    EXPECT_EQ(wisp.life, 999);
    EXPECT_EQ(wisp.state, 0);
    EXPECT_EQ(wisp.speed, 1.0f);
    const auto& trail = particles.All()[0];
    ASSERT_TRUE(trail.alive);
    EXPECT_EQ(trail.type, 0x5ac);
    EXPECT_EQ(trail.subtype, 9);
    EXPECT_EQ(trail.position, glm::vec3(1000.0f, 1000.0f, 100.0f));
    EXPECT_EQ(trail.angles, wisp.transform.angles);
    EXPECT_EQ(trail.light, glm::vec3(0.1f, 0.1f, 0.25f));
    EXPECT_EQ(trail.scale, 0.95f);
    EXPECT_EQ(trail.life, 37); // ftol(0,95f · 40) = ftol(37,999998).
    EXPECT_FALSE(particles.All()[1].alive);
    const glm::vec3 step = Client::Render::Models::OriginalVectorRotate(
        glm::vec3(0.0f, -4.5f, 0.0f), Client::Render::Models::OriginalAngleMatrix(wisp.transform.angles));
    EXPECT_EQ(wisp.transform.position, glm::vec3(static_cast<float>(static_cast<double>(step.x) + 1000.0),
                                                 static_cast<float>(static_cast<double>(step.y) + 1000.0),
                                                 static_cast<float>(static_cast<double>(step.z) + 100.0)));
}

// Con rand() % 40 en 1, el sentido +0xd8 se da vuelta y +0xe4 pasa a (rand() % 3 + 5) · sentido (el yaw gira
// al revés); el segundo hace lo mismo con +0xec y rand() % 3 + 1. Lejos de la partida, vida 50 y estado 1, una
// sola vez.
TEST(GameModelEffectsTest, TheAidaWispFlipsItsTurnsAndShortensItsLifeFarFromItsStart)
{
    const std::uint32_t seed = SeedWhereDraw(1, [](int value) { return value % 40 == 1; });
    Client::Render::Models::CrtRandom draws(seed);
    static_cast<void>(draws.Next());
    static_cast<void>(draws.Next());
    const int yawTurn = draws.Next() % 3 + 5;
    const bool second = draws.Next() % 40 == 1;
    const int pitchTurn = second ? draws.Next() % 3 + 1 : 0;
    Client::World::ModelEffect wisp = AidaWisp();
    wisp.start = {1000.0f, 1500.0f, 100.0f};
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(seed);
    ASSERT_TRUE(Client::World::StepGameModelEffect(wisp, nullptr, particles, 5000.0f, random).HasValue());
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_EQ(wisp.speed, second ? 1.0f : -1.0f);
    EXPECT_EQ(wisp.turnDirection, static_cast<float>(-yawTurn));
    EXPECT_EQ(wisp.flagEc, second ? static_cast<float>(pitchTurn) : 1.0f);
    const double argument = 5000.0 * static_cast<double>(std::bit_cast<float>(0x38d1b717u));
    EXPECT_EQ(wisp.transform.angles.z, static_cast<float>(SineTimes(argument, static_cast<float>(-yawTurn)) + 30.0));
    // 0x520c3f: a 500 justo no es "menor"; vida 50 menos la de la cola.
    EXPECT_EQ(wisp.life, 49);
    EXPECT_EQ(wisp.state, 1);
    ASSERT_TRUE(Client::World::StepGameModelEffect(wisp, nullptr, particles, 5000.0f, random).HasValue());
    EXPECT_EQ(wisp.life, 48);

    Client::World::ModelEffect near = AidaWisp();
    near.start = {1000.0f, 1499.0f, 100.0f};
    ASSERT_TRUE(Client::World::StepGameModelEffect(near, nullptr, particles, 5000.0f, random).HasValue());
    EXPECT_EQ(near.life, 999);
    EXPECT_EQ(near.state, 0);
}

// 0x52c5ca..0x52c7a6, la onda 0x6eb/1: después del rand() común, la escala crece (rand() % 5) · 0,1f y la posición
// tiembla (rand() & 7) − 4 en x y en y; con la vida hasta 20 la luz es la vida por 0,05f, y pasados los 20, 40 menos
// la vida. La cola baja la vida. Con la semilla 1 (41 de la creación, 18.467 común, 6.334, 26.500 y 19.169): la
// escala 1,5, x + 0 e y − 3.
TEST(GameModelEffectsTest, TheGallopShockWaveGrowsShakesAndFadesWithThreeRandoms)
{
    Client::World::ModelEffects effects;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(effects.CreateGallopShockWave({100.5f, 200.25f, 300.0f}, {0.0f, 0.0f, 45.0f}, glm::vec3(1.0f), random)
                    .has_value());
    Client::World::ModelEffect& wave = effects.Effects()[0];
    Client::World::Particles particles;

    ASSERT_TRUE(Client::World::StepGameModelEffect(wave, nullptr, particles, 0.0f, random).HasValue());

    EXPECT_EQ(wave.transform.scale, 1.5f);
    EXPECT_EQ(wave.transform.position, glm::vec3(100.5f, 197.25f, 300.0f));
    EXPECT_EQ(wave.light, glm::vec3(1.0f));
    EXPECT_EQ(wave.life, 19);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 5).State());

    ASSERT_TRUE(Client::World::StepGameModelEffect(wave, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(wave.light.x), 0x3f733333u);
    EXPECT_EQ(wave.life, 18);

    // Pasados los 20, 40 − 25 = 15: 0,75. Una escala negativa vuelve a 0, también sin orden.
    for (const float scale : {-5.0f, std::numeric_limits<float>::quiet_NaN()})
    {
        Client::World::ModelEffect young = wave;
        young.life = 25;
        young.transform.scale = scale;
        ASSERT_TRUE(Client::World::StepGameModelEffect(young, nullptr, particles, 0.0f, random).HasValue());
        EXPECT_EQ(young.light, glm::vec3(0.75f));
        EXPECT_EQ(young.transform.scale, 0.0f);
        EXPECT_EQ(young.life, 24);
    }
}

// Los otros subtipos del 0x6eb cortan en su rama, después del rand() común.
TEST(GameModelEffectsTest, OtherShockWavesStopAfterTheCommonRandom)
{
    Client::World::ModelEffects effects;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(effects.CreateGallopShockWave({}, {}, glm::vec3(1.0f), random).has_value());
    Client::World::ModelEffect& wave = effects.Effects()[0];
    wave.subtype = 2;
    Client::World::Particles particles;

    const auto stepped = Client::World::StepGameModelEffect(wave, nullptr, particles, 0.0f, random);

    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x52c5ca"), std::string::npos) << stepped.Error();
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 2).State());
}

namespace
{
// 0x5295a4, el rayo 0x14d de los Fenrir, después del rand() común: con el subtipo 0 el alpha sube 0,3f hasta pasar a 1
// con el subtipo 1; con el 1 baja 0,3f hasta apagarse en 0; los tres ángulos giran 0,15f; y la vida común.
TEST(GameModelEffectsTest, TheLightningFlashesOnAndOffAndSpins)
{
    using Client::World::ModelEffect;
    ModelEffect bolt;
    bolt.alive = true;
    bolt.type = Client::World::LightningEffect;
    bolt.life = 100;
    bolt.alpha = std::bit_cast<float>(0x3f333333u);
    bolt.transform.angles = {10.0f, 20.0f, 30.0f};
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    const auto step = [&]
    {
        const auto stepped = Client::World::StepGameModelEffect(bolt, nullptr, particles, 0.0f, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    };
    const auto spun = [](float angle)
    { return static_cast<float>(static_cast<double>(angle) + static_cast<double>(0.15f)); };

    // 0,7f + 0,3f da 1 justo en double: el subtipo 1 y el alpha en 1 en el primer paso.
    ASSERT_EQ(static_cast<double>(std::bit_cast<float>(0x3f333333u)) + static_cast<double>(0.3f), 1.0);
    step();
    EXPECT_EQ(bolt.subtype, 1);
    EXPECT_EQ(bolt.alpha, 1.0f);
    EXPECT_EQ(bolt.transform.angles, glm::vec3(spun(10.0f), spun(20.0f), spun(30.0f)));
    EXPECT_EQ(bolt.life, 99);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
    for (int dim = 0; dim < 3; ++dim)
        step();
    EXPECT_TRUE(bolt.alive);
    EXPECT_EQ(bolt.subtype, 1);
    step(); // la cuarta resta no deja nada: se apaga con el alpha en 0.
    EXPECT_FALSE(bolt.alive);
    EXPECT_EQ(bolt.alpha, 0.0f);

    ModelEffect dim = bolt; // por debajo de 1 sigue en el 0 con la suma guardada.
    dim.alive = true;
    dim.subtype = 0;
    dim.life = 50;
    dim.alpha = 0.5f;
    ASSERT_TRUE(Client::World::StepGameModelEffect(dim, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(dim.subtype, 0);
    EXPECT_EQ(dim.alpha, static_cast<float>(0.5 + static_cast<double>(0.3f)));

    ModelEffect negative = bolt; // un alpha guardado negativo (o sin orden) con el subtipo 0 vuelve a 1.
    negative.alive = true;
    negative.subtype = 0;
    negative.life = 50;
    for (const float alpha : {-2.0f, std::numeric_limits<float>::quiet_NaN()})
    {
        negative.alpha = alpha;
        ASSERT_TRUE(Client::World::StepGameModelEffect(negative, nullptr, particles, 0.0f, random).HasValue());
        EXPECT_EQ(negative.alpha, 1.0f);
        EXPECT_EQ(negative.subtype, 0);
    }
}

// 0x52965a, el calco 0x14e: x gira 0,1f, la luz baja 0,05f en los canales del subtipo y el alpha 0,05f; con más de 200
// ms desde su hora pasa al cuadro siguiente con una hora nueva, y pasado el cuarto se apaga. Sin timeGetTime(), corta
// antes de tocarlo.
TEST(GameModelEffectsTest, TheLightningDecalFadesItsColourAndTurnsItsFramesByTheClock)
{
    using Client::World::ModelEffect;
    std::uint32_t now = 1000;
    Client::World::ModelEffectStepServices services;
    services.systemTime = [&now]() { return now; };
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    const auto fade = [](float value)
    { return static_cast<float>(static_cast<double>(value) - static_cast<double>(0.05f)); };
    struct Case
    {
        std::int32_t subtype;
        glm::vec3 light;
    };
    for (const Case value : {Case{1, {1.0f, fade(1.0f), fade(1.0f)}}, Case{2, {fade(1.0f), fade(1.0f), 1.0f}},
                             Case{3, {fade(1.0f), 1.0f, fade(1.0f)}}, Case{4, glm::vec3(1.0f)}})
    {
        SCOPED_TRACE(value.subtype);
        ModelEffect decal;
        decal.alive = true;
        decal.type = Client::World::LightningDecalEffect;
        decal.subtype = value.subtype;
        decal.life = 200;
        decal.alpha = 1.0f;
        decal.light = glm::vec3(1.0f);
        decal.textureTime = 800;
        decal.transform.angles = {5.0f, 0.0f, 45.0f};
        const auto stepped =
            Client::World::StepGameModelEffect(decal, nullptr, particles, 0.0f, random, nullptr, services);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(decal.light, value.light);
        EXPECT_EQ(decal.alpha, fade(1.0f));
        EXPECT_EQ(decal.transform.angles, glm::vec3(static_cast<float>(5.0 + static_cast<double>(0.1f)), 0.0f, 45.0f));
        EXPECT_EQ(decal.textureFrame, 0); // 1000 − 800 = 200, que no pasa de 200.
        EXPECT_EQ(decal.textureTime, 800u);
        EXPECT_EQ(decal.life, 199);
    }

    ModelEffect decal;
    decal.alive = true;
    decal.type = Client::World::LightningDecalEffect;
    decal.subtype = 1;
    decal.life = 200;
    decal.alpha = 1.0f;
    decal.textureTime = 799;
    decal.textureFrame = 3;
    ASSERT_TRUE(
        Client::World::StepGameModelEffect(decal, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
    EXPECT_EQ(decal.textureFrame, 4);
    EXPECT_EQ(decal.textureTime, 1000u);
    EXPECT_TRUE(decal.alive);
    now = 1201;
    ASSERT_TRUE(
        Client::World::StepGameModelEffect(decal, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
    EXPECT_EQ(decal.textureFrame, 5);
    EXPECT_EQ(decal.textureTime, 1201u);
    EXPECT_FALSE(decal.alive);

    ModelEffect faded = decal; // la resta del alpha en 0 o menos lo apaga.
    faded.alive = true;
    faded.textureFrame = 0;
    faded.textureTime = now;
    faded.alpha = 0.05f;
    ASSERT_TRUE(
        Client::World::StepGameModelEffect(faded, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
    EXPECT_FALSE(faded.alive);

    ModelEffect untouched = faded;
    untouched.alive = true;
    untouched.alpha = 1.0f;
    const ModelEffect before = untouched;
    const auto cut = Client::World::StepGameModelEffect(untouched, nullptr, particles, 0.0f, random);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x529701"), std::string::npos) << cut.Error();
    EXPECT_EQ(untouched.alpha, before.alpha);
    EXPECT_EQ(untouched.transform.angles, before.transform.angles);
}
} // namespace

// El terremoto 0xfa del caballo y sus grietas 0xfb..0x102 (0x521c5c..0x522bd2).
namespace
{
using Client::World::ModelEffect;
using Client::World::ModelEffectOwner;
using Client::World::ModelEffects;

constexpr std::uint16_t HorseType = 0xbf; // Scenes::DarkHorseModel
constexpr std::uint16_t SeparateType = 0x1b1;
const glm::vec3 QuakeLight{0.25f, 0.5f, 0.75f};

float Narrow(double value)
{
    return static_cast<float>(value);
}

double Wide(float value)
{
    return static_cast<double>(value);
}

glm::vec3 QuakeTurned(const glm::vec3& offset, const glm::vec3& angles)
{
    return Client::Render::Models::OriginalVectorRotate(offset, Client::Render::Models::OriginalAngleMatrix(angles));
}

// La altura del terreno de los tests: depende de x y de y para que se vea qué punto se pidió.
double QuakeGround(float x, float y)
{
    return Wide(x) * 0.0625 + Wide(y) * 0.03125;
}

// ftol(x · 0,01f) e y · 256 + x, como 0x5f8290.
std::size_t QuakeCell(const glm::vec3& point)
{
    const double step = Wide(std::bit_cast<float>(0x3c23d70au));
    const auto x = static_cast<std::int32_t>(Wide(point.x) * step);
    const auto y = static_cast<std::int32_t>(Wide(point.y) * step);
    return static_cast<std::size_t>(y) * 256u + static_cast<std::size_t>(x);
}

// La tabla de efectos, los atributos, el mapa, la luz dinámica, el temblor y los sonidos que pide el terremoto.
struct QuakeScene
{
    std::unique_ptr<ModelEffects> effects = std::make_unique<ModelEffects>();
    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> attributes =
        std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    std::unique_ptr<Client::World::TerrainLighting> lighting = std::make_unique<Client::World::TerrainLighting>();
    std::int32_t map = 0;
    float shake = 0.0f;
    std::vector<std::int32_t> sounds;

    [[nodiscard]] Client::World::ModelEffectStepServices Services()
    {
        Client::World::ModelEffectStepServices services;
        services.terrainHeight = QuakeGround;
        services.effects = effects.get();
        services.lighting = lighting.get();
        services.terrainAttributes = attributes.get();
        services.map = &map;
        services.cameraShake = &shake;
        services.sound = [this](std::int32_t sound) { sounds.push_back(sound); };
        return services;
    }
};

// Lo que dejó 0x51a66d con el estado y el subtipo 17: la partida en la posición, +0x28 con 80 y 180 más, +0xe4 en 50.
ModelEffect Earthquake(std::uint8_t state, std::int32_t life, const ModelEffectOwner& owner)
{
    ModelEffect quake;
    quake.alive = true;
    quake.type = Client::World::EarthquakeEffect;
    quake.subtype = 17;
    quake.state = state;
    quake.life = life;
    quake.transform.scale = 0.9f;
    quake.transform.position = {5000.5f, 6000.25f, 300.0f};
    quake.transform.angles = {0.0f, 0.0f, 375.0f};
    quake.start = quake.transform.position;
    quake.copiedAngles = {80.0f, 0.0f, 225.0f};
    quake.light = QuakeLight;
    quake.turnDirection = 50.0f;
    quake.owner = owner;
    quake.hasOwner = owner.type != nullptr || owner.effect != nullptr || owner.angles != nullptr;
    quake.disturbanceState = 0xffff;
    return quake;
}

void ExpectSameEffects(const ModelEffects& actual, const ModelEffects& expected)
{
    for (std::size_t slot = 0; slot < actual.Effects().size(); ++slot)
    {
        SCOPED_TRACE(slot);
        const ModelEffect& got = actual.Effects()[slot];
        const ModelEffect& want = expected.Effects()[slot];
        ASSERT_EQ(got.alive, want.alive);
        if (!want.alive)
            continue;
        EXPECT_EQ(got.type, want.type);
        EXPECT_EQ(got.subtype, want.subtype);
        EXPECT_EQ(got.transform.position, want.transform.position);
        EXPECT_EQ(got.transform.angles, want.transform.angles);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(got.transform.scale),
                  std::bit_cast<std::uint32_t>(want.transform.scale));
        EXPECT_EQ(got.disturbanceState, want.disturbanceState);
        EXPECT_EQ(got.life, want.life);
        EXPECT_EQ(got.light, want.light);
        EXPECT_EQ(got.velocity, want.velocity);
        EXPECT_EQ(got.objectState.blendMesh, want.objectState.blendMesh);
        EXPECT_EQ(got.owner.type, want.owner.type);
        EXPECT_EQ(got.owner.effect, want.owner.effect);
        EXPECT_EQ(got.owner.angles, want.owner.angles);
    }
}

// Un rayo de la vida 11, antes de mirar la celda: el punto con la z de la partida más 3, el tamaño y la inclinación.
struct QuakeSpoke
{
    glm::vec3 point;
    std::int16_t size;
    glm::vec3 tilt;
};

// Los cinco rayos de 0x522004..0x5221ea con sus tres rand(): rand() % 150 + 100 de largo a (k · 72 + subtipo)°,
// rand() % 50 + 40 de tamaño y (rand() % 30 − 15) + 45 de inclinación.
std::array<QuakeSpoke, 5> QuakeSpokes(Client::Render::Models::CrtRandom& draws, const glm::vec3& start,
                                      std::int32_t subtype)
{
    std::array<QuakeSpoke, 5> spokes{};
    for (std::size_t spoke = 0; spoke < spokes.size(); ++spoke)
    {
        const auto reach = static_cast<float>(draws.Next() % 150 + 100);
        const glm::vec3 local{0.0f, 0.0f, Narrow(static_cast<double>(spoke) * 72.0 + static_cast<double>(subtype))};
        const glm::vec3 ray = QuakeTurned({0.0f, reach, 0.0f}, local);
        spokes[spoke].point = {Narrow(Wide(ray.x) + Wide(start.x)), Narrow(Wide(ray.y) + Wide(start.y)),
                               Narrow((Wide(ray.z) + Wide(start.z)) + 3.0)};
        spokes[spoke].size = static_cast<std::int16_t>(draws.Next() % 50 + 40);
        spokes[spoke].tilt = {0.0f, 0.0f, Narrow(static_cast<double>(draws.Next() % 30 - 15) + 45.0)};
    }
    return spokes;
}
} // namespace

// 0x521c8f..0x5221f0, la vida 11 con el estado 2 del caballo: la partida 25 atrás y 80 al costado girados por los
// ángulos del dueño, en el suelo más 25; la explosión 5ed/0 de 0,5f con su sonido 0x40; la partida 27 más abajo; con
// un dueño que no es 0x1b1, las grietas 0xfd, 0xfb y 0xfc de tamaño 0x96 con el terremoto de dueño; y cinco rayos con
// las grietas 0xfe y 0xff en el suelo más 3, con el dueño del terremoto. Una celda con 0x4, 0x8 o 0x10 deja su rayo
// sin grietas. La cola no mueve el terremoto: sólo le quita una vida.
TEST(GameModelEffectsTest, TheEarthquakeAtElevenBurstsAndCracksTheGroundAroundIt)
{
    for (const float yaw : {0.0f, 33.0f})
    {
        SCOPED_TRACE(yaw);
        const std::uint16_t horseType = HorseType;
        const glm::vec3 horseAngles{0.0f, 0.0f, yaw};
        const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = &horseAngles};
        ModelEffect quake = Earthquake(2, 11, horse);
        QuakeScene scene;
        Client::World::Particles particles;

        // Lo esperado, con otra CRT en el orden del ASM.
        Client::Render::Models::CrtRandom draws(5);
        static_cast<void>(draws.Next()); // 0x5208c5
        const glm::vec3 behind = QuakeTurned({-25.0f, -80.0f, 0.0f}, horseAngles);
        glm::vec3 start{Narrow(Wide(behind.x) + 5000.5), Narrow(Wide(behind.y) + 6000.25), 0.0f};
        start.z = Narrow(QuakeGround(start.x, start.y) + 25.0);
        const glm::vec3 burst = start;
        Client::World::Particles expectedParticles;
        ASSERT_TRUE(expectedParticles
                        .CreateChecked(Client::World::ExplosionParticle, start, glm::vec3(0.0f), glm::vec3(1.0f), 0,
                                       0.5f, 0.0f, draws)
                        .HasValue());
        start.z = Narrow(Wide(start.z) - 27.0);
        if (yaw == 0.0f)
        {
            EXPECT_EQ(burst, (glm::vec3{4975.5f, 5920.25f, 520.9765625f}));
            EXPECT_EQ(start.z, 493.9765625f);
        }
        auto expected = std::make_unique<ModelEffects>();
        const ModelEffectOwner self{.type = nullptr, .effect = &quake, .angles = &quake.transform.angles};
        for (const std::int16_t type : {std::int16_t{0xfd}, std::int16_t{0xfb}, std::int16_t{0xfc}})
            ASSERT_TRUE(
                expected->CreateEarthquakeCrack(type, start, glm::vec3(0.0f), QuakeLight, 0, self, 0x96).has_value());
        const auto spokes = QuakeSpokes(draws, start, quake.subtype);
        // El rayo 1 cae en una celda con un solo bit (0x8): sin grietas.
        const std::size_t blocked = QuakeCell(spokes[1].point);
        scene.attributes->cells[blocked] = 0x8;
        for (std::size_t spoke = 0; spoke < spokes.size(); ++spoke)
        {
            if (QuakeCell(spokes[spoke].point) == blocked)
                continue;
            glm::vec3 point = spokes[spoke].point;
            point.z = Narrow(QuakeGround(point.x, point.y) + 3.0);
            for (const std::int16_t type : {std::int16_t{0xfe}, std::int16_t{0xff}})
                ASSERT_TRUE(expected
                                ->CreateEarthquakeCrack(type, point, spokes[spoke].tilt, QuakeLight, 0, horse,
                                                        spokes[spoke].size)
                                .has_value());
        }

        Client::Render::Models::CrtRandom random(5);
        const auto stepped =
            Client::World::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, nullptr, scene.Services());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(quake.start, start);
        const auto& explosion = particles.All()[0];
        ASSERT_TRUE(explosion.alive);
        EXPECT_EQ(explosion.type, Client::World::ExplosionParticle);
        EXPECT_EQ(explosion.position, burst);
        EXPECT_EQ(explosion.scale, 0.5f);
        EXPECT_EQ(explosion.life, 20);
        EXPECT_FALSE(particles.All()[1].alive);
        EXPECT_EQ(scene.sounds, std::vector<std::int32_t>{0x40});
        ExpectSameEffects(*scene.effects, *expected);
        EXPECT_EQ(scene.effects->Effects()[0].type, 0xfd);
        EXPECT_EQ(scene.effects->Effects()[3].type, 0xfe);
        EXPECT_EQ(scene.effects->Effects()[4].type, 0xff);
        EXPECT_EQ(random.State(), draws.State());
        // 0x52cbb0: sin animación ni 0x51fc80.
        EXPECT_TRUE(quake.alive);
        EXPECT_EQ(quake.life, 10);
        EXPECT_EQ(quake.transform.position, (glm::vec3{5000.5f, 6000.25f, 300.0f}));
        EXPECT_EQ(quake.previousPosition, (glm::vec3{5000.5f, 6000.25f, 300.0f}));
        EXPECT_EQ(quake.transform.angles, (glm::vec3{0.0f, 0.0f, 375.0f}));
    }
}

// Con un dueño 0x1b1 (o sin dueño) no salen las tres primeras grietas, sólo las de los rayos. El estado 3 deja la
// partida donde estaba y no lee los ángulos del dueño.
TEST(GameModelEffectsTest, TheEarthquakeSkipsItsFirstCracksForTheSeparateOwnerAndKeepsAnUnanchoredStart)
{
    for (const bool separate : {true, false})
    {
        SCOPED_TRACE(separate);
        const std::uint16_t ownerType = SeparateType;
        const ModelEffectOwner owner{.type = separate ? &ownerType : nullptr, .effect = nullptr, .angles = nullptr};
        ModelEffect quake = Earthquake(3, 11, owner);
        QuakeScene scene;
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(9);
        ASSERT_TRUE(
            Client::World::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        Client::Render::Models::CrtRandom draws(9);
        static_cast<void>(draws.Next());
        glm::vec3 start{5000.5f, 6000.25f, Narrow(QuakeGround(5000.5f, 6000.25f) + 25.0)};
        Client::World::Particles expectedParticles;
        ASSERT_TRUE(expectedParticles
                        .CreateChecked(Client::World::ExplosionParticle, start, glm::vec3(0.0f), glm::vec3(1.0f), 0,
                                       0.5f, 0.0f, draws)
                        .HasValue());
        start.z = Narrow(Wide(start.z) - 27.0);
        auto expected = std::make_unique<ModelEffects>();
        for (const QuakeSpoke& spoke : QuakeSpokes(draws, start, quake.subtype))
        {
            const glm::vec3 point{spoke.point.x, spoke.point.y,
                                  Narrow(QuakeGround(spoke.point.x, spoke.point.y) + 3.0)};
            for (const std::int16_t type : {std::int16_t{0xfe}, std::int16_t{0xff}})
                ASSERT_TRUE(expected->CreateEarthquakeCrack(type, point, spoke.tilt, QuakeLight, 0, owner, spoke.size)
                                .has_value());
        }
        EXPECT_EQ(quake.start, start);
        EXPECT_EQ(particles.All()[0].position,
                  (glm::vec3{5000.5f, 6000.25f, Narrow(QuakeGround(5000.5f, 6000.25f) + 25.0)}));
        ExpectSameEffects(*scene.effects, *expected);
        EXPECT_EQ(scene.effects->Effects()[0].type, 0xfe);
        EXPECT_EQ(random.State(), draws.State());
        EXPECT_EQ(quake.life, 10);
    }
}

// En Kalima (24..29 y 36), con el estado 0 o 2 el terremoto agita el agua (0x437860 en la celda de la partida, radio 2
// y −1.000) y sube 25; con otro estado no la agita y sube 100. Las grietas de los rayos quedan 100 sobre el suelo.
TEST(GameModelEffectsTest, TheEarthquakeInKalimaDisturbsTheWaterOrRisesHigher)
{
    for (const std::uint8_t state : {std::uint8_t{2}, std::uint8_t{3}})
    {
        SCOPED_TRACE(static_cast<int>(state));
        const std::uint16_t horseType = HorseType;
        const glm::vec3 horseAngles(0.0f);
        const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = &horseAngles};
        ModelEffect quake = Earthquake(state, 11, horse);
        QuakeScene scene;
        scene.map = 24;
        auto water = std::make_unique<Client::World::KalimaWorld>(24);
        auto stillWater = water->CloneForFrame();
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(3);
        const auto stepped =
            Client::World::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, water.get(), scene.Services());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();

        Client::Render::Models::CrtRandom draws(3);
        static_cast<void>(draws.Next());
        glm::vec3 start = state == 2 ? glm::vec3{4975.5f, 5920.25f, 0.0f} : glm::vec3{5000.5f, 6000.25f, 0.0f};
        auto expectedWater = std::make_unique<Client::World::KalimaWorld>(24);
        if (state == 2)
        {
            ASSERT_TRUE(expectedWater->Disturb(49, 59, 2, -1000, draws).HasValue());
        }
        start.z = Narrow(QuakeGround(start.x, start.y) + (state == 2 ? 25.0 : 100.0));
        EXPECT_EQ(particles.All()[0].position, start);
        EXPECT_EQ(water->Waves().planes, expectedWater->Waves().planes);
        if (state == 2)
        {
            EXPECT_NE(water->Waves().planes, stillWater->Waves().planes);
        }
        Client::World::Particles expectedParticles;
        ASSERT_TRUE(expectedParticles
                        .CreateChecked(Client::World::ExplosionParticle, start, glm::vec3(0.0f), glm::vec3(1.0f), 0,
                                       0.5f, 0.0f, draws)
                        .HasValue());
        start.z = Narrow(Wide(start.z) - 27.0);
        const auto spokes = QuakeSpokes(draws, start, quake.subtype);
        const ModelEffect& firstSpoke = scene.effects->Effects()[3];
        EXPECT_EQ(firstSpoke.type, 0xfe);
        EXPECT_EQ(firstSpoke.transform.position,
                  (glm::vec3{spokes[0].point.x, spokes[0].point.y,
                             Narrow(QuakeGround(spokes[0].point.x, spokes[0].point.y) + 100.0)}));
        EXPECT_EQ(random.State(), draws.State());

        // Sin el agua de Kalima, el estado 2 corta antes de la altura.
        if (state == 2)
        {
            ModelEffect dry = Earthquake(state, 11, horse);
            QuakeScene other;
            other.map = 24;
            const auto cut =
                Client::World::StepGameModelEffect(dry, nullptr, particles, 0.0f, random, nullptr, other.Services());
            ASSERT_FALSE(cut.HasValue());
            EXPECT_NE(cut.Error().find("0x521db2"), std::string::npos) << cut.Error();
        }
    }
}

// El estado 0 (la habilidad del Dark Lord) corta después de la explosión y su sonido en la vida 11 (0x521e13) y en la
// vida 13 (0x5224ec); lo que el terremoto pide y no tiene corta en su dirección.
TEST(GameModelEffectsTest, TheEarthquakeCutsTheDarkLordStateAndWhatItLacks)
{
    const std::uint16_t horseType = HorseType;
    const glm::vec3 horseAngles(0.0f);
    const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = &horseAngles};
    {
        ModelEffect quake = Earthquake(0, 11, horse);
        QuakeScene scene;
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(1);
        const auto cut =
            Client::World::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, nullptr, scene.Services());
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x521e13"), std::string::npos) << cut.Error();
        EXPECT_TRUE(particles.All()[0].alive);
        EXPECT_EQ(scene.sounds, std::vector<std::int32_t>{0x40});
        EXPECT_FALSE(scene.effects->Effects()[0].alive);
    }
    {
        ModelEffect quake = Earthquake(0, 13, horse);
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(1);
        const auto cut = Client::World::StepGameModelEffect(quake, nullptr, particles, 0.0f, random);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x5224ec"), std::string::npos) << cut.Error();
        ModelEffect anchored = Earthquake(2, 13, horse);
        EXPECT_TRUE(Client::World::StepGameModelEffect(anchored, nullptr, particles, 0.0f, random).HasValue());
    }
    const auto cutWith = [](std::uint8_t state, std::int32_t life, const ModelEffectOwner& owner,
                            const std::function<void(QuakeScene&, Client::World::ModelEffectStepServices&)>& take)
    {
        ModelEffect quake = Earthquake(state, life, owner);
        QuakeScene scene;
        auto services = scene.Services();
        take(scene, services);
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(1);
        const auto cut = Client::World::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, nullptr, services);
        return cut.HasValue() ? std::string("sin corte") : cut.Error();
    };
    const ModelEffectOwner blind{.type = &horseType, .effect = nullptr, .angles = nullptr};
    EXPECT_NE(cutWith(2, 11, blind, [](QuakeScene&, auto&) {}).find("0x521cf8"), std::string::npos);
    EXPECT_NE(cutWith(2, 11, horse, [](QuakeScene&, auto& services) { services.map = nullptr; }).find("0x521d55"),
              std::string::npos);
    EXPECT_NE(cutWith(2, 11, horse, [](QuakeScene&, auto& services) { services.terrainHeight = {}; }).find("0x521dce"),
              std::string::npos);
    EXPECT_NE(cutWith(2, 11, horse, [](QuakeScene&, auto& services) { services.sound = {}; }).find("0x542a82"),
              std::string::npos);
    EXPECT_NE(cutWith(2, 11, horse, [](QuakeScene&, auto& services) { services.effects = nullptr; }).find("0x521fb9"),
              std::string::npos);
    EXPECT_NE(cutWith(2, 11, horse, [](QuakeScene&, auto& services) { services.terrainAttributes = nullptr; })
                  .find("0x52211d"),
              std::string::npos);
    EXPECT_NE(cutWith(2, 10, horse, [](QuakeScene&, auto& services) { services.sound = {}; }).find("0x52248b"),
              std::string::npos);
    EXPECT_NE(cutWith(2, 10, horse, [](QuakeScene&, auto& services) { services.terrainAttributes = nullptr; })
                  .find("0x522378"),
              std::string::npos);
    // Q-C425: 0x5f8290 no enmascara; una celda fuera de las 256 × 256 corta.
    const auto outside = cutWith(3, 11, horse, [](QuakeScene&, auto&) {});
    EXPECT_EQ(outside, "sin corte");
    ModelEffect far = Earthquake(3, 11, horse);
    far.start = {5000.5f, -500.0f, 0.0f};
    QuakeScene scene;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    const auto cut =
        Client::World::StepGameModelEffect(far, nullptr, particles, 0.0f, random, nullptr, scene.Services());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52211d: la celda (49, -3) de 0x5f8290 cae fuera"), std::string::npos) << cut.Error();
}

// 0x5221fe..0x522493, la vida 10: cuatro anillos de cinco grietas 0x101 y 0x102 de tamaño 100 que salen de la
// partida. Cada anillo avanza (rand() % 15) + 85; los tres primeros alternan afuera (rand() % 30 + 50) y adentro
// (−50 − rand() % 30) y el cuarto lo decide un rand() nuevo (Q-C429); el giro suma (rand() % 10 + 62) · i. Sólo una
// celda con los tres bits deja la grieta sin crear (un solo bit no, Q-C424); la grieta mira 270° más. Después, la vida
// en 0 y el sonido 0x97; la cola la deja en −1 y muere.
TEST(GameModelEffectsTest, TheEarthquakeAtTenOpensFourRingsOfCracks)
{
    const std::uint16_t horseType = HorseType;
    const glm::vec3 horseAngles(0.0f);
    const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = &horseAngles};
    ModelEffect quake = Earthquake(2, 10, horse);
    QuakeScene scene;

    struct RingCrack
    {
        glm::vec3 point;
        glm::vec3 local;
    };
    std::array<std::array<RingCrack, 5>, 4> plan{};
    Client::Render::Models::CrtRandom draws(11);
    static_cast<void>(draws.Next());
    std::array<glm::vec3, 5> points{};
    points.fill(quake.start);
    std::array<float, 5> sums{};
    int parity = 0;
    for (std::size_t ring = 0; ring < 4; ++ring)
    {
        const float reach = Narrow(static_cast<double>(draws.Next() % 15) + 85.0);
        if (ring == 3)
            parity = draws.Next();
        const bool outward = parity % 2 == 0;
        for (std::size_t crack = 0; crack < 5; ++crack)
        {
            const int push = outward ? draws.Next() % 30 + 50 : -50 - draws.Next() % 30;
            sums[crack] = Narrow(static_cast<double>(push) + Wide(sums[crack]));
            const int turn = (draws.Next() % 10 + 62) * static_cast<int>(crack);
            const glm::vec3 local{0.0f, 0.0f, Narrow(static_cast<double>(turn) + Wide(sums[crack]))};
            const glm::vec3 step = QuakeTurned({0.0f, reach, 0.0f}, local);
            for (int axis = 0; axis < 3; ++axis)
                points[crack][axis] = Narrow(Wide(step[axis]) + Wide(points[crack][axis]));
            plan[ring][crack] = {points[crack], local};
            // La z del suelo la escribe sólo una grieta creada; las celdas se eligen abajo, antes de correr.
            points[crack].z = Narrow(QuakeGround(points[crack].x, points[crack].y) + 3.0);
        }
        ++parity;
    }
    // La grieta 2 del primer anillo cae en una celda con los tres bits, y la 3 en una con uno solo.
    const std::size_t walled = QuakeCell(plan[0][2].point);
    const std::size_t marked = QuakeCell(plan[0][3].point);
    ASSERT_NE(walled, marked);
    scene.attributes->cells[walled] = 0x1c;
    scene.attributes->cells[marked] = 0x08;

    auto expected = std::make_unique<ModelEffects>();
    Client::Render::Models::CrtRandom replay(11);
    static_cast<void>(replay.Next());
    points.fill(quake.start);
    sums.fill(0.0f);
    parity = 0;
    std::size_t walledCracks = 0;
    for (std::size_t ring = 0; ring < 4; ++ring)
    {
        const float reach = Narrow(static_cast<double>(replay.Next() % 15) + 85.0);
        if (ring == 3)
            parity = replay.Next();
        const bool outward = parity % 2 == 0;
        for (std::size_t crack = 0; crack < 5; ++crack)
        {
            const int push = outward ? replay.Next() % 30 + 50 : -50 - replay.Next() % 30;
            sums[crack] = Narrow(static_cast<double>(push) + Wide(sums[crack]));
            const int turn = (replay.Next() % 10 + 62) * static_cast<int>(crack);
            glm::vec3 local{0.0f, 0.0f, Narrow(static_cast<double>(turn) + Wide(sums[crack]))};
            const glm::vec3 step = QuakeTurned({0.0f, reach, 0.0f}, local);
            for (int axis = 0; axis < 3; ++axis)
                points[crack][axis] = Narrow(Wide(step[axis]) + Wide(points[crack][axis]));
            if (scene.attributes->cells[QuakeCell(points[crack])] == 0x1c)
            {
                ++walledCracks;
                continue;
            }
            points[crack].z = Narrow(QuakeGround(points[crack].x, points[crack].y) + 3.0);
            local.z = Narrow(Wide(local.z) + 270.0);
            for (const std::int16_t type : {std::int16_t{0x101}, std::int16_t{0x102}})
                ASSERT_TRUE(
                    expected->CreateEarthquakeCrack(type, points[crack], local, QuakeLight, 0, horse, 100).has_value());
        }
        ++parity;
    }
    EXPECT_GE(walledCracks, 1u);

    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(11);
    const auto stepped =
        Client::World::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, nullptr, scene.Services());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ExpectSameEffects(*scene.effects, *expected);
    EXPECT_EQ(scene.effects->Effects()[0].type, 0x101);
    EXPECT_EQ(scene.effects->Effects()[1].type, 0x102);
    EXPECT_EQ(scene.effects->Effects()[0].disturbanceState, 100u);
    EXPECT_EQ(scene.effects->Effects()[0].life, 40);
    EXPECT_EQ(random.State(), replay.State());
    EXPECT_EQ(scene.sounds, std::vector<std::int32_t>{0x97});
    EXPECT_FALSE(particles.All()[0].alive);
    EXPECT_FALSE(quake.alive);
    EXPECT_EQ(quake.life, -1);
}

// 0x522498..0x522765, las demás vidas: el pitch suma 80; la y de la velocidad es sen((20 − fase) · paso · π / 180) ·
// 260 con la fase en la vida y el paso 15, o 12,5 y 18 entre 10 y 15; +0xe4 baja 8 con la fase en 12,5 y si no sube 8
// (en la 15, antes cambia de signo); la posición es la partida más la velocidad girada por +0x28, con la z más +0xe4 y
// 200. Un rand(), el del prólogo.
TEST(GameModelEffectsTest, TheEarthquakeSwingsAroundItsStartBetweenItsBursts)
{
    struct SwingCase
    {
        std::int32_t life;
        double phase;
        double pace;
        float bob;
        double sine;
    };
    constexpr double Pi = std::bit_cast<double>(0x400921fafc8b007aull);
    constexpr double InverseDegrees = std::bit_cast<double>(0x3f76c16c16c16c17ull);
    for (const SwingCase& swing :
         {SwingCase{18, 18.0, 15.0, 58.0f, 130.0}, SwingCase{14, 12.5, 18.0, 42.0f, 183.8478},
          SwingCase{15, 12.5, 18.0, -58.0f, 183.8478}, SwingCase{19, 19.0, 15.0, 58.0f, 67.2929}})
    {
        SCOPED_TRACE(swing.life);
        ModelEffect quake = Earthquake(2, swing.life, {});
        quake.transform.angles = {10.0f, 0.0f, 375.0f};
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(1);
        ASSERT_TRUE(Client::World::StepGameModelEffect(quake, nullptr, particles, 0.0f, random).HasValue());
        EXPECT_EQ(quake.transform.angles, (glm::vec3{90.0f, 0.0f, 375.0f}));
        const double argument = (((20.0 - swing.phase) * swing.pace) * Pi) * InverseDegrees;
        const float sine = Narrow(SineTimes(argument, 260.0f));
        EXPECT_EQ(quake.velocity, (glm::vec3{0.0f, sine, 0.0f}));
        EXPECT_NEAR(quake.velocity.y, swing.sine, 1e-3);
        EXPECT_EQ(quake.turnDirection, swing.bob);
        const glm::vec3 moved = QuakeTurned(quake.velocity, quake.copiedAngles);
        EXPECT_EQ(quake.transform.position, (glm::vec3{Narrow(Wide(moved.x) + 5000.5), Narrow(Wide(moved.y) + 6000.25),
                                                       Narrow(((Wide(moved.z) + 300.0) + Wide(swing.bob)) + 200.0)}));
        EXPECT_EQ(quake.start, (glm::vec3{5000.5f, 6000.25f, 300.0f}));
        EXPECT_EQ(quake.previousPosition, (glm::vec3{5000.5f, 6000.25f, 300.0f}));
        EXPECT_EQ(quake.life, swing.life - 1);
        EXPECT_TRUE(quake.alive);
        EXPECT_TRUE(particles.All()[0].alive == false);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
    }
}

namespace
{
// Una grieta como la deja 0x518cf0, con la escala 1,5 y el dueño dado.
ModelEffect Crack(std::int16_t type, std::int32_t life, const ModelEffectOwner& owner)
{
    ModelEffect crack;
    crack.alive = true;
    crack.type = type;
    crack.life = life;
    crack.transform.scale = 1.5f;
    crack.transform.position = {5000.5f, 6000.25f, 300.0f};
    crack.transform.angles = {0.0f, 0.0f, 45.0f};
    crack.light = QuakeLight;
    crack.owner = owner;
    crack.hasOwner = owner.type != nullptr || owner.effect != nullptr || owner.angles != nullptr;
    crack.animation.action = 0xff;
    return crack;
}

float CrackGlow(double factor)
{
    return Narrow(factor);
}

const double CrackTenth = Wide(std::bit_cast<float>(0x3dcccccdu));
const double CrackThird = Wide(std::bit_cast<float>(0x3eaaaaabu));

// La luz dinámica de radio 1 que deja una grieta con la luz local del primer rand().
void ExpectCrackLight(const QuakeScene& scene, const ModelEffect& crack, const glm::vec3& color)
{
    auto expected = std::make_unique<Client::World::TerrainLighting>();
    ASSERT_TRUE(Client::World::AddDynamicLight(expected.get(), crack.transform.position, color, 1, "test").HasValue());
    EXPECT_EQ(scene.lighting->cells, expected->cells);
}

float CrackLuminosity(std::uint32_t seed)
{
    Client::Render::Models::CrtRandom draws(seed);
    return Narrow(static_cast<double>((draws.Next() & 3) + 7) * CrackTenth);
}
} // namespace

// 0x52276a: la 0xfb brilla vida · 0,1f · 1/3f, se hunde 0,5f con menos de 10 y, con más de 15 y múltiplo de 3, tiembla
// la cámara (rand() % 8 − 4) · 0,1f. Sin luz dinámica; la cola la anima y le quita una vida.
TEST(GameModelEffectsTest, TheFirstCrackGlowsSinksAndShakesTheCamera)
{
    const std::uint16_t horseType = HorseType;
    const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = nullptr};
    for (const std::int32_t life : {18, 16, 9})
    {
        SCOPED_TRACE(life);
        ModelEffect crack = Crack(0xfb, life, horse);
        QuakeScene scene;
        scene.shake = 7.0f;
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(4);
        ASSERT_TRUE(
            Client::World::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(static_cast<double>(life) * CrackTenth * CrackThird));
        EXPECT_EQ(crack.transform.position.z, life < 10 ? 299.5f : 300.0f);
        Client::Render::Models::CrtRandom draws(4);
        static_cast<void>(draws.Next());
        if (life == 18)
        {
            EXPECT_EQ(scene.shake, Narrow(static_cast<double>(draws.Next() % 8 - 4) * CrackTenth));
        }
        else
        {
            EXPECT_EQ(scene.shake, 7.0f);
        }
        EXPECT_EQ(random.State(), draws.State());
        EXPECT_EQ(scene.lighting->cells, std::make_unique<Client::World::TerrainLighting>()->cells);
        EXPECT_EQ(crack.life, life - 1);
        EXPECT_EQ(crack.previousPosition, (glm::vec3{5000.5f, 6000.25f, 300.0f}));
    }
    ModelEffect crack = Crack(0xfb, 18, horse);
    QuakeScene scene;
    auto services = scene.Services();
    services.cameraShake = nullptr;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(4);
    const auto cut = Client::World::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5227ca"), std::string::npos) << cut.Error();
}

// 0x5227d5 y 0x522b04: la 0xfd y la 0x100 brillan vida · 0,1f · 0,1f y se hunden con menos de 13; la 0xfe y la 0x101,
// vida · 0,1f · 1/3f y con menos de 10. Sin rand() propios ni luz dinámica.
TEST(GameModelEffectsTest, TheQuietCracksOnlyGlowAndSink)
{
    struct QuietCase
    {
        std::int16_t type;
        std::int32_t life;
        double third;
        bool sinks;
    };
    for (const QuietCase& quiet : {QuietCase{0xfd, 12, CrackTenth, true}, QuietCase{0x100, 13, CrackTenth, false},
                                   QuietCase{0xfe, 9, CrackThird, true}, QuietCase{0x101, 10, CrackThird, false}})
    {
        SCOPED_TRACE(quiet.type);
        ModelEffect crack = Crack(quiet.type, quiet.life, {});
        QuakeScene scene;
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(4);
        ASSERT_TRUE(
            Client::World::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(static_cast<double>(quiet.life) * CrackTenth * quiet.third));
        EXPECT_EQ(crack.transform.position.z, quiet.sinks ? 299.5f : 300.0f);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(4, 1).State());
        EXPECT_EQ(crack.life, quiet.life - 1);
        EXPECT_FALSE(scene.effects->Effects()[0].alive);
    }
}

// 0x5227f0: la 0xfc con escala mayor que 50 pierde la vida; con 10 o más brilla (20 − vida) · 0,1f; con menos, vida ·
// 0,1f, se hunde debajo de 5 y, con 5 o más, rand() % 10 en 0 suelta una piedrita 0xc8 + (rand() & 1) a rand() % 150
// girado rand() % 360. Siempre, +0x6c en vida · −0,01f y la luz dinámica de radio 1: roja con un dueño que no es 0x1b1.
TEST(GameModelEffectsTest, TheWideCrackDropsChipsAndLightsTheGroundRed)
{
    const std::uint16_t horseType = HorseType;
    const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = nullptr};
    {
        ModelEffect crack = Crack(0xfc, 15, horse);
        QuakeScene scene;
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(6);
        ASSERT_TRUE(
            Client::World::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(5.0 * CrackTenth));
        EXPECT_EQ(crack.objectState.uv.x, Narrow(15.0 * Wide(std::bit_cast<float>(0xbc23d70au))));
        EXPECT_EQ(crack.transform.position.z, 300.0f);
        ExpectCrackLight(scene, crack, {CrackLuminosity(6), 0.0f, 0.0f});
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(6, 1).State());
        EXPECT_EQ(crack.life, 14);
    }
    for (const bool chips : {true, false})
    {
        SCOPED_TRACE(chips);
        const std::uint32_t seed = chips ? SeedWhereDraw(1, [](int value) { return value % 10 == 0; })
                                         : SeedWhereDraw(1, [](int value) { return value % 10 != 0; });
        // El dueño es el terremoto que la creó: su tipo 0xfa no es 0x1b1.
        ModelEffect quake = Earthquake(2, 5, {});
        const ModelEffectOwner self{.type = nullptr, .effect = &quake, .angles = &quake.transform.angles};
        ModelEffect crack = Crack(0xfc, 7, self);
        QuakeScene scene;
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(seed);
        ASSERT_TRUE(
            Client::World::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(7.0 * CrackTenth));
        Client::Render::Models::CrtRandom draws(seed);
        static_cast<void>(draws.Next());
        auto expected = std::make_unique<ModelEffects>();
        if (draws.Next() % 10 == 0)
        {
            const glm::vec3 reach{0.0f, static_cast<float>(draws.Next() % 150), 0.0f};
            const glm::vec3 turn{0.0f, 0.0f, static_cast<float>(draws.Next() % 360)};
            const glm::vec3 offset = QuakeTurned(reach, turn);
            const glm::vec3 point{Narrow(Wide(offset.x) + 5000.5), Narrow(Wide(offset.y) + 6000.25),
                                  Narrow(Wide(offset.z) + 300.0)};
            const auto type = static_cast<std::int16_t>(0xc8 + draws.Next() % 2);
            ASSERT_TRUE(expected->CreateStoneChip(type, point, crack.transform.angles, QuakeLight, draws).has_value());
        }
        EXPECT_EQ(scene.effects->Effects()[0].alive, chips);
        ExpectSameEffects(*scene.effects, *expected);
        EXPECT_EQ(random.State(), draws.State());
        ExpectCrackLight(scene, crack, {CrackLuminosity(seed), 0.0f, 0.0f});
    }
    {
        ModelEffect crack = Crack(0xfc, 3, horse);
        crack.transform.scale = 50.5f;
        QuakeScene scene;
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(6);
        ASSERT_TRUE(
            Client::World::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        // La vida en 0: hundida, sin brillo, +0x6c en −0 (0 · −0,01f) y sin rand() de piedrita; la cola la mata.
        EXPECT_EQ(crack.objectState.light, 0.0f);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(crack.objectState.uv.x), 0x80000000u);
        EXPECT_EQ(crack.transform.position.z, 299.5f);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(6, 1).State());
        EXPECT_FALSE(crack.alive);
        EXPECT_EQ(crack.life, -1);
    }
    {
        const std::uint32_t seed = SeedWhereDraw(1, [](int value) { return value % 10 == 0; });
        ModelEffect crack = Crack(0xfc, 7, horse);
        QuakeScene scene;
        auto services = scene.Services();
        services.effects = nullptr;
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(seed);
        const auto cut = Client::World::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, services);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x522a94"), std::string::npos) << cut.Error();
    }
    {
        ModelEffect crack = Crack(0xfc, 15, horse);
        QuakeScene scene;
        auto services = scene.Services();
        services.lighting = nullptr;
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(6);
        const auto cut = Client::World::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, services);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x522af7"), std::string::npos) << cut.Error();
    }
    {
        // Un dueño sin tipo que leer corta en la lectura de 0x522ab2.
        const glm::vec3 angles(0.0f);
        ModelEffect crack = Crack(0xfc, 15, {.type = nullptr, .effect = nullptr, .angles = &angles});
        QuakeScene scene;
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(6);
        const auto cut =
            Client::World::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services());
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x522ab2"), std::string::npos) << cut.Error();
    }
}

// 0x52294c: la 0xff brilla (40 − vida) · 0,1f con 30 o más; si no, vida · 0,1f, se hunde debajo de 15 y, con 5 o más,
// rand() % 15 en 0 suelta una piedrita. La luz es azul con un dueño 0x1b1 y blanca sin dueño (Q-C427).
TEST(GameModelEffectsTest, TheLongCrackLightsBlueForTheSeparateOwnerAndWhiteWithoutOne)
{
    const std::uint16_t separate = SeparateType;
    {
        ModelEffect crack = Crack(0xff, 35, {.type = &separate, .effect = nullptr, .angles = nullptr});
        QuakeScene scene;
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(8);
        ASSERT_TRUE(
            Client::World::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(5.0 * CrackTenth));
        EXPECT_EQ(crack.transform.position.z, 300.0f);
        ExpectCrackLight(scene, crack, {0.0f, 0.0f, CrackLuminosity(8)});
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(8, 1).State());
    }
    for (const bool chips : {true, false})
    {
        SCOPED_TRACE(chips);
        const std::uint32_t seed = chips ? SeedWhereDraw(1, [](int value) { return value % 15 == 0; })
                                         : SeedWhereDraw(1, [](int value) { return value % 15 != 0; });
        ModelEffect crack = Crack(0xff, 12, {});
        QuakeScene scene;
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(seed);
        ASSERT_TRUE(
            Client::World::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(12.0 * CrackTenth));
        EXPECT_EQ(crack.transform.position.z, 299.5f);
        EXPECT_EQ(scene.effects->Effects()[0].alive, chips);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(seed, chips ? 2 + 3 + 5 : 2).State());
        ExpectCrackLight(scene, crack, glm::vec3(1.0f));
    }
}

// 0x522b31: la 0x102 brilla (40 − vida) · 0,1f con 30 o más y si no vida · 0,1f, se hunde debajo de 15 y con 13 o más
// saca un rand() que nadie usa (Q-C428). La luz va por 0x5212ce: roja con el caballo.
TEST(GameModelEffectsTest, TheLastCrackDrawsARandomItNeverUses)
{
    const std::uint16_t horseType = HorseType;
    const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = nullptr};
    struct LastCase
    {
        std::int32_t life;
        double glow;
        bool sinks;
        int draws;
    };
    for (const LastCase& last : {LastCase{33, 7.0, false, 2}, LastCase{20, 20.0, false, 2}, LastCase{13, 13.0, true, 2},
                                 LastCase{12, 12.0, true, 1}})
    {
        SCOPED_TRACE(last.life);
        ModelEffect crack = Crack(0x102, last.life, horse);
        QuakeScene scene;
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(2);
        ASSERT_TRUE(
            Client::World::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(last.glow * CrackTenth));
        EXPECT_EQ(crack.transform.position.z, last.sinks ? 299.5f : 300.0f);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(2, last.draws).State());
        ExpectCrackLight(scene, crack, {CrackLuminosity(2), 0.0f, 0.0f});
    }
    ModelEffect crack = Crack(0x102, 20, horse);
    QuakeScene scene;
    auto services = scene.Services();
    services.lighting = nullptr;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(2);
    const auto cut = Client::World::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5212da"), std::string::npos) << cut.Error();
}
