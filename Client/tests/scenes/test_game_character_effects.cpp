#include "Models/IModelLibrary.h"
#include "Actors/Drawing/GameCharacterEffects.h"

#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Models/ModelId.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Scenes/DarkSpirit.h"
#include "Kernel/PoseBuffer.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/ActorEffectHooks.h"
#include "Session/CharacterDrawHooks.h"
#include "Actors/FullSetScan.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameParticles.h"
#include "Kernel/WorldState.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Scenes/SocialState.h"
#include "Kernel/JointRequest.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <optional>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The tail of 0x50ba30 (0x50ea20..0x510ab6) and the raven of 0x420df0, against the ASM of c32bbd5f. The expected floats
// come from the ASM's arithmetic in double and float (the bits, in hexadecimal); the rand() calls, from the CRT with
// seed 1.
namespace
{
using Client::Actors::Drawing::GameCharacterEffectsPass;
using Client::Actors::Drawing::GameRavenPass;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Scenes::PartySlot;

const Client::Session::CharacterDrawHooks JointHooks;
const Client::Scenes::ActorEffectHooks EffectHooks;

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

// How many rand() calls must be drawn with seed 1 for the next one to meet `wanted`.
template <typename Predicate> std::size_t RandomsBefore(Predicate wanted)
{
    CrtRandom random{1};
    for (std::size_t count = 0;; ++count)
    {
        if (wanted(random.Next()))
            return count;
    }
}

// How many rand() calls must be drawn with seed 1 for the next two to meet `first` and `second`.
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

// The rand() calls of seed 1 from the one after the first `skipped`.
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
        // Bone k without a turn and at (0, 0, k): the point of 0x4ec110 is the shift plus (0, 0, k) plus the
        // character's position.
        m_body.assign(48, Client::Math::BoneTransform(1.0f));
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
                .jointHooks = &JointHooks,
                .effects = &m_effects,
                .effectHooks = &EffectHooks,
                .slot = Slot,
                .scan = {},
                .actors = m_actors,
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
    std::vector<Client::Math::BoneTransform> m_body;
    CrtRandom m_random{1};
    Client::World::FrameSprites m_sprites;
    Client::World::Particles m_particles;
    std::vector<Client::Scenes::JointState> m_joints = std::vector<Client::Scenes::JointState>(16);
    std::vector<ActorState> m_actors = std::vector<ActorState>(400);
    Client::World::ModelEffects m_effects;
};

TEST_F(GameCharacterEffectsTest, TheMageSkinPutsTheSparkAndTheShinyOnBoneNineteenWithoutRandoms)
{
    m_actor.skin = 8;

    const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites(m_sprites);
    ASSERT_EQ(sprites.size(), 2u);
    const glm::vec3 point = At(19, {-4.0f, 11.0f, 0.0f});
    EXPECT_EQ(sprites[0].texture, 0x5c6);
    EXPECT_EQ(sprites[0].position, point);
    EXPECT_EQ(sprites[0].scale, Float(0x3f19999au));
    EXPECT_EQ(sprites[0].light, glm::vec3(1.0f));
    EXPECT_EQ(sprites[0].blend, 0);
    // sin(1000 · 0.001f) · 0.4f.
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

        const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        // Class 8 has (8 & 7) at 0: it is also the wizard's.
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

    const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites(m_sprites);
    ASSERT_EQ(sprites.size(), 3u);
    const glm::vec3 point = At(20, {});
    for (const auto& sprite : sprites)
    {
        EXPECT_EQ(sprite.texture, 0x5d5);
        EXPECT_EQ(sprite.position, point);
    }
    // s = 3 · 0.1f; s · 0.3f, s and s · 2.5, with ∓ftol(1234.5) · 0.1f of turn.
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

// 0x50f96c..0x50f9c6 and 0x50fac5..0x50fb9c: in [1.2, 1.6) the 0x621/2 glow with the character as owner and the sound
// 0x1cc; in [7, 8) two 0x117/2 without an owner in the hand (+0x2bd), with (180, 45, 0) and (0, 0, z) and the light
// (0.8f, 0.5f, 1). Outside the windows or between frames, nothing. None draws rand(). Without the world, they stop.
TEST_F(GameCharacterEffectsTest, TheActionWindowsCreateTheirEffectsOnlyInsideTheirFramesOfAnOriginalFrame)
{
    enum class Window : std::uint8_t
    {
        None,
        Glow,
        Skills,
    };
    struct Case
    {
        std::uint8_t characterClass;
        std::uint8_t action;
        float frame;
        bool originalFrame;
        Window window;
    };
    constexpr std::array cases{
        Case{4, 0x94, 1.2f, true, Window::Glow},   Case{0, 0x51, 1.5f, true, Window::Glow},
        Case{0, 0x5b, 7.0f, true, Window::Skills}, Case{4, 0x94, 7.9f, true, Window::Skills},
        Case{4, 0x94, 1.6f, true, Window::None},   Case{4, 0x94, 1.1f, true, Window::None},
        Case{4, 0x94, 8.0f, true, Window::None},   Case{4, 0x94, 1.3f, false, Window::None},
        Case{0, 0x51, 7.5f, false, Window::None},
    };
    m_actor.angles = {10.0f, 20.0f, 30.0f};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << int{probe.action} << ' ' << probe.frame << ' ' << probe.originalFrame);
        m_sprites = {};
        m_random = CrtRandom{1};
        m_actor.characterClass = probe.characterClass;
        m_actor.animation.action = probe.action;
        m_actor.animation.frame = probe.frame;
        auto world = std::make_unique<Client::Kernel::WorldState>();
        GameCharacterEffectsPass pass = Pass();
        pass.originalFrame = probe.originalFrame;
        pass.world = world.get();

        const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(LiveSprites(m_sprites).empty());
        EXPECT_EQ(m_random.State(), 1u);
        const auto& effects = world->Map().effects.Effects();
        const auto alive = std::ranges::count_if(effects, [](const auto& effect) { return effect.alive; });
        if (probe.window == Window::Glow)
        {
            ASSERT_EQ(alive, 1);
            EXPECT_EQ(effects[0].type, 0x621);
            EXPECT_EQ(effects[0].subtype, 2);
            EXPECT_EQ(effects[0].life, 20);
            EXPECT_EQ(effects[0].transform.position, m_actor.position);
            EXPECT_EQ(effects[0].start, m_actor.position);
            EXPECT_EQ(effects[0].transform.angles, m_actor.angles);
            EXPECT_EQ(effects[0].light, m_actor.light);
            EXPECT_EQ(effects[0].ownerContext, &m_actor);
            EXPECT_EQ(world->Runtime().lastSoundId, Client::Audio::SoundId{0x1cc});
        }
        else if (probe.window == Window::Skills)
        {
            ASSERT_EQ(alive, 2);
            const glm::vec3 hand = At(0x21, glm::vec3(0.0f));
            const std::array angles{glm::vec3{180.0f, 45.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 30.0f}};
            for (std::size_t index = 0; index < 2; ++index)
            {
                SCOPED_TRACE(index);
                EXPECT_EQ(effects[index].type, 0x117);
                EXPECT_EQ(effects[index].subtype, 2);
                EXPECT_EQ(effects[index].life, 12);
                EXPECT_EQ(effects[index].transform.position, hand);
                EXPECT_EQ(effects[index].transform.angles, angles[index]);
                EXPECT_EQ(effects[index].light, glm::vec3(Float(0x3f4ccccdu), 0.5f, 1.0f));
                EXPECT_EQ(effects[index].ownerContext, nullptr);
            }
            EXPECT_FALSE(world->Runtime().lastSoundId.has_value());
        }
        else
        {
            EXPECT_EQ(alive, 0);
            EXPECT_FALSE(world->Runtime().lastSoundId.has_value());
        }

        // Without the world, the windows stop with their address; outside them, nothing.
        pass.world = nullptr;
        m_random = CrtRandom{1};
        const auto alone = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);
        if (probe.window == Window::None)
        {
            EXPECT_TRUE(alone.HasValue()) << alone.Error();
        }
        else
        {
            ASSERT_FALSE(alone.HasValue());
            EXPECT_NE(alone.Error().find(probe.window == Window::Glow ? "0x50f9b3" : "0x50fb64"), std::string::npos)
                << alone.Error();
        }
    }
}

