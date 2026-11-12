#include "Scenes/GameCharacterEffects.h"

#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameDrawTail.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// La cola de 0x50ba30 (0x50ea20..0x510ab6) y el cuervo de 0x420df0, contra el ASM de c32bbd5f. Los floats
// esperados salen de la aritmética del ASM en double y float (los bits, en hexadecimal); los rand(), del
// CRT con la semilla 1.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::GameCharacterEffectsPass;
using Client::Scenes::GameRavenPass;

float Float(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

std::uint32_t AfterRandoms(std::size_t count)
{
    CrtRandom random{1};
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random.State();
}

// Cuántos rand() hay que sacar con la semilla 1 para que el siguiente cumpla `wanted`.
template <typename Predicate> std::size_t RandomsBefore(Predicate wanted)
{
    CrtRandom random{1};
    for (std::size_t count = 0;; ++count)
    {
        if (wanted(random.Next()))
            return count;
    }
}

// Cuántos rand() hay que sacar con la semilla 1 para que los dos siguientes cumplan `first` y `second`.
template <typename First, typename Second> std::size_t RandomsBeforePair(First first, Second second)
{
    CrtRandom random{1};
    int previous = random.Next();
    for (std::size_t count = 0;; ++count)
    {
        const int next = random.Next();
        if (first(previous) && second(next))
            return count;
        previous = next;
    }
}

// Los rand() de la semilla 1 desde el que sigue a los primeros `skipped`.
std::vector<int> RandomsAfter(std::size_t skipped, std::size_t count)
{
    CrtRandom random{1};
    for (std::size_t call = 0; call < skipped; ++call)
        static_cast<void>(random.Next());
    std::vector<int> values;
    for (std::size_t call = 0; call < count; ++call)
        values.push_back(random.Next());
    return values;
}

std::vector<Client::World::Particle> LiveParticles(Client::World::Particles& pool)
{
    std::vector<Client::World::Particle> live;
    for (const auto& particle : pool.All())
    {
        if (particle.alive)
            live.push_back(particle);
    }
    return live;
}

std::vector<Client::Scenes::JointState> LiveJoints(std::span<const Client::Scenes::JointState> joints)
{
    std::vector<Client::Scenes::JointState> live;
    for (const auto& joint : joints)
    {
        if (joint.alive)
            live.push_back(joint);
    }
    return live;
}

std::vector<Client::World::Sprite> LiveSprites(Client::World::FrameSprites& table)
{
    std::vector<Client::World::Sprite> live;
    for (const auto& sprite : table.Sprites())
    {
        if (sprite.alive)
            live.push_back(sprite);
    }
    return live;
}

class GameCharacterEffectsTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_actor.type = 0x20a;
        m_actor.scale = 1.0f;
        m_actor.alpha = 1.0f;
        m_actor.position = {100.0f, 200.0f, 300.0f};
        m_actor.light = {0.1f, 0.2f, 0.3f};
        for (auto& part : m_actor.body)
            part.model = 0xffff;
        m_actor.hands[0].bone = 0x21;
        m_actor.hands[1].bone = 0x2a;
        // El hueso k sin giro y en (0, 0, k): el punto de 0x4ec110 es el corrimiento más (0, 0, k) más la
        // posición del personaje.
        m_body.assign(48, Client::Render::Models::BoneTransform(1.0f));
        for (std::size_t bone = 0; bone < m_body.size(); ++bone)
            m_body[bone][3] = {0.0f, 0.0f, static_cast<float>(bone)};
    }

    GameCharacterEffectsPass Pass()
    {
        return {.body = m_body,
                .random = m_random,
                .sprites = &m_sprites,
                .particles = &m_particles,
                .joints = m_joints,
                .effects = &m_effects,
                .slot = Slot,
                .scan = {},
                .worldClock = 1000.0f,
                .map = 0,
                .logicSteps = 1,
                .originalFrame = true};
    }

    glm::vec3 At(std::int32_t bone, const glm::vec3& offset) const
    {
        return offset + glm::vec3{0.0f, 0.0f, static_cast<float>(bone)} + m_actor.position;
    }

    static constexpr std::size_t Slot = 3;
    ActorState m_actor;
    std::vector<Client::Render::Models::BoneTransform> m_body;
    CrtRandom m_random{1};
    Client::World::FrameSprites m_sprites;
    Client::World::Particles m_particles;
    std::vector<Client::Scenes::JointState> m_joints = std::vector<Client::Scenes::JointState>(16);
    Client::World::ModelEffects m_effects;
};

TEST_F(GameCharacterEffectsTest, TheMageSkinPutsTheSparkAndTheShinyOnBoneNineteenWithoutRandoms)
{
    m_actor.skin = 8;

    const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites(m_sprites);
    ASSERT_EQ(sprites.size(), 2u);
    const glm::vec3 point = At(19, {-4.0f, 11.0f, 0.0f});
    EXPECT_EQ(sprites[0].texture, 0x5c6);
    EXPECT_EQ(sprites[0].position, point);
    EXPECT_EQ(sprites[0].scale, Float(0x3f19999au));
    EXPECT_EQ(sprites[0].light, glm::vec3(1.0f));
    EXPECT_EQ(sprites[0].blend, 0);
    // sin(1000 · 0,001f) · 0,4f.
    EXPECT_EQ(sprites[1].texture, 0x5fd);
    EXPECT_EQ(sprites[1].position, point);
    EXPECT_EQ(sprites[1].scale, Float(0x3eac5551u));
    EXPECT_EQ(sprites[1].light, glm::vec3(1.0f));
    EXPECT_EQ(m_random.State(), 1u);
}

TEST_F(GameCharacterEffectsTest, TheMageSkinNeedsClassZeroTheSkinBitsAndTheVisibleAttachments)
{
    struct Case
    {
        std::uint8_t characterClass;
        std::uint8_t skin;
        std::uint32_t flags;
    };
    for (const Case probe : {Case{1, 8, 0}, Case{8, 8, 0}, Case{0, 0x10, 0}, Case{0, 8, 0x4000}})
    {
        SCOPED_TRACE(testing::Message() << int{probe.characterClass} << ' ' << int{probe.skin} << ' ' << probe.flags);
        m_sprites = {};
        m_actor.characterClass = probe.characterClass;
        m_actor.skin = probe.skin;
        m_actor.flags78 = probe.flags;

        const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        // La clase 8 tiene (8 & 7) en 0: también es del mago.
        EXPECT_EQ(LiveSprites(m_sprites).size(), probe.characterClass == 8 ? 2u : 0u);
    }
}

