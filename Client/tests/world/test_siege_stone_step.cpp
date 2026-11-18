#include "World/SiegeStoneEffects.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"
#include "Render/Models/Cloth.h"
#include <gtest/gtest.h>
#include <glm/ext/vector_float3.hpp>
#include <memory>
#include <cstdint>
namespace
{
using namespace Client;
World::ModelEffect Flight(std::int16_t type)
{
    World::ModelEffect effect;
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
    std::unique_ptr<World::TerrainLighting> lighting = std::make_unique<World::TerrainLighting>();
    glm::vec3 camera{};
    float shake = 0;
    World::SiegeStoneStepServices services{
        .terrainHeight = [](float, float) { return 0.0; },
        .lighting = lighting.get(),
        .cameraTarget = &camera,
        .cameraShake = &shake,
        .collide = [](World::ModelEffect&, float, float, bool) -> Core::Expected<bool, std::string> { return false; }};
};
TEST(SiegeStoneStepTest, StoneNotifiesOnlyAfterItsAgeAndWithTwoNonzeroKeyBytes)
{
    World::Particles particles;
    Render::Models::CrtRandom random(1);
    Services state;
    int notifications = 0;
    state.services.notify = [&](std::uint8_t hi, std::uint8_t lo)
    {
        EXPECT_EQ(hi, 1);
        EXPECT_EQ(lo, 1);
        ++notifications;
    };
    auto effect = Flight(World::SiegeStoneEffect);
    effect.life = 250;
    ASSERT_TRUE(World::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(notifications, 0);
    EXPECT_EQ(effect.life, 249);
    ASSERT_TRUE(World::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(notifications, 1);
    EXPECT_EQ(effect.state, 0);
    EXPECT_EQ(effect.byte89, 0);
    effect.state = 0;
    effect.byte89 = 1;
    ASSERT_TRUE(World::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(notifications, 1);
}
TEST(SiegeStoneStepTest, IceCanNotifyAtTheFirstFlightTick)
{
    World::Particles particles;
    Render::Models::CrtRandom random(1);
    Services state;
    int notifications = 0;
    state.services.notify = [&](auto, auto) { ++notifications; };
    auto effect = Flight(World::SiegeIceStoneEffect);
    effect.life = 300;
    ASSERT_TRUE(World::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(notifications, 1);
    EXPECT_EQ(effect.life, 299);
}
TEST(SiegeStoneStepTest, InvisibleLandingStillSetsImpactAndConsumesShakeBeforeTheLifeEpilogue)
{
    World::Particles particles;
    Render::Models::CrtRandom random(1);
    Services state;
    state.services.terrainHeight = [](float, float) { return 200.0; };
    auto effect = Flight(World::SiegeStoneEffect);
    effect.subtype = 0;
    effect.transform.position.z = 100;
    effect.start.z = 100;
    effect.seen = false;
    ASSERT_TRUE(World::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(effect.subtype, 88);
    EXPECT_EQ(effect.life, 14);
    EXPECT_EQ(effect.byte90, 0);
    EXPECT_EQ(effect.objectState.hiddenMesh, 99);
    EXPECT_EQ(effect.transform.position.z, 195);
    Render::Models::CrtRandom expected(1);
    expected.Discard(5);
    EXPECT_EQ(random.State(), expected.State());
}
TEST(SiegeStoneStepTest, HeroCameraFollowsPersistentImpactEvenBelowTheCutoff)
{
    World::Particles particles;
    Render::Models::CrtRandom random(1);
    Services state;
    int hero = 0;
    state.services.hero = &hero;
    auto effect = Flight(World::SiegeStoneEffect);
    effect.subtype = 99;
    effect.life = 25;
    effect.byte90 = 25;
    effect.ownerContext = &hero;
    ASSERT_TRUE(World::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(effect.life, 24);
    EXPECT_EQ(state.camera, effect.transform.position);
    EXPECT_FALSE(particles.All()[0].alive);
    Render::Models::CrtRandom expected(1);
    expected.Discard(1);
    EXPECT_EQ(random.State(), expected.State());
}
TEST(SiegeStoneStepTest, AirCollisionPreservesTheOldImpactCutoff)
{
    World::Particles particles;
    Render::Models::CrtRandom random(1);
    Services state;
    state.services.collide = [](World::ModelEffect& effect, float r, float h,
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
    auto effect = Flight(World::SiegeStoneEffect);
    effect.byte90 = 71;
    ASSERT_TRUE(World::StepSiegeStone(effect, nullptr, particles, random, state.services).HasValue());
    EXPECT_EQ(effect.life, 14);
    EXPECT_EQ(effect.byte90, 71);
}
} // namespace