// 0x50fba4..0x50fbcf: with 0x4 or 0x2000000 in +0x78 and without 0x4000, the hands glow: one rand() and three sprites
// per hand. Between frames, without the joints.
TEST_F(GameCharacterEffectsTest, TheGlowFlagsLightTheHandsWithTheExactConditionOfTheAssembly)
{
    struct Case
    {
        std::uint32_t flags;
        bool glows;
    };
    for (const Case probe : {Case{0x4, true}, Case{0x2000000, true}, Case{0x2000004, true}, Case{0x4004, false},
                             Case{0x2004000, false}, Case{0x800, false}, Case{0x1, false}})
    {
        SCOPED_TRACE(probe.flags);
        m_sprites = {};
        m_random = CrtRandom{1};
        m_actor.flags78 = probe.flags;
        GameCharacterEffectsPass pass = Pass();
        pass.originalFrame = false;

        const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(LiveSprites(m_sprites).size(), probe.glows ? 6u : 0u);
        EXPECT_EQ(m_random.State(), AfterRandoms(probe.glows ? 2 : 0));
        EXPECT_TRUE(LiveJoints(m_joints).empty());
    }
}

// 0x50fbe5..0x50fd5c in an original frame: per hand, k = (rand() % 30 + 70) · 0.01f (with seed 1, 41 and 18,467: 81 and
// 87) and the light (k, k · 0.3f, k · 0.2f), with k not stored as a float for the products; the 0x5fd sprites of scale
// 1.5 on bones hand, hand − 6 and hand − 7 with the shift of [ebp−0x50] (here 0). Then, without a 0x10f/4 or /9 of its
// own, five 0x10f/4 of width 20 from it, with its angles and the signed +0x3a4 as the target (+0xa0c).
TEST_F(GameCharacterEffectsTest, TheGlowFlagsBindTheCharacterWithFiveAuras)
{
    m_actor.flags78 = 0x2000000;
    m_actor.angles = {0.0f, 0.0f, 90.0f};
    m_actor.attackTarget = 0xfffe;

    const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites(m_sprites);
    ASSERT_EQ(sprites.size(), 6u);
    const std::array<glm::vec3, 2> lights{glm::vec3{Float(0x3f4f5c29u), Float(0x3e78d4feu), Float(0x3e25e354u)},
                                          glm::vec3{Float(0x3f5eb852u), Float(0x3e85a1cbu), Float(0x3e322d0eu)}};
    const std::array<std::int32_t, 6> bones{0x21, 0x21 - 6, 0x21 - 7, 0x2a, 0x2a - 6, 0x2a - 7};
    for (std::size_t index = 0; index < sprites.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(sprites[index].texture, 0x5fd);
        EXPECT_EQ(sprites[index].scale, 1.5f);
        EXPECT_EQ(sprites[index].rotation, 0.0f);
        EXPECT_EQ(sprites[index].light, lights[index / 3]);
        EXPECT_EQ(sprites[index].position, At(bones[index], {}));
    }
    const auto auras = LiveJoints(m_joints);
    ASSERT_EQ(auras.size(), 5u);
    for (const auto& aura : auras)
    {
        EXPECT_EQ(aura.type, 0x10f);
        EXPECT_EQ(aura.subtype, 4);
        EXPECT_EQ(aura.width, 20.0f);
        EXPECT_EQ(aura.position, m_actor.position);
        EXPECT_EQ(aura.angles, m_actor.angles);
        EXPECT_EQ(aura.ownerContext, &m_actor);
        EXPECT_EQ(aura.target, -2);
    }
    EXPECT_EQ(m_random.State(), AfterRandoms(2));

    // With a live 0x10f/9 of its own, no more; the sprites and the rand() calls the same.
    for (auto& joint : m_joints)
        joint.alive = false;
    m_joints[7].alive = true;
    m_joints[7].type = 0x10f;
    m_joints[7].subtype = 9;
    m_joints[7].ownerContext = &m_actor;
    m_sprites = {};
    m_random = CrtRandom{1};
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, Pass()).HasValue());
    EXPECT_EQ(LiveJoints(m_joints).size(), 1u);
    EXPECT_EQ(LiveSprites(m_sprites).size(), 6u);
    EXPECT_EQ(m_random.State(), AfterRandoms(2));

    // One of another owner or with another subtype does not count.
    m_joints[7].subtype = 2;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, Pass()).HasValue());
    EXPECT_EQ(LiveJoints(m_joints).size(), 6u);
}