TEST_F(GameCharacterEffectsTest, TheLightningActionOutsideTheDarkLordPutsThreeSpritesOnTheRightHand)
{
    m_actor.animation.action = 0x94;
    m_actor.animation.frame = 3.0f;
    m_actor.hands[0].bone = 20;
    GameCharacterEffectsPass pass = Pass();
    pass.worldClock = 1234.5f;

    const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites(m_sprites);
    ASSERT_EQ(sprites.size(), 3u);
    const glm::vec3 point = At(20, {});
    for (const auto& sprite : sprites)
    {
        EXPECT_EQ(sprite.texture, 0x5d5);
        EXPECT_EQ(sprite.position, point);
    }
    // s = 3 · 0,1f; s · 0,3f, s y s · 2,5, con ∓ftol(1234,5) · 0,1f de giro.
    EXPECT_EQ(sprites[0].scale, Float(0x3db851ecu));
    EXPECT_EQ(sprites[0].light, glm::vec3(Float(0x3dcccccdu), Float(0x3dcccccdu), 1.0f));
    EXPECT_EQ(sprites[0].rotation, 0.0f);
    EXPECT_EQ(sprites[1].scale, Float(0x3e99999au));
    EXPECT_EQ(sprites[1].light, m_actor.light);
    EXPECT_EQ(sprites[1].rotation, Float(0xc2f6cccdu));
    EXPECT_EQ(sprites[2].scale, Float(0x3f400000u));
    EXPECT_EQ(sprites[2].light, m_actor.light);
    EXPECT_EQ(sprites[2].rotation, Float(0x42f6cccdu));
    EXPECT_EQ(m_random.State(), 1u);
}

TEST_F(GameCharacterEffectsTest, TheActionWindowsCutOnlyInsideTheirFramesOfAnOriginalFrame)
{
    struct Case
    {
        std::uint8_t characterClass;
        std::uint8_t action;
        float frame;
        bool originalFrame;
        const char* cut;
    };
    constexpr std::array cases{
        Case{4, 0x94, 1.2f, true, "0x50f9b3"}, Case{0, 0x51, 1.5f, true, "0x50f9b3"},
        Case{0, 0x5b, 7.0f, true, "0x50fb64"}, Case{4, 0x94, 7.9f, true, "0x50fb64"},
        Case{4, 0x94, 1.6f, true, nullptr},    Case{4, 0x94, 1.1f, true, nullptr},
        Case{4, 0x94, 8.0f, true, nullptr},    Case{4, 0x94, 1.3f, false, nullptr},
        Case{0, 0x51, 7.5f, false, nullptr},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << int{probe.action} << ' ' << probe.frame << ' ' << probe.originalFrame);
        m_sprites = {};
        m_actor.characterClass = probe.characterClass;
        m_actor.animation.action = probe.action;
        m_actor.animation.frame = probe.frame;
        GameCharacterEffectsPass pass = Pass();
        pass.originalFrame = probe.originalFrame;

        const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

        if (probe.cut == nullptr)
        {
            ASSERT_TRUE(result.HasValue()) << result.Error();
        }
        else
        {
            ASSERT_FALSE(result.HasValue());
            EXPECT_NE(result.Error().find(probe.cut), std::string::npos) << result.Error();
        }
        EXPECT_TRUE(LiveSprites(m_sprites).empty());
    }
}

TEST_F(GameCharacterEffectsTest, TheGlowFlagsCutWithTheExactConditionOfTheAssembly)
{
    struct Case
    {
        std::uint32_t flags;
        bool cuts;
    };
    for (const Case probe : {Case{0x4, true}, Case{0x2000000, true}, Case{0x2000004, true}, Case{0x4004, false},
                             Case{0x2004000, false}, Case{0x800, false}, Case{0x1, false}})
    {
        SCOPED_TRACE(probe.flags);
        m_actor.flags78 = probe.flags;
        GameCharacterEffectsPass pass = Pass();
        pass.originalFrame = false;

        const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

        EXPECT_EQ(!result.HasValue(), probe.cuts);
        if (probe.cuts)
        {
            EXPECT_NE(result.Error().find("0x50fbd5"), std::string::npos) << result.Error();
        }
    }
}

// 0x50fd74..0x50fdd5: con +0x358, cada dibujo cuenta; con 100 o más y la acción por debajo de 0xd, el 5ac/3 en el
// personaje (0x518cf0 → 0x51e489) y el contador vuelve a 0 antes de contar. Sin rand().
TEST_F(GameCharacterEffectsTest, TheSetCounterCreatesTheFullSetFlareAtTheHundredthWithALowAction)
{
    m_actor.byte358 = 1;
    m_actor.counter35c = 0;
    m_actor.angles = {0.0f, 0.0f, 45.0f};

    ASSERT_TRUE(Client::Scenes::DrawGameCharacterEffects(m_actor, Pass()).HasValue());
    EXPECT_EQ(m_actor.counter35c, 1);

    m_actor.counter35c = 100;
    m_actor.animation.action = 0xd;
    ASSERT_TRUE(Client::Scenes::DrawGameCharacterEffects(m_actor, Pass()).HasValue());
    EXPECT_EQ(m_actor.counter35c, 101);
    EXPECT_FALSE(m_effects.Effects()[0].alive);

    m_actor.animation.action = 0xc;
    const auto created = Client::Scenes::DrawGameCharacterEffects(m_actor, Pass());
    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(m_actor.counter35c, 1);
    const Client::World::ModelEffect& flare = m_effects.Effects()[0];
    EXPECT_TRUE(flare.alive);
    EXPECT_EQ(flare.type, 0x5ac);
    EXPECT_EQ(flare.subtype, 3);
    EXPECT_EQ(flare.transform.position, m_actor.position);
    EXPECT_EQ(flare.transform.angles, m_actor.angles);
    EXPECT_EQ(flare.transform.scale, Float(0x3f666666u));
    EXPECT_EQ(flare.light, glm::vec3(Float(0x3e4ccccdu), Float(0x3f333333u), Float(0x3f666666u)));
    EXPECT_EQ(flare.life, 30);
    EXPECT_EQ(flare.speed, 0.0f);
    EXPECT_EQ(flare.disturbanceState, 0xffff);
    EXPECT_TRUE(flare.hasOwner);
    EXPECT_EQ(flare.ownerContext, &m_actor);
    EXPECT_FALSE(m_effects.Effects()[1].alive);
    EXPECT_EQ(m_random.State(), 1u);

    // Entre frames, sobre la copia del caller: vuelve a 0 y cuenta, sin el efecto.
    m_actor.counter35c = 100;
    GameCharacterEffectsPass projected = Pass();
    projected.originalFrame = false;
    ASSERT_TRUE(Client::Scenes::DrawGameCharacterEffects(m_actor, projected).HasValue());
    EXPECT_EQ(m_actor.counter35c, 1);
    EXPECT_FALSE(m_effects.Effects()[1].alive);
    EXPECT_EQ(m_random.State(), 1u);

    // Sin la tabla de efectos, corta con la dirección de la llamada.
    m_actor.counter35c = 100;
    GameCharacterEffectsPass missing = Pass();
    missing.effects = nullptr;
    const auto cut = Client::Scenes::DrawGameCharacterEffects(m_actor, missing);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x50fdc3"), std::string::npos) << cut.Error();
}

