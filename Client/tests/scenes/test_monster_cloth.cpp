#include "Models/IModelLibrary.h"
#include "Actors/Drawing/MonsterCloth.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Render/ModelBatch.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Models/ModelId.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Kernel/PoseBuffer.h"
#include "Assets/FaunaModels.h"
#include "Actors/Drawing/GameCharacters.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"
#include "Effects/ActorEffectHooks.h"
#include "Session/CharacterDrawHooks.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The cloth on the monsters' mesh (0x50bae7..0x50bbca) in the game draw: literal vectors from the ASM c32bbd5f,
// without running the original. The scene has to do the same as the render class (test_mesh_cloth) with mesh 2, bone
// 0x12, flags 0x400, the two spheres and five steps of 0.005f.
namespace
{
using Client::Actors::Drawing::GameCharacterPass;
using Client::Kernel::ActorState;
using Client::Models::Cloth;
using Client::Models::ClothBody;
using Client::Models::ClothWindSource;
using Client::Models::ClothWorld;

constexpr std::uint16_t Spider = 0x15b;        // a model with no cases in 0x6002e0
constexpr std::uint16_t MagicSkeleton = 0x190; // Monster63.bmd, the one of 89, 95, 112, 118, 124, 130 and 143
constexpr std::uint16_t ClothMonster = 0x59;
constexpr std::size_t Slot = 5;
constexpr std::size_t FrameRandoms = 20; // five steps of 0x4565f0, with the 4 of the counter's guard
constexpr std::size_t TailRandoms = 3;   // 0x50db57 and the two of the hands (0x50e288), like the monster without cloth

std::uint32_t AfterRandoms(std::size_t count)
{
    Client::Crt::CrtRandom random{1};
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random.State();
}

Client::Data::Models::Mesh Triangle()
{
    Client::Data::Models::Mesh mesh;
    mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
    mesh.normals = {{0, {0, 0, 1}, 0}};
    mesh.texCoords = {{0, 0}};
    mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
    mesh.textureName = "monster.jpg";
    return mesh;
}

// Mesh 2: a 12 by 10 square 200 from bone 0x12 (outside the two spheres), vertices 1 and 2 on that bone and 0 and 3 on
// bone 0.
Client::Data::Models::Mesh Robe()
{
    Client::Data::Models::Mesh mesh = Triangle();
    mesh.vertices = {{0, {200, 0, 0}}, {0x12, {200, 0, -8}}, {0x12, {212, 0, -8}}, {0, {212, 0, 0}}};
    mesh.triangles = {{3, {1, 2, 3, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, {3, {1, 3, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
    mesh.textureName = "robe.jpg";
    return mesh;
}

class ClothModels final : public Client::Models::IModelLibrary
{
public:
    ClothModels()
    {
        using namespace Client::Math;
        using namespace Client::Models;
        SkeletonBone bone;
        bone.parent = -1;
        bone.actions = {{BoneKey{}}};
        bone.actions[0][0].rotation = AngleQuaternion({0.0f, 0.0f, 0.0f});
        // Bone k at (0, 0, k): 0x12 at (0, 0, 18).
        m_skeleton.bones.assign(48, bone);
        for (std::size_t index = 0; index < m_skeleton.bones.size(); ++index)
            m_skeleton.bones[index].actions[0][0].position = {0.0f, 0.0f, static_cast<float>(index)};
        m_skeleton.actions = {ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
        m_model.meshes = {Triangle(), Triangle(), Robe()};
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
        return m_textures;
    }

private:
    Client::Models::Skeleton m_skeleton;
    Client::Data::Models::Model m_model;
    std::array<Client::Render::TextureSlot, 3> m_textures{
        Client::Render::TextureSlot{0x300}, Client::Render::TextureSlot{0x301}, Client::Render::TextureSlot{0x302}};
};

// The vertices of mesh 2 that 0x4ebc80 leaves with the monster at `position`, unrotated and with scale 1.
std::vector<glm::vec3> RobeAt(const glm::vec3& position)
{
    return {position + glm::vec3(200.0f, 0.0f, 0.0f), position + glm::vec3(200.0f, 0.0f, 10.0f),
            position + glm::vec3(212.0f, 0.0f, 10.0f), position + glm::vec3(212.0f, 0.0f, 0.0f)};
}

TEST(MonsterMeshClothTest, TheMeshRobeUsesTheMapWind)
{
    Client::Data::Models::Model model;
    model.meshes = {Triangle(), Triangle(), Robe()};
    const std::vector<Client::Math::BoneTransform> bones(19, Client::Math::BoneTransform{1.0f});
    const ClothBody body{.bones = bones};
    for (const std::uint8_t map : std::array<std::uint8_t, 3>{0x36, 0x37, 0x38})
    {
        SCOPED_TRACE(static_cast<unsigned>(map));
        Client::Actors::Drawing::MonsterMeshCloth cloth;
        std::array<Client::Models::SkinnedMesh, 3> meshes;
        meshes[2].positions = RobeAt({0.0f, 0.0f, 0.0f});
        ClothWorld world;
        world.windBase = 0.3f;

        ASSERT_TRUE(cloth.Frame(model, Spider, meshes, body, world, map).HasValue());

        ASSERT_TRUE(cloth.Cloth().has_value());
        const bool login = map == 0x37;
        const auto wind = std::bit_cast<std::uint32_t>(cloth.Cloth()->Wind());
        EXPECT_EQ(wind, login ? 0x3f51eb85u : std::bit_cast<std::uint32_t>(world.windBase));
        EXPECT_EQ(world.random.State(), AfterRandoms(login ? FrameRandoms + 5 : FrameRandoms));
    }
}

// What 0x6a0210, the +0x24 and the two 0x455cd0 build (0x50bb0b..0x50bb84).
Cloth BuiltRobe(std::span<const glm::vec3> vertices)
{
    Cloth cloth;
    cloth.BuildOnTheMesh(Robe(), 2, 0x12, 0x400, vertices);
    cloth.AddSphere({0.0f, 0.0f, 0.0f}, 50.0f, 0x12);
    cloth.AddSphere({0.0f, -20.0f, 0.0f}, 30.0f, 0x12);
    return cloth;
}

// 0x455190(0.005f, 5) (0x50bba0..0x50bba9).
bool StepTheRobe(Cloth& cloth, const ActorState& actor, std::span<const glm::vec3> vertices, ClothWorld& world,
                 std::uint8_t map)
{
    const ClothBody body{.bones = actor.bodyPose,
                         .angles = actor.angles,
                         .scale = actor.scale,
                         .position = actor.position,
                         .meshVertices = vertices};
    const auto fallbackWind = map == 0x37 ? ClothWindSource::Randomized : ClothWindSource::Base;
    return cloth.Simulate(std::bit_cast<float>(0x3ba3d70au), 5, body, world, fallbackWind);
}

class MonsterClothTest : public testing::Test
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
        m_cloths.resize(16);
        m_clothWorld.windBase = 0.3f;
        m_batch.Begin(Client::Render::Pass3D{});
    }

    void TearDown() override
    {
        m_faunaModels.Bind(nullptr);
    }

    // A monster with the cloth on the mesh as 0x511290 leaves it with what the creation writes.
    ActorState& Monster(std::uint16_t type)
    {
        ActorState& actor = m_world->Actors()[Slot];
        actor.alive = true;
        actor.seen = true;
        actor.type = type;
        actor.word376 = ClothMonster;
        actor.head = 0xffff;
        actor.word1f2 = 0xffff;
        actor.scale = 1.0f;
        actor.alpha = 1.0f;
        actor.byte88 = 2;
        actor.byte92 = 1;
        actor.float94 = 1.2f;
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
                .cloths = m_cloths,
                .sprites = &m_sprites,
                .poseCarry = &m_carry,
                .joints = m_joints,
                .terrain = &m_terrain,
                .faunaModels = &m_faunaModels,
                .hooks = &m_drawHooks,
                .effectHooks = &m_effectHooks};
    }

    Client::Core::Expected<void, std::string> Draw(const GameCharacterPass& pass)
    {
        m_batch.Begin(Client::Render::Pass3D{});
        return Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);
    }

    const std::optional<Cloth>& Worn()
    {
        return m_cloths[Slot].OnTheMesh().Cloth();
    }

    // The six vertices of mesh 2 in the body (triangles (1, 2, 3) and (1, 3, 0)), after meshes 0 and 1.
    void ExpectTheBodyDrawsTheParticles()
    {
        ASSERT_TRUE(Worn().has_value());
        const auto particles = Worn()->Particles();
        ASSERT_GE(m_batch.Vertices().size(), 12u);
        constexpr std::array<std::size_t, 6> Order{1, 2, 3, 1, 3, 0};
        for (std::size_t corner = 0; corner < Order.size(); ++corner)
            EXPECT_EQ(m_batch.Vertices()[6 + corner].position, particles[Order[corner]].position) << corner;
    }

    ClothModels m_models;
    Client::Assets::FaunaModels m_faunaModels;
    std::unique_ptr<Client::Kernel::WorldState> m_world;
    Client::Terrain::WorldTerrain m_terrain;
    Client::Session::CharacterDrawHooks m_drawHooks;
    Client::Effects::ActorEffectHooks m_effectHooks;
    std::unique_ptr<Client::Terrain::TerrainLighting> m_light;
    ClothWorld m_clothWorld;
    Client::Particles::FrameSprites m_sprites;
    Client::Kernel::PoseBuffer m_carry;
    std::vector<Client::Kernel::JointState> m_joints;
    std::vector<Client::Actors::Drawing::CharacterCloth> m_cloths;
    Client::Render::ModelBatch m_batch;
};

// Lorencia: the first frame builds it on mesh 2 of the current pose and simulates it five steps, with 20 rand() before
// the 3 of the monster without cloth; the body draws its particles.
TEST_F(MonsterClothTest, ALorenciaFrameBuildsTheClothOnMeshTwoAndStepsItFiveTimes)
{
    ActorState& monster = Monster(Spider);
    ClothWorld expectedWorld = m_clothWorld;

    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_TRUE(Worn().has_value());
    EXPECT_EQ(Worn()->Mesh(), 2);
    EXPECT_EQ(Worn()->Flags(), 0x400u);
    const std::vector<glm::vec3> vertices = RobeAt(monster.position);
    Cloth expected = BuiltRobe(vertices);
    ASSERT_TRUE(StepTheRobe(expected, monster, vertices, expectedWorld, 0));
    const auto particles = Worn()->Particles();
    ASSERT_EQ(particles.size(), 4u);
    for (std::size_t vertex = 0; vertex < particles.size(); ++vertex)
    {
        SCOPED_TRACE(vertex);
        EXPECT_EQ(particles[vertex].anchored, vertex == 1 || vertex == 2);
        EXPECT_EQ(particles[vertex].position, expected.Particles()[vertex].position);
        EXPECT_EQ(particles[vertex].velocity, expected.Particles()[vertex].velocity);
    }
    // The free ones fall; those on bone 0x12 stay where 0x4ebc80 left them.
    EXPECT_LT(particles[0].position.z, vertices[0].z);
    EXPECT_EQ(particles[1].position, vertices[1]);
    EXPECT_EQ(Worn()->Springs().size(), 7u);
    EXPECT_EQ(Worn()->Spheres().size(), 2u);
    ExpectTheBodyDrawsTheParticles();
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(FrameRandoms + TailRandoms));
    EXPECT_EQ(expectedWorld.random.State(), AfterRandoms(FrameRandoms));
}

TEST_F(MonsterClothTest, ADeviasClothPersistsAcrossFrames)
{
    m_world->Runtime().map = 2;
    ActorState& monster = Monster(Spider);
    ClothWorld expectedWorld = m_clothWorld;

    ASSERT_TRUE(Draw(Pass()).HasValue());
    const auto second = Draw(Pass());

    ASSERT_TRUE(second.HasValue()) << second.Error();
    const std::vector<glm::vec3> vertices = RobeAt(monster.position);
    Cloth expected = BuiltRobe(vertices);
    ASSERT_TRUE(StepTheRobe(expected, monster, vertices, expectedWorld, 2));
    for (std::size_t call = 0; call < TailRandoms; ++call)
        static_cast<void>(expectedWorld.random.Next());
    ASSERT_TRUE(StepTheRobe(expected, monster, vertices, expectedWorld, 2));
    for (std::size_t vertex = 0; vertex < 4; ++vertex)
        EXPECT_EQ(Worn()->Particles()[vertex].position, expected.Particles()[vertex].position) << vertex;
    ExpectTheBodyDrawsTheParticles();
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(2 * (FrameRandoms + TailRandoms)));
}

// Blood Castle (map 11), with the real model: the cloth is built and simulated, and then the 0x190 case in 0x6002e0
// (0x60115e) draws its RenderBody(2) and mesh 2 again, with the blendMesh at 2 and the light at 1, both with the
// particles.
TEST_F(MonsterClothTest, InBloodCastleTheMagicSkeletonStepsItsClothBeforeTheBodyCase)
{
    m_world->Runtime().map = 11;
    Monster(MagicSkeleton);

    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ExpectTheBodyDrawsTheParticles();
    ASSERT_GE(m_batch.Vertices().size(), 18u);
    const auto particles = Worn()->Particles();
    constexpr std::array<std::size_t, 6> Order{1, 2, 3, 1, 3, 0};
    for (std::size_t corner = 0; corner < Order.size(); ++corner)
        EXPECT_EQ(m_batch.Vertices()[12 + corner].position, particles[Order[corner]].position) << corner;
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(FrameRandoms + TailRandoms));
}

