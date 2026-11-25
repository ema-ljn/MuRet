#include "Crt/CrtRandom.h"
#include "Scenes/ModelEffects.h"
#include "Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldMap.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Math/HalfLifeMath.h"
#include "support/PlayerActors.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <algorithm>

namespace
{
TEST(CrywolfHelpersTest, TheThreeAttackWindowsContinueWhenTheRealPoolsAreFull)
{
    struct Attack
    {
        std::uint16_t type;
        std::uint8_t action;
        float frame;
    };
    for (const Attack hit : {Attack{0x1ab, 4, 7.5f}, Attack{0x1b0, 3, 6.5f}, Attack{0x1b4, 4, 5.5f}})
    {
        SCOPED_TRACE(hit.type);
        auto world = std::make_unique<Client::Scenes::WorldState>();
        auto actor = std::make_unique<Client::Scenes::ActorState>();
        actor->alive = true;
        actor->type = hit.type;
        actor->scale = 1;
        actor->animation.action = hit.action;
        actor->animation.frame = hit.frame;
        for (auto& effect : world->Map().effects.Effects())
            effect.alive = true;
        for (auto& particle : world->Particles().All())
            particle.alive = true;
        Client::Render::Models::Skeleton skeleton;
        skeleton.bones.assign(50, {.dummy = true, .parent = -1, .actions = {}});
        skeleton.actions.assign(12, {.repeats = true, .speed = 0.25f, .keyCount = 10, .lockPositions = false});
        Client::World::FrameSprites sprites;
        Client::Crt::CrtRandom random(7);
        Client::Scenes::ModelEffectsPass pass{.actor = *actor,
                                              .slot = 3,
                                              .world = *world,
                                              .random = random,
                                              .skeleton = skeleton,
                                              .particles = &world->Particles(),
                                              .sprites = &sprites,
                                              .map = 34};
        const auto stepped = Client::Scenes::StepTheModelEffects(pass);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(random.State(), 0x003d7aaeu);
    }
}

TEST(CrywolfHelpersTest, TheAttackWindowsCreateTheirNativeChildrenOnlyInsideTheHalfOpenGate)
{
    struct Attack
    {
        std::uint16_t type;
        std::uint8_t action;
        float frame;
        int draws;
        int models;
        int particles;
    };
    for (const Attack hit :
         {Attack{0x1ab, 4, 7.5f, 31, 7, 1}, Attack{0x1b0, 3, 6.5f, 52, 14, 2}, Attack{0x1b4, 4, 5.5f, 61, 12, 0}})
        for (const int edge : {-1, 0, 1})
        {
            SCOPED_TRACE(hit.type);
            SCOPED_TRACE(edge);
            auto world = std::make_unique<Client::Scenes::WorldState>();
            auto actor = std::make_unique<Client::Scenes::ActorState>();
            actor->alive = true;
            actor->type = hit.type;
            actor->scale = 1;
            actor->position = {1000, 2000, 50};
            actor->animation.action = hit.action;
            actor->animation.frame = hit.frame + static_cast<float>(edge) * 0.25f;
            Client::Render::Models::Skeleton skeleton;
            skeleton.bones.assign(50, {.dummy = true, .parent = -1, .actions = {}});
            skeleton.actions.assign(12, {.repeats = true, .speed = 0.25f, .keyCount = 10, .lockPositions = false});
            Client::World::FrameSprites sprites;
            Client::Crt::CrtRandom random(7);
            Client::Scenes::ModelEffectsPass pass{.actor = *actor,
                                                  .slot = 3,
                                                  .world = *world,
                                                  .random = random,
                                                  .skeleton = skeleton,
                                                  .particles = &world->Particles(),
                                                  .sprites = &sprites,
                                                  .map = 34};
            const auto stepped = Client::Scenes::StepTheModelEffects(pass);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            const bool inside = edge == 0;
            EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, inside ? hit.draws : 1).State());
            EXPECT_EQ(std::ranges::count_if(world->Map().effects.Effects(), [](const auto& e) { return e.alive; }),
                      inside ? hit.models : 0);
            EXPECT_EQ(std::ranges::count_if(world->Particles().All(), [](const auto& p) { return p.alive; }),
                      inside ? hit.particles : 0);
            if (inside && hit.type == 0x1ab)
            {
                EXPECT_EQ(world->Map().effects.Effects()[0].type, 0x636);
                EXPECT_EQ(world->Particles().All()[0].subtype, 2);
                EXPECT_EQ(world->Runtime().lastSoundId, 0x40);
            }
        }
}

