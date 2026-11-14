#include "Scenes/MonsterDrawing.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Attachment.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameCharacters.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// El dibujo de los monstruos y los NPC (0x50ba30 con otro tipo que el 0x20a): vectores literales del ASM c32bbd5f, sin
// ejecutar el original. Un modelo sintético de un triángulo en el hueso 0 deja ver el cuerpo, el contorno y la sombra.
namespace
{
using Client::Scenes::ActorState;
using Client::Scenes::GameCharacterPass;

constexpr std::uint16_t Spider = 0x15b;    // Monster10.bmd, el 3 (Lorencia)
constexpr std::uint16_t Yeti = 0x15e;      // Monster13.bmd, el 19 (Devias)
constexpr std::uint16_t DeviasNpc = 0x1d5; // el NPC 241
constexpr std::uint16_t FairyNpc = 0x1d9;  // el NPC 237 de Lorencia
constexpr std::uint16_t Golem = 0x176;     // un caso con sprites de 0x50b870 y 0x50b920
constexpr std::uint16_t HelperType = 0x1b6;

std::uint32_t AfterRandoms(std::size_t count)
{
    Client::Render::Models::CrtRandom random{1};
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random.State();
}

class MonsterModels final : public Client::Scenes::IModelLibrary
{
public:
    MonsterModels()
    {
        using namespace Client::Render::Models;
        SkeletonBone bone;
        bone.parent = -1;
        bone.actions = {{BoneKey{}}};
        bone.actions[0][0].rotation = AngleQuaternion({0.0f, 0.0f, 0.0f});
        // El hueso k en (0, 0, k), como en los tests del jugador: un sprite deja ver de qué hueso sale.
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

    // `count` huesos en vez de 48, como los arma el constructor.
    void SetBoneCount(std::size_t count)
    {
        const auto bone = m_skeleton.bones.front();
        m_skeleton.bones.assign(count, bone);
        for (std::size_t index = 0; index < m_skeleton.bones.size(); ++index)
            m_skeleton.bones[index].actions[0][0].position = {0.0f, 0.0f, static_cast<float>(index)};
    }

    // `count` acciones iguales a la primera.
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

    // Una segunda malla igual a la primera, con su textura.
    void AddMesh()
    {
        m_model.meshes.push_back(m_model.meshes.front());
        m_texture.push_back(Client::Render::TextureSlot{0x301});
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
        return m_texture;
    }

private:
    Client::Render::Models::Skeleton m_skeleton;
    Client::Data::Models::Model m_model;
    std::vector<Client::Render::TextureSlot> m_texture{Client::Render::TextureSlot{0x300}};
};

class MonsterDrawingTest : public testing::Test
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
        m_batch.Begin(Client::Render::Pass3D{});
    }

    void TearDown() override
    {
        m_world->FaunaResources().Bind(nullptr);
    }

    // Un monstruo como lo deja 0x511290 con lo que escribe el alta (0x511c70..0x511c88, 0x511d45, 0x51248d).
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
                .joints = m_joints};
    }

    Client::Core::Expected<void, std::string> Draw(const GameCharacterPass& pass)
    {
        return Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);
    }

    std::vector<Client::World::Sprite> LiveSprites()
    {
        std::vector<Client::World::Sprite> live;
        for (const auto& sprite : m_sprites.Sprites())
        {
            if (sprite.alive)
                live.push_back(sprite);
        }
        return live;
    }

    MonsterModels m_models;
    std::unique_ptr<Client::Scenes::WorldState> m_world;
    std::unique_ptr<Client::World::TerrainLighting> m_light;
    Client::Render::Models::ClothWorld m_clothWorld;
    Client::World::FrameSprites m_sprites;
    Client::Scenes::FaunaDrawCarry m_carry;
    std::vector<Client::Scenes::JointState> m_joints;
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
    m_world->FaunaResources().RecordPoseKey(Spider, 7);

    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // 0x4eb8cd..0x4eb94c: la key de la pose en +0xa8 del modelo; la acción tiene una sola, la 0.
    EXPECT_EQ(m_world->FaunaResources().PoseKey(Spider), 0);
    // 0x6002e0 y después la sombra de 0x50bd89.
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    ASSERT_EQ(m_batch.Commands().size(), 2u);
    // 0x5ffce0 sin la luz por vértice: un décimo del terreno más la propia.
    ExpectColor(m_batch.Vertices()[0], 0.12f, 0.24f, 0.36f, 1.0f);
    EXPECT_EQ(m_batch.Commands()[1].state.blend, Client::Render::ModelBlend::Shadow);
    ExpectColor(m_batch.Vertices()[3], 0.0f, 0.0f, 0.0f, 1.0f);
    // La sombra queda aplastada sobre el piso del objeto.
    EXPECT_FLOAT_EQ(m_batch.Vertices()[3].position.z, 15.0f);
    // 0x50db94: la luz de lo que cuelga, el terreno más la propia.
    EXPECT_FLOAT_EQ(spider.drawnLight.x, 0.3f);
    EXPECT_FLOAT_EQ(spider.drawnLight.y, 0.6f);
    EXPECT_FLOAT_EQ(spider.drawnLight.z, 0.9f);
    EXPECT_EQ(spider.bodyPose.size(), 48u);
    // El de 0x50db57 y los dos de +0x38e en el lazo de las manos (0x50e288), sin ninguna mano.
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
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
    // El 7 la mezcla con alpha 0,2; el 10 (0x50bdf5) y Kalima (0x60b459) no la tienen.
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

