#include "Scenes/MonsterCloth.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameCharacters.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

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

// La tela sobre la malla de los monstruos (0x50bae7..0x50bbca) en el dibujo del juego: vectores literales del ASM
// c32bbd5f, sin ejecutar el original. La escena tiene que hacer lo mismo que la clase de render (test_mesh_cloth) con
// la malla 2, el hueso 0x12, las banderas 0x400, las dos esferas y cinco pasos de 0,005f.
namespace
{
using Client::Render::Models::Cloth;
using Client::Render::Models::ClothBody;
using Client::Render::Models::ClothWorld;
using Client::Scenes::ActorState;
using Client::Scenes::GameCharacterPass;

constexpr std::uint16_t Spider = 0x15b;        // un modelo sin casos en 0x6002e0
constexpr std::uint16_t MagicSkeleton = 0x190; // Monster63.bmd, el de los 89, 95, 112, 118, 124, 130 y 143
constexpr std::uint16_t ClothMonster = 0x59;
constexpr std::size_t Slot = 5;
constexpr std::size_t FrameRandoms = 20; // cinco pasos de 0x4565f0, con las 4 de la protección del contador
constexpr std::size_t TailRandoms = 3;   // 0x50db57 y los dos de las manos (0x50e288), como el monstruo sin tela

std::uint32_t AfterRandoms(std::size_t count)
{
    Client::Render::Models::CrtRandom random{1};
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

// La malla 2: un cuadrado de 12 por 10 a 200 del hueso 0x12 (fuera de las dos esferas), los vértices 1 y 2 en ese
// hueso y el 0 y el 3 en el 0.
Client::Data::Models::Mesh Robe()
{
    Client::Data::Models::Mesh mesh = Triangle();
    mesh.vertices = {{0, {200, 0, 0}}, {0x12, {200, 0, -8}}, {0x12, {212, 0, -8}}, {0, {212, 0, 0}}};
    mesh.triangles = {{3, {1, 2, 3, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, {3, {1, 3, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
    mesh.textureName = "robe.jpg";
    return mesh;
}

class ClothModels final : public Client::Scenes::IModelLibrary
{
public:
    ClothModels()
    {
        using namespace Client::Render::Models;
        SkeletonBone bone;
        bone.parent = -1;
        bone.actions = {{BoneKey{}}};
        bone.actions[0][0].rotation = AngleQuaternion({0.0f, 0.0f, 0.0f});
        // El hueso k en (0, 0, k): el 0x12, en (0, 0, 18).
        m_skeleton.bones.assign(48, bone);
        for (std::size_t index = 0; index < m_skeleton.bones.size(); ++index)
            m_skeleton.bones[index].actions[0][0].position = {0.0f, 0.0f, static_cast<float>(index)};
        m_skeleton.actions = {ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
        m_model.meshes = {Triangle(), Triangle(), Robe()};
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t model) const override
    {
        return model == 0x20a ? nullptr : &m_model;
    }
    [[nodiscard]] const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &m_skeleton;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return m_textures;
    }

private:
    Client::Render::Models::Skeleton m_skeleton;
    Client::Data::Models::Model m_model;
    std::array<Client::Render::TextureSlot, 3> m_textures{
        Client::Render::TextureSlot{0x300}, Client::Render::TextureSlot{0x301}, Client::Render::TextureSlot{0x302}};
};

// Los vértices de la malla 2 que deja 0x4ebc80 con el monstruo en `position`, sin girar y con la escala 1.
std::vector<glm::vec3> RobeAt(const glm::vec3& position)
{
    return {position + glm::vec3(200.0f, 0.0f, 0.0f), position + glm::vec3(200.0f, 0.0f, 10.0f),
            position + glm::vec3(212.0f, 0.0f, 10.0f), position + glm::vec3(212.0f, 0.0f, 0.0f)};
}

// Lo que arman 0x6a0210, el +0x24 y los dos 0x455cd0 (0x50bb0b..0x50bb84).
Cloth BuiltRobe(std::span<const glm::vec3> vertices)
{
    Cloth cloth;
    cloth.BuildOnTheMesh(Robe(), 2, 0x12, 0x400, vertices);
    cloth.AddSphere({0.0f, 0.0f, 0.0f}, 50.0f, 0x12);
    cloth.AddSphere({0.0f, -20.0f, 0.0f}, 30.0f, 0x12);
    return cloth;
}

// 0x455190(0,005f, 5) (0x50bba0..0x50bba9).
bool StepTheRobe(Cloth& cloth, const ActorState& actor, std::span<const glm::vec3> vertices, ClothWorld& world,
                 std::uint8_t map)
{
    const ClothBody body{.bones = actor.bodyPose,
                         .angles = actor.angles,
                         .scale = actor.scale,
                         .position = actor.position,
                         .meshVertices = vertices};
    return cloth.Simulate(std::bit_cast<float>(0x3ba3d70au), 5, body, world, map);
}

class MonsterClothTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_world = std::make_unique<Client::Scenes::WorldState>();
        m_world->FaunaResources().Bind(&m_models);
        m_world->Runtime().map = 0;
        m_world->Runtime().hero = 42;
        m_world->Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        m_world->Map().terrain.attributes->cells.fill(0);
        m_light = std::make_unique<Client::World::TerrainLighting>();
        m_light->cells.fill({0.2f, 0.4f, 0.6f});
        m_joints.resize(16);
        m_cloths.resize(16);
        m_clothWorld.windBase = 0.3f;
        m_batch.Begin(Client::Render::Pass3D{});
    }

    void TearDown() override
    {
        m_world->FaunaResources().Bind(nullptr);
    }

    // Un monstruo con la tela sobre la malla como lo deja 0x511290 con lo que escribe el alta.
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
                .joints = m_joints};
    }

    Client::Core::Expected<void, std::string> Draw(const GameCharacterPass& pass)
    {
        m_batch.Begin(Client::Render::Pass3D{});
        return Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);
    }

    const std::optional<Cloth>& Worn()
    {
        return m_cloths[Slot].OnTheMesh().Cloth();
    }

    // Los seis vértices de la malla 2 en el cuerpo (los triángulos (1, 2, 3) y (1, 3, 0)), después de las mallas 0 y
    // 1.
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
    std::unique_ptr<Client::Scenes::WorldState> m_world;
    std::unique_ptr<Client::World::TerrainLighting> m_light;
    ClothWorld m_clothWorld;
    Client::World::FrameSprites m_sprites;
    Client::Scenes::FaunaDrawCarry m_carry;
    std::vector<Client::Scenes::JointState> m_joints;
    std::vector<Client::Scenes::CharacterCloth> m_cloths;
    Client::Render::ModelBatch m_batch;
};

// Lorencia: el primer frame la arma sobre la malla 2 de la pose de ahora y la simula cinco pasos, con 20 rand() antes
// de los 3 del monstruo sin tela; el cuerpo dibuja sus partículas.
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
    // Las sueltas caen; las del hueso 0x12 quedan donde las dejó 0x4ebc80.
    EXPECT_LT(particles[0].position.z, vertices[0].z);
    EXPECT_EQ(particles[1].position, vertices[1]);
    EXPECT_EQ(Worn()->Springs().size(), 7u);
    EXPECT_EQ(Worn()->Spheres().size(), 2u);
    ExpectTheBodyDrawsTheParticles();
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(FrameRandoms + TailRandoms));
    EXPECT_EQ(expectedWorld.random.State(), AfterRandoms(FrameRandoms));
}