// 0x50bafd: without a draw (alpha < 0.01) the cloth is simulated anyway; MuRet anchors it to the stored pose (Q-C862)
// and, without one, cuts.
TEST_F(MonsterClothTest, AnUndrawnMonsterStillStepsItsClothOnItsStoredPose)
{
    ActorState& monster = Monster(Spider);
    monster.alpha = 0.005f;
    const auto unposed = Draw(Pass());
    ASSERT_FALSE(unposed.HasValue());
    EXPECT_NE(unposed.Error().find("0x50bb3b"), std::string::npos) << unposed.Error();
    EXPECT_FALSE(Worn().has_value());
    EXPECT_EQ(m_clothWorld.random.State(), 1u);

    monster.alpha = 1.0f;
    ASSERT_TRUE(Draw(Pass()).HasValue());
    monster.alpha = 0.005f;
    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 0u);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(2 * (FrameRandoms + TailRandoms)));
}

// 0x455190 returning 0 (a spring at more than 20 times its length): 0x4fb1a0 releases it, the body draws mesh 2 as
// 0x4ebc80 left it, and the next frame builds it again.
TEST_F(MonsterClothTest, ABrokenClothIsReleasedAndRebuiltTheNextFrame)
{
    ActorState& monster = Monster(Spider);
    ASSERT_TRUE(Draw(Pass()).HasValue());

    monster.position.x += 5000.0f;
    const auto broken = Draw(Pass());

    ASSERT_TRUE(broken.HasValue()) << broken.Error();
    EXPECT_FALSE(Worn().has_value());
    const std::vector<glm::vec3> vertices = RobeAt(monster.position);
    EXPECT_EQ(m_batch.Vertices()[6].position, vertices[1]);
    EXPECT_EQ(m_batch.Vertices()[11].position, vertices[0]);

    ASSERT_TRUE(Draw(Pass()).HasValue());
    ASSERT_TRUE(Worn().has_value());
    EXPECT_EQ(Worn()->Particles()[1].position, vertices[1]);
}