// 0x52a9ad: el rand() de la luz, +0xd8 uno más y, con la vida múltiplo de 15, el giro z 180 más y la posición a 50 del
// dueño con el seno de la suma sin redondear y el coseno del float guardado, 20 más arriba, con un 619/9 ahí. La cola
// común descuenta la vida: con 30, dos joints y muere en el paso 30.
TEST_F(GameCharacterEffectsTest, TheFullSetFlareCirclesItsOwnerEveryFifteenStepsWithAnEnergyJoint)
{
    using Client::Render::Models::Detail::CosineProductPC53;
    using Client::Render::Models::Detail::SineProductPC53;
    m_actor.angles = {0.0f, 0.0f, 33.3f};
    ASSERT_TRUE(m_effects.CreateFullSetFlare(m_actor.position, m_actor.angles, glm::vec3(0.5f), &m_actor).has_value());
    Client::World::ModelEffect& flare = m_effects.Effects()[0];
    const float tenth = Float(0x3dcccccdu);

    std::vector<glm::vec3> expected;
    for (int step = 1; step <= 30; ++step)
    {
        SCOPED_TRACE(step);
        const glm::vec3 before = flare.transform.position;
        const float yaw = flare.transform.angles.z;
        if (step == 16)
            m_actor.position = {130.0f, 170.0f, 290.0f};
        const auto stepped = Client::Scenes::StepFullSetFlare(flare, m_joints, m_random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(m_random.State(), AfterRandoms(static_cast<std::size_t>(step)));
        EXPECT_EQ(flare.speed, static_cast<float>(step));
        if (step == 1 || step == 16)
        {
            const double turned = static_cast<double>(yaw) + 180.0;
            const float storedYaw = static_cast<float>(turned);
            EXPECT_EQ(flare.transform.angles.z, storedYaw);
            const glm::vec3 at{
                static_cast<float>(*SineProductPC53(turned * static_cast<double>(tenth), 50.0f) +
                                   static_cast<double>(m_actor.position.x)),
                static_cast<float>(
                    *CosineProductPC53(static_cast<double>(storedYaw) * static_cast<double>(tenth), 50.0f) +
                    static_cast<double>(m_actor.position.y)),
                static_cast<float>(static_cast<double>(m_actor.position.z) + 20.0)};
            EXPECT_EQ(flare.transform.position, at);
            expected.push_back(at);
        }
        else
        {
            EXPECT_EQ(flare.transform.angles.z, yaw);
            EXPECT_EQ(flare.transform.position, before);
        }
        EXPECT_EQ(flare.life, 30 - step);
        EXPECT_EQ(flare.alive, step < 30);
    }
    const auto joints = LiveJoints(m_joints);
    ASSERT_EQ(joints.size(), 2u);
    for (std::size_t index = 0; index < joints.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(joints[index].type, 0x619);
        EXPECT_EQ(joints[index].subtype, 9);
        EXPECT_EQ(joints[index].ownerContext, &m_actor);
        EXPECT_EQ(joints[index].width, 10.0f);
        EXPECT_EQ(joints[index].key, -1);
    }
    // El primer 619/9 nace en el dueño de entonces; el segundo en la posición nueva.
    EXPECT_EQ(joints[0].field9ec, 213);
    EXPECT_EQ(joints[0].position, glm::vec3(100.0f, 200.0f, 300.0f));
    EXPECT_EQ(joints[1].field9ec, 393);
    EXPECT_EQ(joints[1].position, m_actor.position);
}

// Sin dueño, el 5ac/3 no se corre ni suelta joints; otro efecto no es el 5ac/3.
TEST_F(GameCharacterEffectsTest, TheFullSetFlareWithoutOwnerOnlyCountsAndOtherEffectsAreRefused)
{
    ASSERT_TRUE(m_effects.CreateFullSetFlare(m_actor.position, {}, glm::vec3(0.5f), nullptr).has_value());
    Client::World::ModelEffect& flare = m_effects.Effects()[0];
    EXPECT_FALSE(flare.hasOwner);

    ASSERT_TRUE(Client::Scenes::StepFullSetFlare(flare, m_joints, m_random).HasValue());

    EXPECT_EQ(flare.transform.position, m_actor.position);
    EXPECT_EQ(flare.transform.angles.z, 0.0f);
    EXPECT_TRUE(LiveJoints(m_joints).empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(1));

    flare.subtype = 0;
    const auto refused = Client::Scenes::StepFullSetFlare(flare, m_joints, m_random);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("0x52a9ad"), std::string::npos) << refused.Error();
}

