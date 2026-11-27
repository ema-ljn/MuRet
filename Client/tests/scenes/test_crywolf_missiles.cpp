#include "Crt/CrtRandom.h"
#include "Scenes/EventPackets.h"
#include "Protocol/Events/Events.h"
#include "Kernel/WorldState.h"
#include "Kernel/WorldMap.h"
#include "World/ModelEffects.h"
#include "World/GameModelEffects.h"
#include "World/Particles.h"
#include "World/ParticleOwner.h"
#include "Terrain/TerrainLighting.h"
#include "support/Bytes.h"
#include "support/Packets.h"
#include "support/PlayerActors.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <memory>

namespace
{
TEST(CrywolfMissilesTest, TheEffectPacketCreatesAllThreeNativeDirections)
{
    for (const std::uint8_t direction :
         {std::uint8_t{0x3e}, std::uint8_t{0xb7}, std::uint8_t{0x7a}, std::uint8_t{0x74}})
    {
        SCOPED_TRACE(direction);
        auto world = std::make_unique<Client::Scenes::WorldState>();
        Client::Crt::CrtRandom random(7);
        const auto wire =
            Client::Tests::PacketOf(Client::Tests::Bytes({0xc1, 0x0a, 0xbd, 0x0c, 0, 0, direction, 0xff, 12, 34}));
        const auto read = Client::Protocol::Events::ReadEventPacket(wire);
        ASSERT_TRUE(read.HasValue());
        const auto applied = Client::Scenes::ApplyEventPacket(read.Value(), {.world = *world, .random = random});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const auto& effect = world->Map().effects.Effects()[0];
        EXPECT_EQ(effect.type, 0xee);
        EXPECT_EQ(effect.life, 100);
        EXPECT_EQ(effect.transform.position.z, 800);
        const int subtype = direction == 0xb7 ? 1 : direction == 0x7a || direction == 0x74 ? 2 : 0;
        EXPECT_EQ(effect.subtype, subtype);
        EXPECT_EQ(effect.siegeTarget, (glm::vec3{1200, 3400, 100}));
        EXPECT_EQ(effect.start, (glm::vec3{subtype == 0   ? 400
                                           : subtype == 1 ? 2000
                                                          : 1200,
                                           subtype == 2 ? 4200 : 3400, 800}));
        EXPECT_EQ(effect.transform.position, effect.start);
        EXPECT_EQ(effect.transform.angles, (glm::vec3{40, 0, subtype == 0 ? 90 : subtype == 1 ? -90 : 0}));
        EXPECT_EQ(effect.velocity, (glm::vec3{0, -80, 0}));
        EXPECT_EQ(effect.light, glm::vec3(1));
        EXPECT_EQ(random.State(), 7u);
    }
}

TEST(CrywolfMissilesTest, TheSeenMissileCreatesThreeOwnedParticlesBeforeTheCommonMotion)
{
    auto particles = std::make_unique<Client::World::Particles>();
    auto effects = std::make_unique<Client::World::ModelEffects>();
    auto lighting = std::make_unique<Client::World::TerrainLighting>();
    const auto made =
        effects->CreateCrywolfMissile({.type = 0xee, .position = {1000, 2000, 300}, .angles = {10, 20, 30}});
    ASSERT_TRUE(made);
    auto& effect = effects->Effects()[*made];
    effect.seen = true;
    effect.transform.angles = {0, 0, 0};
    Client::Crt::CrtRandom random(7);
    const auto stepped = Client::World::StepGameModelEffect(
        effect, nullptr, *particles, 0, random, nullptr,
        {.terrainHeight = [](float, float) { return 0.0; }, .effects = effects.get(), .lighting = lighting.get()});
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(effect.life, 99);
    EXPECT_EQ(effect.transform.position, (glm::vec3{1000, 1920, 300}));
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, 11).State());
    const auto pool = particles->All();
    EXPECT_EQ(pool[0].type, 0x5da);
    EXPECT_EQ(pool[0].subtype, 8);
    EXPECT_EQ(pool[1].type, 0x5f2);
    EXPECT_EQ(pool[1].subtype, 38);
    EXPECT_EQ(pool[2].type, 0x5da);
    EXPECT_EQ(pool[2].subtype, 9);
    EXPECT_EQ(pool[1].position, (glm::vec3{1000, 2000, 300}));
    EXPECT_EQ(pool[2].position, pool[1].position);
    EXPECT_EQ(pool[0].typedOwner, (Client::World::ParticleOwner{Client::World::ParticleOwnerKind::ModelEffect, 0}));
    EXPECT_EQ(pool[1].typedOwner, pool[0].typedOwner);
    EXPECT_EQ(pool[2].typedOwner, pool[0].typedOwner);
    EXPECT_EQ(pool[1].light, glm::vec3(1));
    EXPECT_EQ(pool[2].light, (glm::vec3{1, 0.4f, 0}));
}

