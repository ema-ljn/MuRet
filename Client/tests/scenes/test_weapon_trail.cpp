#include "Scenes/WeaponTrail.h"

#include "Core/Expected.h"
#include <string>

#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldState.h"
#include "World/Particles.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace
{
using Client::Render::Models::BoneTransform;

class WeaponTrailTest : public ::testing::Test
{
protected:
    WeaponTrailTest()
    {
        m_actor = Client::Tests::UnarmedPlayer(1);
        m_actor.position = {100, 200, 300};
        m_actor.scale = 2.0f;
        m_actor.animation.action = 0x23;
        m_actor.animation.frame = 4.0f;
        m_actor.animation.previousAction = 0x23;
        m_actor.hands[0].model = 0x222;
        m_actor.hands[0].level = 7;
        m_actor.hands[0].bone = 0;
        m_actor.hands[1].bone = 1;
        m_skeleton.actions.resize(0x8e, {.repeats = true, .speed = 0.0f, .keyCount = 10});
        m_skeleton.bones.resize(2);
        for (auto& bone : m_skeleton.bones)
        {
            bone.actions.resize(m_skeleton.actions.size());
            for (auto& keys : bone.actions)
                keys.resize(10);
        }
        m_world->PlaceModel(m_actor);
    }

    Client::Core::Expected<void, std::string> Emit()
    {
        return Client::Scenes::EmitWeaponTrail(
            m_actor, &m_skeleton,
            {.world = *m_world, .random = m_random, .playerSkeleton = &m_skeleton, .particles = &m_world->Particles()});
    }

    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
    Client::Scenes::ActorState m_actor;
    Client::Render::Models::Skeleton m_skeleton;
    Client::Render::Models::CrtRandom m_random{0x12345678u};
};

// 4fd138..4fd13f: el194 que no está en acción3 salta a los helpers, sin probar el arma.
TEST_F(WeaponTrailTest, Type194RejectsActionFourBeforeTheGenericSwordFallback)
{
    m_actor.type = 0x194;
    m_actor.animation.action = 4;
    m_world->PlaceModel(m_actor);

    const auto emitted = Emit();

    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    EXPECT_FALSE(m_world->DrawTail().ribbons[0].alive);
}

// 4fd3a4..4fd47a: esta espada reutiliza la matriz propia y la colocación compartida, una vez.
TEST_F(WeaponTrailTest, TheSingleSampleUsesTheRetainedOwnBoneAndSharedPlacement)
{
    m_actor.hands[0].model = 0x215;
    m_actor.bodyPose.assign(1, BoneTransform(1.0f));
    m_actor.bodyPose[0][3] = {3, 4, 5};
    m_actor.position = {900, 800, 700}; // No se vuelve a publicar en el modelo.

    const auto emitted = Emit();

    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    const auto& ribbon = m_world->DrawTail().ribbons[0];
    ASSERT_TRUE(ribbon.alive);
    EXPECT_EQ(ribbon.count, 1);
    EXPECT_EQ(ribbon.life, 30);
    EXPECT_EQ(ribbon.first[0], (glm::vec3{106, 168, 310}));
    EXPECT_EQ(ribbon.second[0], (glm::vec3{106, -32, 310}));
}

// 4fcf3e..4fcf4a: 25/26 seleccionan nivel/modelo y hueso de la izquierda para ambos extremos.
TEST_F(WeaponTrailTest, TheLeftSwingUsesTheLeftHandForBothEndpoints)
{
    m_actor.animation.action = 0x25;
    m_actor.hands[0].model = 0x21f; // El rojo de la derecha no participa.
    m_actor.hands[1].model = 0x215;
    m_actor.hands[1].level = 0;
    m_actor.bodyPose.assign(2, BoneTransform(1.0f));
    m_actor.bodyPose[1][3] = {3, 4, 5};
    m_world->PlaceModel(m_actor);

    const auto emitted = Emit();

    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    const auto& ribbon = m_world->DrawTail().ribbons[0];
    ASSERT_TRUE(ribbon.alive);
    EXPECT_EQ(ribbon.first[0], (glm::vec3{106, 168, 310}));
    EXPECT_EQ(ribbon.second[0], (glm::vec3{106, -32, 310}));
    EXPECT_EQ(ribbon.light, (glm::vec3{0.8f, 0.8f, 0.8f}));
}

// 4fd487 consulta la acción publicada, aunque el cuerpo haya cambiado ya la del actor.
TEST_F(WeaponTrailTest, TenSamplesUseThePublishedModelActionAndRetainTheFinalPose)
{
    m_skeleton.actions[0x23].speed = 1.0f;
    m_skeleton.actions[0x29].speed = 2.0f;
    for (std::size_t key = 0; key < 10; ++key)
        m_skeleton.bones[0].actions[0x23][key].position.x = static_cast<float>(key) * 10.0f;
    m_actor.animation.action = 0x29;

    const auto emitted = Emit();

    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    const auto& ribbon = m_world->DrawTail().ribbons[0];
    ASSERT_TRUE(ribbon.alive);
    EXPECT_EQ(ribbon.type, 1);
    EXPECT_EQ(ribbon.count, 10);
    EXPECT_EQ(ribbon.life, 15);
    EXPECT_NEAR(ribbon.first[0].x, 154.0f, 0.001f);
    EXPECT_EQ(ribbon.first[9].x, 100.0f);
    EXPECT_NEAR(m_world->PoseCarry().bones[0][3].x, 154.0f, 0.001f);
    EXPECT_EQ(m_world->FaunaResources().PoseKey(m_actor.type), 3);
    EXPECT_EQ(m_random.State(), 0x12345678u);
}

TEST_F(WeaponTrailTest, AFullPoolStillAppendsToTheExistingOwnerWithoutRestartingLife)
{
    for (auto& ribbon : m_world->DrawTail().ribbons)
        ribbon.alive = true;
    auto& retained = m_world->DrawTail().ribbons[57];
    retained.ownerContext = &m_actor;
    retained.ownerEquipment = &m_actor.level;
    retained.count = 2;
    retained.life = 23;

    const auto emitted = Emit();

    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    EXPECT_EQ(retained.count, 12);
    EXPECT_EQ(retained.life, 23);
    EXPECT_EQ(retained.first[0], (glm::vec3{100, 160, 300}));
    EXPECT_EQ(m_random.State(), 0x12345678u);
}
// 4fd575/4fd585: la primera partícula impar va después de dos poses y de una sola cinta.
TEST_F(WeaponTrailTest, TheUnportedType194ParticleKeepsTheCompletedPrefix)
{
    m_actor.type = 0x194;
    m_actor.animation.action = m_actor.animation.previousAction = 3;
    m_skeleton.actions[3].speed = 1.0f;
    m_world->PlaceModel(m_actor);

    const auto emitted = Emit();

    ASSERT_FALSE(emitted.HasValue());
    EXPECT_NE(emitted.Error().find("0x4fd585"), std::string::npos);
    const auto& ribbon = m_world->DrawTail().ribbons[0];
    ASSERT_TRUE(ribbon.alive);
    EXPECT_EQ(ribbon.count, 1);
    EXPECT_EQ(ribbon.type, 2);
    EXPECT_FLOAT_EQ(m_world->FaunaResources().PoseFrame(m_actor.type), 3.1f);
    EXPECT_EQ(m_world->FaunaResources().PoseKey(m_actor.type), 3);
}

// Selección literal 4fcf87..4fd22b: fronteras inclusivas y prioridad de acción sobre familia.
TEST_F(WeaponTrailTest, ThePlayerSelectorsCoverTheirActionAndWeaponBoundaries)
{
    struct Case
    {
        std::uint16_t model;
        std::uint8_t action;
        std::int32_t type;
        int count;
    };
    constexpr std::array cases{
        Case{0xffff, 0x84, 2, 10}, Case{0xffff, 0x85, 2, 10}, Case{0xffff, 0x4c, 2, 10}, Case{0x220, 0x39, 1, 10},
        Case{0x227, 0x3a, 1, 10},  Case{0x231, 0x3b, 1, 10},  Case{0x221, 0x39, 2, 10},  Case{0x617, 0x3c, 1, 10},
        Case{0x618, 0x3c, 2, 10},  Case{0x211, 0x23, 0, 0},   Case{0x212, 0x23, 0, 10},  Case{0x411, 0x28, 0, 10},
        Case{0x412, 0x23, 0, 0},   Case{0x223, 0x29, 6, 10},  Case{0x223, 0x8d, 6, 10},  Case{0x222, 0x29, 1, 10},
        Case{0x222, 0x8d, 1, 10},  Case{0x222, 0x22, 0, 0},   Case{0x222, 0x2a, 0, 0},   Case{0x222, 0x38, 0, 0},
        Case{0x222, 0x8c, 0, 0},   Case{0x415, 0x38, 2, 10},  Case{0x416, 0x38, 0, 0},   Case{0x417, 0x38, 2, 10},
        Case{0x811, 0x38, 2, 10},  Case{0x812, 0x38, 0, 0},   Case{0x812, 0x2a, 0, 10},  Case{0x815, 0x2d, 1, 10},
        Case{0x816, 0x2a, 0, 1},   Case{0x81c, 0x2d, 0, 10},  Case{0xa11, 0x2c, 0, 10},  Case{0xa12, 0x2a, 0, 0},
        Case{0x215, 0x23, 0, 1},   Case{0x218, 0x23, 0, 1},   Case{0x21b, 0x23, 0, 1},   Case{0x21d, 0x23, 0, 1}};
    for (const auto& probe : cases)
    {
        SCOPED_TRACE(::testing::Message() << probe.model << "/" << static_cast<int>(probe.action));
        m_world->DrawTail().ribbons = {};
        m_actor.hands[0].model = m_actor.hands[1].model = probe.model;
        m_actor.animation.action = m_actor.animation.previousAction = probe.action;
        m_world->PlaceModel(m_actor);

        const auto emitted = Emit();

        ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
        const auto& ribbon = m_world->DrawTail().ribbons[0];
        EXPECT_EQ(ribbon.alive, probe.count != 0);
        if (probe.count == 0)
            continue;
        EXPECT_EQ(ribbon.type, probe.type);
        EXPECT_EQ(ribbon.count, probe.count);
    }
}

// 4fd2e4..4fd3a4: niveles, nivel99 y arma roja preceden al color blanco por tipo.
TEST_F(WeaponTrailTest, TheColorBranchesRetainTheirNativePriority)
{
    struct Case
    {
        std::uint8_t level;
        glm::vec3 color;
    };
    const std::array cases{Case{0, {0.8f, 0.8f, 0.8f}}, Case{2, {0.8f, 0.8f, 0.8f}}, Case{3, {1, 0.2f, 0.2f}},
                           Case{4, {1, 0.2f, 0.2f}},    Case{5, {0.2f, 0.4f, 1}},    Case{6, {0.2f, 0.4f, 1}},
                           Case{7, {1, 0.6f, 0.2f}},    Case{255, {1, 0.6f, 0.2f}},  Case{99, {0.3f, 0.2f, 1}}};
    for (const auto& probe : cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.level));
        m_world->DrawTail().ribbons = {};
        m_actor.hands[0].level = probe.level;
        ASSERT_TRUE(Emit().HasValue());
        EXPECT_EQ(m_world->DrawTail().ribbons[0].light, probe.color);
    }
    m_actor.hands[0].model = 0x21f;
    m_actor.animation.action = 0x29;
    m_world->PlaceModel(m_actor);
    m_world->DrawTail().ribbons = {};
    ASSERT_TRUE(Emit().HasValue());
    EXPECT_EQ(m_world->DrawTail().ribbons[0].type, 1);
    EXPECT_EQ(m_world->DrawTail().ribbons[0].light, (glm::vec3{1, 0.2f, 0.2f}));
}

