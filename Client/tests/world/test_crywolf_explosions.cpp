#include "Crt/CrtRandom.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "Terrain/TerrainLighting.h"
#include "Audio/SoundId.h"
#include <gtest/gtest.h>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace
{
TEST(CrywolfExplosionsTest, TheLargeExplosionHasThirtyStepsAndTheRingExplosionTwenty)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Crt::CrtRandom random(7);
    for (int subtype = 0; subtype <= 2; ++subtype)
    {
        const auto made = particles->CreateChecked(0x5ed, {100, 200, 300}, {}, {1, 2, 3}, subtype, 7, 0, random);
        ASSERT_TRUE(made.HasValue());
        ASSERT_TRUE(made.Value());
        EXPECT_EQ(particles->All()[*made.Value()].life, subtype == 2 ? 30 : 20);
        EXPECT_EQ(particles->All()[*made.Value()].scale, 7);
    }
    EXPECT_EQ(random.State(), 7u);
}

TEST(CrywolfExplosionsTest, TheFullParticlePoolSkipsTheRingCalleesAndKeepsTheFrameAndLight)
{
    auto particles = std::make_unique<Client::World::Particles>();
    auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    for (auto& p : particles->All())
    {
        p.alive = true;
        p.type = 0x60d;
        p.subtype = -1;
        p.life = 50;
    }
    auto& explosion = particles->All()[0];
    explosion.type = 0x5ed;
    explosion.subtype = 2;
    explosion.life = 21;
    explosion.position = {1000, 2000, 300};
    Client::Crt::CrtRandom random(7);
    Client::World::PortableParticleTrigonometry trig;
    const float clock = 0;
    const auto stepped = Client::World::StepParticlesChecked(*particles, random, std::cref(clock), nullptr, trig, {},
                                                             {}, {}, {}, lighting.get());
    ASSERT_TRUE(stepped.HasValue());
    EXPECT_EQ(explosion.life, 20);
    EXPECT_EQ(std::bit_cast<std::int32_t>(explosion.field48), 5);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}

// 0x5446e0 recorre índices físicos. Los hijos creados detrás del cursor esperan al próximo pase;
// los de delante dan su primer paso en este mismo frame. Sólo la explosión aceptada produce sonido.
TEST(CrywolfExplosionsTest, TheRingRetainsThePhysicalVisitOrderAndTheAcceptedExplosionSound)
{
    for (const bool sourceLast : {false, true})
    {
        SCOPED_TRACE(sourceLast);
        auto particles = std::make_unique<Client::World::Particles>();
        auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
        for (auto& p : particles->All())
        {
            p.alive = true;
            p.type = 0x60d;
            p.subtype = -1;
            p.life = 50;
        }
        const std::size_t source = sourceLast ? particles->All().size() - 1 : 0;
        const std::size_t smoke = sourceLast ? 0 : 1;
        const std::size_t burst = smoke + 1;
        particles->All()[smoke].alive = false;
        particles->All()[burst].alive = false;
        auto& explosion = particles->All()[source];
        explosion.type = 0x5ed;
        explosion.subtype = 2;
        explosion.life = 21;
        explosion.position = {1000, 2000, 300};
        Client::Crt::CrtRandom random(7);
        Client::World::PortableParticleTrigonometry trig;
        const float clock = 0;
        std::vector<int> sounds;
        const auto stepped = Client::World::StepParticlesChecked(
            *particles, random, std::cref(clock), nullptr, trig, {}, {}, {}, {}, lighting.get(), {}, {},
            [&](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); });
        ASSERT_TRUE(stepped.HasValue());
        EXPECT_EQ(random.State(), 0xbb6f8e98u);
        EXPECT_EQ(sounds, (std::vector<int>{0x40}));
        EXPECT_EQ(particles->All()[smoke].type, 0x5f2);
        EXPECT_EQ(particles->All()[smoke].subtype, 35);
        EXPECT_EQ(particles->All()[smoke].life, sourceLast ? 30 : 29);
        EXPECT_EQ(particles->All()[smoke].position, (glm::vec3{1000, sourceLast ? 2200 : 2195, 300}));
        EXPECT_EQ(particles->All()[burst].type, 0x5ed);
        EXPECT_EQ(particles->All()[burst].subtype, 1);
        EXPECT_EQ(particles->All()[burst].life, sourceLast ? 20 : 19);
    }
}
} // namespace