// 0x50f9c9..0x50fac2: below frame 2 (0x6b8818) and with a party, 0x59e680 on the name. Outside the party it jumps to
// 0x510a7a, without the flags of 0x50fba4, also between frames. In the party, the 0x618 joints toward another member in
// view (+0x1c at 0 or more and not itself) are born in an original frame (0x50fa74 and 0x50fa97: /0xc and /0xd of width
// 20, from the member 150 higher, with it as owner) and then the flags follow (0x4: two rand() calls, six sprites and,
// in an original frame, five 0x10f/4). With the frame at 2 or without a party, also the flags. Each 0x618/0xc and /0xd
// draws three rand() calls (the direction at 0x53217c and the two turns at 0x5321af and 0x5321c5): six per member, and
// none without joints.
TEST_F(GameCharacterEffectsTest, BelowFrameTwoTheActionsReadTheParty)
{
    struct Case
    {
        const char* member;
        std::int32_t memberIndex;
        float frame;
        bool originalFrame;
        bool partyListed;
        bool flags;
        bool beams = false;
    };
    constexpr std::int32_t Self = static_cast<std::int32_t>(Slot);
    constexpr std::array cases{
        Case{"Otro", -1, 1.9f, true, true, false},       Case{"Otro", -1, 0.5f, false, true, false},
        Case{"Otro", -1, 2.0f, false, true, true},       Case{"Otro", -1, 1.9f, false, false, true},
        Case{"Lordes", -1, 1.9f, true, true, true},      Case{"Lordes", Self, 1.9f, true, true, true},
        Case{"Lordes", 7, 1.9f, true, true, true, true}, Case{"Lordes", 7, 1.9f, false, true, true},
        Case{"Lord", 0, 0.0f, true, true, true, true},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << probe.member << ' ' << probe.memberIndex << ' ' << probe.frame << ' '
                                        << probe.originalFrame << ' ' << probe.partyListed);
        m_actor = {};
        SetUp();
        m_actor.characterClass = 4;
        m_actor.animation.action = 0x51;
        m_actor.animation.frame = probe.frame;
        m_actor.flags78 = 0x4;
        const std::string_view name = "Lord";
        for (std::size_t at = 0; at < name.size(); ++at)
            m_actor.name[at] = static_cast<std::byte>(name[at]);
        std::array<PartySlot, 2> party{};
        const std::string_view member = probe.member;
        for (std::size_t at = 0; at < member.size(); ++at)
            party[1].name[at] = static_cast<std::byte>(member[at]);
        party[0].actorIndex = -1;
        party[1].actorIndex = probe.memberIndex;
        GameCharacterEffectsPass pass = Pass();
        pass.originalFrame = probe.originalFrame;
        if (probe.partyListed)
            pass.party = party;
        if (probe.memberIndex >= 0)
            m_actors[static_cast<std::size_t>(probe.memberIndex)].position = {500.0f, 600.0f, 70.0f};
        for (auto& joint : m_joints)
            joint.alive = false;
        m_random = CrtRandom{1};

        m_sprites = {};

        const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        const auto joints = LiveJoints(m_joints);
        const std::size_t auras = probe.flags && probe.originalFrame ? 5u : 0u;
        ASSERT_EQ(joints.size(), (probe.beams ? 2u : 0u) + auras);
        constexpr std::size_t RandomsPerMember = 2 * 3;
        EXPECT_EQ(m_random.State(), AfterRandoms((probe.beams ? RandomsPerMember : 0) + (probe.flags ? 2 : 0)));
        EXPECT_EQ(LiveSprites(m_sprites).size(), probe.flags ? 6u : 0u);
        for (std::size_t index = 0; index + auras < joints.size(); ++index)
        {
            EXPECT_EQ(joints[index].type, 0x618);
            EXPECT_EQ(joints[index].subtype, index == 0 ? 0xc : 0xd);
            EXPECT_EQ(joints[index].width, 20.0f);
            EXPECT_EQ(joints[index].ownerContext, &m_actor);
            EXPECT_EQ(joints[index].position, glm::vec3(500.0f, 600.0f, 220.0f));
        }
    }
}

// 0x50fd74..0x50fdd5: with +0x358, each draw counts; with 100 or more and the action below 0xd, the 0x5ac/3 on the
// character (0x518cf0 → 0x51e489) and the counter goes back to 0 before counting. No rand().
TEST_F(GameCharacterEffectsTest, TheSetCounterCreatesTheFullSetFlareAtTheHundredthWithALowAction)
{
    m_actor.byte358 = 1;
    m_actor.counter35c = 0;
    m_actor.angles = {0.0f, 0.0f, 45.0f};

    ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, Pass()).HasValue());
    EXPECT_EQ(m_actor.counter35c, 1);

    m_actor.counter35c = 100;
    m_actor.animation.action = 0xd;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, Pass()).HasValue());
    EXPECT_EQ(m_actor.counter35c, 101);
    EXPECT_FALSE(m_effects.Effects()[0].alive);

    m_actor.animation.action = 0xc;
    const auto created = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, Pass());
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

    // Between frames, on the caller's copy: it goes back to 0 and counts, without the effect.
    m_actor.counter35c = 100;
    GameCharacterEffectsPass projected = Pass();
    projected.originalFrame = false;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, projected).HasValue());
    EXPECT_EQ(m_actor.counter35c, 1);
    EXPECT_FALSE(m_effects.Effects()[1].alive);
    EXPECT_EQ(m_random.State(), 1u);

    // Without the effects table, it stops with the call's address.
    m_actor.counter35c = 100;
    GameCharacterEffectsPass missing = Pass();
    missing.effects = nullptr;
    const auto cut = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, missing);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x50fdc3"), std::string::npos) << cut.Error();

    // Without the effect hooks, too, and nothing is born.
    m_actor.counter35c = 100;
    GameCharacterEffectsPass unhooked = Pass();
    unhooked.effectHooks = nullptr;
    const auto unborn = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, unhooked);
    ASSERT_FALSE(unborn.HasValue());
    EXPECT_NE(unborn.Error().find("0x50fdc3"), std::string::npos) << unborn.Error();
    EXPECT_FALSE(m_effects.Effects()[1].alive);
}

TEST_F(GameCharacterEffectsTest, ChaosCastleSkipsTheCounterAndTheFullSet)
{
    m_actor.byte358 = 1;
    m_actor.counter35c = 100;
    GameCharacterEffectsPass pass = Pass();
    pass.map = 18;
    pass.scan = {.complete = true, .globals = {.dword743a178 = 13}};

    const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

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

    const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites(m_sprites);
    ASSERT_EQ(sprites.size(), 6u);
    // 0x608e00(0x1822, 0.8, 0.5): (0.75, 0.65, 0.5) times 0.8 · 0.5.
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
    // With the level at 9, neither the boots nor the rand() % 20.
    EXPECT_EQ(m_random.State(), 1u);
}

TEST_F(GameCharacterEffectsTest, TheMageSkinOffsetAlsoMovesTheFullSetFlares)
{
    m_actor.skin = 8;
    GameCharacterEffectsPass pass = Pass();
    pass.scan = {.complete = true, .globals = {.dword743a178 = 9}};

    const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites(m_sprites);
    ASSERT_EQ(sprites.size(), 8u);
    // Q-C350: the skin's shift stays in [ebp−0x50] for the hands.
    EXPECT_EQ(sprites[2].position, At(0x21, {-4.0f, 11.0f, 0.0f}));
    EXPECT_EQ(sprites[7].position, At(0x2a - 7, {-4.0f, 11.0f, 0.0f}));
}

