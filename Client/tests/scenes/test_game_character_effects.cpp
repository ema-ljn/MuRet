#include "Models/IModelLibrary.h"
#include "Scenes/GameCharacterEffects.h"

#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameParticles.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "support/ParticleDrawRecorder.h"
#include "Scenes/SocialState.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

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

// La cola de 0x50ba30 (0x50ea20..0x510ab6) y el cuervo de 0x420df0, contra el ASM de c32bbd5f. Los floats
// esperados salen de la aritmética del ASM en double y float (los bits, en hexadecimal); los rand(), del
// CRT con la semilla 1.
namespace
{
using Client::Crt::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::GameCharacterEffectsPass;
using Client::Scenes::GameRavenPass;
using Client::Scenes::PartySlot;

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
                .effects = &m_effects,
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

// 0x50f96c..0x50f9c6 y 0x50fac5..0x50fb9c: en [1,2; 1,6) el brillo 0x621/2 con el personaje de dueño y el sonido
// 0x1cc; en [7, 8) dos 0x117/2 sin dueño en la mano (+0x2bd), con (180, 45, 0) y (0, 0, z) y la luz (0,8f; 0,5f; 1).
// Fuera de las ventanas o entre frames, nada. Ninguna saca rand(). Sin el mundo, cortan.
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
        auto world = std::make_unique<Client::Scenes::WorldState>();
        GameCharacterEffectsPass pass = Pass();
        pass.originalFrame = probe.originalFrame;
        pass.world = world.get();

        const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

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
            EXPECT_EQ(world->Runtime().lastSoundId, 0x1cc);
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

        // Sin el mundo, las ventanas cortan con su dirección; fuera de ellas, nada.
        pass.world = nullptr;
        m_random = CrtRandom{1};
        const auto alone = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);
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

// 0x50fba4..0x50fbcf: con 0x4 o 0x2000000 en +0x78 y sin 0x4000, las manos brillan: un rand() y tres sprites por mano.
// Entre frames, sin los joints.
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

        const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(LiveSprites(m_sprites).size(), probe.glows ? 6u : 0u);
        EXPECT_EQ(m_random.State(), AfterRandoms(probe.glows ? 2 : 0));
        EXPECT_TRUE(LiveJoints(m_joints).empty());
    }
}

// 0x50fbe5..0x50fd5c en un frame del original: por mano, k = (rand() % 30 + 70) · 0,01f (con la semilla 1, 41 y
// 18.467: 81 y 87) y la luz (k; k · 0,3f; k · 0,2f), con k sin guardar en float para los productos; los sprites 0x5fd
// de escala 1,5 en los huesos mano, mano − 6 y mano − 7 con el corrimiento de [ebp−0x50] (acá 0). Después, sin un
// 0x10f/4 ni /9 suyo, cinco 0x10f/4 de ancho 20 desde él, con sus ángulos y el +0x3a4 con signo de blanco (+0xa0c).
TEST_F(GameCharacterEffectsTest, TheGlowFlagsBindTheCharacterWithFiveAuras)
{
    m_actor.flags78 = 0x2000000;
    m_actor.angles = {0.0f, 0.0f, 90.0f};
    m_actor.attackTarget = 0xfffe;

    const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, Pass());

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

    // Con un 0x10f/9 suyo vivo, ninguno más; los sprites y los rand() igual.
    for (auto& joint : m_joints)
        joint.alive = false;
    m_joints[7].alive = true;
    m_joints[7].type = 0x10f;
    m_joints[7].subtype = 9;
    m_joints[7].ownerContext = &m_actor;
    m_sprites = {};
    m_random = CrtRandom{1};
    ASSERT_TRUE(Client::Scenes::DrawGameCharacterEffects(m_actor, Pass()).HasValue());
    EXPECT_EQ(LiveJoints(m_joints).size(), 1u);
    EXPECT_EQ(LiveSprites(m_sprites).size(), 6u);
    EXPECT_EQ(m_random.State(), AfterRandoms(2));

    // El de otro dueño o con otro subtipo no cuenta.
    m_joints[7].subtype = 2;
    ASSERT_TRUE(Client::Scenes::DrawGameCharacterEffects(m_actor, Pass()).HasValue());
    EXPECT_EQ(LiveJoints(m_joints).size(), 6u);
}