// Between frames of the original: it neither builds nor simulates it nor spends rand(); its particles are carried with
// bone 0x12 from the body of the last simulation.
TEST_F(MonsterClothTest, BetweenOriginalFramesTheClothIsCarriedWithoutSteps)
{
    ActorState& monster = Monster(Spider);
    GameCharacterPass between = Pass();
    between.originalFrames = 0;
    ASSERT_TRUE(Draw(between).HasValue());
    EXPECT_FALSE(Worn().has_value());
    EXPECT_EQ(m_clothWorld.random.State(), 1u);

    ASSERT_TRUE(Draw(Pass()).HasValue());
    const std::uint32_t state = m_clothWorld.random.State();
    const std::vector<Client::Models::ClothParticle> before(Worn()->Particles().begin(), Worn()->Particles().end());
    monster.position.x += 5.0f;
    const auto result = Draw(between);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_clothWorld.random.State(), state);
    constexpr std::array<std::size_t, 6> Order{1, 2, 3, 1, 3, 0};
    for (std::size_t corner = 0; corner < Order.size(); ++corner)
    {
        const glm::vec3& drawn = m_batch.Vertices()[6 + corner].position;
        const glm::vec3& particle = before[Order[corner]].position;
        EXPECT_NEAR(drawn.x, particle.x + 5.0f, 1e-3f) << corner;
        EXPECT_NEAR(drawn.y, particle.y, 1e-3f) << corner;
        EXPECT_NEAR(drawn.z, particle.z, 1e-3f) << corner;
    }
    for (std::size_t vertex = 0; vertex < before.size(); ++vertex)
        EXPECT_EQ(Worn()->Particles()[vertex].position, before[vertex].position) << vertex;
}
} // namespace
