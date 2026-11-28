#include "Crt/CrtRandom.h"
#include "CastleSiege/SiegeStoneEffects.h"
#include "Core/Expected.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainLighting.h"
#include "Audio/SoundId.h"
#include <gtest/gtest.h>
#include <glm/ext/vector_float3.hpp>
#include <memory>
#include <cstdint>
#include <string>
namespace
{
using namespace Client;
Effects::ModelEffect Flight(std::int16_t type)
{
    Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = type;
    effect.subtype = 1;
    effect.life = 249;
    effect.transform.position = {5000, 12000, 100};
    effect.start = effect.transform.position;
    effect.velocity = {0, 0, 0};
    effect.turnDirection = 2;
    effect.speed = 29;
    effect.state = 1;
    effect.byte89 = 1;
    return effect;
}
struct Services
{
    std::unique_ptr<Terrain::TerrainLighting> lighting = std::make_unique<Terrain::TerrainLighting>();
    glm::vec3 camera{};
    float shake = 0;
    CastleSiege::SiegeStoneStepServices services{
        .terrainHeight = [](float, float) { return 0.0; },
        .lighting = lighting.get(),
        .cameraTarget = &camera,
        .cameraShake = &shake,
        .collide = [](Effects::ModelEffect&, float, float, bool) -> Core::Expected<bool, std::string>
        { return false; }};
};
TEST(SiegeStoneStepTest, StoneNotifiesOnlyAfterItsAgeAndWithTwoNonzeroKeyBytes)
{
    Particles::Particles particles;
    Crt::CrtRandom random(1);
    Services state;
    int notifications = 0;
    state.services.notify = [&](std::uint8_t hi, std::uint8_t lo)
    {
        EXPECT_EQ(hi, 1);
        EXPECT_EQ(lo, 1);
        ++notifications;
    };
    auto effect = Flight(CastleSiege::SiegeStoneEffect);
    effect.life = 250;
    ASSERT_TRUE(CastleSiege::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(notifications, 0);
    EXPECT_EQ(effect.life, 249);
    ASSERT_TRUE(CastleSiege::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(notifications, 1);
    EXPECT_EQ(effect.state, 0);
    EXPECT_EQ(effect.byte89, 0);
    effect.state = 0;
    effect.byte89 = 1;
    ASSERT_TRUE(CastleSiege::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(notifications, 1);
}
TEST(SiegeStoneStepTest, IceCanNotifyAtTheFirstFlightTick)
{
    Particles::Particles particles;
    Crt::CrtRandom random(1);
    Services state;
    int notifications = 0;
    state.services.notify = [&](auto, auto) { ++notifications; };
    auto effect = Flight(CastleSiege::SiegeIceStoneEffect);
    effect.life = 300;
    ASSERT_TRUE(CastleSiege::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(notifications, 1);
    EXPECT_EQ(effect.life, 299);
}
TEST(SiegeStoneStepTest, InvisibleLandingStillSetsImpactAndConsumesShakeBeforeTheLifeEpilogue)
{
    Particles::Particles particles;
    Crt::CrtRandom random(1);
    Services state;
    state.services.terrainHeight = [](float, float) { return 200.0; };
    auto effect = Flight(CastleSiege::SiegeStoneEffect);
    effect.subtype = 0;
    effect.transform.position.z = 100;
    effect.start.z = 100;
    effect.seen = false;
    ASSERT_TRUE(CastleSiege::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(effect.subtype, 88);
    EXPECT_EQ(effect.life, 14);
    EXPECT_EQ(effect.byte90, 0);
    EXPECT_EQ(effect.objectState.hiddenMesh, 99);
    EXPECT_EQ(effect.transform.position.z, 195);
    Crt::CrtRandom expected(1);
    expected.Discard(5);
    EXPECT_EQ(random.State(), expected.State());
}
TEST(SiegeStoneStepTest, HeroCameraFollowsPersistentImpactEvenBelowTheCutoff)
{
    Particles::Particles particles;
    Crt::CrtRandom random(1);
    Services state;
    int hero = 0;
    state.services.hero = &hero;
    auto effect = Flight(CastleSiege::SiegeStoneEffect);
    effect.subtype = 99;
    effect.life = 25;
    effect.byte90 = 25;
    effect.ownerContext = &hero;
    ASSERT_TRUE(CastleSiege::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(effect.life, 24);
    EXPECT_EQ(state.camera, effect.transform.position);
    EXPECT_FALSE(particles.All()[0].alive);
    Crt::CrtRandom expected(1);
    expected.Discard(1);
    EXPECT_EQ(random.State(), expected.State());
}
TEST(SiegeStoneStepTest, AirCollisionPreservesTheOldImpactCutoff)
{
    Particles::Particles particles;
    Crt::CrtRandom random(1);
    Services state;
    state.services.collide = [](Effects::ModelEffect& effect, float r, float h,
                                bool mark) -> Core::Expected<bool, std::string>
    {
        EXPECT_EQ(r, 200);
        EXPECT_EQ(h, 250);
        EXPECT_FALSE(mark);
        effect.life = 15;
        effect.subtype = 99;
        effect.objectState.hiddenMesh = 99;
        return true;
    };
    auto effect = Flight(CastleSiege::SiegeStoneEffect);
    effect.seen = true;
    int sounds = 0;
    state.services.sound = [&](Client::Audio::SoundId id, const glm::vec3& position)
    {
        EXPECT_EQ(id, Client::Audio::SoundId{0x1eb});
        EXPECT_EQ(position, effect.transform.position);
        ++sounds;
    };
    effect.byte90 = 71;
    ASSERT_TRUE(CastleSiege::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(effect.life, 14);
    EXPECT_EQ(effect.byte90, 71);
    EXPECT_EQ(sounds, 1);
}
TEST(SiegeStoneStepTest, VisibleLandingSoundsAtTheGroundAdjustedImpactPosition)
{
    Particles::Particles particles;
    Crt::CrtRandom random(1);
    Services state;
    auto effects = std::make_unique<Effects::ModelEffects>();
    state.services.effects = effects.get();
    state.services.heroAction = [](const glm::vec3&, float) -> Core::Expected<void, std::string> { return {}; };
    auto effect = Flight(CastleSiege::SiegeStoneEffect);
    effect.subtype = 0;
    effect.transform.position.z = -100;
    effect.start.z = -100;
    effect.seen = true;
    int sounds = 0;
    state.services.sound = [&](Client::Audio::SoundId id, const glm::vec3& position)
    {
        EXPECT_EQ(id, Client::Audio::SoundId{0x1eb});
        EXPECT_EQ(position, effect.transform.position);
        EXPECT_EQ(position.z, -5.0f);
        ++sounds;
    };
    const auto stepped = CastleSiege::StepSiegeStone(effect, nullptr, particles, random, state.services);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(sounds, 1);
}
} // namespace
