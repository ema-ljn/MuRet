#include "Models/IModelLibrary.h"
#include "Monsters/Drawing/MonsterDrawing.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Protocol/Skills/Skill.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Actors/Attachment.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Math/RoundedMath.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Models/SkinningBuffers.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Kernel/PoseBuffer.h"
#include "Assets/FaunaModels.h"
#include "Actors/Drawing/GameCharacters.h"
#include "Kernel/DrawTailState.h"
#include "FrontEnd/SelectionState.h"
#include "Skills/SkillReplies.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "Effects/ActorEffectHooks.h"
#include "Session/CharacterDrawHooks.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// The drawing of monsters and NPCs (0x50ba30 with a type other than 0x20a): literal vectors from the c32bbd5f ASM,
// without running the original. A synthetic one-triangle model on bone 0 shows the body, the outline and the shadow.
namespace
{
using Client::Actors::Drawing::GameCharacterPass;
using Client::Kernel::ActorState;
using Client::Math::RoundedSin;

constexpr std::uint16_t Spider = 0x15b;    // Monster10.bmd, type 3 (Lorencia)
constexpr std::uint16_t Yeti = 0x15e;      // Monster13.bmd, type 19 (Devias)
constexpr std::uint16_t DeviasNpc = 0x1d5; // NPC 241
constexpr std::uint16_t FairyNpc = 0x1d9;  // NPC 237 of Lorencia
constexpr std::uint16_t Golem = 0x176;     // a case with sprites from 0x50b870 and 0x50b920
constexpr std::uint16_t HelperType = 0x1b6;

// The three rand() the rest of 0x50ba30 draws in a draw that does not cut (those of the other tests with
// AfterRandoms(3)).
constexpr std::size_t TailDraws = 3;

std::uint32_t AfterRandoms(std::size_t count)
{
    Client::Crt::CrtRandom random{1};
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random.State();
}

class MonsterModels final : public Client::Models::IModelLibrary
{
public:
    MonsterModels()
    {
        using namespace Client::Math;
        using namespace Client::Models;
        SkeletonBone bone;
        bone.parent = -1;
        bone.actions = {{BoneKey{}}};
        bone.actions[0][0].rotation = AngleQuaternion({0.0f, 0.0f, 0.0f});
        // Bone k at (0, 0, k), as in the player tests: a sprite shows which bone it comes from.
        m_skeleton.bones.assign(48, bone);
        for (std::size_t index = 0; index < m_skeleton.bones.size(); ++index)
            m_skeleton.bones[index].actions[0][0].position = {0.0f, 0.0f, static_cast<float>(index)};
        m_skeleton.actions = {ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "monster.jpg";
        m_model.meshes = {mesh};
    }

    // `count` bones instead of 48, as the constructor builds them.
    void SetBoneCount(std::size_t count)
    {
        const auto bone = m_skeleton.bones.front();
        m_skeleton.bones.assign(count, bone);
        for (std::size_t index = 0; index < m_skeleton.bones.size(); ++index)
            m_skeleton.bones[index].actions[0][0].position = {0.0f, 0.0f, static_cast<float>(index)};
    }

    // `count` actions equal to the first.
    void SetActionCount(std::size_t count)
    {
        {
            const auto first = m_skeleton.actions.front();
            m_skeleton.actions.assign(count, first);
        }
        for (auto& bone : m_skeleton.bones)
        {
            const auto first = bone.actions.front();
            bone.actions.assign(count, first);
        }
    }

    // A second mesh equal to the first, with its texture.
    void AddMesh()
    {
        m_model.meshes.push_back(m_model.meshes.front());
        m_texture.push_back(Client::Render::TextureSlot{0x301});
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId model) const override
    {
        return model.Value() == 0x20a ? nullptr : &m_model;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return &m_skeleton;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return m_texture;
    }

private:
    Client::Models::Skeleton m_skeleton;
    Client::Data::Models::Model m_model;
    std::vector<Client::Render::TextureSlot> m_texture{Client::Render::TextureSlot{0x300}};
};

class MonsterDrawingTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_world = std::make_unique<Client::Kernel::WorldState>();
        m_faunaModels.Bind(&m_models);
        m_world->Runtime().map = 0;
        m_world->Runtime().hero = 42;
        m_terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        m_terrain.attributes->cells.fill(0);
        m_light = std::make_unique<Client::Terrain::TerrainLighting>();
        m_light->cells.fill({0.2f, 0.4f, 0.6f});
        m_joints.resize(16);
        m_batch.Begin(Client::Render::Pass3D{});
    }

    void TearDown() override
    {
        m_faunaModels.Bind(nullptr);
    }

    // A monster as 0x511290 leaves it, with what the creation writes (0x511c70..0x511c88, 0x511d45, 0x51248d).
    ActorState& Monster(std::size_t slot, std::uint16_t type, std::uint16_t monster)
    {
        ActorState& actor = m_world->Actors()[slot];
        actor.alive = true;
        actor.seen = true;
        actor.type = type;
        actor.word376 = monster;
        actor.head = 0xffff;
        actor.word1f2 = 0xffff;
        actor.scale = 1.0f;
        actor.alpha = 1.0f;
        actor.byte88 = 2;
        actor.byte92 = 1;
        actor.float94 = 1.2f;
        actor.byteE8 = 0;
        actor.dword58 = -1;
        actor.dword64 = -1;
        actor.blendMeshLight = 1.0f;
        actor.position = {150.0f, 150.0f, 10.0f};
        actor.light = {0.1f, 0.2f, 0.3f};
        actor.wings.model = 0xffff;
        actor.pet.model = 0xffff;
        for (auto& part : actor.body)
            part.model = 0xffff;
        for (auto& part : actor.hands)
            part.model = 0xffff;
        return actor;
    }

    GameCharacterPass Pass()
    {
        return {.dynamicLighting = m_light.get(),
                .clothWorld = &m_clothWorld,
                .frameClock = Client::Core::Duration{},
                .originalFrames = 1,
                .sprites = &m_sprites,
                .poseCarry = &m_carry,
                .joints = m_joints,
                .terrain = &m_terrain,
                .faunaModels = &m_faunaModels,
                .drawBuffers = &m_buffers,
                .hooks = &m_drawHooks,
                .effectHooks = &m_effectHooks};
    }

    Client::Core::Expected<void, std::string> Draw(const GameCharacterPass& pass)
    {
        return Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);
    }

    std::vector<Client::Particles::Sprite> LiveSprites()
    {
        std::vector<Client::Particles::Sprite> live;
        for (const auto& sprite : m_sprites.Sprites())
        {
            if (sprite.alive)
                live.push_back(sprite);
        }
        return live;
    }

    MonsterModels m_models;
    Client::Assets::FaunaModels m_faunaModels;
    std::unique_ptr<Client::Kernel::WorldState> m_world;
    Client::Terrain::WorldTerrain m_terrain;
    Client::Session::CharacterDrawHooks m_drawHooks;
    Client::Effects::ActorEffectHooks m_effectHooks;
    std::unique_ptr<Client::Terrain::TerrainLighting> m_light;
    Client::Models::ClothWorld m_clothWorld;
    Client::Particles::FrameSprites m_sprites;
    Client::Kernel::PoseBuffer m_carry;
    std::vector<Client::Kernel::JointState> m_joints;
    Client::Models::SkinningBuffers m_buffers;
    Client::Render::ModelBatch m_batch;
};

void ExpectColor(const Client::Render::ModelVertex& vertex, float red, float green, float blue, float alpha)
{
    EXPECT_FLOAT_EQ(vertex.color.red, red);
    EXPECT_FLOAT_EQ(vertex.color.green, green);
    EXPECT_FLOAT_EQ(vertex.color.blue, blue);
    EXPECT_FLOAT_EQ(vertex.color.alpha, alpha);
}

TEST_F(MonsterDrawingTest, ALorenciaSpiderDrawsItsBodyThenItsShadowWithTheOneAttachmentRandom)
{
    ActorState& spider = Monster(7, Spider, 3);
    m_faunaModels.RecordAnimation(Spider, 6.25f, 7);

    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // 0x4eb8cd..0x4eb94c: the pose key in the model's +0xa8; the action has only one, key 0.
    EXPECT_EQ(m_faunaModels.PoseKey(Spider), 0);
    EXPECT_EQ(m_faunaModels.PoseFrame(Spider), spider.animation.frame);
    // 0x6002e0 and then the shadow of 0x50bd89.
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    ASSERT_EQ(m_batch.Commands().size(), 2u);
    // 0x5ffce0 without the per-vertex light: a tenth of the terrain plus its own.
    ExpectColor(m_batch.Vertices()[0], 0.12f, 0.24f, 0.36f, 1.0f);
    EXPECT_EQ(m_batch.Commands()[1].state.blend, Client::Render::ModelBlend::Shadow);
    ExpectColor(m_batch.Vertices()[3], 0.0f, 0.0f, 0.0f, 1.0f);
    // The shadow stays flattened on the object's floor.
    EXPECT_FLOAT_EQ(m_batch.Vertices()[3].position.z, 15.0f);
    // 0x50db94: the light of what hangs, the terrain plus its own.
    EXPECT_FLOAT_EQ(spider.drawnLight.x, 0.3f);
    EXPECT_FLOAT_EQ(spider.drawnLight.y, 0.6f);
    EXPECT_FLOAT_EQ(spider.drawnLight.z, 0.9f);
    EXPECT_EQ(spider.bodyPose.size(), 48u);
    // The one of 0x50db57 and the two of +0x38e in the hands loop (0x50e288), with no hand.
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
}

// The buffers the frame's owner lends keep what a bigger model skinned: a monster of one mesh drawn in the buffers of
// one of two draws its body and its shadow as it draws them with buffers of its own.
TEST_F(MonsterDrawingTest, AMonsterDrawnInTheBuffersOfABiggerOneDrawsAsWithItsOwn)
{
    Monster(7, Spider, 3);
    MonsterModels bigger;
    bigger.AddMesh();
    m_faunaModels.Bind(&bigger);
    ASSERT_TRUE(Draw(Pass()).HasValue());
    m_faunaModels.Bind(&m_models);
    m_batch.Begin(Client::Render::Pass3D{});

    const auto reused = Draw(Pass());

    ASSERT_TRUE(reused.HasValue()) << reused.Error();
    const std::vector<Client::Render::ModelVertex> drawn(m_batch.Vertices().begin(), m_batch.Vertices().end());
    ASSERT_EQ(drawn.size(), 6u);
    GameCharacterPass own = Pass();
    own.drawBuffers = nullptr;
    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Draw(own).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), drawn.size());
    for (std::size_t vertex = 0; vertex < drawn.size(); ++vertex)
    {
        EXPECT_EQ(m_batch.Vertices()[vertex].position, drawn[vertex].position) << vertex;
        EXPECT_EQ(m_batch.Vertices()[vertex].uv, drawn[vertex].uv) << vertex;
        ExpectColor(m_batch.Vertices()[vertex], drawn[vertex].color.red, drawn[vertex].color.green,
                    drawn[vertex].color.blue, drawn[vertex].color.alpha);
    }
}

// 0x50ba30 is another module's (Session::CharacterDrawHooks): a pass without its hooks stops at the first monster,
// before any vertex or random.
TEST_F(MonsterDrawingTest, AMonsterWithoutTheDrawHooksStopsBeforeDrawing)
{
    Monster(7, Spider, 3);
    GameCharacterPass pass = Pass();
    pass.hooks = nullptr;

    const auto result = Draw(pass);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x50ba30"), std::string::npos) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(0));
}

