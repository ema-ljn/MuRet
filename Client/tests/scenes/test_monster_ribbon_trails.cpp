#include "Monsters/Drawing/MonsterRibbonTrails.h"

#include "Core/Expected.h"

#include "Crt/CrtRandom.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Actors/CharacterStep.h"
#include "Scenes/GameDrawTail.h"
#include "Kernel/WorldState.h"
#include "Assets/FaunaModels.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace
{
class MonsterRibbonTrailsTest : public ::testing::Test
{
protected:
    MonsterRibbonTrailsTest()
    {
        m_actor.type = 0x1a7;
        m_actor.scale = 1.0f;
        m_actor.animation.action = 3;
        m_actor.animation.frame = 8.0f;
        m_actor.animation.previousAction = 3;
        m_actor.level = 12;
        m_skeleton.actions.resize(5, {.repeats = true, .speed = 1.0f, .keyCount = 12});
        m_skeleton.bones.resize(90);
        for (auto& bone : m_skeleton.bones)
        {
            bone.actions.resize(m_skeleton.actions.size());
            for (auto& keys : bone.actions)
                keys.resize(12);
        }
        m_world->PlaceModel(m_actor);
    }

    Client::Core::Expected<void, std::string> EmitEarly()
    {
        return Client::Monsters::Drawing::EmitEarlyMonsterRibbonTrails(
            m_actor, &m_skeleton, {.world = *m_world, .random = m_random, .faunaModels = &m_faunaModels});
    }

    Client::Core::Expected<void, std::string> EmitLate()
    {
        return Client::Monsters::Drawing::EmitLateMonsterRibbonTrails(
            m_actor, &m_skeleton, {.world = *m_world, .random = m_random, .faunaModels = &m_faunaModels});
    }

    void SetBonePosition(std::size_t boneIndex, std::uint8_t action, const glm::vec3& position)
    {
        for (auto& key : m_skeleton.bones[boneIndex].actions[action])
            key.position = position;
    }

    std::unique_ptr<Client::Kernel::WorldState> m_world = std::make_unique<Client::Kernel::WorldState>();
    Client::Kernel::ActorState m_actor;
    Client::Models::Skeleton m_skeleton;
    Client::Crt::CrtRandom m_random{0x12345678u};
    Client::Assets::FaunaModels m_faunaModels;
};

TEST_F(MonsterRibbonTrailsTest, Type1a7EmitsTwoTenSampleRibbonsWithLiteralBoneGeometry)
{
    m_skeleton.actions[3].speed = 2.0f;
    SetBonePosition(23, 3, {10, 20, 30});
    SetBonePosition(34, 3, {40, 50, 60});

    const auto emitted = EmitEarly();

    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    const auto& ribbons = m_world->DrawTail().ribbons;
    ASSERT_TRUE(ribbons[0].alive);
    ASSERT_TRUE(ribbons[1].alive);
    EXPECT_EQ(ribbons[0].count, 10);
    EXPECT_EQ(ribbons[1].count, 10);
    EXPECT_EQ(ribbons[0].life, 15);
    EXPECT_EQ(ribbons[1].life, 15);
    EXPECT_EQ(ribbons[0].type, 3);
    EXPECT_EQ(ribbons[1].type, 3);
    EXPECT_EQ(ribbons[0].key, 23);
    EXPECT_EQ(ribbons[1].key, 34);
    EXPECT_EQ(ribbons[0].first[0], (glm::vec3{10, 10, -50}));
    EXPECT_EQ(ribbons[0].second[0], (glm::vec3{40, -10, -200}));
    EXPECT_EQ(ribbons[0].light, (glm::vec3{1, 1, 1}));
    EXPECT_TRUE(ribbons[0].ownerContext == &m_actor);
    EXPECT_TRUE(ribbons[0].ownerEquipment == &m_actor.level);
    EXPECT_EQ(m_random.State(), 0x12345678u);
}

TEST_F(MonsterRibbonTrailsTest, Type1b3UsesBone25AndPublishesTenSamples)
{
    m_actor.type = 0x1b3;
    m_actor.animation.action = 3;
    SetBonePosition(25, 3, {7, 8, 9});
    m_world->PlaceModel(m_actor);

    const auto emitted = EmitEarly();

    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    const auto& ribbon = m_world->DrawTail().ribbons[0];
    ASSERT_TRUE(ribbon.alive);
    EXPECT_EQ(ribbon.count, 10);
    EXPECT_EQ(ribbon.key, 25);
    EXPECT_EQ(ribbon.first[0], (glm::vec3{7, 8, 129}));
    EXPECT_EQ(ribbon.second[0], (glm::vec3{7, 8, 9}));
}

TEST_F(MonsterRibbonTrailsTest, Type1b1InterleavesKeys80And84AcrossTwentyAppends)
{
    m_actor.type = 0x1b1;
    m_actor.animation.action = 4;
    m_actor.animation.previousAction = 4;
    m_actor.animation.previousKey = 6;
    m_skeleton.actions[4].speed = 2.0f;
    SetBonePosition(80, 4, {1, 2, 3});
    SetBonePosition(82, 4, {4, 5, 6});
    m_world->PlaceModel(m_actor);

    const auto emitted = EmitEarly();

    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    const auto& ribbons = m_world->DrawTail().ribbons;
    ASSERT_TRUE(ribbons[0].alive);
    ASSERT_TRUE(ribbons[1].alive);
    EXPECT_EQ(ribbons[0].key, 80);
    EXPECT_EQ(ribbons[1].key, 84);
    EXPECT_EQ(ribbons[0].count, 10);
    EXPECT_EQ(ribbons[1].count, 10);
    EXPECT_EQ(ribbons[0].first[0], (glm::vec3{1, 2, -87}));
    EXPECT_EQ(ribbons[0].second[0], (glm::vec3{1, 2, 3}));
    EXPECT_EQ(ribbons[1].first[0], (glm::vec3{4, 5, 96}));
    EXPECT_EQ(ribbons[1].second[0], (glm::vec3{4, 5, 6}));
}

TEST_F(MonsterRibbonTrailsTest, LateTypesRespectTheirFrameGateAndUseInterleavedKeyOrder)
{
    m_actor.type = 0x1b8;
    m_actor.animation.action = 3;
    m_actor.animation.frame = 5.0f;
    SetBonePosition(18, 3, {1, 0, 0});
    SetBonePosition(25, 3, {2, 0, 0});
    m_world->PlaceModel(m_actor);

    auto emitted = EmitLate();

    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    ASSERT_TRUE(m_world->DrawTail().ribbons[0].alive);
    ASSERT_TRUE(m_world->DrawTail().ribbons[1].alive);
    EXPECT_EQ(m_world->DrawTail().ribbons[0].key, 23);
    EXPECT_EQ(m_world->DrawTail().ribbons[1].key, 24);
    EXPECT_EQ(m_world->DrawTail().ribbons[0].count, 10);
    EXPECT_EQ(m_world->DrawTail().ribbons[1].count, 10);
    EXPECT_EQ(m_world->DrawTail().ribbons[0].light, (glm::vec3{1, 1, 1}));

    m_actor.animation.frame = 5.06f;
    m_world->DrawTail().ribbons = {};
    emitted = EmitLate();
    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    EXPECT_TRUE(m_world->DrawTail().ribbons[0].alive);

    m_actor.animation.frame = 5.07f;
    m_world->DrawTail().ribbons = {};
    emitted = EmitLate();
    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    EXPECT_FALSE(m_world->DrawTail().ribbons[0].alive);
}

TEST_F(MonsterRibbonTrailsTest, Type1b9EmitsThreeInterleavedColoredRibbonKeys)
{
    m_actor.type = 0x1b9;
    m_actor.animation.action = 4;
    m_world->PlaceModel(m_actor);

    const auto emitted = EmitLate();

    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    const auto& ribbons = m_world->DrawTail().ribbons;
    ASSERT_TRUE(ribbons[0].alive);
    ASSERT_TRUE(ribbons[1].alive);
    ASSERT_TRUE(ribbons[2].alive);
    EXPECT_EQ(ribbons[0].key, 23);
    EXPECT_EQ(ribbons[1].key, 24);
    EXPECT_EQ(ribbons[2].key, 25);
    EXPECT_EQ(ribbons[0].count, 10);
    EXPECT_EQ(ribbons[1].count, 10);
    EXPECT_EQ(ribbons[2].count, 10);
    EXPECT_EQ(ribbons[0].light, (glm::vec3{0.5f, 0.5f, 0.0f}));
    EXPECT_EQ(ribbons[0].first[0], (glm::vec3{20, 0, 0}));
}

TEST_F(MonsterRibbonTrailsTest, Type1baUsesOneGreenTrailOnActionThreeOnly)
{
    m_actor.type = 0x1ba;
    m_actor.animation.action = 3;
    m_world->PlaceModel(m_actor);

    const auto emitted = EmitLate();

    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    const auto& ribbon = m_world->DrawTail().ribbons[0];
    ASSERT_TRUE(ribbon.alive);
    EXPECT_EQ(ribbon.key, 24);
    EXPECT_EQ(ribbon.count, 10);
    EXPECT_EQ(ribbon.light, (glm::vec3{0.3f, 0.3f, 0.3f}));
    EXPECT_EQ(ribbon.first[0], (glm::vec3{0, 0, 20}));

    m_actor.animation.action = 4;
    m_world->DrawTail().ribbons = {};
    const auto ignored = EmitLate();
    ASSERT_TRUE(ignored.HasValue()) << ignored.Error();
    EXPECT_FALSE(m_world->DrawTail().ribbons[0].alive);
}

TEST_F(MonsterRibbonTrailsTest, FullPoolStillPosesEverySampleAndAppendsToFirstMatchingOwnerKey)
{
    m_actor.type = 0x1b3;
    m_actor.animation.action = 3;
    m_actor.animation.previousKey = 7;
    m_world->PlaceModel(m_actor);
    for (std::size_t key = 0; key < m_skeleton.bones[25].actions[3].size(); ++key)
        m_skeleton.bones[25].actions[3][key].position.x = static_cast<float>(key) * 10.0f;
    for (auto& ribbon : m_world->DrawTail().ribbons)
        ribbon.alive = true;
    auto& retained = m_world->DrawTail().ribbons[57];
    retained.ownerContext = &m_actor;
    retained.ownerEquipment = &m_actor.level;
    retained.key = 25;
    retained.count = 2;
    retained.life = 23;

    const auto emitted = EmitEarly();

    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    EXPECT_EQ(retained.count, 12);
    EXPECT_EQ(retained.life, 23);
    EXPECT_EQ(m_world->PoseCarry().bones.size(), m_skeleton.bones.size());
    EXPECT_NEAR(m_world->PoseCarry().bones[25][3].x, 70.0f, 0.01f);
    EXPECT_EQ(m_random.State(), 0x12345678u);
}
} // namespace