// 0x50bed8: el 0xe8 y el 0xe9 llevan, después de la sombra sin la malla 2, el calco 0x61e aditivo de 2,7 celdas a 5 del
// piso, con la luz (v / 2, v / 2, v) de v = sin(T · 0,0015f) · 0,3f + 0,8f; +0x58 vuelve a −1.
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
        m_world = std::make_unique<Client::Scenes::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
        m_world->Map().terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        m_world->Runtime().map = value.map;
        ActorState& archangel = Monster(6, Spider, value.monster);
        archangel.position = {12850.0f, 12850.0f, 10.0f};
        archangel.angles.z = 30.0f;
        GameCharacterPass pass = Pass();
        pass.frameClock = std::chrono::milliseconds(1000);

        const auto result = Draw(pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(archangel.dword58, -1);
        // El cuerpo, la sombra (la malla 0: +0x58 en 2 oculta otra) y las 7 · 7 celdas del calco, de dos triángulos.
        ASSERT_EQ(m_batch.Vertices().size(), 6u + 49u * 6u);
        const auto& decal = m_batch.Commands().back();
        EXPECT_EQ(decal.state.blend, Client::Render::ModelBlend::Additive);
        ASSERT_TRUE(decal.state.texture.has_value());
        EXPECT_EQ(decal.state.texture->value, 0x61e);
        const double pulse = std::sin(1000.0 * static_cast<double>(std::bit_cast<float>(0x3ac49ba6u))) *
                                 static_cast<double>(std::bit_cast<float>(0x3e99999au)) +
                             static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));
        ExpectColor(m_batch.Vertices()[6], static_cast<float>(pulse * 0.5), static_cast<float>(pulse * 0.5),
                    static_cast<float>(pulse), 1.0f);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[6].position.z, 5.0f);
        EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
        TearDown();
    }
}

// Sin las alturas del terreno, el calco corta en 0x5fb2f0 después de la sombra.
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
    // Cada uno: dos pases del contorno, el cuerpo y la sombra.
    ASSERT_EQ(m_batch.Vertices().size(), 24u);
    EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Additive);
    ExpectColor(m_batch.Vertices()[0], 0.1f, 0.01f, 0.0f, 1.0f);
    ExpectColor(m_batch.Vertices()[3], 0.7f, 0.07f, 0.0f, 1.0f);
    ExpectColor(m_batch.Vertices()[12], 0.02f, 0.1f, 0.0f, 1.0f);
    ExpectColor(m_batch.Vertices()[15], 0.16f, 0.7f, 0.0f, 1.0f);
    // El monstruo con +0x94 en 1,2: 0,1 / escala + 1. El NPC: +0x94, el vértice (1, 0, 0) a 1,2.
    EXPECT_FLOAT_EQ(m_batch.Vertices()[1].position.x, 151.1f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[13].position.x, 151.2f);
}

// 0x60cdd9: las puertas de Kalima (0xc5) llevan el contorno solo en la malla 0 (0x60ce4e); otro, en las dos. El cuerpo
// del 0xc5 lo dibuja 0x438fe0 (las dos mallas) y su helper, 0x43c150, quieto (acción 0) no agrega nada.
TEST_F(MonsterDrawingTest, TheKalimaGateOutlinesOnlyItsFirstMesh)
{
    constexpr std::uint16_t Gate = 0xc5;
    m_models.AddMesh();
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world = std::make_unique<Client::Scenes::WorldState>();
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

        m_world = std::make_unique<Client::Scenes::WorldState>();
        SetUp();
        m_world->Runtime().map = map;
        Monster(4, Spider, 3);
        // SetUp rehízo la luz: el pase se vuelve a armar para no apuntar a la anterior.
        pass = Pass();
        pass.hovered = 4;
        const auto spider = Draw(pass);
        ASSERT_TRUE(spider.HasValue()) << spider.Error();
        // Dos pases por las dos mallas, el cuerpo y la sombra.
        EXPECT_EQ(m_batch.Vertices().size(), 24u);
        TearDown();
    }
    m_world = std::make_unique<Client::Scenes::WorldState>();
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

// Sin los cortes del cuerpo (0x438fe0 y 0x42cc10), la cola llega a los helpers de 0x510a1e: el 0xc5 en la acción 1
// cuenta su +0x60 en 0x43c150 (en Lorencia y Devias, con cualquier mapa) y el 0x1a1 del 30 sin el asedio apaga su
// +0x42c y deja +0x58 en −2 en 0x42dfe0; ninguno saca rand() fuera de los tres de lo que cuelga.
TEST_F(MonsterDrawingTest, TheBodyLeadsToTheKalimaAndSiegeHelpers)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world = std::make_unique<Client::Scenes::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
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
    m_world = std::make_unique<Client::Scenes::WorldState>();
    SetUp();
    m_clothWorld.random = Client::Render::Models::CrtRandom{1};
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
        // (3,5; −12; 10) del hueso 20, en (0, 0, 20).
        EXPECT_EQ(sprite.position, glm::vec3(153.5f, 138.0f, 40.0f));
        EXPECT_FLOAT_EQ(sprite.scale, 0.3f);
        EXPECT_FLOAT_EQ(sprite.light.x, 0.65f);
    }
    // El rand() de 0x50db57, los dos de +0x38e en las manos y el % 30 de 0x510815 (26500 % 30 = 10: sin joint).
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(4));
}

TEST_F(MonsterDrawingTest, TheFairyJointCutsInItsCreationAfterItsThreeRandoms)
{
    // Con la semilla 51 el cuarto rand() es 17250: % 30 da 0, y salen los tres % 100 del punto de partida.
    m_clothWorld.random = Client::Render::Models::CrtRandom{51};
    Monster(6, FairyNpc, 237).byte88 = 4;

    const auto result = Draw(Pass());

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x5108a9"), std::string::npos) << result.Error();
    EXPECT_NE(result.Error().find("0x618/6"), std::string::npos) << result.Error();
    EXPECT_EQ(LiveSprites().size(), 2u);
}