TEST(CrywolfMissilesTest, TheImpactKeepsCallerRandomsAndTheCommonTailWithBothPoolsFull)
{
    auto particles = std::make_unique<Client::World::Particles>();
    auto effects = std::make_unique<Client::World::ModelEffects>();
    auto lighting = std::make_unique<Client::World::TerrainLighting>();
    const auto made = effects->CreateCrywolfMissile({.type = 0xee, .position = {1000, 2000, 30}});
    ASSERT_TRUE(made);
    auto& effect = effects->Effects()[*made];
    effect.seen = true;
    effect.transform.angles = {0, 0, 0};
    for (auto& p : particles->All())
        p.alive = true;
    for (auto& e : effects->Effects())
        e.alive = true;
    float shake = 0;
    Client::Crt::CrtRandom random(7);
    const auto stepped = Client::World::StepGameModelEffect(effect, nullptr, *particles, 0, random, nullptr,
                                                            {.terrainHeight = [](float, float) { return 0.0; },
                                                             .effects = effects.get(),
                                                             .lighting = lighting.get(),
                                                             .cameraShake = &shake});
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, 341).State());
    EXPECT_EQ(effect.objectState.hiddenMesh, 99);
    EXPECT_EQ(effect.start, (glm::vec3{1000, 2000, 30}));
    EXPECT_EQ(effect.transform.position, (glm::vec3{1000, 2000, 0}));
    EXPECT_EQ(effect.life, 99);
    EXPECT_EQ(effect.transform.scale, 1);
    EXPECT_FLOAT_EQ(effect.alpha, 0.9523810148239136f);
    EXPECT_LT(shake, 0);
}
// 0x51fff0(kind1): ocho bursts de20 chispas (cinco sorteos cada una) y dos piedras (cinco cada una).
// Más6 del fuego6ac/0, cuarenta sorteos caller por grupo, dieciséis selectores y cinco de la cola del misil.
TEST(CrywolfMissilesTest, TheNormalImpactBuildsAllEightBurstsSixteenStonesAndTheCrater)
{
    auto particles = std::make_unique<Client::World::Particles>();
    auto effects = std::make_unique<Client::World::ModelEffects>();
    auto lighting = std::make_unique<Client::World::TerrainLighting>();
    const auto made = effects->CreateCrywolfMissile({.type = 0xee, .position = {1000, 2000, 30}});
    ASSERT_TRUE(made);
    auto& effect = effects->Effects()[*made];
    effect.transform.angles = {0, 0, 0};
    float shake = 0;
    std::vector<int> sounds;
    Client::Crt::CrtRandom random(7);
    const auto stepped = Client::World::StepGameModelEffect(effect, nullptr, *particles, 0, random, nullptr,
                                                            {.terrainHeight = [](float, float) { return 0.0; },
                                                             .effects = effects.get(),
                                                             .lighting = lighting.get(),
                                                             .cameraShake = &shake,
                                                             .sound = [&](int sound) { sounds.push_back(sound); }});
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, 1227).State());
    EXPECT_EQ(std::ranges::count_if(particles->All(), [](const auto& p) { return p.alive; }), 169);
    EXPECT_EQ(std::ranges::count_if(effects->Effects(), [](const auto& e) { return e.alive; }), 18);
    EXPECT_EQ(sounds, std::vector<int>(8, 0x40));
    EXPECT_EQ(effects->Effects()[17].type, 0x636);
    EXPECT_EQ(effect.transform.position, (glm::vec3{1000, 2000, 0}));
}

} // namespace
