#include "Scenes/MonsterDrawing.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
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
// del 0xc5 lo dibuja 0x438fe0 (las dos mallas) y después corta su helper, 0x43c150.
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

        ASSERT_FALSE(gate.HasValue());
        EXPECT_NE(gate.Error().find("0x43c150"), std::string::npos) << gate.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 12u);
        ExpectColor(m_batch.Vertices()[0], 0.1f, 0.01f, 0.0f, 1.0f);
        ExpectColor(m_batch.Vertices()[3], 0.7f, 0.07f, 0.0f, 1.0f);
        TearDown();

        m_world = std::make_unique<Client::Scenes::WorldState>();
        SetUp();
        m_world->Runtime().map = map;
        Monster(4, Spider, 3);
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
                               Case{Spider, 0x26, "0x50c671", 6, 0}, Case{0x18e, 0x21, "0x4ee770", 0, 4},
                               Case{0x16e, 0x22, "0x5108b6", 6, 3}};
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
    EXPECT_TRUE(Client::Scenes::WearsClothOnTheMesh(0x8f));
    EXPECT_FALSE(Client::Scenes::WearsClothOnTheMesh(0x58));
}
} // namespace