TEST_F(MonsterDrawingTest, AModelCaseDrawsItsSpritesAndSkipsTheHelperChain)
{
    Monster(8, Golem, 0x1e);

    const auto result = Draw(Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // 0x50b920 (dos 0x5ff) y ocho 0x50b870.
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 10u);
    EXPECT_EQ(sprites[0].texture, 0x5ff);
    EXPECT_EQ(sprites[0].position, glm::vec3(155.0f, 150.0f, 52.0f));
    EXPECT_FLOAT_EQ(sprites[0].light.x, 0.8f);
    EXPECT_EQ(sprites[2].texture, 0x5c5);
    EXPECT_FLOAT_EQ(sprites[2].light.y, 0.42f);
    EXPECT_EQ(m_world->Actors()[8].effectOrigin, glm::vec3(145.0f, 150.0f, 53.0f));
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
    // La tela sobre la malla sin la tela persistente del lugar corta antes del cuerpo; el 0x1b6 de 0x4271d0, después de
    // lo que cuelga y de su rand() % 30, en el primer hueso con nombre que su alta no registró (0x427291); lo de
    // 0x50bf45, después de la sombra.
    constexpr std::array cases{Case{Spider, 0x59, "0x50bb96", 0, 0}, Case{HelperType, 0x20, "0x427291", 6, 4},
                               Case{Spider, 0x49, "0x50c36b", 6, 0}, Case{0x18e, 0x21, "0x4ee770", 0, 4}};
    for (const Case& value : cases)
    {
        SCOPED_TRACE(value.address);
        m_world = std::make_unique<Client::Scenes::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
        Monster(0, value.type, value.monster);
        const auto result = Draw(Pass());
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find(value.address), std::string::npos) << result.Error();
        EXPECT_EQ(m_batch.Vertices().size(), value.vertices);
        EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(value.randoms));
        TearDown();
    }
}

// 0x50c671, 0x50c71b y 0x50c8ce: después de la sombra, los pases de brillo de 0x60b130 sobre la pose del cuerpo, con
// el color de 0x608e00 (k = alpha · factor por el del modelo: (1; 0,5; 0) el de todos, (0,8; 0,8; 1) el 0x16d con el
// 0x100) y RenderBody aditivo: el 0x44 con la textura 0x5c0, el 0x48 con la 0x5fc (o la 0x5fd del 0x35 y el 0x36) y el
// 0x140 sin brillo, con la del modelo. En Lorencia y en Devias, igual: no miran el mapa ni sacan rand().
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
        // El 0x3b, sin sombra, con el factor en 0,5.
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
            m_world = std::make_unique<Client::Scenes::WorldState>();
            SetUp();
            m_clothWorld.random = Client::Render::Models::CrtRandom{1};
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
            // Las mallas de los pases son las del cuerpo (los vértices 0 a 2), sin aplastar.
            EXPECT_EQ(m_batch.Vertices()[first].position, m_batch.Vertices()[0].position);
            // El de 0x50db57 y los dos de +0x38e en el lazo de las manos: los pases no sacan ninguno.
            EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
            TearDown();
        }
    }
}

// Q-C95 en el brillo de un monstruo que se desvanece: con el alpha en 0,5, 0x608e00 deja k = 0,5 (el factor del 0x3b,
// 0,5 · 0,5 = 0,25) por el color del modelo, y RenderMesh lo multiplica otra vez por el alpha en el lugar en cada
// malla.
TEST_F(MonsterDrawingTest, AFadingShineDarkensEachMeshInPlace)
{
    m_models.AddMesh();
    struct Case
    {
        std::uint16_t monster;
        float k;
        std::size_t first;
    };
    // El 0x3b no tiene sombra; el 0x26, sí (el alpha no baja de 0,3f).
    for (const Case& value : {Case{0x26, 0.5f, 12}, Case{0x3b, 0.25f, 6}})
    {
        SCOPED_TRACE(value.monster);
        m_world = std::make_unique<Client::Scenes::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
        Monster(0, Spider, value.monster).alpha = 0.5f;

        const auto result = Draw(Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        // Las dos mallas del cuerpo, las de la sombra y las dos del pase 0x44.
        ASSERT_EQ(m_batch.Vertices().size(), value.first + 6);
        ExpectColor(m_batch.Vertices()[value.first], value.k * 0.5f, value.k * 0.25f, 0.0f, 1.0f);
        ExpectColor(m_batch.Vertices()[value.first + 3], value.k * 0.25f, value.k * 0.125f, 0.0f, 1.0f);
        TearDown();
    }
}

// 0x50c951 y 0x50d58b: el +0x376 0x37, con cualquier tipo, arma y simula la tela de ClothMonster (bVar23) fuera de los
// mapas 0x12 a 0x17, sobre los huesos propios, antes de lo que cuelga; la escena hace lo mismo que CharacterCloth.
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
        m_world = std::make_unique<Client::Scenes::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
        m_world->Runtime().map = value.map;
        ActorState& monster = Monster(3, Spider, 0x37);
        std::vector<Client::Scenes::CharacterCloth> cloths(4);
        GameCharacterPass pass = Pass();
        pass.cloths = cloths;
        Client::Render::Models::ClothWorld expectedWorld = m_clothWorld;

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
        Client::Scenes::CharacterCloth expected;
        Client::Render::ModelBatch expectedBatch;
        expectedBatch.Begin(Client::Render::Pass3D{});
        Client::Scenes::ClothWearer wearer;
        wearer.monster = std::uint16_t{0x37};
        const Client::Render::Models::ClothBody body{
            .bones = monster.bodyPose, .angles = monster.angles, .scale = monster.scale, .position = monster.position};
        expected.Frame(expectedBatch, wearer, body, expectedWorld, static_cast<std::uint8_t>(value.map), 0.0f, true);
        ASSERT_EQ(expected.Cloths().size(), 1u);
        const auto particles = cloths[3].Cloths()[0].Particles();
        const auto reference = expected.Cloths()[0].Particles();
        ASSERT_EQ(particles.size(), reference.size());
        for (std::size_t index = 0; index < particles.size(); ++index)
            EXPECT_EQ(particles[index].position, reference[index].position) << index;
        // El cuerpo, la sombra y después la tela, como la dibuja CharacterCloth.
        ASSERT_EQ(m_batch.Vertices().size(), 6u + expectedBatch.Vertices().size());
        for (std::size_t index = 0; index < expectedBatch.Vertices().size(); ++index)
            EXPECT_EQ(m_batch.Vertices()[6 + index].position, expectedBatch.Vertices()[index].position) << index;
        for (std::size_t call = 0; call < 3; ++call)
            static_cast<void>(expectedWorld.random.Next());
        EXPECT_EQ(m_clothWorld.random.State(), expectedWorld.random.State());
        EXPECT_NE(m_clothWorld.random.State(), AfterRandoms(3));
        TearDown();
    }
    m_world = std::make_unique<Client::Scenes::WorldState>();
    SetUp();
    Monster(3, Spider, 0x37);
    const auto unsupplied = Draw(Pass());
    ASSERT_FALSE(unsupplied.HasValue());
    EXPECT_NE(unsupplied.Error().find("0x50d58b"), std::string::npos) << unsupplied.Error();
}

