#include "World/GameModelEffects.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"

#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
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
} // namespace