// 0x50f9c9..0x50fac2: por debajo del frame 2 (0x6b8818) y con party, 0x59e680 sobre el nombre. Fuera de la party salta
// a 0x510a7a, sin las banderas de 0x50fba4, también entre frames. En la party, los joints 0x618 hacia otro miembro a la
// vista (+0x1c en 0 o más y no él) nacen en un frame del original (0x50fa74 y 0x50fa97: /0xc y /0xd de ancho 20, desde
// el miembro 150 más arriba, con él de dueño) y después siguen las banderas (0x4: dos rand(), seis sprites y, en un
// frame del original, cinco 0x10f/4). Con el frame en 2 o sin party, también las banderas. Cada 0x618/0xc y /0xd saca
// tres rand() (el sentido en 0x53217c y los dos giros en 0x5321af y 0x5321c5): seis por miembro, y ninguno sin joints.
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

        const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

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
    using Client::Math::Detail::CosineProductPC53;
    using Client::Math::Detail::SineProductPC53;
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
        const glm::vec3 reach =
            Client::Math::OriginalVectorRotate({0.0f, -200.0f, 0.0f}, Client::Math::OriginalAngleMatrix(angles));
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

// 0x52b984..0x52bb2c: el /0 no emite joints y va a la cola común, con un solo rand() por paso.
TEST_F(GameCharacterEffectsTest, TheLevelUpDecalExpiresWithoutAJointTable)
{
    ASSERT_TRUE(m_effects.CreatePetSummon(m_actor.position, {}, glm::vec3(0.5f), &m_actor, 0).has_value());
    Client::World::ModelEffect& decal = m_effects.Effects()[0];
    for (int step = 1; step <= 20; ++step)
    {
        const auto stepped = Client::Scenes::StepPetSummon(decal, {}, m_random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(decal.life, 20 - step);
        EXPECT_EQ(decal.alive, step < 20);
        EXPECT_EQ(decal.transform.position, m_actor.position);
        EXPECT_EQ(m_random.State(), AfterRandoms(static_cast<std::size_t>(step)));
    }
}

// Otro efecto no es el 0x61e/0, /1 ni /3, y sin la tabla de joints el /1 corta antes de sacar rand().
TEST_F(GameCharacterEffectsTest, ThePetSummonRefusesOtherEffectsAndAMissingJointTable)
{
    ASSERT_TRUE(m_effects.CreatePetSummon(m_actor.position, {}, glm::vec3(0.5f), &m_actor).has_value());
    Client::World::ModelEffect& summon = m_effects.Effects()[0];

    const auto tableless = Client::Scenes::StepPetSummon(summon, {}, m_random);
    ASSERT_FALSE(tableless.HasValue());
    EXPECT_NE(tableless.Error().find("0x5189cc"), std::string::npos) << tableless.Error();
    EXPECT_EQ(m_random.State(), 1u);
    EXPECT_EQ(summon.life, 20);

    summon.subtype = 4;
    const auto refused = Client::Scenes::StepPetSummon(summon, m_joints, m_random);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("0x52b9a6"), std::string::npos) << refused.Error();
}

// 0x52b9a6 → 0x518900 con el /3 de la 0x1c y de las invocaciones: el rand() de la luz y, como el /1, tres joints
// 619/3 (0x5189cc), cada uno con los dos rand() de su giro, con el personaje de dueño. Con la tabla de joints llena,
// ninguno y sigue: los mismos siete rand() y la cola.
TEST_F(GameCharacterEffectsTest, TheSummonDecalDropsThreeEnergyJoints)
{
    ASSERT_TRUE(
        m_effects.CreatePetSummon(m_actor.position, {}, glm::vec3(0.5f), &m_actor, Client::World::SummonDecalSubtype)
            .has_value());
    Client::World::ModelEffect& summon = m_effects.Effects()[0];

    const auto stepped = Client::Scenes::StepPetSummon(summon, m_joints, m_random);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(m_random.State(), AfterRandoms(7));
    EXPECT_EQ(summon.life, 19);
    const std::vector<int> draws = RandomsAfter(1, 6);
    const auto joints = LiveJoints(m_joints);
    ASSERT_EQ(joints.size(), 3u);
    for (std::size_t index = 0; index < joints.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(joints[index].type, 0x619);
        EXPECT_EQ(joints[index].subtype, 3);
        EXPECT_EQ(joints[index].angles, glm::vec3(static_cast<float>(draws[index * 2] % 90), 0.0f,
                                                  static_cast<float>(draws[(index * 2) + 1] % 360)));
        EXPECT_EQ(joints[index].ownerContext, &m_actor);
        EXPECT_EQ(joints[index].width, 5.0f);
        EXPECT_EQ(joints[index].life, 12);
        EXPECT_EQ(joints[index].maximumCount, 2);
    }

    for (auto& joint : m_joints)
        joint.alive = true;
    CrtRandom full{1};
    ASSERT_TRUE(Client::Scenes::StepPetSummon(summon, m_joints, full).HasValue());
    EXPECT_EQ(full.State(), AfterRandoms(7));
    EXPECT_EQ(summon.life, 18);
}

