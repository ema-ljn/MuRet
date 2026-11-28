#include "Actors/ActorRemains.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/HalfLifeMath.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Scenes/ActorEffectHooks.h"
#include "Terrain/GroundMarks.h"
#include "World/ModelEffects.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// Actors::LeaveTheRemains, 0x52e810: the +0x54 bone of the type's model, the ten ice shards of 0x161, the three that
// leave nothing and the two 0x5e3 blood marks of the others, with their rand() in order.
namespace
{
using Client::Actors::ActorRemainsContext;
using Client::Actors::LeaveTheRemains;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Math::BoneTransform;

constexpr std::uint32_t Seed = 0x13579bdfu;
// Two marks: x, y, the side, the turn and the life from 0x548fe0 with a free slot.
constexpr int BloodDraws = 2 * 5;
constexpr std::int32_t BloodMark = 0x5e3;
constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));

// What 0x52e810 reads and writes besides the character, on the heap (the 200 entries of 0x518cf0 and the 100 marks).
struct Remains
{
    Client::Models::Skeleton skeleton;
    Client::World::ModelEffects effects;
    Client::Scenes::ActorEffectHooks hooks;
    Client::Terrain::GroundMarkTable marks{};
    CrtRandom random{Seed};

    [[nodiscard]] ActorRemainsContext Context(float scale = 0.0f, const glm::vec3& origin = {})
    {
        return ActorRemainsContext{.skeleton = &skeleton,
                                   .modelScale = scale,
                                   .modelOrigin = origin,
                                   .effects = effects,
                                   .random = random,
                                   .marks = &marks,
                                   .effectHooks = &hooks};
    }

    [[nodiscard]] std::size_t LiveEffects() const
    {
        std::size_t live = 0;
        for (const auto& effect : effects.Effects())
            live += effect.alive ? 1 : 0;
        return live;
    }

    [[nodiscard]] std::size_t LiveMarks() const
    {
        std::size_t live = 0;
        for (const auto& mark : marks)
            live += mark.live ? 1 : 0;
        return live;
    }
};

ActorState Dead(std::uint16_t type)
{
    ActorState actor;
    actor.alive = true;
    actor.type = type;
    actor.position = {12345.0f, 6789.0f, 150.0f};
    actor.angles = {0.0f, 0.0f, 45.0f};
    actor.light = {0.5f, 0.6f, 0.7f};
    return actor;
}

// What 0x52e810 draws for a mark, in its order, and the life from 0x548fe0.
struct DrawnMark
{
    int x;
    int y;
    float size;
    float angle;
    std::int32_t life;
};

DrawnMark DrawMark(CrtRandom& draws)
{
    DrawnMark drawn{};
    drawn.x = draws.Next() % 100 - 50;
    drawn.y = draws.Next() % 100 - 50;
    drawn.size = static_cast<float>(static_cast<double>((draws.Next() & 3) + 8) * Tenth);
    drawn.angle = static_cast<float>(draws.Next() % 360);
    drawn.life = draws.Next() % 32 + 50;
    return drawn;
}

// 0x52e834..0x52e83b: with the model's +0x54 at −1 it does nothing, not even with 0x161, and draws no rand().
TEST(ActorRemainsTest, AModelWithoutTheHeadBoneLeavesNothing)
{
    for (const std::uint16_t type : {std::uint16_t{0x161}, std::uint16_t{0x153}, std::uint16_t{0x20a}})
    {
        SCOPED_TRACE(type);
        auto remains = std::make_unique<Remains>();
        ActorState actor = Dead(type);
        ASSERT_TRUE(LeaveTheRemains(actor, remains->Context()).HasValue());
        EXPECT_TRUE(actor.alive);
        EXPECT_EQ(remains->LiveEffects(), 0u);
        EXPECT_EQ(remains->LiveMarks(), 0u);
        EXPECT_EQ(remains->random.State(), Seed);
    }
}

