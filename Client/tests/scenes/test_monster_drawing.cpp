#include "Scenes/MonsterDrawing.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
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
    std::array<Client::Render::TextureSlot, 1> m_texture{Client::Render::TextureSlot{0x300}};
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
    // La tela sobre la malla corta antes de todo; un helper de la cadena, después de lo que cuelga; lo de 0x50bf45,
    // después de la sombra.
    constexpr std::array cases{Case{Spider, 0x59, "0x50bae7", 0, 0}, Case{HelperType, 0x20, "0x4271d0", 6, 3},
                               Case{Spider, 0x26, "0x50c671", 6, 0}, Case{0x18e, 0x21, "0x600654", 0, 0},
                               Case{Spider, 0x37, "0x50d599", 6, 0}};
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