// 4fd10b..4fd22b: rechazos especiales no recaen en espada, subtype9 retiene arma/nivel.
TEST_F(WeaponTrailTest, TheMonsterSelectorsRespectTheirPrecedenceAndRetainedEquipment)
{
    struct Case
    {
        std::uint16_t type;
        std::uint8_t action;
        std::uint16_t word376;
        std::int32_t subtype;
        std::uint16_t model;
        std::int32_t ribbonType;
        int count;
        glm::vec3 color;
    };
    const glm::vec3 white{1, 1, 1};
    const glm::vec3 purple{0.3f, 0.2f, 1};
    const glm::vec3 orange{1, 0.6f, 0.2f};
    const glm::vec3 red{1, 0.2f, 0.2f};
    const std::array cases{Case{0x160, 3, 0x47, 0, 0xffff, 6, 1, white},  Case{0x160, 4, 0x4a, 0, 0xffff, 6, 1, white},
                           Case{0x160, 3, 0x12d, 0, 0xffff, 6, 1, white}, Case{0x160, 2, 0x47, 0, 0x212, 0, 0, {}},
                           Case{0x194, 4, 0, 0, 0x212, 0, 0, {}},         Case{0x194, 3, 0, 0, 0x212, 2, 10, white},
                           Case{0x195, 4, 0, 0, 0xffff, 0, 10, purple},   Case{0x195, 3, 0, 0, 0x21f, 0, 10, red},
                           Case{0x197, 3, 0, 0, 0x21f, 0, 10, purple},    Case{0x197, 4, 0, 9, 0x21f, 2, 10, red},
                           Case{0x160, 3, 0, 0, 0x212, 0, 1, orange},     Case{0x160, 3, 0, 0, 0x411, 0, 1, orange},
                           Case{0x160, 4, 0, 0, 0x412, 0, 0, {}}};
    // 194: la tabla de partículas llena omite sus altas impares y conserva las diez cintas.
    for (auto& particle : m_world->Particles().All())
        particle.alive = true;
    for (const auto& probe : cases)
    {
        SCOPED_TRACE(probe.type);
        m_world->DrawTail().ribbons = {};
        m_actor.type = probe.type;
        m_actor.animation.action = m_actor.animation.previousAction = probe.action;
        m_actor.word376 = probe.word376;
        m_actor.subtype = probe.subtype;
        m_actor.hands[0].model = probe.model;
        m_actor.hands[0].level = 7;
        m_world->PlaceModel(m_actor);

        const auto emitted = Emit();

        ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
        const auto& ribbon = m_world->DrawTail().ribbons[0];
        EXPECT_EQ(ribbon.alive, probe.count != 0);
        if (probe.count == 0)
            continue;
        EXPECT_EQ(ribbon.count, probe.count);
        EXPECT_EQ(ribbon.type, probe.ribbonType);
        EXPECT_EQ(ribbon.light, probe.color);
    }
}

} // namespace