// Devias: la tela queda en el lugar y el frame siguiente sigue la misma, sin armarla de nuevo.
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

// Blood Castle (el mapa 11), con el modelo de verdad: la tela se arma y se simula, y después corta el caso del 0x190
// en 0x6002e0.
TEST_F(MonsterClothTest, InBloodCastleTheMagicSkeletonStepsItsClothBeforeTheBodyCase)
{
    m_world->Runtime().map = 11;
    Monster(MagicSkeleton);

    const auto result = Draw(Pass());

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x600654"), std::string::npos) << result.Error();
    ASSERT_TRUE(Worn().has_value());
    EXPECT_EQ(m_batch.Vertices().size(), 0u);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(FrameRandoms));
}

// 0x50bafd: sin dibujo (alpha < 0,01) la tela se simula igual; MuRet la ancla a la pose guardada (Q-C862) y, sin
// ella, corta.
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

// 0x455190 que da 0 (un resorte a más de 20 veces su largo): 0x4fb1a0 la suelta, el cuerpo dibuja la malla 2 como
// la dejó 0x4ebc80, y el frame siguiente la arma de nuevo.
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

// Entre frames del original: ni la arma ni la simula ni gasta rand(); sus partículas van llevadas con el hueso 0x12
// desde el cuerpo de la última simulación.
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
    const std::vector<Client::Render::Models::ClothParticle> before(Worn()->Particles().begin(),
                                                                    Worn()->Particles().end());
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
    // La tela guardada no cambia.
    for (std::size_t vertex = 0; vertex < before.size(); ++vertex)
        EXPECT_EQ(Worn()->Particles()[vertex].position, before[vertex].position) << vertex;
}
} // namespace