// 0x52b9a6 → 0x518900: el calco 0x61e/1 de la mascota saca el rand() de la luz y, por cada uno de sus tres 619/1, el
// giro (rand() % 90, 0, rand() % 360); cada joint nace donde ese giro lleva (0, −200, 0) restado de la posición del
// calco, 120 más arriba, con ancho 5, key −1 y el héroe de dueño. La cola común descuenta la vida: con 20, muere en el
// paso 20.
TEST_F(GameCharacterEffectsTest, ThePetSummonDropsThreeEnergyJointsAroundItsOwnerEachStep)
{
    m_actor.position = {1300.0f, 1700.0f, 290.0f};
    m_actor.angles = {0.0f, 0.0f, 45.0f};
    ASSERT_TRUE(m_effects.CreatePetSummon(m_actor.position, m_actor.angles, glm::vec3(0.5f), &m_actor).has_value());
    Client::World::ModelEffect& summon = m_effects.Effects()[0];

    const auto stepped = Client::Scenes::StepPetSummon(summon, m_joints, m_random);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(m_random.State(), AfterRandoms(7));
    EXPECT_EQ(summon.life, 19);
    EXPECT_TRUE(summon.alive);
    EXPECT_EQ(summon.transform.position, m_actor.position);
    const std::vector<int> draws = RandomsAfter(1, 6);
    const auto joints = LiveJoints(m_joints);
    ASSERT_EQ(joints.size(), 3u);
    for (std::size_t index = 0; index < joints.size(); ++index)
    {
        SCOPED_TRACE(index);
        const glm::vec3 angles{static_cast<float>(draws[index * 2] % 90), 0.0f,
                               static_cast<float>(draws[(index * 2) + 1] % 360)};
        const glm::vec3 reach = Client::Render::Models::OriginalVectorRotate(
            {0.0f, -200.0f, 0.0f}, Client::Render::Models::OriginalAngleMatrix(angles));
        const glm::vec3 start{
            static_cast<float>(static_cast<double>(m_actor.position.x) - static_cast<double>(reach.x)),
            static_cast<float>(static_cast<double>(m_actor.position.y) - static_cast<double>(reach.y)),
            static_cast<float>(static_cast<double>(m_actor.position.z) - static_cast<double>(reach.z) + 120.0)};
        EXPECT_EQ(joints[index].type, 0x619);
        EXPECT_EQ(joints[index].subtype, 1);
        EXPECT_EQ(joints[index].angles, angles);
        EXPECT_EQ(joints[index].position, start);
        EXPECT_EQ(joints[index].ownerContext, &m_actor);
        EXPECT_EQ(joints[index].width, 5.0f);
        EXPECT_EQ(joints[index].key, -1);
        EXPECT_EQ(joints[index].life, 12);
        EXPECT_EQ(joints[index].maximumCount, 2);
    }

    for (int step = 2; step <= 20; ++step)
    {
        for (auto& joint : m_joints)
            joint.alive = false;
        ASSERT_TRUE(Client::Scenes::StepPetSummon(summon, m_joints, m_random).HasValue());
    }
    EXPECT_FALSE(summon.alive);
    EXPECT_EQ(m_random.State(), AfterRandoms(7 * 20));
}

// Otro efecto no es el 0x61e/1, y sin la tabla de joints corta antes de sacar rand().
TEST_F(GameCharacterEffectsTest, ThePetSummonRefusesOtherEffectsAndAMissingJointTable)
{
    ASSERT_TRUE(m_effects.CreatePetSummon(m_actor.position, {}, glm::vec3(0.5f), &m_actor).has_value());
    Client::World::ModelEffect& summon = m_effects.Effects()[0];

    const auto tableless = Client::Scenes::StepPetSummon(summon, {}, m_random);
    ASSERT_FALSE(tableless.HasValue());
    EXPECT_NE(tableless.Error().find("0x5189cc"), std::string::npos) << tableless.Error();
    EXPECT_EQ(m_random.State(), 1u);
    EXPECT_EQ(summon.life, 20);

    summon.subtype = 0;
    const auto refused = Client::Scenes::StepPetSummon(summon, m_joints, m_random);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("0x52b9a6"), std::string::npos) << refused.Error();
}

TEST_F(GameCharacterEffectsTest, ChaosCastleSkipsTheCounterAndTheFullSet)
{
    m_actor.byte358 = 1;
    m_actor.counter35c = 100;
    GameCharacterEffectsPass pass = Pass();
    pass.map = 18;
    pass.scan = {.complete = true, .globals = {.dword743a178 = 13}};

    const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_actor.counter35c, 100);
    EXPECT_TRUE(LiveSprites(m_sprites).empty());
    EXPECT_EQ(m_random.State(), 1u);
}

TEST_F(GameCharacterEffectsTest, TheFullSetPutsThreeFlaresOnEachHandWithTheShineOfTheBoots)
{
    m_actor.body.back().model = 0x1822;
    m_actor.alpha = 0.8f;
    GameCharacterEffectsPass pass = Pass();
    pass.scan = {.complete = true, .globals = {.dword743a178 = 9}};

    const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites(m_sprites);
    ASSERT_EQ(sprites.size(), 6u);
    // 0x608e00(0x1822, 0,8, 0,5): (0,75; 0,65; 0,5) por 0,8 · 0,5.
    const glm::vec3 color{Float(0x3e99999au), Float(0x3e851eb8u), Float(0x3e4ccccdu)};
    constexpr std::array<std::int32_t, 6> bones{0x21, 0x21 - 6, 0x21 - 7, 0x2a, 0x2a - 6, 0x2a - 7};
    for (std::size_t index = 0; index < sprites.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(sprites[index].texture, 0x5ac);
        EXPECT_EQ(sprites[index].position, At(bones[index], {}));
        EXPECT_EQ(sprites[index].scale, Float(0x3fa66666u));
        EXPECT_EQ(sprites[index].light, color);
    }
    // Con el nivel en 9, ni las botas ni el rand() % 20.
    EXPECT_EQ(m_random.State(), 1u);
}

TEST_F(GameCharacterEffectsTest, TheMageSkinOffsetAlsoMovesTheFullSetFlares)
{
    m_actor.skin = 8;
    GameCharacterEffectsPass pass = Pass();
    pass.scan = {.complete = true, .globals = {.dword743a178 = 9}};

    const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites(m_sprites);
    ASSERT_EQ(sprites.size(), 8u);
    // Q-C350: el corrimiento de la piel queda en [ebp−0x50] para las manos.
    EXPECT_EQ(sprites[2].position, At(0x21, {-4.0f, 11.0f, 0.0f}));
    EXPECT_EQ(sprites[7].position, At(0x2a - 7, {-4.0f, 11.0f, 0.0f}));
}