// Q-C350 in the actions too: the wizard's skin (0x50ea86) leaves (−4, 11, 0) in [ebp−0x50] and 0x4ec110 uses it in the
// hand (+0x2bd, 0x21) of the lightning of the 0x94 (0x50f89b) and of the 0x117/2 of the 0x51 and the 0x5b (0x50fb16).
TEST_F(GameCharacterEffectsTest, TheMageSkinOffsetAlsoMovesTheHandOfTheActions)
{
    m_actor.skin = 8;
    const glm::vec3 hand = At(0x21, {-4.0f, 11.0f, 0.0f});
    m_actor.animation.action = 0x94;
    m_actor.animation.frame = 3.0f;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, Pass()).HasValue());
    const auto sprites = LiveSprites(m_sprites);
    ASSERT_EQ(sprites.size(), 5u);
    for (std::size_t index = 2; index < sprites.size(); ++index)
    {
        EXPECT_EQ(sprites[index].texture, 0x5d5) << index;
        EXPECT_EQ(sprites[index].position, hand) << index;
    }
    for (const std::uint8_t action : {std::uint8_t{0x51}, std::uint8_t{0x5b}})
    {
        SCOPED_TRACE(int{action});
        m_sprites = {};
        m_actor.animation.action = action;
        m_actor.animation.frame = 7.5f;
        auto world = std::make_unique<Client::Kernel::WorldState>();
        GameCharacterEffectsPass pass = Pass();
        pass.world = world.get();
        const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        const auto& effects = world->Map().effects.Effects();
        for (std::size_t index = 0; index < 2; ++index)
        {
            ASSERT_TRUE(effects[index].alive) << index;
            EXPECT_EQ(effects[index].type, 0x117) << index;
            EXPECT_EQ(effects[index].transform.position, hand) << index;
        }
    }
}

TEST_F(GameCharacterEffectsTest, HiddenAttachmentsDropTheFlaresButKeepTheRandomOfTheSparkles)
{
    m_actor.flags78 = 0x4000;
    GameCharacterEffectsPass pass = Pass();
    pass.scan = {.complete = true, .globals = {.dword743a178 = 15}};

    const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(LiveSprites(m_sprites).empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(1));
}

// The 0x6f1/3 of 0x53fe70 (0x543f49..0x544153) with the eleven rand() calls that follow `skipped`: life 12, the turn,
// the scale, vz 2, the texture cra_04, the boot's light and two shifts of ±10, ±10 and ±20 from `point`.
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

// 0x50ff37..0x5102bd with seed 1 (41, 18467, …): 10 (41 & 3), 11 (41 % 3 = 2) and 12 (41 & 1) do not create and also
// draw the rand() % 20; 13 creates the three 0x6f1/3 (33 rand() calls) and draws the rand() % 20 (11538, which is not
// 0); 14 only the rand() % 20. The boot's light goes back to +0xf4.
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

        const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_random.State(), AfterRandoms(probe.draws));
        EXPECT_EQ(m_actor.light, light);
        const auto mists = LiveParticles(m_particles);
        ASSERT_EQ(mists.size(), probe.mists);
        if (probe.mists == 0)
            continue;
        // The light of 0x1831 (0x50ffbc): (0, 0.32f, 0.24f).
        const glm::vec3 bootsLight{0.0f, Float(0x3ea3d70au), Float(0x3e75c28fu)};
        const std::array<glm::vec3, 3> offsets{{{0.0f, -20.0f, 50.0f}, {0.0f, 0.0f, 70.0f}, {0.0f, 20.0f, 50.0f}}};
        for (std::size_t index = 0; index < mists.size(); ++index)
        {
            SCOPED_TRACE(index);
            ExpectBootsMist(mists[index], index * 11, At(0, offsets[index]), bootsLight);
        }
    }
}

// Those from 10 to 12 that do create: the three 0x6f1/3 at ±18, with the rand() that chose before; between frames only
// the one that chooses and the rand() % 20, without particles.
TEST_F(GameCharacterEffectsTest, TheBootsMistsOfEachLevelAndTheirProjectedFrame)
{
    struct Case
    {
        std::int32_t level;
        std::size_t skipped;
    };
    // With seed 1: the first multiple of 4 is the fourth rand(), that of 3 the seventh and the first even one the
    // third; the rand() % 20 that follows the three particles is not 0 in any of them (19, 3 and 7).
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

            const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

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
            // The light of 0x182f (0x50ff5e): (0.65f, 0.3f, 0.1f).
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

// Without the particle pool, the first 0x6f1/3 stops with its call.
TEST_F(GameCharacterEffectsTest, TheBootsMistNeedsTheParticlePool)
{
    m_actor.body.back().model = 0x1833;
    GameCharacterEffectsPass pass = Pass();
    pass.particles = nullptr;
    pass.scan = {.complete = true, .globals = {.dword743a178 = 13}};

    const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x51020c"), std::string::npos) << result.Error();
}

// 0x5102c3..0x5104cb, after the rand() % 20 at 0 (the fourth of seed 1): by level and logic steps, how many 0x60f/0
// orbits and how many 0x60f/0, 0x60f/0x12 and 0x610/7 joints, and how many more rand() calls. 11 draws another rand() &
// 7 (19169 & 7 is not 0: the orbit). The orbit draws four, the 0x60f/0 of width 10 two, each 0x60f/0x12 of width 20
// three and the 0x610/7 two. With +13, the two 0x60f/0x12 and the orbit with the 0x610/7 are independent branches
// (Q-C381).
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

        const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

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

// 11 with rand() & 7 at 0 (0x51037c): the 0x60f/0 of width 10 with the character's light at 1, two rand() calls.
TEST_F(GameCharacterEffectsTest, ThePlusElevenSparkleJointTakesTheWhiteLightOfItsOwner)
{
    const std::size_t skipped =
        RandomsBeforePair([](int value) { return value % 20 == 0; }, [](int value) { return (value & 7) == 0; });
    for (std::size_t skip = 0; skip < skipped; ++skip)
        static_cast<void>(m_random.Next());
    GameCharacterEffectsPass pass = Pass();
    pass.scan = {.complete = true, .globals = {.dword743a178 = 11}};

    const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

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

// Between frames, only the rand() calls that choose (the % 20 and, with +11, the & 7): neither orbits nor joints.
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

        const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_random.State(), AfterRandoms(skipped + 1 + (level == 11 ? 1 : 0)));
        EXPECT_TRUE(LiveParticles(m_particles).empty());
        EXPECT_TRUE(LiveJoints(m_joints).empty());
    }
}

// Without joints, the first of the two 0x60f/0x12 stops with its call.
TEST_F(GameCharacterEffectsTest, TheWideSparklesNeedTheJoints)
{
    const std::size_t skipped = RandomsBefore([](int value) { return value % 20 == 0; });
    for (std::size_t skip = 0; skip < skipped; ++skip)
        static_cast<void>(m_random.Next());
    GameCharacterEffectsPass pass = Pass();
    pass.joints = {};
    pass.logicSteps = 6;
    pass.scan = {.complete = true, .globals = {.dword743a178 = 13}};

    const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x510435"), std::string::npos) << result.Error();
}