TEST(CrywolfHelpersTest, TheNamedSpritesReadTheRegisteredActorBonesAndKeepBothRotationDraws)
{
    for (const int map : {34, 35})
    {
        auto world = std::make_unique<Client::Scenes::WorldState>();
        auto actor = std::make_unique<Client::Scenes::ActorState>();
        actor->alive = true;
        actor->type = 0x1b2;
        actor->scale = 1;
        actor->position = {1000, 2000, 50};
        actor->animation.action = 3;
        actor->bodyPose.assign(3, Client::Math::BoneTransform(1.0f));
        actor->bodyPose[0][3] = {10, 20, 30};
        actor->bodyPose[1][3] = {40, 50, 60};
        actor->bodyPose[2][3] = {70, 80, 90};
        world->Runtime().namedBones = {{3, "Monster96_Center", 0}, {3, "Monster96_Top", 1}, {3, "Monster96_Bottom", 2}};
        Client::Render::Models::Skeleton skeleton;
        Client::World::FrameSprites sprites;
        Client::Crt::CrtRandom random(7);
        Client::Scenes::ModelEffectsPass pass{.actor = *actor,
                                              .slot = 3,
                                              .world = *world,
                                              .random = random,
                                              .skeleton = skeleton,
                                              .sprites = &sprites,
                                              .map = map};
        const auto stepped = Client::Scenes::StepTheModelEffects(pass);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, map == 35 ? 3 : 2).State());
        const auto out = sprites.Sprites();
        EXPECT_EQ(std::ranges::count_if(out, [](const auto& s) { return s.alive; }), map == 35 ? 10 : 5);
        EXPECT_EQ(out[0].texture, 0x5ac);
        EXPECT_EQ(out[0].position, (glm::vec3{1010, 2020, 80}));
        EXPECT_EQ(out[0].scale, 0.5f);
        EXPECT_FLOAT_EQ(out[0].light.g, 0.65f);
        EXPECT_FLOAT_EQ(out[0].light.b, 0.13f);
        EXPECT_EQ(out[1].texture, 0x5fd);
        EXPECT_EQ(out[1].position, (glm::vec3{1040, 2050, 110}));
        EXPECT_EQ(out[1].scale, 0.25f);
        EXPECT_EQ(out[1].rotation, 142);
        EXPECT_EQ(out[2].rotation, 218);
        EXPECT_EQ(out[3].position, (glm::vec3{1070, 2080, 140}));
    }
}

TEST(CrywolfHelpersTest, ThePiercingKeepsBothInclusiveFrameEdgesAndBothHelpersInMap35)
{
    for (const int map : {34, 35})
        for (const float frame : {3.99f, 4.0f, 6.0f, 6.01f})
        {
            auto world = std::make_unique<Client::Scenes::WorldState>();
            auto actor = std::make_unique<Client::Scenes::ActorState>();
            actor->alive = true;
            actor->type = 0x1b3;
            actor->animation.action = 4;
            actor->animation.frame = frame;
            actor->position = {100, 200, 300};
            Client::Render::Models::Skeleton skeleton;
            Client::Crt::CrtRandom random(7);
            Client::Scenes::ModelEffectsPass pass{
                .actor = *actor, .slot = 3, .world = *world, .random = random, .skeleton = skeleton, .map = map};
            const auto stepped = Client::Scenes::StepTheModelEffects(pass);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            const int expected = frame >= 4 && frame <= 6 ? map == 35 ? 2 : 1 : 0;
            EXPECT_EQ(std::ranges::count_if(world->Map().effects.Effects(), [](const auto& e) { return e.alive; }),
                      expected);
            EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, 1).State());
        }
}