// 0x50c177: el +0x376 0x4d (el monstruo 77, el tipo 0x189 con su segundo modelo 0x18a). Con el reloj en 0, s = 0,5: el
// pase 0x44 con el factor 0,65 (k por el (1; 0,5; 0) del modelo), +0x68 en f(1,5 · 0,3f), el cuerpo otra vez a 1,2 y
// a 1,4 alrededor de su hueso con la malla 0 de blendMesh, el 0x18a con su pase 0x42 de Chrome01 y su común en 0,6, y
// la tela de la grilla del 0x4d, como la arma y la simula CharacterCloth. En Lorencia, en Devias y en Icarus (el 10,
// sin sombra), igual.
TEST_F(MonsterDrawingTest, ThePhoenixDrawsItsWiderBodiesItsSecondModelAndItsCloth)
{
    constexpr std::uint16_t Phoenix = 0x189;
    for (const std::int32_t map : {0, 2, 10})
    {
        SCOPED_TRACE(map);
        m_world = std::make_unique<Client::Scenes::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
        m_world->Runtime().map = map;
        ActorState& phoenix = Monster(3, Phoenix, 0x4d);
        std::vector<Client::Scenes::CharacterCloth> cloths(4);
        GameCharacterPass pass = Pass();
        pass.cloths = cloths;
        Client::Render::Models::ClothWorld expectedWorld = m_clothWorld;

        const auto result = Draw(pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        // El tipo vuelve, +0x64 queda en −1 y +0x68 en la luz de la tela del blendMesh.
        EXPECT_EQ(phoenix.type, Phoenix);
        EXPECT_EQ(phoenix.dword64, -1);
        EXPECT_EQ(phoenix.blendMeshLight, static_cast<float>((1.0 - 0.5 + 1.0) * static_cast<double>(0.3f)));
        // 0x743748c, 0x74374bc y 0x74374ec: los huesos 23 y 14 del 0x18a y el 24 del 0x189 (el hueso k en (0, 0, k)).
        const auto& bones = m_world->Runtime().phoenixBones;
        EXPECT_EQ(bones[0][3], glm::vec3(0.0f, 0.0f, 23.0f));
        EXPECT_EQ(bones[1][3], glm::vec3(0.0f, 0.0f, 14.0f));
        EXPECT_EQ(bones[2][3], glm::vec3(0.0f, 0.0f, 24.0f));
        const std::size_t shadow = map == 10 ? 0u : 3u;
        const auto& vertices = m_batch.Vertices();
        ASSERT_GE(vertices.size(), 3u + shadow + 15u);
        // El cuerpo en 0,6 (0x5ffce0 con el 0x189).
        ExpectColor(vertices[0], 0.6f, 0.6f, 0.6f, 1.0f);
        // El pase 0x44: k = 0,65.
        const std::size_t shine = 3 + shadow;
        ExpectColor(vertices[shine], 0.65f, 0.325f, 0.0f, 1.0f);
        // 0x602e90(objeto, 1, 2, 0) y (objeto, 1, 3, 0): el vértice (1, 0, 0) del hueso 0 a 1,2 y a 1,4.
        EXPECT_FLOAT_EQ(vertices[shine + 3 + 1].position.x, 151.2f);
        EXPECT_FLOAT_EQ(vertices[shine + 6 + 1].position.x, 151.4f);
        EXPECT_FLOAT_EQ(vertices[shine + 6 + 2].position.y, 151.4f);
        // El 0x18a: RenderBody(0x42) con Chrome01 y el común en 0,6, a la escala 1.
        const auto& commands = m_batch.Commands();
        const std::size_t second = shine + 9;
        EXPECT_FLOAT_EQ(vertices[second + 1].position.x, 151.0f);
        ExpectColor(vertices[second + 3], 0.6f, 0.6f, 0.6f, 1.0f);
        bool chromed = false;
        for (const auto& command : commands)
            chromed = chromed || (command.state.texture.has_value() && command.state.texture->value == 0x5c0 &&
                                  command.state.blend == Client::Render::ModelBlend::Additive);
        EXPECT_TRUE(chromed);
        // La tela: una, armada y simulada sobre la pose del 0x18a.
        ASSERT_EQ(cloths[3].Cloths().size(), 1u);
        EXPECT_EQ(cloths[3].Count(), 1);
        EXPECT_EQ(cloths[3].Cloths()[0].Front().value, 0x629);
        EXPECT_EQ(cloths[3].Cloths()[0].Columns(), 5);
        EXPECT_EQ(cloths[3].Cloths()[0].Rows(), 12);
        EXPECT_EQ(cloths[3].Cloths()[0].Spheres().size(), 1u);
        Client::Scenes::CharacterCloth expected;
        Client::Render::ModelBatch expectedBatch;
        expectedBatch.Begin(Client::Render::Pass3D{});
        const Client::Render::Models::ClothBody body{
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
        // Los de la tela y, después, el de 0x50db57 y los dos de +0x38e del lazo de las manos.
        for (std::size_t call = 0; call < 3; ++call)
            static_cast<void>(expectedWorld.random.Next());
        EXPECT_EQ(m_clothWorld.random.State(), expectedWorld.random.State());
        EXPECT_NE(m_clothWorld.random.State(), AfterRandoms(3));
        TearDown();
    }
}

// 0x50c59f: el +0x376 0x47 (con la textura 0x253), el 0x4a y el 0x12d (con la 0x255), con cualquier tipo, arman en su
// lugar una tela de la grilla en el hueso 0x13 (5 · 15, sin esferas) sobre la pose guardada, la simulan y la dibujan
// después de la sombra, como CharacterCloth. En Lorencia, en Devias y en Icarus (el 10, sin sombra), igual.
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
            m_world = std::make_unique<Client::Scenes::WorldState>();
            SetUp();
            m_clothWorld.random = Client::Render::Models::CrtRandom{1};
            m_world->Runtime().map = map;
            ActorState& crust = Monster(5, Spider, value.monster);
            std::vector<Client::Scenes::CharacterCloth> cloths(6);
            GameCharacterPass pass = Pass();
            pass.cloths = cloths;
            Client::Render::Models::ClothWorld expectedWorld = m_clothWorld;

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
            Client::Scenes::CharacterCloth expected;
            Client::Render::ModelBatch expectedBatch;
            expectedBatch.Begin(Client::Render::Pass3D{});
            const Client::Render::Models::ClothBody body{
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
    m_world = std::make_unique<Client::Scenes::WorldState>();
    SetUp();
    Monster(5, Spider, 0x47);
    const auto unsupplied = Draw(Pass());
    ASSERT_FALSE(unsupplied.HasValue());
    EXPECT_NE(unsupplied.Error().find("0x50c59f"), std::string::npos) << unsupplied.Error();
}

// Entre frames del original el 0x4d dibuja lo mismo sobre la copia: no guarda +0x64, +0x68, el tipo ni los huesos,
// no arma la tela ni saca rand(); sin la tela persistente del lugar, corta en 0x50c283.
TEST_F(MonsterDrawingTest, ThePhoenixBetweenOriginalFramesStoresNothing)
{
    ActorState& phoenix = Monster(3, 0x189, 0x4d);
    std::vector<Client::Scenes::CharacterCloth> cloths(4);
    GameCharacterPass pass = Pass();
    pass.cloths = cloths;
    pass.originalFrames = 0;

    const auto result = Draw(pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(phoenix.dword64, -1);
    EXPECT_EQ(phoenix.blendMeshLight, 1.0f);
    EXPECT_TRUE(phoenix.bodyPose.empty());
    EXPECT_EQ(m_world->Runtime().phoenixBones[2], Client::Render::Models::BoneTransform{});
    EXPECT_TRUE(cloths[3].Cloths().empty());
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
    const auto unsupplied = Draw(Pass());
    ASSERT_FALSE(unsupplied.HasValue());
    EXPECT_NE(unsupplied.Error().find("0x50c283"), std::string::npos) << unsupplied.Error();
}

// 0x50c762: el +0x376 0x2a cuelga del lugar de las alas el 0xef del hueso 9 con (0, 0, −40) y el 0xf0 del 0x3d con
// (0, −40, 45), este a 0,9f con la luz en 1; la escala y la luz vuelven. El origen de cada uno es el desplazamiento
// girado por su hueso y escalado como el cuerpo (0x4ec190), más la posición; el vértice del hueso 0, ese hueso por la
// escala del modelo que cuelga más el origen; el color del 0xef es la luz de lo que cuelga que quedó del frame
// anterior. Cada 0x509340 saca el cierre de +0x38e y el rand() de 0x509a92. En Lorencia, en Devias y en el mapa 5, el
// de los dos modelos; el 0x2a no tiene sombra (0x50bd89).
TEST_F(MonsterDrawingTest, The0x2aCarriesTheHeadAndThePrincessFromTheWingsSlot)
{
    for (const std::int32_t map : {0, 2, 5})
    {
        SCOPED_TRACE(map);
        m_world = std::make_unique<Client::Scenes::WorldState>();
        SetUp();
        m_models.SetBoneCount(64);
        m_models.SetActionCount(2);
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
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
        // El cuerpo, el 0xef y el 0xf0.
        ASSERT_EQ(vertices.size(), 9u);
        // El 0xef: (150, 150, 10) + (0, 0, −40) · 1,25, más el hueso 9 por 1,25.
        EXPECT_FLOAT_EQ(vertices[3].position.x, 150.0f);
        EXPECT_FLOAT_EQ(vertices[3].position.y, 150.0f);
        EXPECT_FLOAT_EQ(vertices[3].position.z, -40.0f + 9.0f * 1.25f);
        EXPECT_FLOAT_EQ(vertices[4].position.x, 151.25f);
        ExpectColor(vertices[3], 0.5f, 0.25f, 0.125f, 1.0f);
        // El 0xf0: (150, 150, 10) + (0, −40, 45) · 1,25, más el hueso 0x3d por 0,9, con la luz en 1.
        EXPECT_FLOAT_EQ(vertices[6].position.x, 150.0f);
        EXPECT_FLOAT_EQ(vertices[6].position.y, 100.0f);
        EXPECT_FLOAT_EQ(vertices[6].position.z, 66.25f + 61.0f * std::bit_cast<float>(0x3f666666u));
        EXPECT_FLOAT_EQ(vertices[7].position.x, 150.0f + std::bit_cast<float>(0x3f666666u));
        ExpectColor(vertices[6], 1.0f, 1.0f, 1.0f, 1.0f);
        // Los dos de cada 0x509340 y los tres de la cola.
        EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(7));
        TearDown();
    }
}

// 0x50c83b: las estatuas de Blood Castle (+0x376 de 0x84 a 0x86) cuelgan del hueso 1, por el lugar de 0x5093f3, el
// 0xc1c y el 0x225 a 0,7f con (90, 0, 90) y (0, 80, 120), y el 0xa24 a 0,9f con (10, 0, 0) y (0, 110, 80), desde la
// posición del personaje. Los sprites del 0xc1c y del 0x225 sacan sus rand(); el 0x225, uno más con la acción 0. En
// Lorencia, en Devias y en Blood Castle (el 11).
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
            m_world = std::make_unique<Client::Scenes::WorldState>();
            SetUp();
            m_models.SetActionCount(2);
            m_clothWorld.random = Client::Render::Models::CrtRandom{1};
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
            // El cuerpo, la sombra (sin ella en el 11, como Spider) y los pases del arma (0x60b3d0).
            ASSERT_GE(vertices.size(), 9u);
            // El hueso 0 de lo que cuelga es la raíz: el hueso 1 del cuerpo por el lugar, a la escala del modelo.
            const Client::Render::Models::BoneTransform root = Client::Render::Models::PlacedRoot(
                statue.bodyPose[1], Client::Render::Models::HangingPlacement{value.angles, value.translation});
            const glm::vec3 expected = root[3] * value.scale + statue.position;
            EXPECT_NEAR(vertices[6].position.x, expected.x, 1e-3f);
            EXPECT_NEAR(vertices[6].position.y, expected.y, 1e-3f);
            EXPECT_NEAR(vertices[6].position.z, expected.z, 1e-3f);
            EXPECT_NEAR(root[3].y, value.translation.y, 1e-3f);
            // El cierre de +0x38e, el de 0x509a92 y los de los sprites del modelo; después, los tres de la cola.
            Client::Render::Models::CrtRandom expectedRandom{1};
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

// Q-C1119: Staff11 (0xc1c) y Sword20 (0x225) tienen una sola acción, y la estatua los cuelga con la 1: 0x4ec200 la lee
// fuera del arreglo y MuRet corta, después del cierre de +0x38e. Bow19 (0xa24) no avanza (un arma de 0xa12 a 0xc11 con
// la bandera) y 0x4eb860 lleva la acción a la 0: se dibuja.
TEST_F(MonsterDrawingTest, TheStatueStaffAndSwordReadAnActionTheirModelLacks)
{
    Monster(6, Spider, 0x84);
    const auto staff = Draw(Pass());
    ASSERT_FALSE(staff.HasValue());
    EXPECT_NE(staff.Error().find("0x4ec200: el modelo 0xc1c no tiene la acción 1"), std::string::npos) << staff.Error();
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(1));

    m_world = std::make_unique<Client::Scenes::WorldState>();
    SetUp();
    m_clothWorld.random = Client::Render::Models::CrtRandom{1};
    Monster(6, Spider, 0x86);
    const auto bow = Draw(Pass());
    ASSERT_TRUE(bow.HasValue()) << bow.Error();
    EXPECT_GE(m_batch.Vertices().size(), 9u);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(5));
}