// 0x510a7a and 0x510ef0: with +0x88 at 1 and +0x369 at 1, outside maps 11 to 23 and 30 and in an original frame, the
// effect 0x140 at the point of bone 20 (z 60 higher), its ten 0x5c6/4 sparks (thirty rand() calls) and +0x36c at the
// clock; then, with less than 5000 of difference, nothing, and with more, another.
TEST_F(GameCharacterEffectsTest, TheGuildMarkRisesWithPlus369OutsideTheEventMaps)
{
    struct Case
    {
        std::uint8_t byte88;
        std::uint8_t byte369;
        std::int32_t map;
        bool originalFrame;
        bool rises;
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
        m_actor.dword36c = 0;
        m_random = CrtRandom{1};
        for (auto& effect : m_effects.Effects())
            effect.alive = false;
        for (auto& particle : m_particles.All())
            particle.alive = false;
        GameCharacterEffectsPass pass = Pass();
        pass.map = probe.map;
        pass.originalFrame = probe.originalFrame;

        const auto result = Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        const Client::World::ModelEffect& mark = m_effects.Effects()[0];
        EXPECT_EQ(mark.alive, probe.rises);
        if (!probe.rises)
        {
            EXPECT_EQ(m_actor.dword36c, 0u);
            continue;
        }
        EXPECT_EQ(mark.type, 0x140);
        EXPECT_EQ(mark.ownerContext, &m_actor);
        EXPECT_EQ(mark.life, 130);
        const glm::vec3 point = At(20, {});
        EXPECT_EQ(mark.transform.position.x, point.x);
        EXPECT_EQ(mark.transform.position.y, point.y);
        EXPECT_EQ(mark.transform.position.z, point.z + 60.0f);
        EXPECT_EQ(m_actor.dword36c, std::bit_cast<std::uint32_t>(1000.0f));
        EXPECT_EQ(std::ranges::count_if(m_particles.All(), [](const Client::World::Particle& particle)
                                        { return particle.alive && particle.type == 0x5c6 && particle.subtype == 4; }),
                  10);
        CrtRandom expected{1};
        for (int draw = 0; draw < 30; ++draw)
            static_cast<void>(expected.Next());
        EXPECT_EQ(m_random.State(), expected.State());
    }
    // With 5000 or less since +0x36c, nothing; with more, another 0x140.
    m_actor.byte88 = 1;
    m_actor.byte369 = 1;
    for (auto& effect : m_effects.Effects())
        effect.alive = false;
    m_actor.dword36c = std::bit_cast<std::uint32_t>(1000.0f);
    GameCharacterEffectsPass soon = Pass();
    soon.worldClock = 6000.0f;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, soon).HasValue());
    EXPECT_FALSE(m_effects.Effects()[0].alive);
    GameCharacterEffectsPass later = Pass();
    later.worldClock = 6001.0f;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacterEffects(m_actor, later).HasValue());
    EXPECT_TRUE(m_effects.Effects()[0].alive);
    EXPECT_EQ(m_actor.dword36c, std::bit_cast<std::uint32_t>(6001.0f));
}

// A synthetic raven: one bone and five meshes of one triangle, to see which meshes it draws and the shadow.
class RavenModels final : public Client::Models::IModelLibrary
{
public:
    RavenModels()
    {
        using namespace Client::Math;
        using namespace Client::Models;
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

    // With `count` bones, all roots; bone 6 shifted to (1, 2, 3) and turned 90 in z, for the beak of the 0x113
    // (0x418bc8).
    void GrowBones(std::size_t count)
    {
        using namespace Client::Math;
        using namespace Client::Models;
        SkeletonBone bone = m_skeleton.bones[0];
        m_skeleton.bones.assign(count, bone);
        if (count > 6)
        {
            m_skeleton.bones[6].actions[0][0].position = {1.0f, 2.0f, 3.0f};
            m_skeleton.bones[6].actions[0][0].rotation = AngleQuaternion({0.0f, 0.0f, 1.5707964f});
        }
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId model) const override
    {
        return model.Value() == Client::Scenes::DarkSpiritModel ? &m_model : nullptr;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return model.Value() == Client::Scenes::DarkSpiritModel ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return m_textures;
    }

private:
    Client::Models::Skeleton m_skeleton;
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
        m_lighting = std::make_unique<Client::Terrain::TerrainLighting>();
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
                .originalFrame = true,
                .random = &m_random,
                .world = m_world.get(),
                .jointHooks = &JointHooks,
                .effectHooks = &EffectHooks};
    }

    ActorState m_owner;
    Client::Crt::CrtRandom m_random;
    std::unique_ptr<Client::Kernel::WorldState> m_world = std::make_unique<Client::Kernel::WorldState>();
    RavenModels m_models;
    Client::Terrain::WorldTerrain m_terrain;
    std::unique_ptr<Client::Terrain::TerrainLighting> m_lighting;
    Client::Terrain::TerrainPolygon m_polygon;
    Client::Render::ModelBatch m_batch;
};

TEST_F(GameRavenTest, OutOfTheTrapeziumTheRavenSlowsDownAndForgetsItsStateWithoutDrawing)
{
    // A trapezoid of one cell at (200, 200), far from the raven at (10.5, 10.6).
    m_polygon = {.x = {200.0f, 201.0f, 201.0f, 200.0f}, .y = {200.0f, 200.0f, 201.0f, 201.0f}, .everything = false};
    m_owner.darkSpirit->state = 1;
    m_owner.darkSpirit->speed = 1.0f;

    const auto result = Client::Actors::Drawing::DrawGameRaven(m_owner, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_owner.darkSpirit->speed, 3.0f);
    EXPECT_EQ(m_owner.darkSpirit->state, 0);
    EXPECT_TRUE(m_batch.Vertices().empty());
}

TEST_F(GameRavenTest, ASeenRavenDrawsMeshesThreeAndTwoAndTheShadowOfAllAndKeepsItsPose)
{
    Client::Kernel::PoseBuffer carry;
    GameRavenPass pass = Pass();
    pass.carry = &carry;

    const auto result = Client::Actors::Drawing::DrawGameRaven(m_owner, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), (2u + 5u) * 3u);
    EXPECT_EQ(m_owner.darkSpirit->pose.size(), 1u);
    // 0x600a1c: the shadow's origin in the model, at the terrain height under the raven.
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

        const auto result = Client::Actors::Drawing::DrawGameRaven(m_owner, Pass(probe.map));

        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), probe.vertices);
        if (probe.vertices > 6)
        {
            EXPECT_EQ(m_batch.Vertices().back().color.alpha, probe.shadowAlpha);
        }
    }
}

// 0x600a24..0x600a2c calls 0x4eff40, which in map 30 opens the shadow with 2500 and not 2000 (0x4eff8d..0x4eff9f). The
// shadow's vertices are those of the base pass flattened from the raven to the terrain height.
TEST_F(GameRavenTest, InCastleSiegeTheShadowFansOutWith2500)
{
    for (const std::int32_t map : {0, 30})
    {
        SCOPED_TRACE(map);
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Client::Actors::Drawing::DrawGameRaven(m_owner, Pass(map));

        ASSERT_TRUE(result.HasValue()) << result.Error();
        const auto& vertices = m_batch.Vertices();
        ASSERT_EQ(vertices.size(), 21u);
        const glm::vec3 ground(1050.0f, 1060.0f, 0.0f);
        const float spread = map == 30 ? 2500.0f : 2000.0f;
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const glm::vec3 body = vertices[corner].position;
            const glm::vec3 shadow = vertices[6 + corner].position;
            EXPECT_EQ(shadow, Client::Models::FlattenOnTheGround(body, ground, spread)) << corner;
        }
    }
}