TEST_F(GameCharacterEffectsTest, HiddenAttachmentsDropTheFlaresButKeepTheRandomOfTheSparkles)
{
    m_actor.flags78 = 0x4000;
    GameCharacterEffectsPass pass = Pass();
    pass.scan = {.complete = true, .globals = {.dword743a178 = 15}};

    const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(LiveSprites(m_sprites).empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(1));
}

// La 6f1/3 de 0x53fe70 (0x543f49..0x544153) con los once rand() que siguen a `skipped`: la vida 12, el giro, la escala,
// vz 2, la textura cra_04, la luz de la bota y dos corrimientos de ±10, ±10 y ±20 desde `point`.
void ExpectBootsMist(const Client::World::Particle& mist, std::size_t skipped, const glm::vec3& point,
                     const glm::vec3& light)
{
    const std::vector<int> r = RandomsAfter(skipped, 11);
    EXPECT_EQ(mist.type, 0x6f1);
    EXPECT_EQ(mist.subtype, 3);
    EXPECT_EQ(mist.texture, 0x5ae);
    EXPECT_EQ(mist.life, 12);
    EXPECT_EQ(mist.light, light);
    EXPECT_EQ(mist.rotation, static_cast<float>(r[6] % 360));
    EXPECT_EQ(mist.scale,
              static_cast<float>(static_cast<double>(r[7] % 3 + 3) * static_cast<double>(Float(0x3e3851ecu))));
    EXPECT_EQ(mist.velocity, glm::vec3(0.0f, 0.0f, 2.0f));
    const glm::vec3 at{point.x + static_cast<float>(r[3] % 20 - 10) + static_cast<float>(r[8] % 20 - 10),
                       point.y + static_cast<float>(r[4] % 20 - 10) + static_cast<float>(r[9] % 20 - 10),
                       point.z + static_cast<float>(r[5] % 40 - 20) + static_cast<float>(r[10] % 40 - 20)};
    EXPECT_EQ(mist.position, at);
    EXPECT_EQ(mist.typedOwner, Client::World::ParticleOwner{});
}

// 0x50ff37..0x5102bd con la semilla 1 (41, 18467, …): el 10 (41 & 3), el 11 (41 % 3 = 2) y el 12 (41 & 1) no crean y
// sacan además el rand() % 20; el 13 crea las tres 6f1/3 (33 rand()) y saca el rand() % 20 (11538, que no es 0); el 14
// sólo el rand() % 20. La luz de la bota vuelve a +0xf4.
TEST_F(GameCharacterEffectsTest, TheGlowingBootsDrawTheirRandomByLevelAndThePlusThirteenMists)
{
    struct Case
    {
        std::int32_t level;
        std::size_t draws;
        std::size_t mists;
    };
    constexpr std::array cases{Case{10, 2, 0}, Case{11, 2, 0}, Case{12, 2, 0}, Case{13, 34, 3}, Case{14, 1, 0}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.level);
        m_random = CrtRandom{1};
        m_particles.Clear();
        m_actor.body.back().model = 0x1831;
        const glm::vec3 light = m_actor.light;
        GameCharacterEffectsPass pass = Pass();
        pass.scan = {.complete = true, .globals = {.dword743a178 = probe.level}};

        const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_random.State(), AfterRandoms(probe.draws));
        EXPECT_EQ(m_actor.light, light);
        const auto mists = LiveParticles(m_particles);
        ASSERT_EQ(mists.size(), probe.mists);
        if (probe.mists == 0)
            continue;
        // La luz de 0x1831 (0x50ffbc): (0; 0,32f; 0,24f).
        const glm::vec3 bootsLight{0.0f, Float(0x3ea3d70au), Float(0x3e75c28fu)};
        const std::array<glm::vec3, 3> offsets{{{0.0f, -20.0f, 50.0f}, {0.0f, 0.0f, 70.0f}, {0.0f, 20.0f, 50.0f}}};
        for (std::size_t index = 0; index < mists.size(); ++index)
        {
            SCOPED_TRACE(index);
            ExpectBootsMist(mists[index], index * 11, At(0, offsets[index]), bootsLight);
        }
    }
}

// Los del 10 al 12 que sí crean: las tres 6f1/3 a ±18, con el rand() que eligió antes; entre frames sólo el que elige y
// el rand() % 20, sin partículas.
TEST_F(GameCharacterEffectsTest, TheBootsMistsOfEachLevelAndTheirProjectedFrame)
{
    struct Case
    {
        std::int32_t level;
        std::size_t skipped;
    };
    // Con la semilla 1: el primer múltiplo de 4 es el cuarto rand(), el de 3 el séptimo y el primer par el tercero; el
    // rand() % 20 que sigue a las tres partículas no es 0 en ninguno (19, 3 y 7).
    const std::array cases{
        Case{10, RandomsBefore([](int value) { return (value & 3) == 0; })},
        Case{11, RandomsBefore([](int value) { return value % 3 == 0; })},
        Case{12, RandomsBefore([](int value) { return (value & 1) == 0; })},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.level);
        m_actor.body.back().model = 0x182f;
        for (const bool original : {true, false})
        {
            SCOPED_TRACE(original);
            m_random = CrtRandom{1};
            m_particles.Clear();
            for (std::size_t skip = 0; skip < probe.skipped; ++skip)
                static_cast<void>(m_random.Next());
            GameCharacterEffectsPass pass = Pass();
            pass.originalFrame = original;
            pass.scan = {.complete = true, .globals = {.dword743a178 = probe.level}};

            const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

            ASSERT_TRUE(result.HasValue()) << result.Error();
            const auto mists = LiveParticles(m_particles);
            if (!original)
            {
                EXPECT_TRUE(mists.empty());
                EXPECT_EQ(m_random.State(), AfterRandoms(probe.skipped + 2));
                continue;
            }
            EXPECT_EQ(m_random.State(), AfterRandoms(probe.skipped + 1 + 33 + 1));
            ASSERT_EQ(mists.size(), 3u);
            // La luz de 0x182f (0x50ff5e): (0,65f; 0,3f; 0,1f).
            const glm::vec3 bootsLight{Float(0x3f266666u), Float(0x3e99999au), Float(0x3dcccccdu)};
            const std::array<glm::vec3, 3> offsets{{{0.0f, -18.0f, 50.0f}, {0.0f, 0.0f, 70.0f}, {0.0f, 18.0f, 50.0f}}};
            for (std::size_t index = 0; index < mists.size(); ++index)
            {
                SCOPED_TRACE(index);
                ExpectBootsMist(mists[index], probe.skipped + 1 + index * 11, At(0, offsets[index]), bootsLight);
            }
        }
    }
}