// 0x50be15..0x50be52: after the body, in Blood Castle (0xb to 0x11), one dying pushed (+0x1a1 and +0x389) drops to the
// terrain height (0x5f93c0) and the shadow flattens there (5 above its z). The fcom with `test ah, 1` also writes with
// the z or the height at NaN; with the terrain at the z's height or above, it does not: with z at −0 and the terrain at
// +0, z stays −0 (a `>` would overwrite it with +0). Outside those maps, without +0x1a1 or without dying, z stays. The
// floor comes after its shadow's guards (0x50bd89..0x50bdfb): with alpha below 0.3f (at 0.3f it drops), with +0x88 at 8
// or with a +0x376 without shadow (the 0x19), neither shadow nor floor. Without the heights it cuts at the call to
// 0x5f93c0, after the body.
TEST_F(MonsterDrawingTest, TheDyingOfBloodCastleSinkToTheGroundUnderTheirShadow)
{
    m_terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    struct Case
    {
        std::int32_t map;
        std::uint8_t byte1a1;
        std::uint8_t dying;
        float z;
        float height;
        float grounded;
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array cases{Case{0xb, 1, 1, 10.0f, 4.0f, 4.0f},  Case{0x11, 1, 1, 10.0f, 4.0f, 4.0f},
                           Case{0xb, 1, 1, nan, 4.0f, 4.0f},    Case{0xb, 1, 1, 10.0f, nan, nan},
                           Case{0xb, 1, 1, 3.0f, 4.0f, 3.0f},   Case{0xb, 1, 1, -0.0f, 0.0f, -0.0f},
                           Case{2, 1, 1, 10.0f, 4.0f, 10.0f},   Case{0x12, 1, 1, 10.0f, 4.0f, 10.0f},
                           Case{0xb, 0, 1, 10.0f, 4.0f, 10.0f}, Case{0xb, 1, 0, 10.0f, 4.0f, 10.0f}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << probe.map << ", +0x1a1 " << int{probe.byte1a1} << ", +0x389 "
                                          << int{probe.dying} << ", z " << probe.z << ", terreno " << probe.height);
        m_terrain.heights->cells.fill(probe.height);
        m_world->Runtime().map = probe.map;
        ActorState& spider = Monster(5, Spider, 3);
        spider.byte1a1 = probe.byte1a1;
        spider.dying = probe.dying;
        spider.position.z = probe.z;
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Draw(Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(spider.position.x, 150.0f);
        EXPECT_EQ(spider.position.y, 150.0f);
        if (std::isnan(probe.grounded))
            EXPECT_TRUE(std::isnan(spider.position.z)) << spider.position.z;
        else
            EXPECT_TRUE(spider.position.z == probe.grounded &&
                        std::signbit(spider.position.z) == std::signbit(probe.grounded))
                << spider.position.z;
        ASSERT_EQ(m_batch.Vertices().size(), 6u);
        const float shadow = m_batch.Vertices()[3].position.z;
        if (std::isnan(probe.grounded))
            EXPECT_TRUE(std::isnan(shadow)) << shadow;
        else
            EXPECT_FLOAT_EQ(shadow, probe.grounded + 5.0f);
    }

    // After the shadow's guards: without it, z stays and only the body is drawn.
    struct Guard
    {
        float alpha;
        std::uint8_t kind;
        std::uint16_t monster;
        float grounded;
    };
    const float shadowAlpha = std::bit_cast<float>(0x3e99999au); // 0x6b89dc, 0.3f
    const std::array guards{Guard{0.29f, 2, 3, 10.0f}, Guard{shadowAlpha, 2, 3, 4.0f}, Guard{1.0f, 8, 3, 10.0f},
                            Guard{1.0f, 2, 0x19, 10.0f}};
    m_terrain.heights->cells.fill(4.0f);
    m_world->Runtime().map = 0xb;
    for (const Guard& guard : guards)
    {
        SCOPED_TRACE(::testing::Message()
                     << "alpha " << guard.alpha << ", +0x88 " << int{guard.kind} << ", +0x376 " << guard.monster);
        ActorState& spider = Monster(5, Spider, guard.monster);
        spider.alpha = guard.alpha;
        spider.byte88 = guard.kind;
        spider.byte1a1 = 1;
        spider.dying = 1;
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Draw(Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(spider.position.z, guard.grounded);
        EXPECT_EQ(m_batch.Vertices().size(), guard.grounded == 4.0f ? 6u : 3u);
    }

    m_terrain.heights.reset();
    ActorState& blind = Monster(5, Spider, 3);
    blind.byte1a1 = 1;
    blind.dying = 1;
    m_batch.Begin(Client::Render::Pass3D{});
    const auto cut = Draw(Pass());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x50be3b"), std::string::npos) << cut.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 3u);
}

// 0x5ffdf0 copies the position to the model's register (+0x6c..+0x74, 0x5ffe8b..0x5ffe9a) when posing the body, before
// the Blood Castle floor (0x50be4d); then, except for the 0x18e, the 0x18f and 0xa8 to 0xac, the shadow's 0x60cf70
// (0x50beb6) copies it again, already on the floor (0x60cfe8..0x60d015), and what follows reads it with 0x4ec110 over
// [ebp−0x14]: the flare01 (0x5ac) of +0x376 0x46 (0x50c0fb..0x50c136) on bone 20 (bone k is at (0, 0, k)) leaves from
// the spider at 4, at (150, 150, 24). The 0xa8 (a world object no monster creation gives: with +0x58 at −2 so 0x6002e0
// does not cut at 0x6004ed) and the 0x18f do not call 0x60cf70 and take it from the earlier z, 10; the 0x18f also adds
// 60 in y (0x5ffea7..0x5ffeef). Between the original's frames, the same, on the copy.
TEST_F(MonsterDrawingTest, TheTailOfTheDyingOfBloodCastleHangsFromTheRegisterOfTheirShadow)
{
    m_terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    m_terrain.heights->cells.fill(4.0f);
    m_world->Runtime().map = 0xb;
    struct Case
    {
        std::uint16_t type;
        std::int32_t hidden;
        std::size_t vertices;
        glm::vec3 flare;
    };
    const std::array cases{Case{Spider, -1, 6, {150.0f, 150.0f, 24.0f}}, Case{0xa8, -2, 0, {150.0f, 150.0f, 30.0f}},
                           Case{0x18f, -1, 3, {150.0f, 210.0f, 30.0f}}};
    for (const int frames : {1, 0})
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message() << "frames " << frames << ", tipo " << probe.type);
            ActorState& monster = Monster(5, probe.type, 0x46);
            monster.byte1a1 = 1;
            monster.dying = 1;
            monster.dword58 = probe.hidden;
            m_sprites.Clear();
            m_batch.Begin(Client::Render::Pass3D{});
            GameCharacterPass pass = Pass();
            pass.originalFrames = frames;

            const auto result = Draw(pass);

            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(monster.position.z, frames > 0 ? 4.0f : 10.0f);
            ASSERT_EQ(m_batch.Vertices().size(), probe.vertices);
            if (probe.vertices == 6)
            {
                EXPECT_FLOAT_EQ(m_batch.Vertices()[3].position.z, 9.0f);
            }
            const auto live = LiveSprites();
            ASSERT_EQ(live.size(), 1u);
            EXPECT_EQ(live[0].texture, 0x5ac);
            EXPECT_EQ(live[0].position, probe.flare);
        }
    }
}

// The shine passes of 0x60b130 (the +0x376 0x26, 0x50c671) draw the meshes of 0x5786f9c as the last 0x4ebc80 left them:
// on a Blood Castle dead with shadow, that of 0x60cf70 (0x60d186), with the origin already on the floor (z 4, below the
// body at 10); the 0x18f does not call it and repeats those of the 0x5ffdf0 body, 60 more in y and at the earlier z.
TEST_F(MonsterDrawingTest, TheShinesOfTheDyingOfBloodCastleRepeatTheMeshesOfTheLastPose)
{
    m_terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    m_terrain.heights->cells.fill(4.0f);
    m_world->Runtime().map = 0xb;
    struct Case
    {
        std::uint16_t type;
        std::size_t first;
        glm::vec3 body;
        glm::vec3 shine;
    };
    const std::array cases{Case{Spider, 6, {150.0f, 150.0f, 10.0f}, {150.0f, 150.0f, 4.0f}},
                           Case{0x18f, 3, {150.0f, 210.0f, 10.0f}, {150.0f, 210.0f, 10.0f}}};
    for (const int frames : {1, 0})
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message() << "frames " << frames << ", tipo " << probe.type);
            ActorState& monster = Monster(5, probe.type, 0x26);
            monster.byte1a1 = 1;
            monster.dying = 1;
            m_batch.Begin(Client::Render::Pass3D{});
            GameCharacterPass pass = Pass();
            pass.originalFrames = frames;

            const auto result = Draw(pass);

            ASSERT_TRUE(result.HasValue()) << result.Error();
            ASSERT_EQ(m_batch.Vertices().size(), probe.first + 3);
            EXPECT_EQ(m_batch.Vertices()[0].position, probe.body);
            EXPECT_EQ(m_batch.Vertices()[probe.first].position, probe.shine);
        }
    }
}

TEST_F(MonsterDrawingTest, ADeviasYetiWithVertexLightTakesTheWholeTerrainLight)
{
    m_world->Runtime().map = 2;
    ActorState& yeti = Monster(3, Yeti, 19);
    yeti.byteE8 = 1;

    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Commands()[1].state.blend, Client::Render::ModelBlend::Shadow);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
}

TEST_F(MonsterDrawingTest, TheShadowFollowsTheMapsOf0x60b3d0)
{
    struct Case
    {
        std::int32_t map;
        std::size_t vertices;
    };
    // Map 7 blends it with alpha 0.2; map 10 (0x50bdf5) and Kalima (0x60b459) do not have it.
    constexpr std::array cases{Case{7, 6}, Case{10, 3}, Case{24, 3}, Case{36, 3}};
    for (const Case& value : cases)
    {
        SCOPED_TRACE(value.map);
        m_world->Runtime().map = value.map;
        Monster(5, Spider, 3);
        m_batch.Begin(Client::Render::Pass3D{});
        const auto result = Draw(Pass());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), value.vertices);
        if (value.map == 7)
        {
            EXPECT_EQ(m_batch.Commands()[1].state.blend, Client::Render::ModelBlend::BlendedShadow);
            ExpectColor(m_batch.Vertices()[3], 0.0f, 0.0f, 0.0f, 0.2f);
        }
    }
}

TEST_F(MonsterDrawingTest, TheShadowSkipsTheListedMonstersTheFaintAndThe0x92Off)
{
    Monster(1, Spider, 0x19);
    Monster(2, Spider, 3).alpha = 0.29f;
    Monster(3, Spider, 3).byte92 = 0;
    Monster(4, Spider, 3).byte88 = 8;

    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 12u); // cuatro cuerpos, ninguna sombra
}

// 0x50bed8: the 0xe8 and 0xe9 carry, after the shadow without mesh 2, the additive 0x61e decal of 2.7 cells 5 above the
// floor, with light (v / 2, v / 2, v) for v = sin(T · 0.0015f) · 0.3f + 0.8f; +0x58 goes back to −1.
TEST_F(MonsterDrawingTest, TheArchangelsMarkTheFloorUnderThem)
{
    struct Case
    {
        std::int32_t map;
        std::uint16_t monster;
    };
    constexpr std::array cases{Case{0, 0xe9}, Case{2, 0xe9}, Case{11, 0xe8}};
    for (const Case& value : cases)
    {
        SCOPED_TRACE(value.map);
        m_world = std::make_unique<Client::Kernel::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        m_terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        m_world->Runtime().map = value.map;
        ActorState& archangel = Monster(6, Spider, value.monster);
        archangel.position = {12850.0f, 12850.0f, 10.0f};
        archangel.angles.z = 30.0f;
        GameCharacterPass pass = Pass();
        pass.frameClock = std::chrono::milliseconds(1000);

        const auto result = Draw(pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(archangel.dword58, -1);
        // The body, the shadow (mesh 0: +0x58 at 2 hides another) and the decal's 7 · 7 cells, of two triangles each.
        ASSERT_EQ(m_batch.Vertices().size(), 6u + 49u * 6u);
        const auto& decal = m_batch.Commands().back();
        EXPECT_EQ(decal.state.blend, Client::Render::ModelBlend::Additive);
        ASSERT_TRUE(decal.state.texture.has_value());
        EXPECT_EQ(decal.state.texture->value, 0x61e);
        const double pulse = RoundedSin(1000.0 * static_cast<double>(std::bit_cast<float>(0x3ac49ba6u))) *
                                 static_cast<double>(std::bit_cast<float>(0x3e99999au)) +
                             static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));
        ExpectColor(m_batch.Vertices()[6], static_cast<float>(pulse * 0.5), static_cast<float>(pulse * 0.5),
                    static_cast<float>(pulse), 1.0f);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[6].position.z, 5.0f);
        EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
        TearDown();
    }
}

// Without the terrain heights, the decal cuts at 0x5fb2f0 after the shadow.
TEST_F(MonsterDrawingTest, TheFloorMarkNeedsTheTerrainHeights)
{
    Monster(6, Spider, 0xe8);
    const auto result = Draw(Pass());
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x5fb2f0"), std::string::npos) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
}