TEST_F(GameRavenTest, BetweenOriginalFramesTheRavenIsDrawnWithoutTouchingItsState)
{
    GameRavenPass pass = Pass(18);
    pass.originalFrame = false;
    pass.heroByte1a1 = 1;

    const auto result = Client::Actors::Drawing::DrawGameRaven(m_owner, pass);

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

    const auto first = Client::Actors::Drawing::DrawGameRaven(m_owner, pass);

    ASSERT_TRUE(first.HasValue()) << first.Error();
    // 0.155f − 0.15f falls below 0.01f: 0x5ffdf0 returns without posing.
    EXPECT_LT(m_owner.darkSpirit->alpha, 0.01f);
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_TRUE(m_owner.darkSpirit->pose.empty());
}

// 0x420e00..0x420e11: with the owner's +0x78 & 0x4000, 0x4182f0(10) and 0x602e90(raven, 0, 0, 10). 0x5ffdf0 poses it
// the same (the pose of +0x120 and the shared model in its place, not on the floor) and 0x6002e0 goes through 0x60033c:
// the raven's +0x108 is 0, so the siege loop does not find it, not even with the siege; the model's color at 1 and the
// five meshes with 0x4ecc30(mesh, 0x4040, 0.5, …, 0x620), additive with Chrome02, each at half the previous one's color
// (Q-C95), without the glColor3f(1, 1, 1) or meshes 3 and 2 of case 0xc0, and without the shadow or the terrain heights
// (0x600a0b). The UVs, with the normal (0, 0, 1) times the scale 0.7 and the clock at 0: u = 0.7f and v = (1 − 0.7f) −
// 0.7f · 2.5.
TEST_F(GameRavenTest, TheOwnersFlag4000DrawsTheRavenAsAChromeShellWithoutShadow)
{
    m_owner.flags78 = 0x4000;
    m_terrain.heights.reset();
    m_terrain.attributes.reset();
    Client::Kernel::PoseBuffer carry;
    GameRavenPass pass = Pass(30);
    pass.carry = &carry;

    const auto result = Client::Actors::Drawing::DrawGameRaven(m_owner, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Commands().size(), 1u);
    EXPECT_EQ(m_batch.Commands()[0].state,
              (Client::Render::ModelDrawState{Client::Render::TextureSlot{0x620}, Client::Render::ModelBlend::Additive,
                                              true}));
    const auto& vertices = m_batch.Vertices();
    ASSERT_EQ(vertices.size(), 5u * 3u);
    const float scaled = 0.7f;
    const glm::vec2 uv(scaled,
                       static_cast<float>((1.0 - static_cast<double>(scaled)) - static_cast<double>(scaled) * 2.5));
    float factor = 1.0f;
    for (std::size_t mesh = 0; mesh < 5; ++mesh)
    {
        factor *= 0.5f;
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const auto& vertex = vertices[mesh * 3 + corner];
            EXPECT_EQ(vertex.color.red, factor) << mesh;
            EXPECT_EQ(vertex.color.green, factor) << mesh;
            EXPECT_EQ(vertex.color.blue, factor) << mesh;
            EXPECT_EQ(vertex.uv, uv) << mesh;
        }
    }
    EXPECT_EQ(m_owner.darkSpirit->pose.size(), 1u);
    EXPECT_EQ(carry.models.at(Client::Scenes::DarkSpiritModel).position, glm::vec3(1050.0f, 1060.0f, 300.0f));
}

// 0x418aab..0x418b71: in state 5, with +0x38d up to 2, after the draw, ten 0x5ac/1 joints from the raven's position and
// angles, without an owner, each of width rand() % 40 + 20 (its rand() before each addition, which draws its own), with
// the 0x4000 or without it; with +0x38d at 1, then the 0x117/3 with the angles (45, rand() % 180 − 90, 0) and the light
// (1, 0.8f, 0.6f), without more rand(). Without the world's tables, it stops at the first addition.
TEST_F(GameRavenTest, TheAttackDropsItsJointsAndTheDarkLordSkill)
{
    for (const std::uint32_t flags : {0u, 0x4000u})
        for (const std::uint8_t counter : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}})
        {
            SCOPED_TRACE(flags);
            SCOPED_TRACE(counter);
            m_world = std::make_unique<Client::Kernel::WorldState>();
            m_owner.flags78 = flags;
            Client::Scenes::DarkSpirit& spirit = *m_owner.darkSpirit;
            spirit.state = 5;
            spirit.byte38d = counter;
            spirit.angles = {10.0f, 0.0f, 30.0f};
            m_batch.Begin(Client::Render::Pass3D{});
            CrtRandom oracle = m_random;
            auto twin = std::make_unique<std::array<Client::Scenes::JointState, 500>>();
            std::array<float, 10> widths{};
            for (float& width : widths)
            {
                width = static_cast<float>(oracle.Next() % 40 + 20);
                glm::vec3 from = spirit.position;
                glm::vec3 angles = spirit.angles;
                const Client::Kernel::JointRequest request{.type = 0x5ac,
                                                           .firstPosition = from,
                                                           .secondPosition = from,
                                                           .angles = angles,
                                                           .subtype = 1,
                                                           .ownerContext = nullptr,
                                                           .scale = width,
                                                           .key = -1,
                                                           .skill = 0,
                                                           .skillSerial = 0,
                                                           .target = -1};
                ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(*twin, request, oracle).HasValue());
            }
            const auto turn = counter == 1 ? static_cast<std::int32_t>(oracle.Next() % 180) : 0;

            const auto attacked = Client::Actors::Drawing::DrawGameRaven(m_owner, Pass());

            ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
            EXPECT_EQ(m_batch.Vertices().size(), flags == 0 ? 21u : 15u);
            EXPECT_EQ(m_random.State(), oracle.State());
            const auto& joints = m_world->DrawTail().joints;
            for (std::size_t slot = 0; slot < joints.size(); ++slot)
            {
                ASSERT_EQ(joints[slot].alive, (*twin)[slot].alive) << slot;
                if (!joints[slot].alive)
                    continue;
                EXPECT_EQ(joints[slot].type, 0x5ac);
                EXPECT_EQ(joints[slot].subtype, 1);
                EXPECT_EQ(joints[slot].width, widths[slot]);
                EXPECT_EQ(joints[slot].position, (*twin)[slot].position);
                EXPECT_EQ(joints[slot].angles, (*twin)[slot].angles);
                EXPECT_EQ(joints[slot].ownerContext, nullptr);
            }
            EXPECT_EQ(std::ranges::count_if(joints, [](const auto& joint) { return joint.alive; }), 10);
            const auto& skill = m_world->Map().effects.Effects()[0];
            if (counter != 1)
            {
                EXPECT_FALSE(skill.alive);
                continue;
            }
            ASSERT_TRUE(skill.alive);
            EXPECT_EQ(skill.type, 0x117);
            EXPECT_EQ(skill.subtype, 3);
            EXPECT_EQ(skill.life, 10);
            EXPECT_EQ(skill.transform.position, spirit.position);
            EXPECT_EQ(skill.transform.angles, glm::vec3(45.0f, static_cast<float>(turn - 90), 0.0f));
            EXPECT_EQ(skill.light,
                      glm::vec3(1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f19999au)));
            EXPECT_EQ(skill.ownerContext, nullptr);
        }

    // Without the joint table it stops at the first 0x530860, after the rand() % 40 of 0x418ad6.
    GameRavenPass bare = Pass();
    bare.world = nullptr;
    m_owner.darkSpirit->byte38d = 0;
    CrtRandom before = m_random;
    const auto cut = Client::Actors::Drawing::DrawGameRaven(m_owner, bare);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x418aff"), std::string::npos) << cut.Error();
    static_cast<void>(before.Next());
    EXPECT_EQ(m_random.State(), before.State());
}

