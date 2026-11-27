#include "World/GameModelEffects.h"

#include "Crt/CrtRandom.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "Terrain/TerrainLighting.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

namespace
{
TEST(CrywolfEffectsTest, TheCraterFadesBeforeItsLastCommonStep)
{
    auto particles = std::make_unique<Client::World::Particles>();
    auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    Client::World::ModelEffect crater;
    crater.alive = true;
    crater.type = 0x636;
    crater.life = 1;
    crater.transform.position = {1000, 2000, 30};
    crater.start = {3, 3, 0};
    crater.alpha = 0.75f;
    Client::Crt::CrtRandom random(7);
    const auto stepped =
        Client::World::StepGameModelEffect(crater, nullptr, *particles, 0, random, nullptr,
                                           Client::World::ModelEffectStepServices{.lighting = lighting.get()});
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(crater.alive);
    EXPECT_EQ(crater.life, 0);
    EXPECT_EQ(crater.alpha, 0.1f);
    EXPECT_EQ(crater.light, glm::vec3(0.1f));
    EXPECT_EQ(crater.start, (glm::vec3{3, 3, 0}));
    EXPECT_EQ(random.State(), 0x003d7aaeu);
}
TEST(CrywolfEffectsTest, TheAttackWaveGrowsAndFadesWithoutLosingItsColor)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::World::ModelEffect wave;
    wave.alive = true;
    wave.type = Client::World::ShockWaveEffect;
    wave.subtype = 8;
    wave.life = 5;
    wave.transform.scale = 3;
    wave.light = {1, 0.6f, 0.4f};
    Client::Crt::CrtRandom random(7);
    const auto stepped = Client::World::StepGameModelEffect(wave, nullptr, *particles, 0, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(wave.life, 4);
    EXPECT_EQ(wave.transform.scale, 4);
    EXPECT_FLOAT_EQ(wave.light.r, 0.7692307829856873f);
    EXPECT_FLOAT_EQ(wave.light.g, 0.46153849363327026f);
    EXPECT_FLOAT_EQ(wave.light.b, 0.3076923191547394f);
    EXPECT_EQ(random.State(), 0x003d7aaeu);
}
TEST(CrywolfEffectsTest, TheFireworkEmitterKeepsCallerAndAllocatedChildRandoms)
{
    for (const bool full : {false, true})
    {
        auto particles = std::make_unique<Client::World::Particles>();
        auto effects = std::make_unique<Client::World::ModelEffects>();
        if (full)
            for (auto& effect : effects->Effects())
                effect.alive = true;
        Client::World::ModelEffect emitter;
        emitter.alive = true;
        emitter.type = 0x60e;
        emitter.life = 75;
        emitter.light = glm::vec3(1);
        emitter.transform.position = {1000, 2000, 100};
        Client::Crt::CrtRandom random(0);
        std::vector<int> sounds;
        std::vector<std::pair<int, glm::vec3>> soundsAt;
        const auto stepped = Client::World::StepGameModelEffect(
            emitter, nullptr, *particles, 0, random, nullptr,
            {.effects = effects.get(),
             .sound = [&sounds](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); },
             .soundAt = [&soundsAt](Client::Audio::SoundId sound, const glm::vec3& origin)
             { soundsAt.emplace_back(sound.Value(), origin); }});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(emitter.life, 74);
        EXPECT_EQ(random.State(), full ? 0x1e278e7au : 0x098520c4u);
        if (full)
        {
            EXPECT_TRUE(sounds.empty());
            EXPECT_TRUE(soundsAt.empty());
        }
        else
        {
            EXPECT_TRUE(sounds.empty());
            EXPECT_EQ(soundsAt, (std::vector<std::pair<int, glm::vec3>>{{0x58, {1000, 2000, 100}}}));
            EXPECT_EQ(effects->Effects()[0].type, 0x60d);
            EXPECT_EQ(effects->Effects()[0].life, 12);
            EXPECT_EQ(effects->Effects()[0].velocity, (glm::vec3{3, 3, 26}));
        }
    }
}

TEST(CrywolfEffectsTest, TheLastFireworkStillSoundsAndDiesWithTheParticlesFull)
{
    auto particles = std::make_unique<Client::World::Particles>();
    auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    for (auto& particle : particles->All())
        particle.alive = true;
    Client::World::ModelEffect firework;
    firework.alive = true;
    firework.type = 0x60d;
    firework.life = 1;
    firework.light = glm::vec3(1);
    firework.transform.position = {1000, 2000, 100};
    Client::Crt::CrtRandom random(0);
    std::vector<int> sounds;
    std::vector<std::pair<int, glm::vec3>> soundsAt;
    const auto stepped = Client::World::StepGameModelEffect(
        firework, nullptr, *particles, 0, random, nullptr,
        {.lighting = lighting.get(),
         .sound = [&sounds](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); },
         .soundAt = [&soundsAt](Client::Audio::SoundId sound, const glm::vec3& origin)
         { soundsAt.emplace_back(sound.Value(), origin); }});
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), 0x098520c4u);
    EXPECT_FALSE(firework.alive);
    EXPECT_TRUE(sounds.empty());
    EXPECT_EQ(soundsAt, (std::vector<std::pair<int, glm::vec3>>{{0x59, {1000, 2000, 100}}}));
    auto expectedLighting = std::make_unique<Client::Terrain::TerrainLighting>();
    Client::Terrain::AddTerrainLight(*expectedLighting, 1000, 2000,
                                     {0.040000010281801224f, 0.030000008642673492f, 0.020000005140900612f}, 1);
    EXPECT_EQ(lighting->cells, expectedLighting->cells);
}
TEST(CrywolfEffectsTest, TheLastFireworkFillsTheOnlyParticleRoomAndKeepsTheRemainingCallerWork)
{
    auto particles = std::make_unique<Client::World::Particles>();
    auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    for (auto& particle : particles->All())
        particle.alive = true;
    particles->All()[0].alive = false;
    Client::World::ModelEffect firework;
    firework.alive = true;
    firework.type = 0x60d;
    firework.life = 1;
    firework.light = glm::vec3(1);
    firework.transform.position = {1000, 2000, 100};
    Client::Crt::CrtRandom random(0);
    std::vector<int> sounds;
    const auto stepped = Client::World::StepGameModelEffect(
        firework, nullptr, *particles, 0, random, nullptr,
        {.lighting = lighting.get(),
         .sound = [&sounds](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); }});
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), 0x7e1dbec8u);
    EXPECT_EQ(sounds, (std::vector<int>{0x59}));
    const auto& burst = particles->All()[0];
    EXPECT_EQ(burst.type, 0x60d);
    EXPECT_EQ(burst.subtype, 9);
    EXPECT_EQ(burst.life, 50);
    EXPECT_EQ(burst.rotation, 215);
    EXPECT_FLOAT_EQ(burst.velocity.x, 0.15f);
    EXPECT_FLOAT_EQ(burst.velocity.y, 0.15f);
    EXPECT_FLOAT_EQ(burst.velocity.z, 8.95f);
    EXPECT_EQ(burst.light, (glm::vec3{0.7000000476837158f, 0.1f, 0}));
}
} // namespace