TEST_F(MonsterDrawingTest, TheOutlineGoesBeforeTheBodyWithTheMonsterOrTheNpcColors)
{
    m_world->Runtime().map = 2;
    Monster(4, Spider, 3);
    ActorState& npc = Monster(9, DeviasNpc, 241);
    npc.byte88 = 4;
    GameCharacterPass pass = Pass();
    pass.hovered = 4;
    pass.targeted = 9;

    const auto result = Draw(pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // Each one: two outline passes, the body and the shadow.
    ASSERT_EQ(m_batch.Vertices().size(), 24u);
    EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Additive);
    ExpectColor(m_batch.Vertices()[0], 0.1f, 0.01f, 0.0f, 1.0f);
    ExpectColor(m_batch.Vertices()[3], 0.7f, 0.07f, 0.0f, 1.0f);
    ExpectColor(m_batch.Vertices()[12], 0.02f, 0.1f, 0.0f, 1.0f);
    ExpectColor(m_batch.Vertices()[15], 0.16f, 0.7f, 0.0f, 1.0f);
    // The monster with +0x94 at 1.2: 0.1 / scale + 1. The NPC: +0x94, vertex (1, 0, 0) at 1.2.
    EXPECT_FLOAT_EQ(m_batch.Vertices()[1].position.x, 151.1f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[13].position.x, 151.2f);
}

// 0x60cdd9: the Kalima gates (0xc5) carry the outline only on mesh 0 (0x60ce4e); any other, on both. The 0xc5's body is
// drawn by 0x438fe0 (both meshes) and its helper, 0x43c150, standing (action 0) adds nothing.
TEST_F(MonsterDrawingTest, TheKalimaGateOutlinesOnlyItsFirstMesh)
{
    constexpr std::uint16_t Gate = 0xc5;
    m_models.AddMesh();
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world = std::make_unique<Client::Kernel::WorldState>();
        SetUp();
        m_world->Runtime().map = map;
        Monster(4, Gate, 152);
        GameCharacterPass pass = Pass();
        pass.hovered = 4;

        const auto gate = Draw(pass);

        ASSERT_TRUE(gate.HasValue()) << gate.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 12u);
        ExpectColor(m_batch.Vertices()[0], 0.1f, 0.01f, 0.0f, 1.0f);
        ExpectColor(m_batch.Vertices()[3], 0.7f, 0.07f, 0.0f, 1.0f);
        TearDown();

        m_world = std::make_unique<Client::Kernel::WorldState>();
        SetUp();
        m_world->Runtime().map = map;
        Monster(4, Spider, 3);
        // SetUp rebuilt the light: the pass is rebuilt so it does not point at the previous one.
        pass = Pass();
        pass.hovered = 4;
        const auto spider = Draw(pass);
        ASSERT_TRUE(spider.HasValue()) << spider.Error();
        // Two passes for the two meshes, the body and the shadow.
        EXPECT_EQ(m_batch.Vertices().size(), 24u);
        TearDown();
    }
    m_world = std::make_unique<Client::Kernel::WorldState>();
    SetUp();
    ActorState& gate = Monster(4, Gate, 152);
    gate.dword58 = 0;
    GameCharacterPass pass = Pass();
    pass.hovered = 4;
    const auto textured = Draw(pass);
    ASSERT_FALSE(textured.HasValue());
    EXPECT_NE(textured.Error().find("0x60ce4e"), std::string::npos) << textured.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 0u);
}

// Without the body's cuts (0x438fe0 and 0x42cc10), the tail reaches the helpers of 0x510a1e: the 0xc5 in action 1
// counts its +0x60 in 0x43c150 (in Lorencia and Devias, with any map) and the 0x1a1 of 30 without the siege turns off
// its +0x42c and leaves +0x58 at −2 in 0x42dfe0; neither draws rand() beyond the three of what hangs.
TEST_F(MonsterDrawingTest, TheBodyLeadsToTheKalimaAndSiegeHelpers)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world = std::make_unique<Client::Kernel::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        m_world->Runtime().map = map;
        ActorState& gate = Monster(4, 0xc5, 152);
        gate.animation.action = 1;
        gate.dword60 = 2;
        const auto drawn = Draw(Pass());
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(gate.dword60, 3);
        EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
        TearDown();
    }
    m_world = std::make_unique<Client::Kernel::WorldState>();
    SetUp();
    m_clothWorld.random = Client::Crt::CrtRandom{1};
    m_world->Runtime().map = 30;
    ActorState& castle = Monster(4, 0x1a1, 3);
    castle.byte42c = 1;
    const auto drawn = Draw(Pass());
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(castle.byte42c, 0);
    EXPECT_EQ(castle.dword58, -2);
    EXPECT_GT(m_batch.Vertices().size(), 0u);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
}

// 0x441688..0x4416a5: the 0x1a4 dying (action 6) requests 0x4ee770(0, 0x13c, 0) over the meshes of its freshly posed
// body: one rand() % 45 per corner (three, of one triangle) after the three of what hangs, the 0x13c stones that come
// out, +0 at 0 and sound 0x209.
TEST_F(MonsterDrawingTest, The1a4DiesInStonesOverItsPosedBody)
{
    // The first seed with a stone at the first corner.
    std::uint32_t seed = 1;
    for (;; ++seed)
    {
        Client::Crt::CrtRandom probe{seed};
        for (int draw = 0; draw < 3; ++draw)
            static_cast<void>(probe.Next());
        if (probe.Next() % 45 == 0)
            break;
    }
    m_clothWorld.random = Client::Crt::CrtRandom{seed};
    ActorState& golem = Monster(4, 0x1a4, 0x1a4);
    golem.animation.action = 6;
    const auto drawn = Draw(Pass());
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_FALSE(golem.alive);
    EXPECT_EQ(m_world->Runtime().lastSoundId, Client::Audio::SoundId{0x209});
    Client::Crt::CrtRandom expected{seed};
    for (int draw = 0; draw < 3; ++draw)
        static_cast<void>(expected.Next());
    std::size_t stones = 0;
    for (int corner = 0; corner < 3; ++corner)
    {
        if (expected.Next() % 45 != 0)
            continue;
        expected.Discard(8);
        ++stones;
    }
    EXPECT_EQ(m_clothWorld.random.State(), expected.State());
    const auto live = static_cast<std::size_t>(std::ranges::count_if(
        m_world->Map().effects.Effects(), [](const auto& effect) { return effect.alive && effect.type == 0x13c; }));
    EXPECT_EQ(live, stones);
    EXPECT_GE(stones, 1u);
}

TEST_F(MonsterDrawingTest, TheFrozenFlagPaintsTheBodyAndAddsAPassWithoutTheBlendMesh)
{
    ActorState& spider = Monster(2, Spider, 3);
    spider.flags78 = 2;

    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 9u);
    ExpectColor(m_batch.Vertices()[0], 0.3f, 0.5f, 1.0f, 1.0f);
    ExpectColor(m_batch.Vertices()[3], 0.3f, 0.5f, 1.0f, 1.0f);
}

TEST_F(MonsterDrawingTest, TheLorenciaFairyNpcDrawsTwoSpinningFlaresAndDrawsItsJointRandom)
{
    ActorState& fairy = Monster(6, FairyNpc, 237);
    fairy.byte88 = 4;

    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 2u);
    for (const auto& sprite : sprites)
    {
        EXPECT_EQ(sprite.texture, 0x5d5);
        // (3.5, −12, 10) from bone 20, at (0, 0, 20).
        EXPECT_EQ(sprite.position, glm::vec3(153.5f, 138.0f, 40.0f));
        EXPECT_FLOAT_EQ(sprite.scale, 0.3f);
        EXPECT_FLOAT_EQ(sprite.light.x, 0.65f);
    }
    // The rand() of 0x50db57, the two of +0x38e in the hands and the % 30 of 0x510815 (26500 % 30 = 10: no joint).
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(4));
}

// 0x5108a9: the fairy's 0x618/6, ownerless, from the random point toward bone 20's, with the fairy's angles and width
// 20 (0x53214b..0x5322df): speed 3, life 120, 8 points and light (0.4, 0.3, 0.2), no rand().
TEST_F(MonsterDrawingTest, TheFairyLeavesItsBeamAfterItsThreeRandoms)
{
    // With seed 51 the fourth rand() is 17250: % 30 gives 0, and the three % 100 of the start point are drawn.
    m_clothWorld.random = Client::Crt::CrtRandom{51};
    Monster(6, FairyNpc, 237).byte88 = 4;

    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(LiveSprites().size(), 2u);
    const auto& beam = m_joints[0];
    ASSERT_TRUE(beam.alive);
    EXPECT_EQ(beam.type, 0x618);
    EXPECT_EQ(beam.subtype, 6);
    EXPECT_EQ(beam.ownerContext, nullptr);
    EXPECT_EQ(beam.targetPosition, glm::vec3(153.5f, 138.0f, 40.0f));
    EXPECT_EQ(beam.width, 20.0f);
    EXPECT_EQ(beam.speed, 3.0f);
    EXPECT_EQ(beam.life, 120);
    EXPECT_EQ(beam.maximumCount, 8);
    EXPECT_EQ(beam.light, glm::vec3(std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3e99999au),
                                    std::bit_cast<float>(0x3e4ccccdu)));
    Client::Crt::CrtRandom expected{51};
    for (int draw = 0; draw < 7; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(m_clothWorld.random.State(), expected.State());
}

TEST_F(MonsterDrawingTest, AModelCaseDrawsItsSpritesAndSkipsTheHelperChain)
{
    Monster(8, Golem, 0x1e);

    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // 0x50b920 (two 0x5ff) and eight 0x50b870.
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 10u);
    EXPECT_EQ(sprites[0].texture, 0x5ff);
    EXPECT_EQ(sprites[0].position, glm::vec3(155.0f, 150.0f, 52.0f));
    EXPECT_FLOAT_EQ(sprites[0].light.x, 0.8f);
    EXPECT_EQ(sprites[2].texture, 0x5c5);
    EXPECT_FLOAT_EQ(sprites[2].light.y, 0.42f);
    EXPECT_EQ(m_world->Actors()[8].effectOrigin, glm::vec3(145.0f, 150.0f, 53.0f));
}

// 0x50c37e: the +0x376 0x4b leaves fire 0x5d9/0 of 0.3 on bone 18 (three rand(), 0x541172) and goes on with its shine.
TEST_F(MonsterDrawingTest, The0x4bLightsItsFireOnBone18)
{
    m_clothWorld.random = Client::Crt::CrtRandom{1};
    Monster(0, Spider, 0x4b);
    auto particles = std::make_unique<Client::Particles::Particles>();
    GameCharacterPass pass = Pass();
    pass.particles = particles.get();
    const auto result = Draw(pass);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    const Client::Particles::Particle& fire = particles->All()[0];
    EXPECT_TRUE(fire.alive);
    EXPECT_EQ(fire.type, 0x5d9);
    EXPECT_EQ(fire.subtype, 0);
    EXPECT_EQ(fire.life, 24);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3 + TailDraws));
}

TEST_F(MonsterDrawingTest, UnportedBranchesCutWithTheirAddress)
{
    struct Case
    {
        std::uint16_t type;
        std::uint16_t monster;
        std::string_view address;
        std::size_t vertices;
        std::size_t randoms;
    };
    // The cloth over the mesh without the slot's persistent cloth cuts before the body; the 0x1b6 of 0x4271d0, after
    // what hangs and its rand() % 30, at the first named bone its creation did not register (0x427291); the one of
    // 0x50bf45, after the shadow.
    constexpr std::array cases{Case{Spider, 0x59, "0x50bb96", 0, 0}, Case{HelperType, 0x20, "0x427291", 6, 4},
                               Case{Spider, 0x49, "0x50c595", 9, 0}};
    for (const Case& value : cases)
    {
        SCOPED_TRACE(value.address);
        m_world = std::make_unique<Client::Kernel::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        Monster(0, value.type, value.monster);
        const auto result = Draw(Pass());
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find(value.address), std::string::npos) << result.Error();
        EXPECT_EQ(m_batch.Vertices().size(), value.vertices);
        EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(value.randoms));
        TearDown();
    }
}

