#include "World/SiegeStoneEffects.h"
#include "World/ModelEffects.h"
#include "World/WorldObjects.h"
#include <gtest/gtest.h>
#include <array>
namespace
{
TEST(SiegeStoneCollisionTest, TheFirstInsertedObjectTakesTheImpactAndTheAirCutoffSurvives)
{
    Client::World::ModelEffect projectile;
    projectile.transform.position = {500, 500, 0};
    projectile.byte90 = 63;
    std::array<Client::World::WorldObject, 2> objects;
    for (auto& object : objects)
    {
        object.transform.position = {500, 500, 0};
        object.field91 = 1;
        object.state.hiddenMesh = -1;
    }
    const auto collided = Client::World::CollideTheSiegeObject(projectile, objects, 200, 250, false);
    ASSERT_TRUE(collided.HasValue());
    EXPECT_TRUE(collided.Value());
    EXPECT_EQ(objects[0].field84, 1);
    EXPECT_EQ(objects[1].field84, 0);
    EXPECT_EQ(projectile.subtype, 99);
    EXPECT_EQ(projectile.life, 15);
    EXPECT_EQ(projectile.byte90, 63);
}
TEST(SiegeStoneCollisionTest, AirRequiresStrictHeightButGroundOnlyMarksWithoutChangingTheProjectile)
{
    Client::World::ModelEffect projectile;
    projectile.transform.position = {500, 500, 250};
    projectile.life = 300;
    std::array<Client::World::WorldObject, 1> objects;
    auto& object = objects[0];
    object.transform.position = {500, 500, 0};
    object.field91 = 1;
    object.state.hiddenMesh = -1;
    object.field84 = 4.8f;
    const auto air = Client::World::CollideTheSiegeObject(projectile, objects, 200, 250, false);
    ASSERT_TRUE(air.HasValue());
    EXPECT_FALSE(air.Value());
    EXPECT_EQ(projectile.life, 300);
    const auto ground = Client::World::CollideTheSiegeObject(projectile, objects, 200, 250, true);
    ASSERT_TRUE(ground.HasValue());
    EXPECT_TRUE(ground.Value());
    EXPECT_EQ(object.field08, 99);
    EXPECT_EQ(projectile.life, 300);
    EXPECT_EQ(projectile.subtype, 0);
}
} // namespace