TEST(CrywolfHelpersTest, TheHeadAttackCreatesTheOwnedEarthquakeAboveTheScaleThreshold)
{
    for (const float scale : {1.25f, 1.2501f})
    {
        auto world = std::make_unique<Client::Scenes::WorldState>();
        auto actor = std::make_unique<Client::Scenes::ActorState>();
        actor->alive = true;
        actor->type = 0x1b1;
        actor->scale = scale;
        actor->animation.action = 3;
        actor->animation.frame = 2.5f;
        actor->bodyPose.assign(21, Client::Math::BoneTransform(1.0f));
        world->Runtime().namedBones = {{3, "Monster95_Head", 0}};
        world->Runtime().ownRecordDepth = 1;
        Client::Render::Models::Skeleton skeleton;
        skeleton.actions.assign(12, {.repeats = true, .speed = 0.25f, .keyCount = 10, .lockPositions = false});
        Client::World::FrameSprites sprites;
        Client::Crt::CrtRandom random(7);
        Client::Scenes::ModelEffectsPass pass{.actor = *actor,
                                              .slot = 3,
                                              .world = *world,
                                              .random = random,
                                              .skeleton = skeleton,
                                              .sprites = &sprites,
                                              .map = 34};
        const auto stepped = Client::Scenes::StepTheModelEffects(pass);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(sprites.Sprites()[0].texture, 0x5ac);
        const bool quake = scale > 1.25f;
        EXPECT_EQ(world->Map().effects.Effects()[0].alive, quake);
        EXPECT_EQ(world->OwnCharacter().bytef04, quake ? 1 : 0);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, quake ? 2 : 1).State());
        if (quake)
        {
            const auto& e = world->Map().effects.Effects()[0];
            EXPECT_EQ(e.type, 0xfa);
            EXPECT_EQ(e.state, 1);
            EXPECT_EQ(e.ownerContext, actor.get());
            EXPECT_EQ(e.transform.position, (glm::vec3{0, 0, 100 * scale}));
        }
    }
}
// SetAction conserva el frame si ya estaba en3. El gate llama antes de comprobar el frame5 y el bit1 de la fase.
TEST(CrywolfHelpersTest, TheBalgassAppliesBothRandomGateOutcomesBeforeItsFrameGate)
{
    for (const bool called : {false, true})
        for (const std::uint8_t initialAction : {std::uint8_t{0}, std::uint8_t{3}})
        {
            const auto seed = Client::Tests::FirstSeedWhere(
                [&](Client::Crt::CrtRandom& candidate)
                {
                    static_cast<void>(candidate.Next());
                    return (candidate.Next() % 100 == 0) == called;
                });
            auto world = std::make_unique<Client::Scenes::WorldState>();
            auto actor = std::make_unique<Client::Scenes::ActorState>();
            actor->alive = true;
            actor->type = 0x1b5;
            actor->animation.action = initialAction;
            actor->animation.frame = 5;
            actor->movementPhase = 1;
            world->Runtime().crywolf.stage = 4;
            world->Runtime().ownRecordDepth = 1;
            Client::Render::Models::Skeleton skeleton;
            skeleton.actions.resize(4);
            Client::Crt::CrtRandom random(seed);
            Client::Scenes::ModelEffectsPass pass{
                .actor = *actor, .slot = 3, .world = *world, .random = random, .skeleton = skeleton, .map = 34};
            const auto stepped = Client::Scenes::StepTheModelEffects(pass);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(world->Map().effects.Effects()[0].alive, initialAction == 3);
            EXPECT_EQ(actor->animation.frame, called && initialAction == 0 ? 0 : 5);
            EXPECT_EQ(actor->movementPhase, initialAction == 3 ? 0 : 1);
            EXPECT_EQ(random.State(), Client::Tests::AfterDraws(seed, 2).State());
        }
}

} // namespace