// 0x52a8a7: el 5ac/2 de la 0x35 saca el rand() de la luz, deja la vida en 10 con el dueño vivo con 0x400 (en 0 si no) y
// sube +0xd8; cada 50 gira 50 en z, se pone a 80 del dueño (el seno de la suma sin redondear, el coseno del float
// guardado) y 50 más arriba, y suelta un 619/9 de ancho 15 ahí. La cola descuenta la vida.
TEST_F(GameCharacterEffectsTest, TheGuardFlareCirclesItsTargetEveryFiftyStepsWithAnEnergyJoint)
{
    using Client::Math::Detail::CosineProductPC53;
    using Client::Math::Detail::SineProductPC53;
    m_actor.alive = true;
    m_actor.flags78 = 0x400;
    m_actor.angles = {0.0f, 0.0f, 33.3f};
    ASSERT_TRUE(m_effects
                    .CreateSkillFlare(m_actor.position, m_actor.angles, glm::vec3(0.5f),
                                      Client::World::GuardFlareSubtype, &m_actor)
                    .has_value());
    Client::World::ModelEffect& flare = m_effects.Effects()[0];
    const float tenth = Float(0x3dcccccdu);

    for (int step = 1; step <= 50; ++step)
    {
        SCOPED_TRACE(step);
        const glm::vec3 before = flare.transform.position;
        const float yaw = flare.transform.angles.z;
        const auto stepped = Client::Scenes::StepSkillFlare(flare, m_joints, m_random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(m_random.State(), AfterRandoms(static_cast<std::size_t>(step)));
        EXPECT_EQ(flare.speed, static_cast<float>(step));
        EXPECT_EQ(flare.life, 9);
        if (step == 50)
        {
            const double turned = static_cast<double>(yaw) + 50.0;
            const float storedYaw = static_cast<float>(turned);
            EXPECT_EQ(flare.transform.angles.z, storedYaw);
            const glm::vec3 at{
                static_cast<float>(*SineProductPC53(turned * static_cast<double>(tenth), 80.0f) +
                                   static_cast<double>(m_actor.position.x)),
                static_cast<float>(
                    *CosineProductPC53(static_cast<double>(storedYaw) * static_cast<double>(tenth), 80.0f) +
                    static_cast<double>(m_actor.position.y)),
                static_cast<float>(static_cast<double>(m_actor.position.z) + 50.0)};
            EXPECT_EQ(flare.transform.position, at);
        }
        else
        {
            EXPECT_EQ(flare.transform.angles.z, yaw);
            EXPECT_EQ(flare.transform.position, before);
        }
    }
    const auto joints = LiveJoints(m_joints);
    ASSERT_EQ(joints.size(), 1u);
    EXPECT_EQ(joints[0].type, 0x619);
    EXPECT_EQ(joints[0].subtype, 9);
    EXPECT_EQ(joints[0].ownerContext, &m_actor);
    EXPECT_EQ(joints[0].width, 15.0f);
    EXPECT_EQ(joints[0].key, -1);

    // Sin 0x400 la vida queda en 0 y muere en la cola; con 0x4000 en el dueño, no cuenta ni se corre.
    m_actor.flags78 = 0x4000;
    const float counted = flare.speed;
    ASSERT_TRUE(Client::Scenes::StepSkillFlare(flare, m_joints, m_random).HasValue());
    EXPECT_EQ(flare.speed, counted);
    EXPECT_FALSE(flare.alive);
    EXPECT_EQ(flare.life, -1);
    EXPECT_EQ(m_random.State(), AfterRandoms(51));
}

// 0x52a866/0x52a89a: el selector precede las dos altas, aun con la tabla llena o en el paso final.
TEST_F(GameCharacterEffectsTest, TheSwellFlareEmitsItsMirroredBonePairBeforeTheCommonTail)
{
    for (const std::size_t freeSlots : std::array<std::size_t, 3>{0, 1, 2})
    {
        SCOPED_TRACE(freeSlots);
        m_random = CrtRandom(1);
        for (std::size_t slot = 0; slot < m_particles.All().size(); ++slot)
            m_particles.All()[slot].alive = slot >= freeSlots;
        m_effects.Clear();
        m_actor.alive = true;
        m_actor.flags78 = 0x10;
        ASSERT_TRUE(m_effects
                        .CreateSkillFlare(m_actor.position, m_actor.angles, glm::vec3(0.5f),
                                          Client::World::SwellFlareSubtype, &m_actor)
                        .has_value());
        auto& flare = m_effects.Effects()[0];
        const Client::World::ParticleOwner owner{Client::World::ParticleOwnerKind::Actor, Slot};

        const auto stepped = Client::Scenes::StepSkillFlare(flare, m_joints, m_random, &m_particles, owner);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(m_random.State(), AfterRandoms(2));
        EXPECT_EQ(flare.life, 9);
        EXPECT_TRUE(flare.alive);
        for (std::size_t slot = 0; slot < freeSlots; ++slot)
        {
            const auto& child = m_particles.All()[slot];
            EXPECT_EQ(child.rotation, slot == 0 ? 26.0f : 35.0f); // CRT(1): R2=18467, k=1.
            EXPECT_EQ(child.typedOwner, owner);
            EXPECT_EQ(child.life, 10);
            EXPECT_EQ(child.scale, 2.0f);
            EXPECT_EQ(child.position, flare.transform.position);
            EXPECT_EQ(child.angles, glm::vec3(0));
            EXPECT_EQ(child.light, glm::vec3(1, 0.5f, 0.1f));
        }
    }
    m_particles.Clear();
    auto& flare = m_effects.Effects()[0];
    m_actor.flags78 = 0x4010;
    ASSERT_TRUE(Client::Scenes::StepSkillFlare(flare, m_joints, m_random, &m_particles).HasValue());
    EXPECT_EQ(flare.life, 9);
    EXPECT_TRUE(LiveParticles(m_particles).empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
    m_actor.flags78 = 0x10;
    m_actor.alive = false;
    ASSERT_TRUE(Client::Scenes::StepSkillFlare(flare, m_joints, m_random, &m_particles).HasValue());
    EXPECT_FALSE(flare.alive);
    EXPECT_EQ(LiveParticles(m_particles).size(), 2u);
    EXPECT_EQ(m_random.State(), AfterRandoms(5));

    m_particles.Clear();
    flare.alive = true;
    m_actor.alive = true;
    m_actor.flags78 = 0;
    ASSERT_TRUE(Client::Scenes::StepSkillFlare(flare, m_joints, m_random, &m_particles).HasValue());
    EXPECT_FALSE(flare.alive);
    EXPECT_EQ(LiveParticles(m_particles).size(), 2u);
    EXPECT_EQ(m_random.State(), AfterRandoms(7));

    flare.alive = true;
    flare.ownerContext = nullptr;
    const auto ownerless = Client::Scenes::StepSkillFlare(flare, m_joints, m_random, &m_particles);
    ASSERT_FALSE(ownerless.HasValue());
    EXPECT_NE(ownerless.Error().find("0x52a7ec"), std::string::npos);
    EXPECT_EQ(flare.life, 0);
}

TEST_F(GameCharacterEffectsTest, TheSwellBoneTableCoversAllSevenMirroredPairs)
{
    constexpr std::array<float, 7> Bones{25, 26, 27, 20, 34, 35, 36};
    m_actor.alive = true;
    m_actor.flags78 = 0x10;
    for (std::size_t index = 0; index < Bones.size(); ++index)
    {
        m_particles.Clear();
        m_effects.Clear();
        m_random = CrtRandom(1);
        const auto skipped = RandomsBeforePair([](int) { return true; }, [index](int value)
                                               { return static_cast<std::size_t>(value % 7) == index; });
        for (std::size_t draw = 0; draw < skipped; ++draw)
            (void)m_random.Next();
        ASSERT_TRUE(m_effects.CreateSkillFlare({}, {}, {}, Client::World::SwellFlareSubtype, &m_actor).has_value());
        const auto stepped = Client::Scenes::StepSkillFlare(m_effects.Effects()[0], m_joints, m_random, &m_particles,
                                                            {Client::World::ParticleOwnerKind::Actor, Slot});
        ASSERT_TRUE(stepped.HasValue());
        ASSERT_EQ(LiveParticles(m_particles).size(), 2u);
        EXPECT_EQ(m_particles.All()[0].rotation, Bones[index]);
        EXPECT_EQ(m_particles.All()[1].rotation, Bones[Bones.size() - 1 - index]);
        EXPECT_EQ(m_random.State(), AfterRandoms(skipped + 2));
    }
}

TEST(SwellLifePipelineTest, RunsFromParentToRetainedActorBonesAndExistingFlareDraw)
{
    auto world = std::make_unique<Client::Scenes::WorldState>();
    auto& actor = world->Actors()[3];
    actor.type = 0x20a;
    actor.alive = true;
    actor.byte88 = 1;
    actor.flags78 = 0x10;
    actor.scale = 7; // Leído por otros efectos: esta partícula no aplica escala.
    actor.position = {100, 200, 300};
    actor.bodyPose.assign(40, Client::Math::BoneTransform(1));
    actor.bodyPose[36][3] = {11, 22, 33};
    actor.bodyPose[25][3] = {44, 55, 66};
    world->PlaceModel(actor);
    auto& effects = world->Map().effects;
    ASSERT_TRUE(
        effects.CreateSkillFlare({1, 2, 3}, {}, glm::vec3(1), Client::World::SwellFlareSubtype, &actor).has_value());
    CrtRandom random(7);
    const Client::World::ParticleOwner owner{Client::World::ParticleOwnerKind::Actor, 3};
    ASSERT_TRUE(Client::Scenes::StepSkillFlare(effects.Effects()[0], world->DrawTail().joints, random,
                                               &world->Particles(), owner)
                    .HasValue());
    const auto resolver = Client::Scenes::ParticleOwnerBonesOf(*world);
    ASSERT_TRUE(
        Client::Scenes::StepGameParticles(world->Particles(), random, 0, {}, 0, {}, {}, {}, nullptr, {}, resolver)
            .HasValue());
    const auto children = LiveParticles(world->Particles());
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0].rotation, 36.0f);
    EXPECT_EQ(children[1].rotation, 25.0f);
    constexpr std::array SpeedBits{0x40b970a4u, 0x410bd70au};
    constexpr std::array ScaleBits{0x3ff9652cu, 0x3ff86595u};
    for (std::size_t slot = 0; slot < children.size(); ++slot)
    {
        auto child = children[slot];
        EXPECT_EQ(std::bit_cast<std::uint32_t>(child.verticalSpeed), SpeedBits[slot]);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(child.scale), ScaleBits[slot]);
        EXPECT_EQ(child.light, (glm::vec3{Float(0x3f3da12fu), Float(0x3ebda12fu), Float(0x3d97b426u)}));
        const glm::vec3 base = slot == 0 ? glm::vec3(111, 222, 333) : glm::vec3(144, 255, 366);
        EXPECT_EQ(
            child.position,
            (glm::vec3{base.x, base.y, static_cast<float>(static_cast<double>(base.z) + Float(SpeedBits[slot]))}));
        const auto drawn = Client::Tests::DrawOneParticle(child, glm::vec2(64), std::uint8_t{3});
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        EXPECT_EQ(drawn.Value()->texture, 0x5ac);
        EXPECT_EQ(drawn.Value()->blend, Client::Tests::RecordedParticleDraw::Blend::Additive);
        EXPECT_EQ(drawn.Value()->light, child.light);
        EXPECT_EQ(drawn.Value()->rotation, child.rotation);
    }
    EXPECT_EQ(random.State(), 0x2e12d22fu);
    // Reusar el lugar del actor conserva la dirección nativa: alive/flags no invalidan, tipo/byte88 sí.
    actor.alive = false;
    actor.flags78 = 0;
    actor.position = {500, 600, 700};
    EXPECT_EQ(resolver(children[0]), std::optional(glm::vec3(511, 622, 733)));
    actor.type = 0x194;
    EXPECT_FALSE(resolver(children[0]).has_value());
    actor.type = 0x20a;
    actor.byte88 = 0;
    EXPECT_FALSE(resolver(children[0]).has_value());
    actor.byte88 = 1;
    actor.bodyPose.clear();
    EXPECT_EQ(resolver(children[0]), std::optional(actor.position));
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