// Entre frames del original el 0x2a cuelga lo mismo sobre la copia: no guarda el lugar de las alas ni saca rand(); con
// menos huesos que el 0x3d del 0xf0, corta en 0x509340.
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

// 0x50bf98: el +0x376 0x45 saca su rand() % 30, pone nueve flare01 de 0,6 con su luz y, antes de cada una de sus tres
// partículas 0x5c6/3, cuatro rand(); la 0x5c6/3 no está en Particles (CL11) y corta en la primera.
TEST_F(MonsterDrawingTest, The0x45LightsItsNineFlaresAndCutsAtItsFirstSpark)
{
    // 0x51483f: el monstruo 69 (el +0x376 0x45) con el modelo 0x184, que no tiene caso en 0x6002e0.
    constexpr std::uint16_t ModelOf69 = 0x184;
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world = std::make_unique<Client::Scenes::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
        m_sprites.Clear();
        m_world->Runtime().map = map;
        Monster(3, ModelOf69, 0x45);
        auto particles = std::make_unique<Client::World::Particles>();
        GameCharacterPass pass = Pass();
        pass.particles = particles.get();

        const auto result = Draw(pass);

        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x50c0db"), std::string::npos) << result.Error();
        // El cuerpo y la sombra antes.
        EXPECT_EQ(m_batch.Vertices().size(), 6u);
        Client::Render::Models::CrtRandom random{1};
        const double x =
            static_cast<double>(random.Next() % 30 + 70) * static_cast<double>(std::bit_cast<float>(0x3c23d70au));
        const auto sprites = LiveSprites();
        ASSERT_EQ(sprites.size(), 9u);
        EXPECT_EQ(sprites[0].texture, 0x5ac);
        EXPECT_FLOAT_EQ(sprites[0].scale, 0.6f);
        EXPECT_EQ(sprites[0].light.x, static_cast<float>(x * static_cast<double>(std::bit_cast<float>(0x3f4ccccdu))));
        EXPECT_EQ(sprites[0].light.y, static_cast<float>(x * static_cast<double>(std::bit_cast<float>(0x3f666666u))));
        EXPECT_EQ(sprites[0].light.z, static_cast<float>(x));
        // El hueso 10 en (0, 0, 10), más la posición.
        EXPECT_EQ(sprites[0].position, glm::vec3(150.0f, 150.0f, 20.0f));
        EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(5));
        TearDown();
    }
    // Entre frames del original, los nueve flare01 sin las partículas, sobre una copia del rand().
    m_world = std::make_unique<Client::Scenes::WorldState>();
    SetUp();
    m_clothWorld.random = Client::Render::Models::CrtRandom{1};
    m_sprites.Clear();
    Monster(3, ModelOf69, 0x45);
    auto particles = std::make_unique<Client::World::Particles>();
    GameCharacterPass between = Pass();
    between.particles = particles.get();
    between.originalFrames = 0;
    const auto drawn = Draw(between);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(LiveSprites().size(), 9u);
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}