// 0x601bd3..0x601c0d through the game draw: the 0x18e with fewer than 25 steps is not drawn and requests
// 0x4ee770(0, 0x10a, 0, 0, 0) over the meshes 0x5ffdf0 posed, after the counter's four rand(): its stones go to the
// frame table. Without the table, it cuts.
TEST_F(MonsterDrawingTest, TheSpiritStatueLeavesItsStonesInTheFrameTable)
{
    m_clothWorld.random = Client::Crt::CrtRandom{1};
    Monster(0, 0x18e, 0x21);
    const auto cut = Draw(Pass());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4ee770: faltan los efectos"), std::string::npos) << cut.Error();
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(4));

    TearDown();
    SetUp();
    m_clothWorld.random = Client::Crt::CrtRandom{1};
    Monster(0, 0x18e, 0x21);
    GameCharacterPass pass = Pass();
    pass.effects = &m_world->Map().effects;
    const auto drawn = Draw(pass);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    const auto effects = m_world->Map().effects.Effects();
    const auto stones =
        std::ranges::count_if(effects, [](const Client::Effects::ModelEffect& effect)
                              { return effect.alive && (effect.type == 0x10a || effect.type == 0x10b); });
    EXPECT_GT(stones, 0);
    EXPECT_NE(m_clothWorld.random.State(), AfterRandoms(4));
}

// 0x50c671, 0x50c71b and 0x50c8ce: after the shadow, the shine passes of 0x60b130 over the body's pose, with the color
// of 0x608e00 (k = alpha · factor times the model's: (1, 0.5, 0) for all, (0.8, 0.8, 1) for the 0x16d with 0x100) and
// additive RenderBody: 0x44 with texture 0x5c0, 0x48 with 0x5fc (or the 0x5fd of the 0x35 and 0x36) and 0x140 without
// shine, with the model's. In Lorencia and in Devias, the same: they neither check the map nor draw rand().
TEST_F(MonsterDrawingTest, TheGlowingMonstersAddTheirShinePasses)
{
    struct Shine
    {
        std::uint16_t slot;
        float red;
        float green;
        float blue;
    };
    struct Case
    {
        std::uint16_t type;
        std::uint16_t monster;
        bool shadow;
        std::vector<Shine> passes;
    };
    const std::vector<Case> cases{
        {Spider, 0x26, true, {{0x5c0, 1.0f, 0.5f, 0.0f}}},
        // The 0x3b, without shadow, with factor 0.5.
        {Spider, 0x3b, false, {{0x5c0, 0.5f, 0.25f, 0.0f}}},
        {Spider, 0x2b, true, {{0x5fc, 1.0f, 0.5f, 0.0f}, {0x5c0, 1.0f, 0.5f, 0.0f}}},
        {Spider, 0x50, true, {{0x5fc, 1.0f, 0.5f, 0.0f}, {0x5c0, 1.0f, 0.5f, 0.0f}}},
        {0x16d, 0x26, true, {{0x5c0, 1.0f, 0.2f, 0.0f}}},
        {0x16d, 0x43, true, {{0x5c0, 0.8f, 0.8f, 1.0f}}},
        {Spider, 0x35, true, {{0x5fd, 1.0f, 0.5f, 0.0f}}},
        {Spider, 0x87, true, {{0x300, 1.0f, 0.5f, 0.0f}}},
    };
    for (const std::int32_t map : {0, 2})
    {
        for (const Case& value : cases)
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", +0x376 " << value.monster);
            m_world = std::make_unique<Client::Kernel::WorldState>();
            SetUp();
            m_clothWorld.random = Client::Crt::CrtRandom{1};
            m_world->Runtime().map = map;
            Monster(0, value.type, value.monster);

            const auto result = Draw(Pass());

            ASSERT_TRUE(result.HasValue()) << result.Error();
            const std::size_t first = value.shadow ? 6u : 3u;
            ASSERT_EQ(m_batch.Vertices().size(), first + 3 * value.passes.size());
            const auto& commands = m_batch.Commands();
            ASSERT_EQ(commands.size(), (value.shadow ? 2u : 1u) + value.passes.size());
            for (std::size_t index = 0; index < value.passes.size(); ++index)
            {
                const Shine& pass = value.passes[index];
                const auto& command = commands[commands.size() - value.passes.size() + index];
                EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Additive);
                ASSERT_TRUE(command.state.texture.has_value());
                EXPECT_EQ(command.state.texture->value, pass.slot);
                ExpectColor(m_batch.Vertices()[first + 3 * index], pass.red, pass.green, pass.blue, 1.0f);
            }
            // The passes' meshes are the body's (vertices 0 to 2), not flattened.
            EXPECT_EQ(m_batch.Vertices()[first].position, m_batch.Vertices()[0].position);
            // The one of 0x50db57 and the two of +0x38e in the hands loop: the passes draw none.
            EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
            TearDown();
        }
    }
}