// Q-C350 también en las acciones: la piel del mago (0x50ea86) deja (−4, 11, 0) en [ebp−0x50] y 0x4ec110 lo usa en la
// mano (+0x2bd, 0x21) de los rayos de la 0x94 (0x50f89b) y de los 0x117/2 de la 0x51 y la 0x5b (0x50fb16).
TEST_F(GameCharacterEffectsTest, TheMageSkinOffsetAlsoMovesTheHandOfTheActions)
{
    m_actor.skin = 8;
    const glm::vec3 hand = At(0x21, {-4.0f, 11.0f, 0.0f});
    m_actor.animation.action = 0x94;
    m_actor.animation.frame = 3.0f;
    ASSERT_TRUE(Client::Scenes::DrawGameCharacterEffects(m_actor, Pass()).HasValue());
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
        auto world = std::make_unique<Client::Scenes::WorldState>();
        GameCharacterEffectsPass pass = Pass();
        pass.world = world.get();
        const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);
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

// 0x510a7a y 0x510ef0: con +0x88 en 1 y +0x369 en 1, fuera de los mapas 11 a 23 y 30 y en un frame del original, el
// efecto 0x140 en el punto del hueso 20 (z 60 más), sus diez chispas 0x5c6/4 (treinta rand()) y +0x36c en el reloj;
// después, con menos de 5000 de diferencia, nada, y con más, otro.
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

        const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

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
    // Con 5000 o menos desde +0x36c, nada; con más, otro 0x140.
    m_actor.byte88 = 1;
    m_actor.byte369 = 1;
    for (auto& effect : m_effects.Effects())
        effect.alive = false;
    m_actor.dword36c = std::bit_cast<std::uint32_t>(1000.0f);
    GameCharacterEffectsPass soon = Pass();
    soon.worldClock = 6000.0f;
    ASSERT_TRUE(Client::Scenes::DrawGameCharacterEffects(m_actor, soon).HasValue());
    EXPECT_FALSE(m_effects.Effects()[0].alive);
    GameCharacterEffectsPass later = Pass();
    later.worldClock = 6001.0f;
    ASSERT_TRUE(Client::Scenes::DrawGameCharacterEffects(m_actor, later).HasValue());
    EXPECT_TRUE(m_effects.Effects()[0].alive);
    EXPECT_EQ(m_actor.dword36c, std::bit_cast<std::uint32_t>(6001.0f));
}