// 0x418b7c..0x418c0a: in state 5, with +0x38d at 3 nothing; odd from 4 on and more than 100 above the owner, the
// 0x113/0 at the beak (bone 6 of the pose of +0x120 with (50, 0, 0), 0x4ec110 without scale), with the raven's angles
// and light and the raven as owner (life 15, scale 0.6f), without rand(). With fewer than seven bones in the pose, it
// stops at 0x418be4.
TEST_F(GameRavenTest, TheAttackEffectNeedsAnOddCounterAndHeight)
{
    m_models.GrowBones(7);
    m_owner.darkSpirit->state = 5;
    m_owner.darkSpirit->angles = {0.0f, 0.0f, 45.0f};
    m_owner.darkSpirit->light = {0.25f, 0.5f, 0.75f};
    for (const std::uint8_t counter : {std::uint8_t{3}, std::uint8_t{4}, std::uint8_t{6}})
    {
        m_owner.darkSpirit->byte38d = counter;
        const auto quiet = Client::Actors::Drawing::DrawGameRaven(m_owner, Pass());
        ASSERT_TRUE(quiet.HasValue()) << quiet.Error();
    }
    EXPECT_FALSE(m_world->Map().effects.Effects()[0].alive);
    m_owner.darkSpirit->byte38d = 5;
    const auto high = Client::Actors::Drawing::DrawGameRaven(m_owner, Pass());
    ASSERT_TRUE(high.HasValue()) << high.Error();
    const Client::Scenes::DarkSpirit& spirit = *m_owner.darkSpirit;
    ASSERT_EQ(spirit.pose.size(), 7u);
    const auto& air = m_world->Map().effects.Effects()[0];
    ASSERT_TRUE(air.alive);
    EXPECT_EQ(air.type, 0x113);
    EXPECT_EQ(air.subtype, 0);
    EXPECT_EQ(air.life, 15);
    EXPECT_EQ(air.transform.scale, 0.6f);
    EXPECT_EQ(air.transform.position, Client::Math::OriginalVectorTransform({50.0f, 0.0f, 0.0f}, spirit.pose[6]));
    EXPECT_NE(air.transform.position, spirit.position);
    EXPECT_EQ(air.transform.angles, spirit.angles);
    EXPECT_EQ(air.light, spirit.light);
    EXPECT_EQ(air.ownerContext, &spirit);
    EXPECT_TRUE(air.hasOwner);
    EXPECT_FALSE(m_world->Map().effects.Effects()[1].alive);
    // 300 does not exceed 200 + 100.
    m_owner.position.z = 200.0f;
    const auto low = Client::Actors::Drawing::DrawGameRaven(m_owner, Pass());
    EXPECT_TRUE(low.HasValue()) << low.Error();
    EXPECT_FALSE(m_world->Map().effects.Effects()[1].alive);
    EXPECT_EQ(m_random.State(), Client::Crt::CrtRandom{}.State());

    m_owner.position.z = 0.0f;
    m_models.GrowBones(6);
    const auto shortPose = Client::Actors::Drawing::DrawGameRaven(m_owner, Pass());
    ASSERT_FALSE(shortPose.HasValue());
    EXPECT_NE(shortPose.Error().find("0x418be4"), std::string::npos) << shortPose.Error();
}

// 0x41896d: in state 6, with the draw's pose, rand() & 1 chooses the first bone; that of the test model (bone 0)
// releases the 0x5ac/6 particle at the bone's point in the world (0x4ec110 of (0, 0, 0) with the pose of +0x120), with
// the raven's angles, the light (1, 0.6f, 0.4f) and the scale 1.3f, with its rand() calls (0x53fe70). From 1 on there
// are no more bones. With the pool full, none, and it goes on. Without the particles, MuRet stops at the first.
TEST_F(GameRavenTest, TheShoutSparksFromEveryOtherBone)
{
    for (const std::uint32_t seed : {1u, 2u, 3u, 4u})
    {
        SCOPED_TRACE(seed);
        m_random = Client::Crt::CrtRandom(seed);
        m_owner.darkSpirit->state = 6;
        m_owner.darkSpirit->byte38d = 0;
        m_owner.darkSpirit->angles = {0.0f, 0.0f, 120.0f};
        auto particles = std::make_unique<Client::World::Particles>();
        GameRavenPass pass = Pass();
        pass.particles = particles.get();
        pass.worldClock = 500.0f;
        Client::Crt::CrtRandom expected = m_random;
        const int first = expected.Next() & 1;

        const auto shout = Client::Actors::Drawing::DrawGameRaven(m_owner, pass);

        ASSERT_TRUE(shout.HasValue()) << shout.Error();
        ASSERT_EQ(m_owner.darkSpirit->pose.size(), 1u);
        std::size_t live = 0;
        for (const auto& particle : particles->All())
            live += particle.alive ? 1u : 0u;
        if (first == 0)
        {
            auto made = std::make_unique<Client::World::Particles>();
            const glm::vec3 point = Client::Math::OriginalVectorTransform(glm::vec3(0.0f), m_owner.darkSpirit->pose[0]);
            ASSERT_TRUE(
                made->CreateChecked(0x5ac, point, m_owner.darkSpirit->angles,
                                    {1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3ecccccdu)}, 6,
                                    std::bit_cast<float>(0x3fa66666u), 500.0f, expected)
                    .HasValue());
            ASSERT_EQ(live, 1u);
            const auto& particle = particles->All()[0];
            EXPECT_EQ(particle.type, 0x5ac);
            EXPECT_EQ(particle.subtype, 6);
            EXPECT_EQ(particle.position, point);
            EXPECT_EQ(particle.light, made->All()[0].light);
            EXPECT_EQ(particle.scale, made->All()[0].scale);
            EXPECT_EQ(particle.life, made->All()[0].life);
        }
        else
        {
            EXPECT_EQ(live, 0u);
        }
        EXPECT_EQ(m_random.State(), expected.State());
    }

    // With the pool full, none and without their rand() calls; without the particles, it stops. Seed 3 starts with bone
    // 0.
    constexpr std::uint32_t BoneZero = 3;
    Client::Crt::CrtRandom afterOne(BoneZero);
    ASSERT_EQ(afterOne.Next() & 1, 0);
    m_random = Client::Crt::CrtRandom(BoneZero);
    auto full = std::make_unique<Client::World::Particles>();
    for (auto& particle : full->All())
        particle.alive = true;
    GameRavenPass crowded = Pass();
    crowded.particles = full.get();
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameRaven(m_owner, crowded).HasValue());
    EXPECT_EQ(m_random.State(), afterOne.State());
    m_random = Client::Crt::CrtRandom(BoneZero);
    const auto missing = Client::Actors::Drawing::DrawGameRaven(m_owner, Pass());
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x418a26"), std::string::npos) << missing.Error();
}