// 0x52e841..0x52e889: Devias's ice monster (0x161, Monster16) turns off its live byte and drops ten 0xca shards at its
// position, with its angles and its light, five rand() each (ModelEffects::CreateIceShard); no marks.
TEST(ActorRemainsTest, TheIceMonsterOfDeviasBreaksIntoTenIceShards)
{
    auto remains = std::make_unique<Remains>();
    remains->skeleton.headBone = 0x13;
    ActorState actor = Dead(0x161);
    ASSERT_TRUE(LeaveTheRemains(actor, remains->Context(1.0f, {1.0f, 2.0f, 3.0f})).HasValue());
    EXPECT_FALSE(actor.alive);
    EXPECT_EQ(remains->LiveEffects(), 10u);
    EXPECT_EQ(remains->LiveMarks(), 0u);
    auto expected = std::make_unique<Client::World::ModelEffects>();
    CrtRandom draws(Seed);
    for (int shard = 0; shard < 10; ++shard)
        ASSERT_TRUE(expected->CreateIceShard(actor.position, actor.angles, actor.light, draws).has_value());
    EXPECT_EQ(remains->random.State(), draws.State());
    EXPECT_EQ(draws.State(), Client::Tests::AfterDraws(Seed, 50).State());
    for (std::size_t slot = 0; slot < 10; ++slot)
    {
        SCOPED_TRACE(slot);
        const auto& shard = remains->effects.Effects()[slot];
        const auto& wanted = expected->Effects()[slot];
        EXPECT_EQ(shard.type, Client::World::IceShardEffect);
        EXPECT_EQ(shard.subtype, 0);
        EXPECT_EQ(shard.transform.position, (glm::vec3{12345.0f, 6789.0f, 200.0f}));
        EXPECT_EQ(shard.light, actor.light);
        EXPECT_EQ(shard.life, wanted.life);
        EXPECT_EQ(shard.transform.scale, wanted.transform.scale);
        EXPECT_EQ(shard.transform.angles, wanted.transform.angles);
        EXPECT_EQ(shard.velocity, wanted.velocity);
        EXPECT_EQ(shard.turnDirection, wanted.turnDirection);
    }
}

// The shards are born through the effect hooks: without them, the monster stops after turning off its live byte.
TEST(ActorRemainsTest, TheIceShardsStopWithoutTheEffectHooks)
{
    auto remains = std::make_unique<Remains>();
    remains->skeleton.headBone = 0x13;
    ActorState actor = Dead(0x161);
    ActorRemainsContext context = remains->Context();
    context.effectHooks = nullptr;
    const auto stopped = LeaveTheRemains(actor, context);
    ASSERT_FALSE(stopped.HasValue());
    EXPECT_NE(stopped.Error().find("0x52e841"), std::string::npos) << stopped.Error();
    EXPECT_FALSE(actor.alive);
    EXPECT_EQ(remains->LiveEffects(), 0u);
    EXPECT_EQ(remains->random.State(), Seed);
}

// 0x52e88a..0x52e8a5: 0x159, 0x160 and 0x164 leave nothing, without rand().
TEST(ActorRemainsTest, ThreeMonstersLeaveNothing)
{
    for (const std::uint16_t type : {std::uint16_t{0x159}, std::uint16_t{0x160}, std::uint16_t{0x164}})
    {
        SCOPED_TRACE(type);
        auto remains = std::make_unique<Remains>();
        remains->skeleton.headBone = 0x14;
        ActorState actor = Dead(type);
        ASSERT_TRUE(LeaveTheRemains(actor, remains->Context()).HasValue());
        EXPECT_TRUE(actor.alive);
        EXPECT_EQ(remains->LiveEffects(), 0u);
        EXPECT_EQ(remains->LiveMarks(), 0u);
        EXPECT_EQ(remains->random.State(), Seed);
    }
}

// 0x52e8ab..0x52e975: twice, the offset (rand() % 100 − 50, rand() % 100 − 50, 0) from the +0x54 bone of the own bones,
// times the scale and plus the shared model's position (0x4ec110), the side ((rand() & 3) + 8) · 0.1f, the turn rand()
// % 360 and mark 0x5e3 with the character's light; 0x548fe0 draws its life. The Lorencia player with its bone 20 posed.
TEST(ActorRemainsTest, ThePlayerLeavesTwoBloodMarksAroundItsHeadBone)
{
    auto remains = std::make_unique<Remains>();
    remains->skeleton.headBone = 20;
    ActorState actor = Dead(0x20a);
    actor.bodyPose.assign(21, BoneTransform(1.0f));
    actor.bodyPose[20][3] = {10.0f, 20.0f, 30.0f};
    const glm::vec3 origin{13000.0f, 13500.0f, 100.0f};
    ASSERT_TRUE(LeaveTheRemains(actor, remains->Context(1.25f, origin)).HasValue());
    EXPECT_TRUE(actor.alive);
    EXPECT_EQ(remains->LiveEffects(), 0u);
    EXPECT_EQ(remains->LiveMarks(), 2u);
    CrtRandom draws(Seed);
    for (std::size_t slot = 0; slot < 2; ++slot)
    {
        SCOPED_TRACE(slot);
        const DrawnMark drawn = DrawMark(draws);
        const auto& mark = remains->marks[slot];
        EXPECT_EQ(mark.type, BloodMark);
        EXPECT_EQ(mark.position,
                  (glm::vec3{(static_cast<float>(drawn.x) + 10.0f) * 1.25f + origin.x,
                             (static_cast<float>(drawn.y) + 20.0f) * 1.25f + origin.y, 30.0f * 1.25f + origin.z}));
        EXPECT_EQ(mark.size, drawn.size);
        EXPECT_EQ(mark.angle, drawn.angle);
        EXPECT_EQ(mark.color, actor.light);
        EXPECT_EQ(mark.alpha, 1.0f);
        EXPECT_EQ(mark.life, drawn.life);
    }
    EXPECT_EQ(remains->random.State(), draws.State());
    EXPECT_EQ(draws.State(), Client::Tests::AfterDraws(Seed, BloodDraws).State());
}

