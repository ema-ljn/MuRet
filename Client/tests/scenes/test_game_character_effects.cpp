#include "Scenes/GameCharacterEffects.h"

#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FullSetScan.h"
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

    ActorState m_actor;
    std::vector<Client::Render::Models::BoneTransform> m_body;
    CrtRandom m_random{1};
    Client::World::FrameSprites m_sprites;
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

TEST_F(GameCharacterEffectsTest, TheSetCounterCountsEveryDrawAndCutsAtTheHundredthWithALowAction)
{
    m_actor.byte358 = 1;
    m_actor.counter35c = 0;

    ASSERT_TRUE(Client::Scenes::DrawGameCharacterEffects(m_actor, Pass()).HasValue());
    EXPECT_EQ(m_actor.counter35c, 1);

    m_actor.counter35c = 100;
    m_actor.animation.action = 0xd;
    ASSERT_TRUE(Client::Scenes::DrawGameCharacterEffects(m_actor, Pass()).HasValue());
    EXPECT_EQ(m_actor.counter35c, 101);

    m_actor.animation.action = 0xc;
    const auto cut = Client::Scenes::DrawGameCharacterEffects(m_actor, Pass());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x50fdc3"), std::string::npos) << cut.Error();
    EXPECT_EQ(m_actor.counter35c, 101);

    // Entre frames, sobre la copia del caller: vuelve a 0 y cuenta, sin el efecto.
    GameCharacterEffectsPass projected = Pass();
    projected.originalFrame = false;
    ASSERT_TRUE(Client::Scenes::DrawGameCharacterEffects(m_actor, projected).HasValue());
    EXPECT_EQ(m_actor.counter35c, 1);
    EXPECT_EQ(m_random.State(), 1u);
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

TEST_F(GameCharacterEffectsTest, TheGlowingBootsDrawTheirRandomByLevelAndCutAtTheirFirstParticle)
{
    struct Case
    {
        std::int32_t level;
        std::size_t draws;
        const char* cut;
    };
    // Con la semilla 1, 41 y 18467: el 10 (41 & 3) y el 12 (41 & 1) no crean y sacan el rand() % 20; el
    // 11 (41 % 3 = 2) tampoco; el 13 crea sin rand(); el 14 ni crea ni saca.
    constexpr std::array cases{Case{10, 2, nullptr}, Case{11, 2, nullptr}, Case{12, 2, nullptr},
                               Case{13, 0, "0x51020c"}, Case{14, 1, nullptr}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.level);
        m_random = CrtRandom{1};
        m_actor.body.back().model = 0x1831;
        const glm::vec3 light = m_actor.light;
        GameCharacterEffectsPass pass = Pass();
        pass.scan = {.complete = true, .globals = {.dword743a178 = probe.level}};

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
        EXPECT_EQ(m_random.State(), AfterRandoms(probe.draws));
        EXPECT_EQ(m_actor.light, light);
    }
}

TEST_F(GameCharacterEffectsTest, TheBootsParticleCutOfEachLevelAndItsProjectedFrame)
{
    struct Case
    {
        std::int32_t level;
        std::size_t skipped;
        const char* cut;
    };
    // Con la semilla 1: 41 % 3 no es 0, el primer múltiplo de 3 y el primer par están más adelante.
    const std::array cases{
        Case{10, RandomsBefore([](int value) { return (value & 3) == 0; }), "0x510158"},
        Case{11, RandomsBefore([](int value) { return value % 3 == 0; }), "0x51008d"},
        Case{12, RandomsBefore([](int value) { return (value & 1) == 0; }), "0x510158"},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.level);
        m_actor.body.back().model = 0x182f;
        for (const bool original : {true, false})
        {
            m_random = CrtRandom{1};
            for (std::size_t skip = 0; skip < probe.skipped; ++skip)
                static_cast<void>(m_random.Next());
            GameCharacterEffectsPass pass = Pass();
            pass.originalFrame = original;
            pass.scan = {.complete = true, .globals = {.dword743a178 = probe.level}};

            const auto result = Client::Scenes::DrawGameCharacterEffects(m_actor, pass);

            if (original)
            {
                ASSERT_FALSE(result.HasValue());
                EXPECT_NE(result.Error().find(probe.cut), std::string::npos) << result.Error();
                EXPECT_EQ(m_random.State(), AfterRandoms(probe.skipped + 1));
            }
            else
            {
                // Entre frames no crea: sigue con el rand() % 20.
                ASSERT_TRUE(result.HasValue()) << result.Error();
                EXPECT_EQ(m_random.State(), AfterRandoms(probe.skipped + 2));
            }
        }
    }
}

TEST_F(GameCharacterEffectsTest, TheSparklesOfEachLevelCutAtTheirFirstCreation)
{
    struct Case
    {
        std::int32_t level;
        std::int32_t steps;
        const char* cut;
        std::size_t extra;
    };
    // El 11 saca otro rand() & 7: el 19169 después del 26500 de la semilla 1 no es múltiplo de 8.
    constexpr std::array cases{
        Case{10, 1, "0x510339", 0}, Case{11, 1, "0x510339", 1},  Case{12, 6, "0x5103be", 0},
        Case{12, 3, "0x510339", 0}, Case{12, 1, nullptr, 0},     Case{13, 12, "0x510435", 0},
        Case{13, 4, "0x51048b", 0}, Case{13, -4, "0x51048b", 0}, Case{13, 2, nullptr, 0},
        Case{14, 6, nullptr, 0},
    };
    const std::size_t skipped = RandomsBefore([](int value) { return value % 20 == 0; });
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << probe.level << ' ' << probe.steps);
        m_random = CrtRandom{1};
        for (std::size_t skip = 0; skip < skipped; ++skip)
            static_cast<void>(m_random.Next());
        const glm::vec3 light = m_actor.light;
        GameCharacterEffectsPass pass = Pass();
        pass.logicSteps = probe.steps;
        pass.scan = {.complete = true, .globals = {.dword743a178 = probe.level}};

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
        EXPECT_EQ(m_random.State(), AfterRandoms(skipped + 1 + probe.extra));
        EXPECT_EQ(m_actor.light, light);
    }
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