// 0x418a51: with +0x38d at 15, without a character in +0x3b0 it only sets the counter back to 0; with one, 0x4ee770(1,
// 0x5ac, 1, raven+0x1c, the character) on mesh 1 of the raven just posed (at each corner of triangle 0, rand() % 90 and
// rand() % 120 and the 0x613/0xd joint toward the character, width 20) and 0x4164f0(0). Outside the original's frames,
// 0x418920 does not run. The corners and the normal come from the scratch 0x602e90(raven, 0, 0, …) leaves: bone 0 at
// the origin without a turn, the root of 0x4eb860 with the flag at 1 (0x5fc8f0 with (−10, 0, 30), each element times
// 0.7f in float and the origin (1050, 1060, 300)), 0x5fca90 with the identity and 0x4ebc80 with Translate at 0
// (0x5fccc0 for (0, 0, 10), (1, 0, 10) and (0, 1, 10); 0x5fcbd0 for the normal (0, 0, 1), with the scale inside). The
// bits come from a separate emulation of the x87 (exact fractions, PC53, fsin and fcos in 64 bits, each FSTP in float),
// not from MuRet.
TEST_F(GameRavenTest, TheEndOfTheShoutLooksAtTheAttackTarget)
{
    m_owner.darkSpirit->state = 6;
    m_owner.darkSpirit->byte38d = 15;
    m_owner.darkSpirit->target3b0 = 0xffff;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameRaven(m_owner, Pass()).HasValue());
    EXPECT_EQ(m_owner.darkSpirit->byte38d, 0);
    EXPECT_EQ(m_owner.darkSpirit->state, 6);

    m_owner.darkSpirit->byte38d = 15;
    m_owner.darkSpirit->target3b0 = 3;
    m_owner.darkSpirit->far = 7;
    m_owner.darkSpirit->angles = {-10.0f, 0.0f, 30.0f};
    m_owner.darkSpirit->scale = std::bit_cast<float>(0x3f333333u); // 0.7f
    GameRavenPass projected = Pass();
    projected.originalFrame = false;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameRaven(m_owner, projected).HasValue());
    EXPECT_EQ(m_owner.darkSpirit->byte38d, 15);

    Client::Kernel::PoseBuffer carry;
    auto world = std::make_unique<Client::Kernel::WorldState>();
    const auto actors = world->Actors();
    actors[3].position = {1200.0f, 1300.0f, 40.0f};
    const auto& joints = world->DrawTail().joints;
    GameRavenPass pass = Pass();
    pass.carry = &carry;
    pass.world = world.get();
    const auto end = Client::Actors::Drawing::DrawGameRaven(m_owner, pass);
    ASSERT_TRUE(end.HasValue()) << end.Error();
    EXPECT_EQ(m_owner.darkSpirit->byte38d, 0);
    EXPECT_EQ(m_owner.darkSpirit->state, 0);
    EXPECT_EQ(m_owner.darkSpirit->far, 0);
    ASSERT_GE(carry.meshes.size(), 2u);
    ASSERT_GE(carry.meshes[1].positions.size(), 3u);
    const auto bits = [](std::uint32_t x, std::uint32_t y, std::uint32_t z)
    { return glm::vec3(std::bit_cast<float>(x), std::bit_cast<float>(y), std::bit_cast<float>(z)); };
    const std::array<glm::vec3, 3> corners{bits(0x44832c8du, 0x4484a1b0u, 0x43997263u),
                                           bits(0x44833ff3u, 0x4484ace3u, 0x43997263u),
                                           bits(0x44832186u, 0x4484b4cau, 0x439962d4u)};
    ASSERT_EQ(carry.meshes[1].normals.size(), 1u);
    EXPECT_EQ(carry.meshes[1].normals[0], bits(0xbd78f129u, 0x3dd79713u, 0x3f307a40u));
    Client::Crt::CrtRandom replay;
    std::vector<Client::Scenes::JointState> twin(joints.size());
    for (std::size_t corner = 0; corner < 3; ++corner)
    {
        SCOPED_TRACE(corner);
        EXPECT_EQ(carry.meshes[1].positions[corner], corners[corner]);
        const auto pitch = static_cast<double>(replay.Next() % 90);
        const auto turn = static_cast<double>((replay.Next() % 120) - 60);
        glm::vec3 angles{static_cast<float>(-pitch), 0.0f, static_cast<float>(turn + 30.0)};
        glm::vec3 first = corners[corner];
        glm::vec3 second = actors[3].position;
        ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(twin,
                                                     {.type = 0x613,
                                                      .firstPosition = first,
                                                      .secondPosition = second,
                                                      .angles = angles,
                                                      .subtype = 0xd,
                                                      .ownerContext = &actors[3],
                                                      .scale = 20.0f,
                                                      .key = 0,
                                                      .skill = 0,
                                                      .skillSerial = 0,
                                                      .target = -1},
                                                     replay, Client::Kernel::JointOwnerKind::Actor)
                        .HasValue());
        const auto& joint = joints[corner];
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x613);
        EXPECT_EQ(joint.subtype, 0xd);
        EXPECT_EQ(joint.ownerContext, &actors[3]);
        EXPECT_EQ(joint.width, 20.0f);
        EXPECT_EQ(joint.position, twin[corner].position);
        EXPECT_EQ(joint.position, corners[corner]);
        EXPECT_EQ(joint.angles, twin[corner].angles);
        EXPECT_EQ(joint.angles, angles);
    }
    EXPECT_FALSE(joints[3].alive);
    EXPECT_EQ(m_random.State(), replay.State());

    // Without the raven posed in this draw (without the pose buffer), the scratch belongs to another model: it stops.
    m_owner.darkSpirit->state = 6;
    m_owner.darkSpirit->byte38d = 15;
    m_owner.darkSpirit->alpha = 0.0f;
    const auto stale = Client::Actors::Drawing::DrawGameRaven(m_owner, pass);
    ASSERT_FALSE(stale.HasValue());
    EXPECT_NE(stale.Error().find("0x418a8c"), std::string::npos) << stale.Error();
}

TEST_F(GameRavenTest, AnOwnerWithoutRavenDrawsNothing)
{
    m_owner.darkSpirit.reset();

    const auto result = Client::Actors::Drawing::DrawGameRaven(m_owner, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
}
} // namespace