// Q-C95 on the shine of a fading monster: with alpha at 0.5, 0x608e00 leaves k = 0.5 (the 0x3b's factor,
// 0.5 · 0.5 = 0.25) times the model's color, and RenderMesh multiplies it again by the alpha in place on each mesh.
TEST_F(MonsterDrawingTest, AFadingShineDarkensEachMeshInPlace)
{
    m_models.AddMesh();
    struct Case
    {
        std::uint16_t monster;
        float k;
        std::size_t first;
    };
    // The 0x3b has no shadow; the 0x26 does (alpha does not drop below 0.3f).
    for (const Case& value : {Case{0x26, 0.5f, 12}, Case{0x3b, 0.25f, 6}})
    {
        SCOPED_TRACE(value.monster);
        m_world = std::make_unique<Client::Kernel::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        Monster(0, Spider, value.monster).alpha = 0.5f;

        const auto result = Draw(Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        // The body's two meshes, the shadow's and the two of the 0x44 pass.
        ASSERT_EQ(m_batch.Vertices().size(), value.first + 6);
        ExpectColor(m_batch.Vertices()[value.first], value.k * 0.5f, value.k * 0.25f, 0.0f, 1.0f);
        ExpectColor(m_batch.Vertices()[value.first + 3], value.k * 0.25f, value.k * 0.125f, 0.0f, 1.0f);
        TearDown();
    }
}

// 0x50c951 and 0x50d58b: the +0x376 0x37, with any type, builds and simulates the ClothMonster cloth (bVar23) outside
// maps 0x12 to 0x17, on its own bones, before what hangs; the scene does the same as CharacterCloth.
TEST_F(MonsterDrawingTest, TheMonster0x37WearsItsClothOutsideChaosCastle)
{
    struct Case
    {
        std::int32_t map;
        bool wears;
    };
    constexpr std::array cases{Case{0, true}, Case{2, true}, Case{0x12, false}};
    for (const Case& value : cases)
    {
        SCOPED_TRACE(value.map);
        m_world = std::make_unique<Client::Kernel::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        m_world->Runtime().map = value.map;
        ActorState& monster = Monster(3, Spider, 0x37);
        std::vector<Client::Actors::Drawing::CharacterCloth> cloths(4);
        GameCharacterPass pass = Pass();
        pass.cloths = cloths;
        Client::Models::ClothWorld expectedWorld = m_clothWorld;

        const auto result = Draw(pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        if (!value.wears)
        {
            EXPECT_TRUE(cloths[3].Cloths().empty());
            EXPECT_EQ(m_batch.Vertices().size(), 6u);
            EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
            TearDown();
            continue;
        }
        ASSERT_EQ(cloths[3].Cloths().size(), 1u);
        EXPECT_EQ(cloths[3].Count(), 1);
        Client::Actors::Drawing::CharacterCloth expected;
        Client::Render::ModelBatch expectedBatch;
        expectedBatch.Begin(Client::Render::Pass3D{});
        Client::Actors::Drawing::ClothWearer wearer;
        wearer.monster = std::uint16_t{0x37};
        const Client::Models::ClothBody body{
            .bones = monster.bodyPose, .angles = monster.angles, .scale = monster.scale, .position = monster.position};
        expected.Frame(expectedBatch, wearer, body, expectedWorld, static_cast<std::uint8_t>(value.map), 0.0f, true);
        ASSERT_EQ(expected.Cloths().size(), 1u);
        const auto particles = cloths[3].Cloths()[0].Particles();
        const auto reference = expected.Cloths()[0].Particles();
        ASSERT_EQ(particles.size(), reference.size());
        for (std::size_t index = 0; index < particles.size(); ++index)
            EXPECT_EQ(particles[index].position, reference[index].position) << index;
        // The body, the shadow and then the cloth, as CharacterCloth draws it.
        ASSERT_EQ(m_batch.Vertices().size(), 6u + expectedBatch.Vertices().size());
        for (std::size_t index = 0; index < expectedBatch.Vertices().size(); ++index)
            EXPECT_EQ(m_batch.Vertices()[6 + index].position, expectedBatch.Vertices()[index].position) << index;
        for (std::size_t call = 0; call < 3; ++call)
            static_cast<void>(expectedWorld.random.Next());
        EXPECT_EQ(m_clothWorld.random.State(), expectedWorld.random.State());
        EXPECT_NE(m_clothWorld.random.State(), AfterRandoms(3));
        TearDown();
    }
    m_world = std::make_unique<Client::Kernel::WorldState>();
    SetUp();
    Monster(3, Spider, 0x37);
    const auto unsupplied = Draw(Pass());
    ASSERT_FALSE(unsupplied.HasValue());
    EXPECT_NE(unsupplied.Error().find("0x50d58b"), std::string::npos) << unsupplied.Error();
}

// 0x50c177: the +0x376 0x4d (monster 77, type 0x189 with its second model 0x18a). With the clock at 0, s = 0.5: the
// 0x44 pass with factor 0.65 (k times the model's (1, 0.5, 0)), +0x68 at f(1.5 · 0.3f), the body again at 1.2 and
// at 1.4 around its bone with the blendMesh mesh 0, the 0x18a with its 0x42 Chrome01 pass and its common one at 0.6,
// and the 0x4d's grid cloth, as CharacterCloth builds and simulates it. In Lorencia, in Devias and in Icarus (map 10,
// no shadow), the same.
TEST_F(MonsterDrawingTest, ThePhoenixDrawsItsWiderBodiesItsSecondModelAndItsCloth)
{
    constexpr std::uint16_t Phoenix = 0x189;
    for (const std::int32_t map : {0, 2, 10})
    {
        SCOPED_TRACE(map);
        m_world = std::make_unique<Client::Kernel::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        m_world->Runtime().map = map;
        ActorState& phoenix = Monster(3, Phoenix, 0x4d);
        std::vector<Client::Actors::Drawing::CharacterCloth> cloths(4);
        GameCharacterPass pass = Pass();
        pass.cloths = cloths;
        Client::Models::ClothWorld expectedWorld = m_clothWorld;

        const auto result = Draw(pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        // The type comes back, +0x64 stays at −1 and +0x68 at the light of the blendMesh cloth.
        EXPECT_EQ(phoenix.type, Phoenix);
        EXPECT_EQ(phoenix.dword64, -1);
        EXPECT_EQ(phoenix.blendMeshLight, static_cast<float>((1.0 - 0.5 + 1.0) * static_cast<double>(0.3f)));
        // 0x743748c, 0x74374bc and 0x74374ec: bones 23 and 14 of the 0x18a and bone 24 of the 0x189 (bone k at (0, 0,
        // k)).
        const auto& bones = m_world->Runtime().phoenixBones;
        EXPECT_EQ(bones[0][3], glm::vec3(0.0f, 0.0f, 23.0f));
        EXPECT_EQ(bones[1][3], glm::vec3(0.0f, 0.0f, 14.0f));
        EXPECT_EQ(bones[2][3], glm::vec3(0.0f, 0.0f, 24.0f));
        const std::size_t shadow = map == 10 ? 0u : 3u;
        const auto& vertices = m_batch.Vertices();
        ASSERT_GE(vertices.size(), 3u + shadow + 15u);
        // The body at 0.6 (0x5ffce0 with the 0x189).
        ExpectColor(vertices[0], 0.6f, 0.6f, 0.6f, 1.0f);
        // The 0x44 pass: k = 0.65.
        const std::size_t shine = 3 + shadow;
        ExpectColor(vertices[shine], 0.65f, 0.325f, 0.0f, 1.0f);
        // 0x602e90(object, 1, 2, 0) and (object, 1, 3, 0): vertex (1, 0, 0) of bone 0 at 1.2 and at 1.4.
        EXPECT_FLOAT_EQ(vertices[shine + 3 + 1].position.x, 151.2f);
        EXPECT_FLOAT_EQ(vertices[shine + 6 + 1].position.x, 151.4f);
        EXPECT_FLOAT_EQ(vertices[shine + 6 + 2].position.y, 151.4f);
        // The 0x18a: RenderBody(0x42) with Chrome01 and the common one at 0.6, at scale 1.
        const auto& commands = m_batch.Commands();
        const std::size_t second = shine + 9;
        EXPECT_FLOAT_EQ(vertices[second + 1].position.x, 151.0f);
        ExpectColor(vertices[second + 3], 0.6f, 0.6f, 0.6f, 1.0f);
        bool chromed = false;
        for (const auto& command : commands)
            chromed = chromed || (command.state.texture.has_value() && command.state.texture->value == 0x5c0 &&
                                  command.state.blend == Client::Render::ModelBlend::Additive);
        EXPECT_TRUE(chromed);
        // The cloth: one, built and simulated on the 0x18a's pose.
        ASSERT_EQ(cloths[3].Cloths().size(), 1u);
        EXPECT_EQ(cloths[3].Count(), 1);
        EXPECT_EQ(cloths[3].Cloths()[0].Front().value, 0x629);
        EXPECT_EQ(cloths[3].Cloths()[0].Columns(), 5);
        EXPECT_EQ(cloths[3].Cloths()[0].Rows(), 12);
        EXPECT_EQ(cloths[3].Cloths()[0].Spheres().size(), 1u);
        Client::Actors::Drawing::CharacterCloth expected;
        Client::Render::ModelBatch expectedBatch;
        expectedBatch.Begin(Client::Render::Pass3D{});
        const Client::Models::ClothBody body{
            .bones = phoenix.bodyPose, .angles = phoenix.angles, .scale = phoenix.scale, .position = phoenix.position};
        expected.FrameTheMonsterCloth(expectedBatch, 0x4d, body, expectedWorld, static_cast<std::uint8_t>(map), 0.0f);
        ASSERT_EQ(expected.Cloths().size(), 1u);
        const auto particles = cloths[3].Cloths()[0].Particles();
        const auto reference = expected.Cloths()[0].Particles();
        ASSERT_EQ(particles.size(), reference.size());
        for (std::size_t index = 0; index < particles.size(); ++index)
            EXPECT_EQ(particles[index].position, reference[index].position) << index;
        ASSERT_EQ(vertices.size(), second + 6 + expectedBatch.Vertices().size());
        for (std::size_t index = 0; index < expectedBatch.Vertices().size(); ++index)
            EXPECT_EQ(vertices[second + 6 + index].position, expectedBatch.Vertices()[index].position) << index;
        // Those of the cloth and then the one of 0x50db57 and the two of +0x38e of the hands loop.
        for (std::size_t call = 0; call < 3; ++call)
            static_cast<void>(expectedWorld.random.Next());
        EXPECT_EQ(m_clothWorld.random.State(), expectedWorld.random.State());
        EXPECT_NE(m_clothWorld.random.State(), AfterRandoms(3));
        TearDown();
    }
}

// 0x50c59f: the +0x376 0x47 (with texture 0x253), the 0x4a and the 0x12d (with 0x255), with any type, build in their
// slot a grid cloth on bone 0x13 (5 · 15, no spheres) over the stored pose, simulate it and draw it after the shadow,
// like CharacterCloth. In Lorencia, in Devias and in Icarus (map 10, no shadow), the same.
TEST_F(MonsterDrawingTest, TheCrustsWearTheirGridClothWithTheirTexture)
{
    struct Case
    {
        std::uint16_t monster;
        std::uint16_t texture;
    };
    constexpr std::array cases{Case{0x47, 0x253}, Case{0x4a, 0x255}, Case{0x12d, 0x255}};
    for (const std::int32_t map : {0, 2, 10})
    {
        for (const Case& value : cases)
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", +0x376 " << value.monster);
            m_world = std::make_unique<Client::Kernel::WorldState>();
            SetUp();
            m_clothWorld.random = Client::Crt::CrtRandom{1};
            m_world->Runtime().map = map;
            ActorState& crust = Monster(5, Spider, value.monster);
            std::vector<Client::Actors::Drawing::CharacterCloth> cloths(6);
            GameCharacterPass pass = Pass();
            pass.cloths = cloths;
            Client::Models::ClothWorld expectedWorld = m_clothWorld;

            const auto result = Draw(pass);

            ASSERT_TRUE(result.HasValue()) << result.Error();
            ASSERT_EQ(cloths[5].Cloths().size(), 1u);
            EXPECT_EQ(cloths[5].Count(), 1);
            const auto& cloth = cloths[5].Cloths()[0];
            EXPECT_EQ(cloth.Front().value, value.texture);
            EXPECT_EQ(cloth.Back().value, value.texture);
            EXPECT_EQ(cloth.Bone(), 0x13);
            EXPECT_EQ(cloth.Columns(), 5);
            EXPECT_EQ(cloth.Rows(), 15);
            EXPECT_TRUE(cloth.Spheres().empty());
            Client::Actors::Drawing::CharacterCloth expected;
            Client::Render::ModelBatch expectedBatch;
            expectedBatch.Begin(Client::Render::Pass3D{});
            const Client::Models::ClothBody body{
                .bones = crust.bodyPose, .angles = crust.angles, .scale = crust.scale, .position = crust.position};
            expected.FrameTheMonsterCloth(expectedBatch, value.monster, body, expectedWorld,
                                          static_cast<std::uint8_t>(map), 0.0f);
            const auto particles = cloth.Particles();
            const auto reference = expected.Cloths()[0].Particles();
            ASSERT_EQ(particles.size(), reference.size());
            for (std::size_t index = 0; index < particles.size(); ++index)
                EXPECT_EQ(particles[index].position, reference[index].position) << index;
            const std::size_t first = map == 10 ? 3u : 6u;
            ASSERT_EQ(m_batch.Vertices().size(), first + expectedBatch.Vertices().size());
            for (std::size_t index = 0; index < expectedBatch.Vertices().size(); ++index)
                EXPECT_EQ(m_batch.Vertices()[first + index].position, expectedBatch.Vertices()[index].position)
                    << index;
            for (std::size_t call = 0; call < 3; ++call)
                static_cast<void>(expectedWorld.random.Next());
            EXPECT_EQ(m_clothWorld.random.State(), expectedWorld.random.State());
            TearDown();
        }
    }
    m_world = std::make_unique<Client::Kernel::WorldState>();
    SetUp();
    Monster(5, Spider, 0x47);
    const auto unsupplied = Draw(Pass());
    ASSERT_FALSE(unsupplied.HasValue());
    EXPECT_NE(unsupplied.Error().find("0x50c59f"), std::string::npos) << unsupplied.Error();
}

// Between the original's frames the 0x4d draws the same on the copy: it does not store +0x64, +0x68, the type or the
// bones, does not build the cloth or draw rand(); without the slot's persistent cloth, it cuts at 0x50c283.
TEST_F(MonsterDrawingTest, ThePhoenixBetweenOriginalFramesStoresNothing)
{
    ActorState& phoenix = Monster(3, 0x189, 0x4d);
    std::vector<Client::Actors::Drawing::CharacterCloth> cloths(4);
    GameCharacterPass pass = Pass();
    pass.cloths = cloths;
    pass.originalFrames = 0;

    const auto result = Draw(pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(phoenix.dword64, -1);
    EXPECT_EQ(phoenix.blendMeshLight, 1.0f);
    EXPECT_TRUE(phoenix.bodyPose.empty());
    EXPECT_EQ(m_world->Runtime().phoenixBones[2], Client::Math::BoneTransform{});
    EXPECT_TRUE(cloths[3].Cloths().empty());
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
    const auto unsupplied = Draw(Pass());
    ASSERT_FALSE(unsupplied.HasValue());
    EXPECT_NE(unsupplied.Error().find("0x50c283"), std::string::npos) << unsupplied.Error();
}

// 0x50c762: the +0x376 0x2a hangs from the wings slot the 0xef from bone 9 with (0, 0, −40) and the 0xf0 from 0x3d with
// (0, −40, 45), the latter at 0.9f with light at 1; the scale and the light come back. Each one's origin is the offset
// turned by its bone and scaled like the body (0x4ec190), plus the position; the vertex of bone 0, that bone times the
// hanging model's scale plus the origin; the 0xef's color is the hanging light left from the previous frame. Each
// 0x509340 draws the close of +0x38e and the rand() of 0x509a92. In Lorencia, in Devias and on map 5, the one of both
// models; the 0x2a has no shadow (0x50bd89).
TEST_F(MonsterDrawingTest, The0x2aCarriesTheHeadAndThePrincessFromTheWingsSlot)
{
    for (const std::int32_t map : {0, 2, 5})
    {
        SCOPED_TRACE(map);
        m_world = std::make_unique<Client::Kernel::WorldState>();
        SetUp();
        m_models.SetBoneCount(64);
        m_models.SetActionCount(2);
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        m_world->Runtime().map = map;
        ActorState& carrier = Monster(4, Spider, 0x2a);
        carrier.scale = 1.25f;
        carrier.drawnLight = {0.5f, 0.25f, 0.125f};

        const auto result = Draw(Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(carrier.scale, 1.25f);
        EXPECT_EQ(carrier.wings.model, 0xf0);
        EXPECT_EQ(carrier.wings.bone, 0x3d);
        EXPECT_EQ(carrier.wings.animation.action, 1);
        EXPECT_EQ(carrier.wings.animation.previousAction, 1);
        EXPECT_EQ(carrier.wings.speed, std::bit_cast<float>(0x3e4ccccdu));
        ASSERT_EQ(carrier.bodyPose.size(), 64u);
        const auto& vertices = m_batch.Vertices();
        // The body, the 0xef and the 0xf0.
        ASSERT_EQ(vertices.size(), 9u);
        // The 0xef: (150, 150, 10) + (0, 0, −40) · 1.25, plus bone 9 times 1.25.
        EXPECT_FLOAT_EQ(vertices[3].position.x, 150.0f);
        EXPECT_FLOAT_EQ(vertices[3].position.y, 150.0f);
        EXPECT_FLOAT_EQ(vertices[3].position.z, -40.0f + 9.0f * 1.25f);
        EXPECT_FLOAT_EQ(vertices[4].position.x, 151.25f);
        ExpectColor(vertices[3], 0.5f, 0.25f, 0.125f, 1.0f);
        // The 0xf0: (150, 150, 10) + (0, −40, 45) · 1.25, plus bone 0x3d times 0.9, with light at 1.
        EXPECT_FLOAT_EQ(vertices[6].position.x, 150.0f);
        EXPECT_FLOAT_EQ(vertices[6].position.y, 100.0f);
        EXPECT_FLOAT_EQ(vertices[6].position.z, 66.25f + 61.0f * std::bit_cast<float>(0x3f666666u));
        EXPECT_FLOAT_EQ(vertices[7].position.x, 150.0f + std::bit_cast<float>(0x3f666666u));
        ExpectColor(vertices[6], 1.0f, 1.0f, 1.0f, 1.0f);
        // The two of each 0x509340 and the three of the tail.
        EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(7));
        TearDown();
    }
}

// 0x50c83b: the Blood Castle statues (+0x376 0x84 to 0x86) hang from bone 1, through the slot of 0x5093f3, the 0xc1c
// and the 0x225 at 0.7f with (90, 0, 90) and (0, 80, 120), and the 0xa24 at 0.9f with (10, 0, 0) and (0, 110, 80), from
// the character's position. The sprites of the 0xc1c and the 0x225 draw their rand(); the 0x225, one more with action
// 0. In Lorencia, in Devias and in Blood Castle (map 11).
TEST_F(MonsterDrawingTest, TheBloodCastleStatuesHoldTheirWeaponInPlace)
{
    struct Case
    {
        std::uint16_t monster;
        std::uint16_t model;
        float scale;
        glm::vec3 angles;
        glm::vec3 translation;
    };
    const std::array cases{
        Case{0x84, 0xc1c, std::bit_cast<float>(0x3f333333u), {90.0f, 0.0f, 90.0f}, {0.0f, 80.0f, 120.0f}},
        Case{0x85, 0x225, std::bit_cast<float>(0x3f333333u), {90.0f, 0.0f, 90.0f}, {0.0f, 80.0f, 120.0f}},
        Case{0x86, 0xa24, std::bit_cast<float>(0x3f666666u), {10.0f, 0.0f, 0.0f}, {0.0f, 110.0f, 80.0f}}};
    for (const std::int32_t map : {0, 2, 11})
    {
        for (const Case& value : cases)
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", +0x376 " << value.monster);
            m_world = std::make_unique<Client::Kernel::WorldState>();
            SetUp();
            m_models.SetActionCount(2);
            m_clothWorld.random = Client::Crt::CrtRandom{1};
            m_world->Runtime().map = map;
            ActorState& statue = Monster(6, Spider, value.monster);
            const std::size_t before = LiveSprites().size();

            const auto result = Draw(Pass());

            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(statue.scale, 1.0f);
            EXPECT_EQ(statue.wings.model, value.model);
            EXPECT_EQ(statue.wings.bone, 1);
            EXPECT_EQ(statue.wings.animation.action, 1);
            EXPECT_EQ(statue.wings.speed, std::bit_cast<float>(0x3e4ccccdu));
            const auto& vertices = m_batch.Vertices();
            // The body, the shadow (none on 11, like Spider) and the weapon's passes (0x60b3d0).
            ASSERT_GE(vertices.size(), 9u);
            // Bone 0 of what hangs is the root: the body's bone 1 times the slot, at the model's scale.
            const Client::Math::BoneTransform root = Client::Actors::PlacedRoot(
                statue.bodyPose[1], Client::Actors::HangingPlacement{value.angles, value.translation});
            const glm::vec3 expected = root[3] * value.scale + statue.position;
            EXPECT_NEAR(vertices[6].position.x, expected.x, 1e-3f);
            EXPECT_NEAR(vertices[6].position.y, expected.y, 1e-3f);
            EXPECT_NEAR(vertices[6].position.z, expected.z, 1e-3f);
            EXPECT_NEAR(root[3].y, value.translation.y, 1e-3f);
            // The close of +0x38e, the one of 0x509a92 and those of the model's sprites; then the three of the tail.
            Client::Crt::CrtRandom expectedRandom{1};
            static_cast<void>(expectedRandom.Next());
            static_cast<void>(expectedRandom.Next());
            std::size_t sprites = 0;
            const int points = value.model == 0xc1c ? 10 : value.model == 0x225 ? 7 : 0;
            for (int point = 0; point < points; ++point)
            {
                if (expectedRandom.Next() % 3 == 0)
                {
                    static_cast<void>(expectedRandom.Next());
                    ++sprites;
                }
                ++sprites;
            }
            if (value.model == 0x225)
                static_cast<void>(expectedRandom.Next());
            for (std::size_t call = 0; call < 3; ++call)
                static_cast<void>(expectedRandom.Next());
            EXPECT_EQ(m_clothWorld.random.State(), expectedRandom.State());
            EXPECT_EQ(LiveSprites().size() - before, sprites);
            TearDown();
        }
    }
}