// Sin el pool de partículas, la primera 6f1/3 corta con su llamada.
TEST_F(GameCharacterEffectsTest, TheBootsMistNeedsTheParticlePool)
{
    m_actor.body.back().model = 0x1833;
    GameCharacterEffectsPass pass = Pass();
    pass.particles = nullptr;
    pass.scan = {.complete = true, .globals = {.dword743a178 = 13}};

    const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x51020c"), std::string::npos) << result.Error();
}

// 0x5102c3..0x5104cb, después del rand() % 20 en 0 (el cuarto de la semilla 1): por nivel y pasos de lógica, cuántas
// órbitas 60f/0 y cuántos joints 60f/0, 60f/0x12 y 610/7, y cuántos rand() más. El 11 saca otro rand() & 7 (19169 & 7
// no es 0: la órbita). La órbita saca cuatro, el 60f/0 de ancho 10 dos, cada 60f/0x12 de ancho 20 tres y el 610/7 dos.
// Con +13, los dos 60f/0x12 y la órbita con el 610/7 son ramas independientes (Q-C381).
TEST_F(GameCharacterEffectsTest, TheSparklesOfEachLevelCreateTheirOrbitsAndJoints)
{
    struct Case
    {
        std::int32_t level;
        std::int32_t steps;
        std::size_t orbits;
        std::size_t wide;
        std::size_t trails;
        std::size_t draws;
    };
    constexpr std::array cases{
        Case{10, 1, 1, 0, 0, 4},  Case{11, 1, 1, 0, 0, 5},   Case{12, 6, 0, 2, 0, 6}, Case{12, 3, 1, 0, 0, 4},
        Case{12, 1, 0, 0, 0, 0},  Case{13, 12, 1, 2, 1, 12}, Case{13, 6, 0, 2, 0, 6}, Case{13, 4, 1, 0, 1, 6},
        Case{13, -4, 1, 0, 1, 6}, Case{13, 2, 0, 0, 0, 0},   Case{14, 6, 0, 0, 0, 0},
    };
    const std::size_t skipped = RandomsBefore([](int value) { return value % 20 == 0; });
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << probe.level << ' ' << probe.steps);
        m_random = CrtRandom{1};
        m_particles.Clear();
        m_joints.assign(m_joints.size(), Client::Scenes::JointState{});
        for (std::size_t skip = 0; skip < skipped; ++skip)
            static_cast<void>(m_random.Next());
        const glm::vec3 light = m_actor.light;
        GameCharacterEffectsPass pass = Pass();
        pass.logicSteps = probe.steps;
        pass.scan = {.complete = true, .globals = {.dword743a178 = probe.level}};

        const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_random.State(), AfterRandoms(skipped + 1 + probe.draws));
        EXPECT_EQ(m_actor.light, light);
        const auto orbits = LiveParticles(m_particles);
        ASSERT_EQ(orbits.size(), probe.orbits);
        for (const auto& orbit : orbits)
        {
            EXPECT_EQ(orbit.type, 0x60f);
            EXPECT_EQ(orbit.subtype, 0);
            EXPECT_EQ(orbit.light, glm::vec3(1.0f));
            EXPECT_EQ(orbit.life, 60);
            EXPECT_EQ(orbit.typedOwner, (Client::World::ParticleOwner{Client::World::ParticleOwnerKind::Actor, Slot}));
        }
        const auto joints = LiveJoints(m_joints);
        ASSERT_EQ(joints.size(), probe.wide + probe.trails);
        std::size_t index = 0;
        for (; index < probe.wide; ++index)
        {
            SCOPED_TRACE(index);
            EXPECT_EQ(joints[index].type, 0x60f);
            EXPECT_EQ(joints[index].subtype, 0x12);
            EXPECT_EQ(joints[index].width, 20.0f);
            EXPECT_EQ(joints[index].skill, static_cast<std::uint8_t>(index));
            EXPECT_EQ(joints[index].blend, 4);
            EXPECT_EQ(joints[index].life, 50);
            EXPECT_EQ(joints[index].light, glm::vec3(1.0f));
            EXPECT_EQ(joints[index].ownerContext, &m_actor);
        }
        for (; index < joints.size(); ++index)
        {
            SCOPED_TRACE(index);
            EXPECT_EQ(joints[index].type, 0x610);
            EXPECT_EQ(joints[index].subtype, 7);
            EXPECT_EQ(joints[index].life, 0x28);
            EXPECT_EQ(joints[index].skill, 1);
            EXPECT_EQ(joints[index].ownerContext, &m_actor);
        }
    }
}

// El 11 con rand() & 7 en 0 (0x51037c): el 60f/0 de ancho 10 con la luz en 1 del personaje, dos rand().
TEST_F(GameCharacterEffectsTest, ThePlusElevenSparkleJointTakesTheWhiteLightOfItsOwner)
{
    const std::size_t skipped =
        RandomsBeforePair([](int value) { return value % 20 == 0; }, [](int value) { return (value & 7) == 0; });
    for (std::size_t skip = 0; skip < skipped; ++skip)
        static_cast<void>(m_random.Next());
    GameCharacterEffectsPass pass = Pass();
    pass.scan = {.complete = true, .globals = {.dword743a178 = 11}};

    const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(LiveParticles(m_particles).empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(skipped + 2 + 2));
    const auto joints = LiveJoints(m_joints);
    ASSERT_EQ(joints.size(), 1u);
    const std::vector<int> r = RandomsAfter(skipped + 2, 2);
    EXPECT_EQ(joints[0].type, 0x60f);
    EXPECT_EQ(joints[0].subtype, 0);
    EXPECT_EQ(joints[0].width, 10.0f);
    EXPECT_EQ(joints[0].life, 100);
    EXPECT_EQ(joints[0].light, glm::vec3(1.0f));
    EXPECT_EQ(joints[0].velocity.z,
              static_cast<float>(static_cast<double>(r[0] % 150) * static_cast<double>(Float(0x3c23d70au))));
    EXPECT_EQ(joints[0].velocity.y, static_cast<float>(r[1] % 500 - 250));
    EXPECT_EQ(joints[0].targetPosition, m_actor.position);
    EXPECT_EQ(m_actor.light, glm::vec3(0.1f, 0.2f, 0.3f));
}