// Un cuervo sintético: un hueso y cinco mallas de un triángulo, para ver qué mallas dibuja y la sombra.
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

    // Con `count` huesos, todos raíz; el 6 corrido a (1, 2, 3) y girado 90 en z, para el pico del 0x113 (0x418bc8).
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

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t model) const override
    {
        return model == Client::Scenes::DarkSpiritModel ? &m_model : nullptr;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == Client::Scenes::DarkSpiritModel ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
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
                .originalFrame = true,
                .random = &m_random,
                .world = m_world.get()};
    }

    ActorState m_owner;
    Client::Crt::CrtRandom m_random;
    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
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
            EXPECT_EQ(shadow, Client::Models::FlattenOnTheGround(body, ground, spread)) << corner;
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

// 0x420e00..0x420e11: con +0x78 & 0x4000 del dueño, 0x4182f0(10) y 0x602e90(cuervo, 0, 0, 10). 0x5ffdf0 lo posa igual
// (la pose de +0x120 y el modelo compartido en su lugar, no en el piso) y 0x6002e0 va por 0x60033c: el +0x108 del
// cuervo es 0, así que el lazo del asedio no lo encuentra, ni con el asedio; el color del modelo en 1 y las cinco
// mallas con 0x4ecc30(malla, 0x4040, 0,5, …, 0x620), aditivas con Chrome02, cada una en la mitad del color de la
// anterior (Q-C95), sin el glColor3f(1, 1, 1) ni las mallas 3 y 2 del caso 0xc0, y sin la sombra ni las alturas del
// terreno (0x600a0b). Las UV, con la normal (0, 0, 1) por la escala 0,7 y el reloj en 0: u = 0,7f y
// v = (1 − 0,7f) − 0,7f · 2,5.
TEST_F(GameRavenTest, TheOwnersFlag4000DrawsTheRavenAsAChromeShellWithoutShadow)
{
    m_owner.flags78 = 0x4000;
    m_terrain.heights.reset();
    m_terrain.attributes.reset();
    Client::Scenes::FaunaDrawCarry carry;
    GameRavenPass pass = Pass(30);
    pass.carry = &carry;

    const auto result = Client::Scenes::DrawGameRaven(m_owner, pass);

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

// 0x418aab..0x418b71: en el 5, con +0x38d hasta 2, después del dibujo, diez joints 0x5ac/1 desde la posición y los
// ángulos del cuervo, sin dueño, de ancho rand() % 40 + 20 cada uno (sus rand() antes de cada alta, que saca los
// suyos), con el 0x4000 o sin él; con +0x38d en 1, después el 0x117/3 con los ángulos (45, rand() % 180 − 90, 0) y la
// luz (1; 0,8f; 0,6f), sin más rand(). Sin las tablas del mundo, corta en la primera alta.
TEST_F(GameRavenTest, TheAttackDropsItsJointsAndTheDarkLordSkill)
{
    for (const std::uint32_t flags : {0u, 0x4000u})
        for (const std::uint8_t counter : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}})
        {
            SCOPED_TRACE(flags);
            SCOPED_TRACE(counter);
            m_world = std::make_unique<Client::Scenes::WorldState>();
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
                const Client::World::FishEffectRequest request{.type = 0x5ac,
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

            const auto attacked = Client::Scenes::DrawGameRaven(m_owner, Pass());

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

    // Sin la tabla de joints corta en la primera 0x530860, después del rand() % 40 de 0x418ad6.
    GameRavenPass bare = Pass();
    bare.world = nullptr;
    m_owner.darkSpirit->byte38d = 0;
    CrtRandom before = m_random;
    const auto cut = Client::Scenes::DrawGameRaven(m_owner, bare);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x418aff"), std::string::npos) << cut.Error();
    static_cast<void>(before.Next());
    EXPECT_EQ(m_random.State(), before.State());
}

// 0x418b7c..0x418c0a: en el 5, con +0x38d en 3 nada; impar desde 4 y más de 100 por encima del dueño, el 0x113/0 en el
// pico (el hueso 6 de la pose de +0x120 con (50, 0, 0), 0x4ec110 sin escala), con los ángulos y la luz del cuervo y el
// cuervo de dueño (vida 15, escala 0,6f), sin rand(). Con menos de siete huesos en la pose, corta en 0x418be4.
TEST_F(GameRavenTest, TheAttackEffectNeedsAnOddCounterAndHeight)
{
    m_models.GrowBones(7);
    m_owner.darkSpirit->state = 5;
    m_owner.darkSpirit->angles = {0.0f, 0.0f, 45.0f};
    m_owner.darkSpirit->light = {0.25f, 0.5f, 0.75f};
    for (const std::uint8_t counter : {std::uint8_t{3}, std::uint8_t{4}, std::uint8_t{6}})
    {
        m_owner.darkSpirit->byte38d = counter;
        const auto quiet = Client::Scenes::DrawGameRaven(m_owner, Pass());
        ASSERT_TRUE(quiet.HasValue()) << quiet.Error();
    }
    EXPECT_FALSE(m_world->Map().effects.Effects()[0].alive);
    m_owner.darkSpirit->byte38d = 5;
    const auto high = Client::Scenes::DrawGameRaven(m_owner, Pass());
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
    // 300 no pasa de 200 + 100.
    m_owner.position.z = 200.0f;
    const auto low = Client::Scenes::DrawGameRaven(m_owner, Pass());
    EXPECT_TRUE(low.HasValue()) << low.Error();
    EXPECT_FALSE(m_world->Map().effects.Effects()[1].alive);
    EXPECT_EQ(m_random.State(), Client::Crt::CrtRandom{}.State());

    m_owner.position.z = 0.0f;
    m_models.GrowBones(6);
    const auto shortPose = Client::Scenes::DrawGameRaven(m_owner, Pass());
    ASSERT_FALSE(shortPose.HasValue());
    EXPECT_NE(shortPose.Error().find("0x418be4"), std::string::npos) << shortPose.Error();
}

// 0x41896d: en el 6, con la pose del dibujo, rand() & 1 elige el primer hueso; el del modelo de prueba (el 0) suelta la
// partícula 0x5ac/6 en el punto del hueso en el mundo (0x4ec110 de (0; 0; 0) con la pose de +0x120), con los ángulos
// del cuervo, la luz (1; 0,6f; 0,4f) y la escala 1,3f, con sus rand() (0x53fe70). Desde el 1 no hay más huesos. Con el
// pool lleno, ninguna y sigue. Sin las partículas, MuRet corta en la primera.
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

        const auto shout = Client::Scenes::DrawGameRaven(m_owner, pass);

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

    // Con el pool lleno, ninguna y sin sus rand(); sin las partículas, corta. La semilla 3 empieza por el hueso 0.
    constexpr std::uint32_t BoneZero = 3;
    Client::Crt::CrtRandom afterOne(BoneZero);
    ASSERT_EQ(afterOne.Next() & 1, 0);
    m_random = Client::Crt::CrtRandom(BoneZero);
    auto full = std::make_unique<Client::World::Particles>();
    for (auto& particle : full->All())
        particle.alive = true;
    GameRavenPass crowded = Pass();
    crowded.particles = full.get();
    ASSERT_TRUE(Client::Scenes::DrawGameRaven(m_owner, crowded).HasValue());
    EXPECT_EQ(m_random.State(), afterOne.State());
    m_random = Client::Crt::CrtRandom(BoneZero);
    const auto missing = Client::Scenes::DrawGameRaven(m_owner, Pass());
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x418a26"), std::string::npos) << missing.Error();
}