// Q-C1119: Staff11 (0xc1c) and Sword20 (0x225) have a single action, and the statue hangs them with action 1: 0x4ec200
// reads it past the array and MuRet cuts, after the close of +0x38e. Bow19 (0xa24) does not advance (a weapon from
// 0xa12 to 0xc11 with the flag) and 0x4eb860 takes the action to 0: it is drawn.
TEST_F(MonsterDrawingTest, TheStatueStaffAndSwordReadAnActionTheirModelLacks)
{
    Monster(6, Spider, 0x84);
    const auto staff = Draw(Pass());
    ASSERT_FALSE(staff.HasValue());
    EXPECT_NE(staff.Error().find("0x4ec200: el modelo 0xc1c no tiene la acción 1"), std::string::npos) << staff.Error();
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(1));

    m_world = std::make_unique<Client::Kernel::WorldState>();
    SetUp();
    m_clothWorld.random = Client::Crt::CrtRandom{1};
    Monster(6, Spider, 0x86);
    const auto bow = Draw(Pass());
    ASSERT_TRUE(bow.HasValue()) << bow.Error();
    EXPECT_GE(m_batch.Vertices().size(), 9u);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(5));
}

// Between the original's frames the 0x2a hangs the same on the copy: it does not store the wings slot or draw rand();
// with fewer bones than the 0xf0's 0x3d, it cuts at 0x509340.
TEST_F(MonsterDrawingTest, The0x2aBetweenOriginalFramesStoresNothing)
{
    m_models.SetBoneCount(64);
    m_models.SetActionCount(2);
    ActorState& carrier = Monster(4, Spider, 0x2a);
    GameCharacterPass pass = Pass();
    pass.originalFrames = 0;

    const auto result = Draw(pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(carrier.wings.model, 0xffff);
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
    m_models.SetBoneCount(48);
    const auto shortened = Draw(Pass());
    ASSERT_FALSE(shortened.HasValue());
    EXPECT_NE(shortened.Error().find("0x509340: el hueso 0x3d"), std::string::npos) << shortened.Error();
}

// 0x50bf98: the +0x376 0x45 draws its rand() % 30, sets nine flare01 of 0.6 with its light and, before each of its
// three 0x5c6/3 particles (life 5, no rand(), 0x540675), four rand().
TEST_F(MonsterDrawingTest, The0x45LightsItsNineFlaresAndItsThreeSparks)
{
    // 0x51483f: monster 69 (+0x376 0x45) with model 0x184, which has no case in 0x6002e0.
    constexpr std::uint16_t ModelOf69 = 0x184;
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world = std::make_unique<Client::Kernel::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        m_sprites.Clear();
        m_world->Runtime().map = map;
        Monster(3, ModelOf69, 0x45);
        auto particles = std::make_unique<Client::Particles::Particles>();
        GameCharacterPass pass = Pass();
        pass.particles = particles.get();

        const auto result = Draw(pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        const auto sparks = std::ranges::count_if(
            particles->All(), [](const auto& particle)
            { return particle.alive && particle.type == 0x5c6 && particle.subtype == 3 && particle.life == 5; });
        EXPECT_EQ(sparks, 3);
        // The body and the shadow first.
        EXPECT_EQ(m_batch.Vertices().size(), 6u);
        Client::Crt::CrtRandom random{1};
        const double x =
            static_cast<double>(random.Next() % 30 + 70) * static_cast<double>(std::bit_cast<float>(0x3c23d70au));
        const auto sprites = LiveSprites();
        ASSERT_EQ(sprites.size(), 9u);
        EXPECT_EQ(sprites[0].texture, 0x5ac);
        EXPECT_FLOAT_EQ(sprites[0].scale, 0.6f);
        EXPECT_EQ(sprites[0].light.x, static_cast<float>(x * static_cast<double>(std::bit_cast<float>(0x3f4ccccdu))));
        EXPECT_EQ(sprites[0].light.y, static_cast<float>(x * static_cast<double>(std::bit_cast<float>(0x3f666666u))));
        EXPECT_EQ(sprites[0].light.z, static_cast<float>(x));
        // Bone 10 at (0, 0, 10), plus the position.
        EXPECT_EQ(sprites[0].position, glm::vec3(150.0f, 150.0f, 20.0f));
        EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(1 + 3 * 4 + TailDraws));
        TearDown();
    }
    // Between the original's frames, the nine flare01 without the particles, on a copy of the rand().
    m_world = std::make_unique<Client::Kernel::WorldState>();
    SetUp();
    m_clothWorld.random = Client::Crt::CrtRandom{1};
    m_sprites.Clear();
    Monster(3, ModelOf69, 0x45);
    auto particles = std::make_unique<Client::Particles::Particles>();
    GameCharacterPass between = Pass();
    between.particles = particles.get();
    between.originalFrames = 0;
    const auto drawn = Draw(between);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(LiveSprites().size(), 9u);
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}

// 0x50c3ed..0x50c50e: the +0x376 0x49 sets fourteen flare01 of 0.8 with (0.1, 0.1, 1) on bones 13 to 26 and seven on
// 52 to 58, and links bones 13 to 16 and 22 to 23 with four 0x614/7 joints of width 20 without rand(); its 0x44 pass
// and then it cuts at 0x60b300 (0x50c595). 0x50c37e..0x50c3e8: the 0x4b drops the 0x5d9/0 of 0.3 on its bone 18 and
// goes on.
TEST_F(MonsterDrawingTest, The0x49ChainsItsBonesAndThe0x4bSparksItsEighteenth)
{
    Monster(0, Spider, 0x49);
    const auto chained = Draw(Pass());
    ASSERT_FALSE(chained.HasValue());
    EXPECT_NE(chained.Error().find("0x50c595"), std::string::npos) << chained.Error();
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 21u);
    EXPECT_EQ(sprites[0].texture, 0x5ac);
    EXPECT_FLOAT_EQ(sprites[0].scale, 0.8f);
    EXPECT_EQ(sprites[0].light, glm::vec3(std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu), 1.0f));
    EXPECT_EQ(sprites[0].position, glm::vec3(150.0f, 150.0f, 23.0f));
    EXPECT_EQ(sprites[13].position, glm::vec3(150.0f, 150.0f, 36.0f));
    // Bones 52 to 58 are outside the test pose: the zero matrix, the position.
    EXPECT_EQ(sprites[14].position, glm::vec3(150.0f, 150.0f, 10.0f));
    std::vector<const Client::Kernel::JointState*> links;
    for (const auto& joint : m_joints)
    {
        if (joint.alive)
            links.push_back(&joint);
    }
    ASSERT_EQ(links.size(), 4u);
    for (const auto* link : links)
    {
        EXPECT_EQ(link->type, 0x614);
        EXPECT_EQ(link->subtype, 7);
        EXPECT_EQ(link->width, 20.0f);
        EXPECT_EQ(link->ownerContext, nullptr);
    }
    EXPECT_EQ(links[0]->position, glm::vec3(150.0f, 150.0f, 23.0f));
    EXPECT_EQ(links[0]->targetPosition, glm::vec3(150.0f, 150.0f, 24.0f));
    EXPECT_EQ(links[3]->position, glm::vec3(150.0f, 150.0f, 32.0f));
    EXPECT_EQ(links[3]->targetPosition, glm::vec3(150.0f, 150.0f, 33.0f));
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(0));

    m_world = std::make_unique<Client::Kernel::WorldState>();
    TearDown();
    SetUp();
    m_sprites.Clear();
    Monster(0, Spider, 0x4b);
    auto particles = std::make_unique<Client::Particles::Particles>();
    GameCharacterPass pass = Pass();
    pass.particles = particles.get();
    const auto sparked = Draw(pass);
    ASSERT_TRUE(sparked.HasValue()) << sparked.Error();
    std::vector<const Client::Particles::Particle*> fires;
    for (const auto& particle : particles->All())
    {
        if (particle.alive && particle.type == Client::Particles::FireParticle)
            fires.push_back(&particle);
    }
    ASSERT_EQ(fires.size(), 1u);
    // The 0x5d9/0's creation (0x541197) overwrites the 0.3 scale with its own.
    EXPECT_EQ(fires[0]->position, glm::vec3(150.0f, 150.0f, 28.0f));
    EXPECT_EQ(fires[0]->light, glm::vec3(1.0f));
    EXPECT_TRUE(LiveSprites().empty());
}

// 0x50c0fb: the +0x376 0x46 sets one white flare01 of 0.8 on its bone 20, no rand(), and goes on.
TEST_F(MonsterDrawingTest, The0x46LightsOneFlareOnItsBone20)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world = std::make_unique<Client::Kernel::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        m_sprites.Clear();
        m_world->Runtime().map = map;
        Monster(3, Spider, 0x46);

        const auto result = Draw(Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_batch.Vertices().size(), 6u);
        const auto sprites = LiveSprites();
        ASSERT_EQ(sprites.size(), 1u);
        EXPECT_EQ(sprites[0].texture, 0x5ac);
        EXPECT_EQ(sprites[0].scale, std::bit_cast<float>(0x3f4ccccdu));
        EXPECT_EQ(sprites[0].light, glm::vec3(1.0f));
        EXPECT_EQ(sprites[0].position, glm::vec3(150.0f, 150.0f, 30.0f));
        EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
        TearDown();
    }
}

// 0x510abd: the +0x430 raven goes with any type (0x420df0), with the draw's trapezoid.
TEST_F(MonsterDrawingTest, AMonsterWithARavenDrawsItAfterItsTail)
{
    Monster(3, Spider, 3).darkSpirit.emplace();
    const auto without = Draw(Pass());
    ASSERT_FALSE(without.HasValue());
    EXPECT_NE(without.Error().find("0x418320"), std::string::npos) << without.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));

    m_world = std::make_unique<Client::Kernel::WorldState>();
    SetUp();
    m_clothWorld.random = Client::Crt::CrtRandom{1};
    m_terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Monster(3, Spider, 3).darkSpirit.emplace();
    const Client::Terrain::TerrainPolygon everything{.everything = true};
    GameCharacterPass pass = Pass();
    pass.polygon = &everything;
    const auto drawn = Draw(pass);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_GT(m_batch.Vertices().size(), 6u);
}