// Entre frames, sólo los rand() que eligen (el % 20 y, con +11, el & 7): ni órbitas ni joints.
TEST_F(GameCharacterEffectsTest, BetweenOriginalFramesTheSparklesOnlyDrawTheirChoices)
{
    const std::size_t skipped = RandomsBefore([](int value) { return value % 20 == 0; });
    for (const std::int32_t level : {10, 11, 12, 13})
    {
        SCOPED_TRACE(level);
        m_random = CrtRandom{1};
        for (std::size_t skip = 0; skip < skipped; ++skip)
            static_cast<void>(m_random.Next());
        GameCharacterEffectsPass pass = Pass();
        pass.originalFrame = false;
        pass.logicSteps = 12;
        pass.scan = {.complete = true, .globals = {.dword743a178 = level}};

        const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_random.State(), AfterRandoms(skipped + 1 + (level == 11 ? 1 : 0)));
        EXPECT_TRUE(LiveParticles(m_particles).empty());
        EXPECT_TRUE(LiveJoints(m_joints).empty());
    }
}

// Sin joints, el primero de los dos 60f/0x12 corta con su llamada.
TEST_F(GameCharacterEffectsTest, TheWideSparklesNeedTheJoints)
{
    const std::size_t skipped = RandomsBefore([](int value) { return value % 20 == 0; });
    for (std::size_t skip = 0; skip < skipped; ++skip)
        static_cast<void>(m_random.Next());
    GameCharacterEffectsPass pass = Pass();
    pass.joints = {};
    pass.logicSteps = 6;
    pass.scan = {.complete = true, .globals = {.dword743a178 = 13}};

    const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x510435"), std::string::npos) << result.Error();
}

TEST_F(GameCharacterEffectsTest, TheGuildMarkCutsWithPlus369OutsideTheEventMaps)
{
    struct Case
    {
        std::uint8_t byte88;
        std::uint8_t byte369;
        std::int32_t map;
        bool originalFrame;
        bool cuts;
    };
    constexpr std::array cases{
        Case{1, 1, 0, true, true},   Case{1, 1, 10, true, true},  Case{1, 1, 24, true, true},
        Case{1, 1, 11, true, false}, Case{1, 1, 17, true, false}, Case{1, 1, 18, true, false},
        Case{1, 1, 23, true, false}, Case{1, 1, 30, true, false}, Case{0, 1, 0, true, false},
        Case{1, 2, 0, true, false},  Case{1, 1, 0, false, false},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << int{probe.byte88} << ' ' << int{probe.byte369} << ' ' << probe.map);
        m_actor.byte88 = probe.byte88;
        m_actor.byte369 = probe.byte369;
        GameCharacterEffectsPass pass = Pass();
        pass.map = probe.map;
        pass.originalFrame = probe.originalFrame;

        const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

        EXPECT_EQ(!result.HasValue(), probe.cuts);
        if (probe.cuts)
        {
            EXPECT_NE(result.Error().find("0x510f4d"), std::string::npos) << result.Error();
        }
    }
}

// Un cuervo sintético: un hueso y cinco mallas de un triángulo, para ver qué mallas dibuja y la sombra.
class RavenModels final : public Client::Scenes::IModelLibrary
{
public:
    RavenModels()
    {
        using namespace Client::Render::Models;
        SkeletonBone bone;
        bone.parent = -1;
        bone.actions = {{BoneKey{}}};
        bone.actions[0][0].rotation = AngleQuaternion({0.0f, 0.0f, 0.0f});
        m_skeleton.bones.assign(1, bone);
        m_skeleton.actions = {ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 10}}, {0, {1, 0, 10}}, {0, {0, 1, 10}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "dksbody.tga";
        m_model.meshes.assign(5, mesh);
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t model) const override
    {
        return model == Client::Scenes::DarkSpiritModel ? &m_model : nullptr;
    }
    [[nodiscard]] const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == Client::Scenes::DarkSpiritModel ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return m_textures;
    }

private:
    Client::Render::Models::Skeleton m_skeleton;
    Client::Data::Models::Model m_model;
    std::array<Client::Render::TextureSlot, 5> m_textures{
        Client::Render::TextureSlot{0x300}, Client::Render::TextureSlot{0x301}, Client::Render::TextureSlot{0x302},
        Client::Render::TextureSlot{0x303}, Client::Render::TextureSlot{0x304}};
};

class GameRavenTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        m_terrain.heights->cells.fill(0.0f);
        m_terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        m_terrain.attributes->cells.fill(0);
        m_lighting = std::make_unique<Client::World::TerrainLighting>();
        m_lighting->cells.fill({0.2f, 0.4f, 0.6f});
        m_polygon.everything = true;
        m_owner.position = {1000.0f, 1000.0f, 0.0f};
        Client::Scenes::DarkSpirit& spirit = m_owner.darkSpirit.emplace();
        spirit.position = {1050.0f, 1060.0f, 300.0f};
        m_batch.Begin(Client::Render::Pass3D{});
    }

    GameRavenPass Pass(std::int32_t map = 0)
    {
        return {.batch = m_batch,
                .models = m_models,
                .polygon = m_polygon,
                .lighting = m_lighting.get(),
                .terrain = m_terrain,
                .fixedHeight = 1200.0f,
                .carry = nullptr,
                .frameClock = Client::Core::Duration{},
                .map = map,
                .heroByte1a1 = 0,
                .originalFrame = true};
    }

    ActorState m_owner;
    RavenModels m_models;
    Client::World::WorldTerrain m_terrain;
    std::unique_ptr<Client::World::TerrainLighting> m_lighting;
    Client::World::TerrainPolygon m_polygon;
    Client::Render::ModelBatch m_batch;
};