// 0x418a51: con +0x38d en 15, sin personaje en +0x3b0 solo vuelve el contador a 0; con uno, 0x4ee770(1, 0x5ac, 1,
// cuervo+0x1c, el personaje) sobre la malla 1 del cuervo recién posado (en cada esquina del triángulo 0, rand() % 90 y
// rand() % 120 y el joint 0x613/0xd hacia el personaje, ancho 20) y 0x4164f0(0). Fuera de los frames del original,
// 0x418920 no corre. Las esquinas y la normal, del scratch que deja 0x602e90(cuervo, 0, 0, …): el hueso 0 en el origen
// sin giro, la raíz de 0x4eb860 con la bandera en 1 (0x5fc8f0 con (−10, 0, 30), cada elemento por 0,7f en float y el
// origen (1050, 1060, 300)), 0x5fca90 con la identidad y 0x4ebc80 con Translate en 0 (0x5fccc0 para (0, 0, 10),
// (1, 0, 10) y (0, 1, 10); 0x5fcbd0 para la normal (0, 0, 1), con la escala dentro). Los bits salen de una emulación
// aparte de la x87 (fracciones exactas, PC53, fsin y fcos en 64 bits, cada FSTP en float), no de MuRet.
TEST_F(GameRavenTest, TheEndOfTheShoutLooksAtTheAttackTarget)
{
    m_owner.darkSpirit->state = 6;
    m_owner.darkSpirit->byte38d = 15;
    m_owner.darkSpirit->target3b0 = 0xffff;
    ASSERT_TRUE(Client::Scenes::DrawGameRaven(m_owner, Pass()).HasValue());
    EXPECT_EQ(m_owner.darkSpirit->byte38d, 0);
    EXPECT_EQ(m_owner.darkSpirit->state, 6);

    m_owner.darkSpirit->byte38d = 15;
    m_owner.darkSpirit->target3b0 = 3;
    m_owner.darkSpirit->far = 7;
    m_owner.darkSpirit->angles = {-10.0f, 0.0f, 30.0f};
    m_owner.darkSpirit->scale = std::bit_cast<float>(0x3f333333u); // 0,7f
    GameRavenPass projected = Pass();
    projected.originalFrame = false;
    ASSERT_TRUE(Client::Scenes::DrawGameRaven(m_owner, projected).HasValue());
    EXPECT_EQ(m_owner.darkSpirit->byte38d, 15);

    Client::Scenes::FaunaDrawCarry carry;
    auto world = std::make_unique<Client::Scenes::WorldState>();
    const auto actors = world->Actors();
    actors[3].position = {1200.0f, 1300.0f, 40.0f};
    const auto& joints = world->DrawTail().joints;
    GameRavenPass pass = Pass();
    pass.carry = &carry;
    pass.world = world.get();
    const auto end = Client::Scenes::DrawGameRaven(m_owner, pass);
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
                                                     replay, Client::Scenes::JointOwnerKind::Actor)
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

    // Sin el cuervo posado en este dibujo (sin el buffer de la pose), el scratch es de otro modelo: corta.
    m_owner.darkSpirit->state = 6;
    m_owner.darkSpirit->byte38d = 15;
    m_owner.darkSpirit->alpha = 0.0f;
    const auto stale = Client::Scenes::DrawGameRaven(m_owner, pass);
    ASSERT_FALSE(stale.HasValue());
    EXPECT_NE(stale.Error().find("0x418a8c"), std::string::npos) << stale.Error();
}

TEST_F(GameRavenTest, AnOwnerWithoutRavenDrawsNothing)
{
    m_owner.darkSpirit.reset();

    const auto result = Client::Scenes::DrawGameRaven(m_owner, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
}
} // namespace