// 0x5108b6..0x510a1c, the 0x16e: for each non-dummy bone outside 15..20 and 27..32 (36 of the 48), the 0x5fd of 2.5
// with blend 1 (+0x1d6 at 0) or the green 0x61e of 0.8, and one rand() & 3; with 0 in actions 3 and 4, particle 0x5ca
// with the same light. Then it skips the helper chain.
TEST_F(MonsterDrawingTest, TheBoneSparksOf0x16eGoOverEachBoneOutsideTheTwoGaps)
{
    ActorState& sparks = Monster(0, 0x16e, 0x22);

    const auto plain = Draw(Pass());

    ASSERT_TRUE(plain.HasValue()) << plain.Error();
    auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 36u);
    for (const auto& sprite : sprites)
    {
        EXPECT_EQ(sprite.texture, 0x5fd);
        EXPECT_FLOAT_EQ(sprite.scale, 2.5f);
        EXPECT_EQ(sprite.blend, 1);
        EXPECT_EQ(sprite.light, glm::vec3(1.0f));
    }
    // Bones 14 and 21 sit on both sides of the first gap, and 26 and 33 on both sides of the second.
    EXPECT_EQ(sprites[14].position, glm::vec3(150.0f, 150.0f, 24.0f));
    EXPECT_EQ(sprites[15].position, glm::vec3(150.0f, 150.0f, 31.0f));
    EXPECT_EQ(sprites[20].position, glm::vec3(150.0f, 150.0f, 36.0f));
    EXPECT_EQ(sprites[21].position, glm::vec3(150.0f, 150.0f, 43.0f));
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3 + 36));

    // With a level, the green 0x61e; action 3 with a rand() & 3 at 0 requests particle 0x5ca, which needs the pool.
    for (auto& sprite : m_sprites.Sprites())
        sprite.alive = false;
    m_batch.Begin(Client::Render::Pass3D{});
    sparks.level = 1;
    sparks.animation.action = 3;
    m_clothWorld.random = Client::Crt::CrtRandom{1};

    const auto missing = Draw(Pass());

    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x5109f2"), std::string::npos) << missing.Error();
    sprites = LiveSprites();
    ASSERT_FALSE(sprites.empty());
    EXPECT_EQ(sprites[0].texture, 0x61e);
    EXPECT_FLOAT_EQ(sprites[0].scale, 0.8f);
    EXPECT_EQ(sprites[0].blend, 0);
    EXPECT_EQ(sprites[0].light, glm::vec3(0.2f, 0.7f, 0.1f));
}

TEST_F(MonsterDrawingTest, BetweenOriginalFramesNothingIsStored)
{
    m_world->Runtime().map = 20;
    m_world->Actors()[42].byte1a1 = 1;
    ActorState& spider = Monster(2, Spider, 3);
    m_faunaModels.RecordAnimation(Spider, 6.25f, 7);
    GameCharacterPass pass = Pass();
    pass.originalFrames = 0;

    const auto result = Draw(pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_faunaModels.PoseKey(Spider), 7);
    EXPECT_EQ(m_faunaModels.PoseFrame(Spider), 6.25f);
    EXPECT_FLOAT_EQ(spider.alpha, 1.0f);
    EXPECT_TRUE(spider.bodyPose.empty());
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}

// 0x43f182..0x43f269: Kundun falling (+0x60 from 100, action 6 from frame 8) drops its 0x56 turns of the 0x613/3 and
// the 0x6ee/3 into the real joint table (0x74a8d78) and shakes once per original frame (ADR 0005). Between the
// original's frames it draws the projected copy's body, but touches no joint table, neither the real one nor the
// projected one, and draws no rand(): the CRT stays as it was (those of the draw's tail, 0x50db57.., also go on the
// copy). In the original's frame it does draw them.
TEST_F(MonsterDrawingTest, KundunFallingBetweenOriginalFramesLeavesTheJointTablesAlone)
{
    constexpr std::uint16_t Kundun = 0x192;
    constexpr std::size_t Rays = 0x56;
    const auto alive = [](const Client::Kernel::JointState& joint) { return joint.alive; };
    m_models.SetActionCount(7);
    ActorState& kundun = Monster(4, Kundun, 0x113);
    kundun.animation = {.action = 6, .frame = 8.0f};
    kundun.dword60 = 100;
    float shake = 0.0f;
    GameCharacterPass pass = Pass();
    pass.cameraShake = &shake;
    pass.originalFrames = 0;
    const Client::Crt::CrtRandom before = m_clothWorld.random;

    const auto between = Draw(pass);

    ASSERT_TRUE(between.HasValue()) << between.Error();
    EXPECT_FALSE(m_batch.Vertices().empty());
    EXPECT_TRUE(std::ranges::none_of(m_world->DrawTail().joints, alive));
    EXPECT_TRUE(std::ranges::none_of(m_joints, alive));
    EXPECT_EQ(kundun.dword60, 100);
    EXPECT_EQ(shake, 0.0f);
    EXPECT_EQ(m_clothWorld.random.State(), before.State());

    pass.originalFrames = 1;
    m_batch.Begin(Client::Render::Pass3D{});
    const auto original = Draw(pass);

    ASSERT_TRUE(original.HasValue()) << original.Error();
    EXPECT_EQ(static_cast<std::size_t>(std::ranges::count_if(m_world->DrawTail().joints, alive)), 2 * Rays);
    EXPECT_TRUE(std::ranges::none_of(m_joints, alive));
    EXPECT_EQ(kundun.dword60, 0x5a);
    EXPECT_NE(shake, 0.0f);
    // The 0x56 of 0x43f1b4 (one per turn), the shake's (0x43f249) and the tail's: one of 0x50db57 and two of 0x50e68a.
    constexpr int FallingDraws = 0x56 + 1 + 1 + 2;
    EXPECT_EQ(m_clothWorld.random.State(), Client::Tests::AfterDraws(before.State(), FallingDraws).State());
}