// Without the marks table, the first mark stops at 0x52e96c after its four rand(), before 0x548fe0 draws its life.
TEST(ActorRemainsTest, WithoutTheMarksTableTheFirstMarkStops)
{
    auto remains = std::make_unique<Remains>();
    remains->skeleton.headBone = 5;
    ActorState actor = Dead(0x153);
    ActorRemainsContext context = remains->Context(1.0f, {500.0f, 600.0f, 0.0f});
    context.marks = nullptr;

    const auto left = LeaveTheRemains(actor, context);

    ASSERT_FALSE(left.HasValue());
    EXPECT_NE(left.Error().find("0x52e96c"), std::string::npos) << left.Error();
    EXPECT_EQ(remains->LiveMarks(), 0u);
    EXPECT_EQ(remains->random.State(), Client::Tests::AfterDraws(Seed, 4).State());
}

// A Devias monster without own bones (its drawing is not ported, or before the first one): the matrix at zero (Q-C318),
// and the two marks fall at the shared model's position, with the same rand().
TEST(ActorRemainsTest, AMonsterWithoutItsBonesBleedsOnItsModelPlacement)
{
    auto remains = std::make_unique<Remains>();
    remains->skeleton.headBone = 0x10;
    ActorState actor = Dead(0x15a);
    const glm::vec3 origin{22050.0f, 3050.0f, 120.0f};
    ASSERT_TRUE(LeaveTheRemains(actor, remains->Context(1.1f, origin)).HasValue());
    ASSERT_EQ(remains->LiveMarks(), 2u);
    CrtRandom draws(Seed);
    for (std::size_t slot = 0; slot < 2; ++slot)
    {
        const DrawnMark drawn = DrawMark(draws);
        EXPECT_EQ(remains->marks[slot].position, origin);
        EXPECT_EQ(remains->marks[slot].size, drawn.size);
        EXPECT_EQ(remains->marks[slot].angle, drawn.angle);
    }
    EXPECT_EQ(remains->random.State(), draws.State());
}

// With the marks table full, 0x548fe0 turns off the one with the least life without placing the new one or drawing its
// life (Q-C231): the second round of 0x52e810 finds that slot off and places it there, with its life.
TEST(ActorRemainsTest, AFullTableOfMarksMakesRoomForTheSecondOne)
{
    auto remains = std::make_unique<Remains>();
    remains->skeleton.headBone = 5;
    for (auto& mark : remains->marks)
        mark = Client::Terrain::GroundMark{.live = true, .type = BloodMark, .alpha = 1.0f, .life = 90};
    remains->marks[40].life = 55;
    ActorState actor = Dead(0x153);
    ASSERT_TRUE(LeaveTheRemains(actor, remains->Context(1.0f, {500.0f, 600.0f, 0.0f})).HasValue());
    CrtRandom draws(Seed);
    for (int draw = 0; draw < 4; ++draw)
        static_cast<void>(draws.Next());
    const DrawnMark second = DrawMark(draws);
    const auto& mark = remains->marks[40];
    EXPECT_TRUE(mark.live);
    EXPECT_EQ(mark.position, (glm::vec3{500.0f, 600.0f, 0.0f}));
    EXPECT_EQ(mark.size, second.size);
    EXPECT_EQ(mark.angle, second.angle);
    EXPECT_EQ(mark.life, second.life);
    EXPECT_EQ(remains->LiveMarks(), Client::Terrain::GroundMarkCount);
    EXPECT_EQ(remains->random.State(), Client::Tests::AfterDraws(Seed, 9).State());
}

// The errors: without the model loaded (0x52e834) and with a +0x54 bone outside the own bones already posed (0x4ec110
// would read other memory), before any rand().
TEST(ActorRemainsTest, WithoutTheModelOrWithTheBoneOutsideTheOwnBonesItCuts)
{
    auto remains = std::make_unique<Remains>();
    ActorState actor = Dead(0x153);
    ActorRemainsContext missing = remains->Context();
    missing.skeleton = nullptr;
    const auto unloaded = LeaveTheRemains(actor, missing);
    ASSERT_FALSE(unloaded.HasValue());
    EXPECT_NE(unloaded.Error().find("0x52e834"), std::string::npos) << unloaded.Error();

    remains->skeleton.headBone = 20;
    actor.bodyPose.assign(5, BoneTransform(1.0f));
    const auto outside = LeaveTheRemains(actor, remains->Context());
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x52e91c"), std::string::npos) << outside.Error();
    EXPECT_EQ(remains->LiveMarks(), 0u);
    EXPECT_EQ(remains->random.State(), Seed);
}
} // namespace