// 0x50c0fb: el +0x376 0x46 pone un flare01 de 0,8 en blanco en su hueso 20, sin rand(), y sigue.
TEST_F(MonsterDrawingTest, The0x46LightsOneFlareOnItsBone20)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world = std::make_unique<Client::Scenes::WorldState>();
        SetUp();
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
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

// 0x510abd: el cuervo de +0x430 va con cualquier tipo (0x420df0), con el trapecio del dibujo.
TEST_F(MonsterDrawingTest, AMonsterWithARavenDrawsItAfterItsTail)
{
    Monster(3, Spider, 3).darkSpirit.emplace();
    const auto without = Draw(Pass());
    ASSERT_FALSE(without.HasValue());
    EXPECT_NE(without.Error().find("0x418320"), std::string::npos) << without.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));

    m_world = std::make_unique<Client::Scenes::WorldState>();
    SetUp();
    m_clothWorld.random = Client::Render::Models::CrtRandom{1};
    m_world->Map().terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Monster(3, Spider, 3).darkSpirit.emplace();
    const Client::World::TerrainPolygon everything{.everything = true};
    GameCharacterPass pass = Pass();
    pass.polygon = &everything;
    const auto drawn = Draw(pass);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_GT(m_batch.Vertices().size(), 6u);
}