// End to end: the 1B with 0xc9 (0x4cbfc9) goes through 0x439d40, which on the 0x197 with 0x100 clears it and sets
// +0x110 to 0x21 (with 0xca and 0x8, 0x22); the 0x197's next draw (0x43fb99) calls 0x439d90, which drops on the corners
// of mesh 6 the stones of 0x4ee770(6, 0x10a, 1 or 2): first the 0x10b/2 or /3, requests sound 0x88 and sets +0x110
// back to 0.
TEST_F(MonsterDrawingTest, TheMagicCancelLeavesThe197sStonesOnItsNextDraw)
{
    constexpr std::uint16_t Glow = 0x197;
    constexpr std::uint16_t GlowKey = 0x1234;
    for (int mesh = 1; mesh <= 6; ++mesh)
        m_models.AddMesh();
    auto selection = std::make_unique<Client::FrontEnd::SelectionState>(*m_world);
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    const Client::Scenes::WorldEntryResources resources{.world = *m_world,
                                                        .selection = *selection,
                                                        .dataRoot = {},
                                                        .cloth = m_clothWorld,
                                                        .camera = camera,
                                                        .cameraInput = input,
                                                        .models = m_models,
                                                        .log = log};
    for (const auto& [skill, flag, strike, stones] : {std::tuple{std::uint8_t{0xc9}, 0x100u, std::uint8_t{0x21}, 2},
                                                      std::tuple{std::uint8_t{0xca}, 0x8u, std::uint8_t{0x22}, 3}})
    {
        SCOPED_TRACE(int{skill});
        ActorState& glow = Monster(5, Glow, 0x11e);
        glow.key = GlowKey;
        glow.flags78 = flag;
        glow.byte110 = 0;
        m_world->Map().effects.Clear();
        m_world->Runtime().lastSoundId.reset();

        ASSERT_TRUE(Client::Skills::ApplyMagicCancel(Client::Protocol::Skills::MagicCancel{skill, GlowKey}, resources)
                        .HasValue());

        EXPECT_EQ(glow.byte110, strike);
        EXPECT_EQ(glow.flags78, 0u);
        auto particles = std::make_unique<Client::Particles::Particles>();
        GameCharacterPass pass = Pass();
        pass.effects = &m_world->Map().effects;
        pass.particles = particles.get();
        m_batch.Begin(Client::Render::Pass3D{});
        const auto drawn = Draw(pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(glow.byte110, 0);
        EXPECT_EQ(m_world->Runtime().lastSoundId, Client::Audio::SoundId{0x88});
        const auto& first = m_world->Map().effects.Effects()[0];
        EXPECT_TRUE(first.alive);
        EXPECT_EQ(first.type, 0x10b);
        EXPECT_EQ(first.subtype, stones);
    }
}

// 0x5ffe9d..0x5ffeef: 0x18f adds 60 and 0x18e 120 to y before copying scale and origin to the shared model; between the
// original's frames it does not publish that transform.
TEST_F(MonsterDrawingTest, TheBodyPublishesItsSharedModelPlacementOnOriginalFrames)
{
    ActorState& first = Monster(1, Spider, 3);
    first.position = {1000.0f, 2000.0f, 30.0f};
    first.scale = 0.5f;
    ActorState& last = Monster(2, Spider, 3);
    last.position = {9000.0f, 11000.0f, 60.0f};
    last.scale = 2.0f;
    last.alpha = 0.01f;
    ActorState& faint = Monster(3, Spider, 3);
    faint.position = {13000.0f, 15000.0f, 90.0f};
    faint.scale = 3.0f;
    faint.alpha = 0.009f;
    ActorState& otherType = Monster(4, Yeti, 19);
    otherType.position = {3000.0f, 4000.0f, 50.0f};
    otherType.scale = 1.5f;

    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto spiderPlacement = m_world->ModelPlacement(Spider);
    EXPECT_EQ(spiderPlacement.origin, last.position);
    EXPECT_FLOAT_EQ(spiderPlacement.scale, last.scale);
    const auto yetiPlacement = m_world->ModelPlacement(Yeti);
    EXPECT_EQ(yetiPlacement.origin, otherType.position);
    EXPECT_FLOAT_EQ(yetiPlacement.scale, otherType.scale);

    struct OffsetCase
    {
        std::uint16_t type;
        std::uint16_t monster;
        glm::vec3 position;
        float scale;
        float yOffset;
    };
    const std::array offsetCases{OffsetCase{0x18e, 0x21, {5000.0f, 6000.0f, 70.0f}, 1.25f, 120.0f},
                                 OffsetCase{0x18f, 3, {7000.0f, 8000.0f, 80.0f}, 0.75f, 60.0f}};
    for (const OffsetCase& value : offsetCases)
    {
        m_world = std::make_unique<Client::Kernel::WorldState>();
        SetUp();
        ActorState& actor = Monster(5, value.type, value.monster);
        actor.position = value.position;
        actor.scale = value.scale;

        const auto offsetResult = Draw(Pass());

        if (!offsetResult.HasValue())
        {
            EXPECT_NE(offsetResult.Error().find("0x4ee770"), std::string::npos) << offsetResult.Error();
        }
        const auto placement = m_world->ModelPlacement(value.type);
        EXPECT_EQ(placement.origin, value.position + glm::vec3(0.0f, value.yOffset, 0.0f));
        EXPECT_FLOAT_EQ(placement.scale, value.scale);
        TearDown();
    }

    m_world = std::make_unique<Client::Kernel::WorldState>();
    SetUp();
    ActorState& projected = Monster(7, Spider, 3);
    projected.position = {17000.0f, 18000.0f, 100.0f};
    projected.scale = 4.0f;
    GameCharacterPass betweenFrames = Pass();
    betweenFrames.originalFrames = 0;
    const auto projectedResult = Draw(betweenFrames);

    ASSERT_TRUE(projectedResult.HasValue()) << projectedResult.Error();
    const auto projectedPlacement = m_world->ModelPlacement(Spider);
    EXPECT_EQ(projectedPlacement.origin, glm::vec3{});
    EXPECT_FLOAT_EQ(projectedPlacement.scale, 0.0f);
}

TEST_F(MonsterDrawingTest, InChaosCastleTheHeroFlagFadesTheStoredAlpha)
{
    m_world->Runtime().map = 20;
    m_world->Actors()[42].byte1a1 = 1;
    ActorState& spider = Monster(2, Spider, 3);

    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(spider.alpha, 0.85f);
}

// The parts of the creation's NPCs (0x5141e0): the monster library with its own triangle for each part model, shifted
// in x by the model number minus 0x1d0, to see the order; `missing` is not loaded.
class PartModels final : public Client::Models::IModelLibrary
{
public:
    explicit PartModels(const Client::Models::IModelLibrary& body) : m_body(body)
    {
        for (std::uint16_t model = 0x1da; model <= 0x1f0; ++model)
        {
            Client::Data::Models::Model part = *body.Find(Client::Models::ModelId{Spider});
            for (auto& vertex : part.meshes.front().vertices)
                vertex.position[0] += static_cast<float>(model - 0x1d0);
            m_parts.push_back(part);
        }
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId model) const override
    {
        if (model.Value() == missing)
            return nullptr;
        if (model.Value() >= 0x1da && model.Value() <= 0x1f0)
            return &m_parts[static_cast<std::size_t>(model.Value() - 0x1da)];
        return m_body.Find(model);
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return m_body.SkeletonOf(model);
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot>
    MeshTextures(Client::Models::ModelId model) const override
    {
        return m_body.MeshTextures(model);
    }

    std::uint16_t missing = 0;

private:
    const Client::Models::IModelLibrary& m_body;
    std::vector<Client::Data::Models::Model> m_parts;
};

// A creation NPC with its parts: the type, its model and the models of +0x218, +0x238, +0x258, +0x278 and +0x298
// (MonsterKindTable.cpp, 0x5162dd..0x5162f8 and the cases of 0x5141e0).
struct DressedNpc
{
    std::int32_t type;
    std::uint16_t model;
    std::array<std::uint16_t, 5> parts;
};

constexpr std::array<DressedNpc, 5> DressedNpcs{{
    {253, 0x1cd, {0x1ec, 0x1ee, 0x1f0, 0xffff, 0xffff}},
    {255, 0x1cb, {0x1db, 0x1dd, 0x1df, 0xffff, 0x1e3}},
    {230, 0x1cc, {0x1e4, 0x1e7, 0xffff, 0x1e9, 0x1ea}},
    {248, 0x1cc, {0x1e5, 0x1e7, 0xffff, 0x1e9, 0x1eb}},
    {250, 0x1cc, {0x1e4, 0x1e6, 0xffff, 0x1e8, 0x1ea}},
}};

class MonsterPartsTest : public MonsterDrawingTest
{
protected:
    void SetUp() override
    {
        MonsterDrawingTest::SetUp();
        m_faunaModels.Bind(&m_parts);
    }

    ActorState& Npc(const DressedNpc& npc)
    {
        ActorState& actor = Monster(6, npc.model, 0xffff);
        actor.byte88 = 4;
        for (std::size_t index = 0; index < npc.parts.size(); ++index)
            actor.body[index].model = npc.parts[index];
        return actor;
    }

    // The x of the first vertex of part `model` with the NPC at 150 and scale 1.
    static float PartX(std::uint16_t model)
    {
        return 150.0f + static_cast<float>(model - 0x1d0);
    }

    PartModels m_parts{m_models};
};

// 0x50d3dc..0x50d4f5: without +0x3e3, after the shadow and the light of 0x50cc55, parts 5 to 0 (the boots, gloves,
// pants, armour, helm and head), each with 0x60cf70: 0x60b3d0 with the light of +0x3b4 and level 0, no shine or rand().
// The potion girl (253) and the barmaid (255) of Lorencia, and NPCs 230, 248 and 250 in Lorencia, Devias and Noria: the
// draw does not check the map outside Chaos Castle.
TEST_F(MonsterPartsTest, TheTownNpcsDrawTheirPartsFromTheBootsToTheHelm)
{
    for (const DressedNpc& npc : DressedNpcs)
    {
        for (const std::int32_t map : {0, 2, 3})
        {
            if (npc.type >= 253 && map != 0)
                continue;
            SCOPED_TRACE(std::to_string(npc.type) + " en el mapa " + std::to_string(map));
            m_world->Runtime().map = map;
            m_batch.Begin(Client::Render::Pass3D{});
            m_clothWorld.random = Client::Crt::CrtRandom{1};
            Npc(npc);

            const auto result = Draw(Pass());

            ASSERT_TRUE(result.HasValue()) << result.Error();
            std::vector<std::uint16_t> drawn;
            for (std::size_t index = npc.parts.size(); index-- > 0;)
                if (npc.parts[index] != 0xffff)
                    drawn.push_back(npc.parts[index]);
            // The body, the shadow and one triangle per part.
            ASSERT_EQ(m_batch.Vertices().size(), 6u + 3u * drawn.size());
            for (std::size_t index = 0; index < drawn.size(); ++index)
            {
                const auto& vertex = m_batch.Vertices()[6 + 3 * index];
                EXPECT_FLOAT_EQ(vertex.position.x, PartX(drawn[index])) << index;
                // The light of +0x3b4 (the terrain plus its own), times the level 0 factor.
                ExpectColor(vertex, 0.3f, 0.6f, 0.9f, 1.0f);
            }
            // The one of 0x50db57 and the two of +0x38e in the hands loop: the parts draw no rand().
            EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
        }
    }
}

// 0x60cf70 with mode 1 (the NPC under the mouse): before each part, the two passes of 0x60cd60 with the NPC's colors
// (+0x88 at 4: 0x60d069 and 0x60d0dd), the scale of +0x94 and 0.02 less; with +0x78 & 0x4000, no outline (0x60cd74).
TEST_F(MonsterPartsTest, AHoveredNpcOutlinesEachPartBeforeIt)
{
    ActorState& npc = Npc(DressedNpcs[0]);
    GameCharacterPass pass = Pass();
    pass.hovered = 6;

    const auto result = Draw(pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // The body with its two outlines, the shadow and, for each of the three parts, two outlines and the body.
    ASSERT_EQ(m_batch.Vertices().size(), 12u + 3u * 9u);
    const auto& outer = m_batch.Vertices()[12];
    ExpectColor(outer, 0.02f, 0.1f, 0.0f, 1.0f);
    ExpectColor(m_batch.Vertices()[15], 0.16f, 0.7f, 0.0f, 1.0f);
    // Vertex (1, 0, 0) of the boot (0x1f0) at 1.2 and at 1.18 around bone 0.
    EXPECT_FLOAT_EQ(m_batch.Vertices()[13].position.x, 150.0f + 1.2f * (1.0f + PartX(0x1f0) - 150.0f));
    EXPECT_FLOAT_EQ(m_batch.Vertices()[16].position.x, 150.0f + 1.18f * (1.0f + PartX(0x1f0) - 150.0f));
    EXPECT_FLOAT_EQ(m_batch.Vertices()[18].position.x, PartX(0x1f0));

    m_batch.Begin(Client::Render::Pass3D{});
    npc.flags78 |= 0x4000;
    ASSERT_TRUE(Draw(pass).HasValue());
    // No outlines (not even the body's) and no shadow (0x50be6e): the body and the three parts.
    EXPECT_EQ(m_batch.Vertices().size(), 3u + 3u * 3u);
}

// The head (+0x1f8) goes last. A model that was not loaded is a zeroed record with no meshes: it draws nothing. One
// outside the 0x449d records that 0x60dbe0 zeroes (the head left by the 34-letter name of the 231, P-C352) would make
// the original write past them: it cuts after the parts it already drew.
TEST_F(MonsterPartsTest, TheHeadGoesLastAndAModelOutsideTheTableCuts)
{
    ActorState& npc = Npc(DressedNpcs[0]);
    npc.head = 0x1da;
    m_parts.missing = 0x1ee;

    auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 6u + 3u * 3u);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[6].position.x, PartX(0x1f0));
    EXPECT_FLOAT_EQ(m_batch.Vertices()[9].position.x, PartX(0x1ec));
    EXPECT_FLOAT_EQ(m_batch.Vertices()[12].position.x, PartX(0x1da));

    m_batch.Begin(Client::Render::Pass3D{});
    npc.head = 0x6e61;
    result = Draw(Pass());
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x50d431"), std::string::npos) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u + 3u * 2u);
}

// 0x50c972..0x50cc29: the +0x1f2 WORD overwritten by a long name (P-C352) does nothing without the guild war or the
// duel
// ([0x576796d], [0x576796f] and [0x78abad8] at 0): the NPC is drawn with its parts, no extra rand().
TEST_F(MonsterPartsTest, ALongNameOverTheGuildWordDrawsAsUsual)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        m_batch.Begin(Client::Render::Pass3D{});
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        ActorState& npc = Npc(DressedNpcs[1]);
        npc.word1f2 = 0x6e61;

        const auto result = Draw(Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_batch.Vertices().size(), 6u + 3u * 4u);
        EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
    }
}

TEST(MonsterHangingLight, BloodCastleKeepersRedAndOrdinaryLight)
{
    ActorState actor;
    actor.light = {0.1f, 0.2f, 0.3f};
    actor.pk = 3;
    const glm::vec3 terrain{0.2f, 0.4f, 0.6f};
    actor.word376 = 0x57;
    // Map 15: (15 − 9) / 3 = 2, times 0.5.
    EXPECT_EQ(Client::Actors::Drawing::HangingLightOf(actor, terrain, 15), glm::vec3(1.0f, 0.1f, 0.1f));
    // Map 10 (Blood Castle 1): the quotient gives 0 and the terrain's remains.
    EXPECT_EQ(Client::Actors::Drawing::HangingLightOf(actor, terrain, 10), terrain + actor.light);
    // Lorencia and Devias: the terrain's plus its own.
    EXPECT_EQ(Client::Actors::Drawing::HangingLightOf(actor, terrain, 0), terrain + actor.light);
    EXPECT_EQ(Client::Actors::Drawing::HangingLightOf(actor, terrain, 2), terrain + actor.light);
    actor.word376 = 0x5a;
    EXPECT_EQ(Client::Actors::Drawing::HangingLightOf(actor, terrain, 15), terrain + actor.light);
    actor.pk = 6;
    EXPECT_EQ(Client::Actors::Drawing::HangingLightOf(actor, terrain, 0), glm::vec3(1.0f, 0.1f, 0.1f));
}

TEST(MonsterDecorations, EachListedMonsterCutsWithItsBranch)
{
    EXPECT_TRUE(Client::Actors::Drawing::CheckMonsterDecorations(3).HasValue());
    EXPECT_TRUE(Client::Actors::Drawing::CheckMonsterDecorations(0xffff).HasValue());
    EXPECT_NE(Client::Actors::Drawing::CheckMonsterDecorations(0x45).Error().find("0x50bf98"), std::string::npos);
    EXPECT_NE(Client::Actors::Drawing::CheckMonsterDecorations(0x50).Error().find("0x50c671"), std::string::npos);
    EXPECT_NE(Client::Actors::Drawing::CheckMonsterDecorations(0x85).Error().find("0x50c83b"), std::string::npos);
    // The 0x20a path also cuts with the shine passes, which DrawGameMonster draws.
    constexpr std::array<std::uint16_t, 10> Shining{0x26, 0x2b, 0x34, 0x3b, 0x43, 0x4e, 0x53, 0x35, 0x36, 0x87};
    for (const std::uint16_t shining : Shining)
        EXPECT_TRUE(Client::Monsters::Drawing::MonsterShines(shining)) << shining;
    constexpr std::array<std::uint16_t, 7> Others{0x45, 0x46, 0x4d, 0x54, 0x2a, 0x86, 0x88};
    for (const std::uint16_t other : Others)
        EXPECT_FALSE(Client::Monsters::Drawing::MonsterShines(other)) << other;
    EXPECT_TRUE(Client::Actors::Drawing::WearsClothOnTheMesh(0x8f));
    EXPECT_FALSE(Client::Actors::Drawing::WearsClothOnTheMesh(0x58));
}
} // namespace

// 0x60b573: with the options' glow detail (0x6c9dcc) at 1, each level 13 part draws as a level 7 one: its body and
// the 0x44 pass.
TEST_F(MonsterPartsTest, TheGlowDetailOfTheOptionsTrimsTheLevelOfEachPart)
{
    for (const std::uint8_t detail : {std::uint8_t{4}, std::uint8_t{1}})
    {
        SCOPED_TRACE(static_cast<int>(detail));
        m_batch.Begin(Client::Render::Pass3D{});
        ActorState& npc = Npc(DressedNpcs[0]);
        for (auto& part : npc.body)
            part.level = 13;
        Client::Actors::Drawing::ItemDrawContext items;
        items.glowDetail = detail;
        GameCharacterPass pass = Pass();
        pass.itemContext = &items;

        const auto result = Draw(pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        // The body, the shadow and three parts.
        EXPECT_EQ(m_batch.Vertices().size(), 6u + 3u * (detail == 4 ? 12u : 6u));
    }
}