TEST_F(GameRavenTest, OutOfTheTrapeziumTheRavenSlowsDownAndForgetsItsStateWithoutDrawing)
{
    // Un trapecio de una celda en (200, 200), lejos del cuervo en (10,5; 10,6).
    m_polygon = {.x = {200.0f, 201.0f, 201.0f, 200.0f}, .y = {200.0f, 200.0f, 201.0f, 201.0f}, .everything = false};
    m_owner.darkSpirit->state = 1;
    m_owner.darkSpirit->speed = 1.0f;

    const auto result = Client::Scenes::DrawGameRaven(m_owner, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_owner.darkSpirit->speed, 3.0f);
    EXPECT_EQ(m_owner.darkSpirit->state, 0);
    EXPECT_TRUE(m_batch.Vertices().empty());
}

TEST_F(GameRavenTest, ASeenRavenDrawsMeshesThreeAndTwoAndTheShadowOfAllAndKeepsItsPose)
{
    Client::Scenes::FaunaDrawCarry carry;
    GameRavenPass pass = Pass();
    pass.carry = &carry;

    const auto result = Client::Scenes::DrawGameRaven(m_owner, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), (2u + 5u) * 3u);
    EXPECT_EQ(m_owner.darkSpirit->pose.size(), 1u);
    // 0x600a1c: el origen de la sombra en el modelo, a la altura del terreno bajo el cuervo.
    EXPECT_EQ(carry.models.at(Client::Scenes::DarkSpiritModel).position, glm::vec3(1050.0f, 1060.0f, 0.0f));
}

TEST_F(GameRavenTest, TheShadowFollowsTheMapRulesOf0x6002e0)
{
    struct Case
    {
        std::int32_t map;
        std::size_t vertices;
        float shadowAlpha;
    };
    for (const Case probe : {Case{0, 21, 1.0f}, Case{7, 21, 0.2f}, Case{10, 6, 0.0f}, Case{24, 6, 0.0f},
                             Case{29, 6, 0.0f}, Case{36, 6, 0.0f}, Case{30, 21, 1.0f}})
    {
        SCOPED_TRACE(probe.map);
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Client::Scenes::DrawGameRaven(m_owner, Pass(probe.map));

        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), probe.vertices);
        if (probe.vertices > 6)
        {
            EXPECT_EQ(m_batch.Vertices().back().color.alpha, probe.shadowAlpha);
        }
    }
}

// 0x600a24..0x600a2c llama a 0x4eff40, que en el mapa 30 abre la sombra con 2500 y no 2000 (0x4eff8d..0x4eff9f).
// Los vértices de la sombra son los del pase base aplastados desde el cuervo a la altura del terreno.
TEST_F(GameRavenTest, InCastleSiegeTheShadowFansOutWith2500)
{
    for (const std::int32_t map : {0, 30})
    {
        SCOPED_TRACE(map);
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Client::Scenes::DrawGameRaven(m_owner, Pass(map));

        ASSERT_TRUE(result.HasValue()) << result.Error();
        const auto& vertices = m_batch.Vertices();
        ASSERT_EQ(vertices.size(), 21u);
        const glm::vec3 ground(1050.0f, 1060.0f, 0.0f);
        const float spread = map == 30 ? 2500.0f : 2000.0f;
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const glm::vec3 body = vertices[corner].position;
            const glm::vec3 shadow = vertices[6 + corner].position;
            EXPECT_EQ(shadow, Client::Render::Models::FlattenOnTheGround(body, ground, spread)) << corner;
        }
    }
}

TEST_F(GameRavenTest, BetweenOriginalFramesTheRavenIsDrawnWithoutTouchingItsState)
{
    GameRavenPass pass = Pass(18);
    pass.originalFrame = false;
    pass.heroByte1a1 = 1;

    const auto result = Client::Scenes::DrawGameRaven(m_owner, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 21u);
    EXPECT_TRUE(m_owner.darkSpirit->pose.empty());
    EXPECT_EQ(m_owner.darkSpirit->alpha, 1.0f);
}

TEST_F(GameRavenTest, InChaosCastleTheHeroByteFadesTheRavenUntilItIsNoLongerDrawn)
{
    GameRavenPass pass = Pass(18);
    pass.heroByte1a1 = 1;
    m_owner.darkSpirit->alpha = 0.155f;

    const auto first = Client::Scenes::DrawGameRaven(m_owner, pass);

    ASSERT_TRUE(first.HasValue()) << first.Error();
    // 0,155f − 0,15f queda por debajo de 0,01f: 0x5ffdf0 sale sin posar.
    EXPECT_LT(m_owner.darkSpirit->alpha, 0.01f);
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_TRUE(m_owner.darkSpirit->pose.empty());
}

TEST_F(GameRavenTest, TheRiderFlagAndTheOrderStatesCutAfterTheirChecks)
{
    m_owner.flags78 = 0x4000;
    const auto rider = Client::Scenes::DrawGameRaven(m_owner, Pass());
    ASSERT_FALSE(rider.HasValue());
    EXPECT_NE(rider.Error().find("0x60033c"), std::string::npos) << rider.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());

    m_owner.flags78 = 0;
    for (const std::uint8_t state : {std::uint8_t{5}, std::uint8_t{6}})
    {
        SCOPED_TRACE(int{state});
        m_owner.darkSpirit->state = state;
        m_batch.Begin(Client::Render::Pass3D{});

        const auto order = Client::Scenes::DrawGameRaven(m_owner, Pass());

        ASSERT_FALSE(order.HasValue());
        EXPECT_NE(order.Error().find("0x418957"), std::string::npos) << order.Error();
        EXPECT_EQ(m_batch.Vertices().size(), 21u);
    }
}

TEST_F(GameRavenTest, AnOwnerWithoutRavenDrawsNothing)
{
    m_owner.darkSpirit.reset();

    const auto result = Client::Scenes::DrawGameRaven(m_owner, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
}
} // namespace