// 0x5108b6..0x510a1c, el 0x16e: por cada hueso que no es dummy, fuera de los 15..20 y 27..32 (36 de los 48), el 0x5fd
// de 2,5 con la mezcla 1 (+0x1d6 en 0) o el 0x61e de 0,8 verde, y un rand() & 3; con 0 en las acciones 3 y 4, la
// partícula 0x5ca con la misma luz. Después salta la cadena de helpers.
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
    // Los huesos 14 y 21 quedan a los lados del primer hueco, y el 26 y el 33 del segundo.
    EXPECT_EQ(sprites[14].position, glm::vec3(150.0f, 150.0f, 24.0f));
    EXPECT_EQ(sprites[15].position, glm::vec3(150.0f, 150.0f, 31.0f));
    EXPECT_EQ(sprites[20].position, glm::vec3(150.0f, 150.0f, 36.0f));
    EXPECT_EQ(sprites[21].position, glm::vec3(150.0f, 150.0f, 43.0f));
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3 + 36));

    // Con nivel, el 0x61e verde; la acción 3 con un rand() & 3 en 0 pide la partícula 0x5ca, que necesita el pool.
    for (auto& sprite : m_sprites.Sprites())
        sprite.alive = false;
    m_batch.Begin(Client::Render::Pass3D{});
    sparks.level = 1;
    sparks.animation.action = 3;
    m_clothWorld.random = Client::Render::Models::CrtRandom{1};

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
    m_world->FaunaResources().RecordPoseKey(Spider, 7);
    GameCharacterPass pass = Pass();
    pass.originalFrames = 0;

    const auto result = Draw(pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_world->FaunaResources().PoseKey(Spider), 7);
    EXPECT_FLOAT_EQ(spider.alpha, 1.0f);
    EXPECT_TRUE(spider.bodyPose.empty());
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
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

// Las partes de los NPC del alta (0x5141e0): la biblioteca del monstruo con un triángulo propio por cada modelo de
// parte, corrido en x por el número del modelo menos 0x1d0, para ver el orden; `missing` no está cargado.
class PartModels final : public Client::Scenes::IModelLibrary
{
public:
    explicit PartModels(const Client::Scenes::IModelLibrary& body) : m_body(body)
    {
        for (std::uint16_t model = 0x1da; model <= 0x1f0; ++model)
        {
            Client::Data::Models::Model part = *body.Find(Spider);
            for (auto& vertex : part.meshes.front().vertices)
                vertex.position[0] += static_cast<float>(model - 0x1d0);
            m_parts.push_back(part);
        }
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t model) const override
    {
        if (model == missing)
            return nullptr;
        if (model >= 0x1da && model <= 0x1f0)
            return &m_parts[static_cast<std::size_t>(model - 0x1da)];
        return m_body.Find(model);
    }
    [[nodiscard]] const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return m_body.SkeletonOf(model);
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t model) const override
    {
        return m_body.MeshTextures(model);
    }

    std::uint16_t missing = 0;

private:
    const Client::Scenes::IModelLibrary& m_body;
    std::vector<Client::Data::Models::Model> m_parts;
};

// Un NPC del alta con sus partes: el tipo, su modelo y los modelos de +0x218, +0x238, +0x258, +0x278 y +0x298
// (MonsterKindTable.cpp, 0x5162dd..0x5162f8 y los casos de 0x5141e0).
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
        m_world->FaunaResources().Bind(&m_parts);
    }

    ActorState& Npc(const DressedNpc& npc)
    {
        ActorState& actor = Monster(6, npc.model, 0xffff);
        actor.byte88 = 4;
        for (std::size_t index = 0; index < npc.parts.size(); ++index)
            actor.body[index].model = npc.parts[index];
        return actor;
    }

    // La x del primer vértice de la parte `model` con el NPC en 150 y la escala 1.
    static float PartX(std::uint16_t model)
    {
        return 150.0f + static_cast<float>(model - 0x1d0);
    }

    PartModels m_parts{m_models};
};

// 0x50d3dc..0x50d4f5: sin +0x3e3, después de la sombra y de la luz de 0x50cc55, las partes de la 5 a la 0 (las botas,
// los guantes, el pantalón, la armadura, el casco y la cabeza), cada una con 0x60cf70: 0x60b3d0 con la luz de +0x3b4 y
// el nivel 0, sin brillo ni rand(). La chica de las pociones (253) y la cantinera (255) de Lorencia, y los NPC 230, 248
// y 250 en Lorencia, Devias y Noria: el dibujo no mira el mapa fuera de Chaos Castle.
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
            m_clothWorld.random = Client::Render::Models::CrtRandom{1};
            Npc(npc);

            const auto result = Draw(Pass());

            ASSERT_TRUE(result.HasValue()) << result.Error();
            std::vector<std::uint16_t> drawn;
            for (std::size_t index = npc.parts.size(); index-- > 0;)
                if (npc.parts[index] != 0xffff)
                    drawn.push_back(npc.parts[index]);
            // El cuerpo, la sombra y un triángulo por parte.
            ASSERT_EQ(m_batch.Vertices().size(), 6u + 3u * drawn.size());
            for (std::size_t index = 0; index < drawn.size(); ++index)
            {
                const auto& vertex = m_batch.Vertices()[6 + 3 * index];
                EXPECT_FLOAT_EQ(vertex.position.x, PartX(drawn[index])) << index;
                // La luz de +0x3b4 (el terreno más la propia), por el factor del nivel 0.
                ExpectColor(vertex, 0.3f, 0.6f, 0.9f, 1.0f);
            }
            // El de 0x50db57 y los dos de +0x38e en el lazo de las manos: las partes no sacan rand().
            EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(3));
        }
    }
}

