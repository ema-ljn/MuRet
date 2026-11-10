#include "Scenes/MapModelEffectServices.h"

#include "Render/Models/Cloth.h"
#include "World/ModelEffects.h"
#include "World/WorldObjects.h"

#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

namespace
{
TEST(MapModelEffectServicesTest, NoriaCopiesTheLightBeforeMutatingItsSourceAndConsumesThreeDraws)
{
    Client::World::ModelEffects effects;
    Client::Render::Models::CrtRandom random(1);
    Client::Scenes::MapModelEffectServices services(effects, random);
    Client::World::WorldObject source;
    source.light = {0.2f, 0.4f, 0.6f};
    const glm::vec3 position(100.0f, 200.0f, 300.0f);
    ASSERT_TRUE(services.CreateModelEffect({322, position, 0.0f, 0}, source));
    EXPECT_EQ(source.light, glm::vec3(0.0f));
    const auto& effect = effects.Effects()[0];
    EXPECT_EQ(effect.light, glm::vec3(0.2f, 0.4f, 0.6f));
    EXPECT_EQ(effect.transform.position, position);
    EXPECT_EQ(effect.life, 0xffffff);
    EXPECT_EQ(effect.objectState.blendMesh, -2);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 3).State());
}
TEST(MapModelEffectServicesTest, TheLastTowerTypePreservesTheSourceAndUsesNoRandom)
{
    Client::World::ModelEffects effects;
    Client::Render::Models::CrtRandom random(1);
    Client::Scenes::MapModelEffectServices services(effects, random);
    Client::World::WorldObject source;
    source.light = {0.2f, 0.4f, 0.6f};
    ASSERT_TRUE(services.CreateModelEffect({324, source.transform.position, 0.0f, 0}, source));
    EXPECT_EQ(source.light, glm::vec3(0.2f, 0.4f, 0.6f));
    EXPECT_EQ(effects.Effects()[0].transform.scale, 0.6f);
    EXPECT_EQ(random.State(), 1u);
}
TEST(MapModelEffectServicesTest, AFullPhysicalPoolSkipsMutationAndRandomWithoutFailingTheBinder)
{
    Client::World::ModelEffects effects;
    for (auto& effect : effects.Effects())
        effect.alive = true;
    Client::Render::Models::CrtRandom random(1);
    Client::Scenes::MapModelEffectServices services(effects, random);
    Client::World::WorldObject source;
    source.light = {0.2f, 0.4f, 0.6f};
    ASSERT_TRUE(services.CreateModelEffect({322, source.transform.position, 0.0f, 0}, source));
    EXPECT_EQ(source.light, glm::vec3(0.2f, 0.4f, 0.6f));
    EXPECT_EQ(random.State(), 1u);
}
TEST(MapModelEffectServicesTest, ReusingASlotRetainsFieldsThatTheNativeCreatorDoesNotWrite)
{
    Client::World::ModelEffects effects;
    auto& slot = effects.Effects()[0];
    slot.turnPending = 17.0f;
    slot.turnDirection = 19.0f;
    slot.reference = 23.0f;
    slot.animation.previousAction = 7;
    slot.seen = true;
    Client::Render::Models::CrtRandom random(1);
    Client::Scenes::MapModelEffectServices services(effects, random);
    Client::World::WorldObject source;
    ASSERT_TRUE(services.CreateModelEffect({324, source.transform.position, 0.0f, 0}, source));
    EXPECT_EQ(slot.turnPending, 17.0f);
    EXPECT_EQ(slot.turnDirection, 19.0f);
    EXPECT_EQ(slot.reference, 23.0f);
    EXPECT_EQ(slot.animation.previousAction, 7);
    EXPECT_TRUE(slot.seen);
    slot.alive = false;
    ASSERT_TRUE(effects.Create(Client::World::TeleportEffect, {}, {}, {}, 0.0f).has_value());
    EXPECT_EQ(slot.turnPending, 17.0f);
    EXPECT_EQ(slot.turnDirection, 19.0f);
    EXPECT_EQ(slot.reference, 23.0f);
    EXPECT_EQ(slot.animation.previousAction, 7);
    EXPECT_TRUE(slot.seen);
}
} // namespace