// 0x60cf70 con el modo 1 (el NPC bajo el mouse): antes de cada parte, los dos pases de 0x60cd60 con los colores del NPC
// (+0x88 en 4: 0x60d069 y 0x60d0dd), la escala de +0x94 y 0,02 menos; con +0x78 & 0x4000, sin contorno (0x60cd74).
TEST_F(MonsterPartsTest, AHoveredNpcOutlinesEachPartBeforeIt)
{
    ActorState& npc = Npc(DressedNpcs[0]);
    GameCharacterPass pass = Pass();
    pass.hovered = 6;

    const auto result = Draw(pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // El cuerpo con sus dos contornos, la sombra y, por cada una de las tres partes, dos contornos y el cuerpo.
    ASSERT_EQ(m_batch.Vertices().size(), 12u + 3u * 9u);
    const auto& outer = m_batch.Vertices()[12];
    ExpectColor(outer, 0.02f, 0.1f, 0.0f, 1.0f);
    ExpectColor(m_batch.Vertices()[15], 0.16f, 0.7f, 0.0f, 1.0f);
    // El vértice (1, 0, 0) de la bota (0x1f0) a 1,2 y a 1,18 alrededor del hueso 0.
    EXPECT_FLOAT_EQ(m_batch.Vertices()[13].position.x, 150.0f + 1.2f * (1.0f + PartX(0x1f0) - 150.0f));
    EXPECT_FLOAT_EQ(m_batch.Vertices()[16].position.x, 150.0f + 1.18f * (1.0f + PartX(0x1f0) - 150.0f));
    EXPECT_FLOAT_EQ(m_batch.Vertices()[18].position.x, PartX(0x1f0));

    m_batch.Begin(Client::Render::Pass3D{});
    npc.flags78 |= 0x4000;
    ASSERT_TRUE(Draw(pass).HasValue());
    // Sin contornos (tampoco el del cuerpo) ni sombra (0x50be6e): el cuerpo y las tres partes.
    EXPECT_EQ(m_batch.Vertices().size(), 3u + 3u * 3u);
}

// La cabeza (+0x1f8) va última. Un modelo que no se cargó es un registro en 0 sin mallas: no dibuja nada. Uno fuera de
// los 0x449d registros que 0x60dbe0 pone en 0 (la cabeza que deja el nombre de 34 letras del 231, P-C352) haría
// escribir al original fuera de ellos: corta después de las partes que ya dibujó.
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

// 0x50c972..0x50cc29: el WORD +0x1f2 pisado por un nombre largo (P-C352) no hace nada sin la guerra de gremios ni el
// duelo ([0x576796d], [0x576796f] y [0x78abad8] en 0): el NPC se dibuja con sus partes, sin rand() de más.
TEST_F(MonsterPartsTest, ALongNameOverTheGuildWordDrawsAsUsual)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        m_batch.Begin(Client::Render::Pass3D{});
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
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
    // El mapa 15: (15 − 9) / 3 = 2, por 0,5.
    EXPECT_EQ(Client::Scenes::HangingLightOf(actor, terrain, 15), glm::vec3(1.0f, 0.1f, 0.1f));
    // El 10 (el 1 de Blood Castle): el cociente da 0 y queda la del terreno.
    EXPECT_EQ(Client::Scenes::HangingLightOf(actor, terrain, 10), terrain + actor.light);
    // Lorencia y Devias: la del terreno más la propia.
    EXPECT_EQ(Client::Scenes::HangingLightOf(actor, terrain, 0), terrain + actor.light);
    EXPECT_EQ(Client::Scenes::HangingLightOf(actor, terrain, 2), terrain + actor.light);
    actor.word376 = 0x5a;
    EXPECT_EQ(Client::Scenes::HangingLightOf(actor, terrain, 15), terrain + actor.light);
    actor.pk = 6;
    EXPECT_EQ(Client::Scenes::HangingLightOf(actor, terrain, 0), glm::vec3(1.0f, 0.1f, 0.1f));
}

TEST(MonsterDecorations, EachListedMonsterCutsWithItsBranch)
{
    EXPECT_TRUE(Client::Scenes::CheckMonsterDecorations(3).HasValue());
    EXPECT_TRUE(Client::Scenes::CheckMonsterDecorations(0xffff).HasValue());
    EXPECT_NE(Client::Scenes::CheckMonsterDecorations(0x45).Error().find("0x50bf98"), std::string::npos);
    EXPECT_NE(Client::Scenes::CheckMonsterDecorations(0x50).Error().find("0x50c671"), std::string::npos);
    EXPECT_NE(Client::Scenes::CheckMonsterDecorations(0x85).Error().find("0x50c83b"), std::string::npos);
    // El camino del 0x20a corta también con los pases de brillo, que DrawGameMonster dibuja.
    constexpr std::array<std::uint16_t, 10> Shining{0x26, 0x2b, 0x34, 0x3b, 0x43, 0x4e, 0x53, 0x35, 0x36, 0x87};
    for (const std::uint16_t shining : Shining)
        EXPECT_TRUE(Client::Scenes::MonsterShines(shining)) << shining;
    constexpr std::array<std::uint16_t, 7> Others{0x45, 0x46, 0x4d, 0x54, 0x2a, 0x86, 0x88};
    for (const std::uint16_t other : Others)
        EXPECT_FALSE(Client::Scenes::MonsterShines(other)) << other;
    EXPECT_TRUE(Client::Scenes::WearsClothOnTheMesh(0x8f));
    EXPECT_FALSE(Client::Scenes::WearsClothOnTheMesh(0x58));
}
} // namespace
