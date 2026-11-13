#include "Scenes/GameCharacters.h"

#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GuildMarks.h"
#include "Scenes/GuildState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/TerrainLighting.h"
#include "World/Sprites.h"
#include "World/WorldTerrain.h"

#include "support/FakeTextureStore.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// 0x50cc29..0x50d2aa y 0x50db57..0x50dc5e: vectores literales del ASM c32bbd5f,
// sin ejecutar el original. Una bota y una sombra sintéticas vuelven observable el color y el contorno.
namespace
{
using Client::Scenes::ActorState;
using Client::Scenes::GameCharacterPass;

constexpr std::uint16_t BootsModel = 0x1800;
constexpr std::uint16_t AngelModel = 0x1f7;

// El estado del rand() de la CRT con la semilla 1 después de `count` llamadas.
std::uint32_t AfterRandoms(std::size_t count)
{
    Client::Render::Models::CrtRandom random{1};
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random.State();
}

// Cerrar el registro propio del héroe gasta 3848 rand() (0x50ccb5..0x50d219).
constexpr std::size_t OwnRecordRandoms = 3848;

void ExpectSameCloths(std::span<const Client::Render::Models::Cloth> actual,
                      std::span<const Client::Render::Models::Cloth> expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t cloth = 0; cloth < actual.size(); ++cloth)
    {
        SCOPED_TRACE(cloth);
        EXPECT_EQ(actual[cloth].Flags(), expected[cloth].Flags());
        EXPECT_EQ(actual[cloth].Wind(), expected[cloth].Wind());
        ASSERT_EQ(actual[cloth].Particles().size(), expected[cloth].Particles().size());
        for (std::size_t particle = 0; particle < actual[cloth].Particles().size(); ++particle)
        {
            SCOPED_TRACE(particle);
            const auto& a = actual[cloth].Particles()[particle];
            const auto& b = expected[cloth].Particles()[particle];
            EXPECT_EQ(a.position, b.position);
            EXPECT_EQ(a.velocity, b.velocity);
            EXPECT_EQ(a.force, b.force);
            EXPECT_EQ(a.anchored, b.anchored);
            EXPECT_EQ(a.corrections, b.corrections);
            EXPECT_EQ(a.correction, b.correction);
        }
        ASSERT_EQ(actual[cloth].Spheres().size(), expected[cloth].Spheres().size());
        for (std::size_t sphere = 0; sphere < actual[cloth].Spheres().size(); ++sphere)
        {
            EXPECT_EQ(actual[cloth].Spheres()[sphere].world, expected[cloth].Spheres()[sphere].world);
        }
    }
}

void ExpectSameGeometry(const Client::Render::ModelBatch& actual, const Client::Render::ModelBatch& expected)
{
    ASSERT_EQ(actual.Vertices().size(), expected.Vertices().size());
    for (std::size_t vertex = 0; vertex < actual.Vertices().size(); ++vertex)
    {
        SCOPED_TRACE(vertex);
        const Client::Render::ModelVertex& a = actual.Vertices()[vertex];
        const Client::Render::ModelVertex& b = expected.Vertices()[vertex];
        EXPECT_EQ(a.position, b.position);
        EXPECT_EQ(a.uv, b.uv);
        EXPECT_EQ(a.color.red, b.color.red);
        EXPECT_EQ(a.color.green, b.color.green);
        EXPECT_EQ(a.color.blue, b.color.blue);
        EXPECT_EQ(a.color.alpha, b.color.alpha);
    }
    ASSERT_EQ(actual.Commands().size(), expected.Commands().size());
    for (std::size_t command = 0; command < actual.Commands().size(); ++command)
    {
        EXPECT_EQ(actual.Commands()[command].state, expected.Commands()[command].state);
        EXPECT_EQ(actual.Commands()[command].operation, expected.Commands()[command].operation);
        EXPECT_EQ(actual.Commands()[command].firstVertex, expected.Commands()[command].firstVertex);
        EXPECT_EQ(actual.Commands()[command].vertexCount, expected.Commands()[command].vertexCount);
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

void ExpectSameSprites(std::span<const Client::World::Sprite> actual, std::span<const Client::World::Sprite> expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t sprite = 0; sprite < actual.size(); ++sprite)
    {
        SCOPED_TRACE(sprite);
        EXPECT_EQ(actual[sprite].texture, expected[sprite].texture);
        EXPECT_EQ(actual[sprite].blend, expected[sprite].blend);
        EXPECT_EQ(actual[sprite].position, expected[sprite].position);
        EXPECT_EQ(actual[sprite].scale, expected[sprite].scale);
        EXPECT_EQ(actual[sprite].light, expected[sprite].light);
        EXPECT_EQ(actual[sprite].rotation, expected[sprite].rotation);
        EXPECT_EQ(actual[sprite].alpha, expected[sprite].alpha);
        EXPECT_EQ(actual[sprite].seen, expected[sprite].seen);
    }
}

class CharacterModels final : public Client::Scenes::IModelLibrary
{
public:
    CharacterModels()
    {
        using namespace Client::Render::Models;
        SkeletonBone bone;
        bone.parent = -1;
        bone.actions = {{BoneKey{}}};
        bone.actions[0][0].rotation = AngleQuaternion({0.0f, 0.0f, 0.0f});
        // El hueso k en (0, 0, k): lo que cuelga deja ver de qué hueso del cuerpo sale. Las mallas solo
        // usan el 0.
        m_skeleton.bones.assign(48, bone);
        for (std::size_t index = 0; index < m_skeleton.bones.size(); ++index)
            m_skeleton.bones[index].actions[0][0].position = {0.0f, 0.0f, static_cast<float>(index)};
        m_skeleton.actions = {ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "boot.jpg";
        m_model.meshes = {mesh};
        // El Angel de Chaos Castle con dos mallas: su pase se distingue del de las botas por los vértices.
        m_angel.meshes = {mesh, mesh};
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t model) const override
    {
        if (model == AngelModel)
            return &m_angel;
        return model == BootsModel || model == 0x20b || HasHand(model) ? &m_model : nullptr;
    }
    [[nodiscard]] const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == 0x20a || (handSkeletons && HasHand(model)) ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return m_texture;
    }

    bool handSkeletons = true;

private:
    static bool HasHand(std::uint16_t model)
    {
        return model == 0x212 || model == 0x220 || model == 0xc19 || model == 0xc17 || model == 0xe20;
    }

    Client::Render::Models::Skeleton m_skeleton;
    Client::Data::Models::Model m_model;
    Client::Data::Models::Model m_angel;
    std::array<Client::Render::TextureSlot, 2> m_texture{Client::Render::TextureSlot{0x300},
                                                         Client::Render::TextureSlot{0x300}};
};

class GameCharacterRenderTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_world = std::make_unique<Client::Scenes::WorldState>();
        m_world->Runtime().map = 0;
        m_world->Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        m_world->Map().terrain.attributes->cells.fill(0);
        m_light = std::make_unique<Client::World::TerrainLighting>();
        m_light->cells.fill({0.2f, 0.4f, 0.6f});
        m_batch.Begin(Client::Render::Pass3D{});
    }

    ActorState& Player(std::size_t slot)
    {
        ActorState& actor = m_world->Actors()[slot];
        actor.alive = true;
        actor.seen = true;
        actor.type = 0x20a;
        actor.word376 = 0xffff;
        actor.head = 0xffff;
        // 0x511290: sin gremio (+0x1f2 en −1).
        actor.word1f2 = 0xffff;
        actor.scale = 1.0f;
        actor.alpha = 1.0f;
        actor.safeZone = 1;
        actor.position = {150.0f, 150.0f, 10.0f};
        actor.light = {0.1f, 0.2f, 0.3f};
        actor.wings.model = 0xffff;
        actor.pet.model = 0xffff;
        for (auto& part : actor.body)
            part.model = 0xffff;
        for (auto& part : actor.hands)
            part.model = 0xffff;
        // 0x5126b2..0x5126d5: las manos de un 0x20a cuelgan de los huesos 33 y 42.
        actor.hands[0].bone = 0x21;
        actor.hands[1].bone = 0x2a;
        actor.body.back().model = BootsModel;
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
                .worldModels = &m_models};
    }

    std::unique_ptr<Client::Scenes::WorldState> m_world;
    std::unique_ptr<Client::World::TerrainLighting> m_light;
    Client::Render::Models::ClothWorld m_clothWorld;
    Client::World::FrameSprites m_sprites;
    Client::Scenes::FaunaDrawCarry m_carry;
    CharacterModels m_models;
    Client::Render::ModelBatch m_batch;
};

TEST_F(GameCharacterRenderTest, OtherPlayersUseTheSuppliedDynamicTerrainLightAndTheirOwnLight)
{
    ActorState& actor = Player(7);
    m_world->Runtime().hero = 42;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    const auto color = m_batch.Vertices()[3].color;
    EXPECT_FLOAT_EQ(color.red, 0.3f);
    EXPECT_FLOAT_EQ(color.green, 0.6f);
    EXPECT_FLOAT_EQ(color.blue, 0.9f);
    EXPECT_FLOAT_EQ(actor.drawnLight.x, 0.3f);
    EXPECT_FLOAT_EQ(actor.drawnLight.y, 0.6f);
    EXPECT_FLOAT_EQ(actor.drawnLight.z, 0.9f);
    EXPECT_EQ(m_clothWorld.random.State(), 0x0029e2c0u); // semilla 1, un rand de 0x50db57
}

TEST_F(GameCharacterRenderTest, TheHeroBodyAppliesEveryAdjustmentBitInAssemblyOrder)
{
    struct Case
    {
        std::uint8_t bits;
        glm::vec3 expected;
    };
    constexpr std::array cases{Case{0, {1.0f, 1.0f, 1.0f}}, Case{1, {0.9f, 0.5f, 0.5f}},
                               Case{2, {0.5f, 0.9f, 0.5f}}, Case{4, {0.9f, 0.5f, 0.5f}},
                               Case{8, {0.9f, 0.5f, 0.5f}}, Case{15, {0.3645f, 0.1125f, 0.0625f}},
                               Case{16, {1.0f, 1.0f, 1.0f}}};
    ActorState& hero = Player(0);
    for (const Case& value : cases)
    {
        SCOPED_TRACE(static_cast<int>(value.bits));
        m_world->OwnCharacter().speedAdjustBits = value.bits;
        m_batch.Begin(Client::Render::Pass3D{});
        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 6u);
        const auto color = m_batch.Vertices()[3].color;
        EXPECT_FLOAT_EQ(color.red, value.expected.x);
        EXPECT_FLOAT_EQ(color.green, value.expected.y);
        EXPECT_FLOAT_EQ(color.blue, value.expected.z);
        // El RGB persistente vuelve al del terreno antes de lo que cuelga, no al del cuerpo del héroe.
        EXPECT_FLOAT_EQ(hero.drawnLight.x, 0.3f);
        EXPECT_FLOAT_EQ(hero.drawnLight.y, 0.6f);
        EXPECT_FLOAT_EQ(hero.drawnLight.z, 0.9f);
    }
}

TEST_F(GameCharacterRenderTest, TheHeroClosesItsOwnRecordBeforeTheUnconditionalAttachmentRandom)
{
    Player(0);

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_world->Runtime().ownRecordDepth, 0);
    EXPECT_EQ(m_clothWorld.random.State(), 0x46d18088u); // semilla 1, 3848 + 1 rand
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(OwnRecordRandoms + 1));
}

TEST_F(GameCharacterRenderTest, AnOuterOwnRecordAccessSuppressesOnlyThe3848CloseRandoms)
{
    Player(0);
    m_world->Runtime().ownRecordDepth = 1;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_world->Runtime().ownRecordDepth, 1);
    EXPECT_EQ(m_clothWorld.random.State(), 0x0029e2c0u);
}

TEST_F(GameCharacterRenderTest, EitherPickingSlotPassesTheOutlineFlagToTheBodyParts)
{
    Player(7);
    m_world->Runtime().hero = 42;
    for (const bool target : {false, true})
    {
        GameCharacterPass pass = Pass();
        pass.hovered = target ? -1 : 7;
        pass.targeted = target ? 7 : -1;
        m_batch.Begin(Client::Render::Pass3D{});
        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Commands().size(), 3u);
        EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Shadow);
        EXPECT_EQ(m_batch.Commands()[1].state.blend, Client::Render::ModelBlend::Additive);
        EXPECT_EQ(m_batch.Commands()[1].vertexCount, 6u);
        EXPECT_EQ(m_batch.Commands()[2].state.blend, Client::Render::ModelBlend::Opaque);
    }
}

TEST_F(GameCharacterRenderTest, AGamePlayerWithLessThanHalfAlphaHasNoShadowAndFadesItsBody)
{
    Player(0).alpha = 0.25f;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 3u);
    ASSERT_EQ(m_batch.Commands().size(), 1u);
    EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Alpha);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[0].color.alpha, 0.25f);
}

// 0x50ddde..0x50de60 en la zona segura: la espada derecha cuelga del hueso de las alas (47) por el camino
// de la bandera, con la raíz de 0x5fc8f0 y 0x5fca90 (70, 0, 90 y −20, 5, 40) y el origen en el personaje,
// y del color de lo que cuelga (+0x3b4). Los rand(): el de 0x50db57, el cierre de +0x38e en 0x509340 y el
// de la luz de 0x509a92. A la espalda no corre el lazo de las manos.
TEST_F(GameCharacterRenderTest, ABackSwordHangsFromTheWingsBoneWithTheFlagPlacement)
{
    ActorState& actor = Player(0);
    actor.hands[0].model = 0x212;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 9u);
    // El vértice 0 de la espada está en su raíz: (−20, 5, 40) sobre el hueso 47, en (0, 0, 47).
    EXPECT_EQ(m_batch.Vertices()[6].position, glm::vec3(130.0f, 155.0f, 97.0f));
    EXPECT_FLOAT_EQ(m_batch.Vertices()[6].color.red, 0.3f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[6].color.green, 0.6f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[6].color.blue, 0.9f);
    EXPECT_EQ(actor.wings.bone, 0x2f);
    EXPECT_EQ(actor.byte38eAccesses, 0);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(OwnRecordRandoms + 3));
}

TEST_F(GameCharacterRenderTest, HangingModelsUseTheirSecondColorAndTheSharedRandom)
{
    Client::Scenes::DrawnCharacter character;
    character.appearance.bodyParts.back().model = BootsModel;
    character.appearance.rightHand = Client::GameLogic::WornItem{0xc17, 0, false, false};
    character.light = {0.1f, 0.2f, 0.3f};
    character.hangingLight = glm::vec3{0.8f, 0.7f, 0.6f};
    character.originalRandom = &m_clothWorld.random;

    Client::Scenes::DrawCharacter(m_batch, m_models, character, {});

    ASSERT_EQ(m_batch.Vertices().size(), 9u);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[3].color.red, 0.1f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[3].color.green, 0.2f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[3].color.blue, 0.3f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[6].color.red, 0.8f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[6].color.green, 0.7f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[6].color.blue, 0.6f);
    EXPECT_EQ(m_clothWorld.random.State(), 0xc823f683u); // semilla 1, los dos rand de 0x607b60
}

TEST_F(GameCharacterRenderTest, BetweenOriginalFramesTheDrawDoesNotRepeatTheRecordOrAttachmentRandoms)
{
    Player(0);
    GameCharacterPass pass = Pass();
    pass.originalFrames = 0;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
}

TEST_F(GameCharacterRenderTest, MultipleOriginalFramesKeepOnlyTheLastGeometryButConsumeBothFrames)
{
    Player(0);
    GameCharacterPass pass = Pass();
    pass.originalFrames = 2;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_clothWorld.random.State(), 0x5fc9f4d3u); // semilla 1, dos veces 3848 + 1
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
}

TEST_F(GameCharacterRenderTest, The401stPoolRecordIsNotPartOfThe400CharacterDraw)
{
    Player(400);

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}

// Fuera de la zona segura corre el lazo de las manos (0x50e298): la espada derecha cuelga del hueso 33 con
// el origen en el hueso (0x4ec190). Los rand(): 0x50db57, el +0x38e antes de cada mano (dos), el de
// 0x509340 y el de la luz de 0x509a92.
TEST_F(GameCharacterRenderTest, OutsideTheSafeZoneTheSwordHangsFromTheRightHandBone)
{
    ActorState& actor = Player(0);
    actor.safeZone = 0;
    actor.hands[0].model = 0x212;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 9u);
    EXPECT_EQ(m_batch.Vertices()[6].position, glm::vec3(150.0f, 150.0f, 43.0f));
    EXPECT_EQ(actor.hands[0].animation.action, 0);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(OwnRecordRandoms + 5));
}

// Entre frames del original, lo que cuelga se dibuja como quedó, sobre una copia del personaje y del
// rand(): ni el hueso de las alas ni los accesos a +0x38e ni el rand() cambian.
TEST_F(GameCharacterRenderTest, BetweenOriginalFramesTheHangingIsDrawnWithoutChangingTheActor)
{
    ActorState& actor = Player(0);
    actor.hands[0].model = 0x212;
    GameCharacterPass pass = Pass();
    pass.originalFrames = 0;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 9u);
    EXPECT_EQ(m_batch.Vertices()[6].position, glm::vec3(130.0f, 155.0f, 97.0f));
    EXPECT_EQ(actor.wings.bone, 0);
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}

// Sin el buffer global de la pose, lo que cuelga corta en 0x509340 con el cuerpo ya dibujado.
TEST_F(GameCharacterRenderTest, AHangingItemWithoutThePoseBufferStopsAfterTheBody)
{
    Player(0).hands[0].model = 0x212;
    GameCharacterPass pass = Pass();
    pass.poseCarry = nullptr;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x509340"), std::string::npos) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(OwnRecordRandoms + 1));
}

TEST_F(GameCharacterRenderTest, AHiddenHeroReturnsBeforeTheRecordAndTheUnconditionalRandom)
{
    Player(0).flags7c = 0x10000000;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}

TEST_F(GameCharacterRenderTest, ACalledHeroWithControlBitFourKeepsTheOriginalUndrawnMarks)
{
    ActorState& hero = Player(0);
    hero.controlCode = 4;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(hero.field13c, 1000.0f);
    EXPECT_EQ(hero.field148, 1.0f);
    EXPECT_EQ(hero.field158, 1.0f);
    EXPECT_EQ(hero.field168, 1.0f);
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}

TEST_F(GameCharacterRenderTest, TheRavenNeedsTheTrapeziumOfTheDrawAfterTheEffects)
{
    Player(0).darkSpirit.emplace();

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x418320"), std::string::npos) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_clothWorld.random.State(), 0x46d18088u);
}

TEST_F(GameCharacterRenderTest, TheSharedRandomAdvancesOnlyForThe607b60CasesThatUseIt)
{
    struct Case
    {
        std::uint16_t model;
        std::uint32_t state;
    };
    constexpr std::array cases{Case{0x212, 1}, Case{0x220, 0x0029e2c0}, Case{0xc19, 0x0029e2c0},
                               Case{0xc17, 0xc823f683}, Case{0xe20, 0xc823f683}};
    for (const Case& value : cases)
    {
        SCOPED_TRACE(value.model);
        Client::Scenes::DrawnCharacter character;
        character.appearance.rightHand = Client::GameLogic::WornItem{value.model, 0, false, false};
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
        character.originalRandom = &m_clothWorld.random;
        m_batch.Begin(Client::Render::Pass3D{});

        Client::Scenes::DrawCharacter(m_batch, m_models, character, {});

        EXPECT_EQ(m_clothWorld.random.State(), value.state);
    }
}

TEST_F(GameCharacterRenderTest, AnUnportedMonsterBranchStopsBeforeThePlayerRecordAndItsRandoms)
{
    Player(0).word376 = 0x59;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x50bad8"), std::string::npos) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}

TEST_F(GameCharacterRenderTest, TheHiddenClothFlagDoesNotRunTheClothSimulationBeforeItsBoundary)
{
    ActorState& hero = Player(0);
    hero.characterClass = 4;
    hero.flags78 = 0x4000;
    std::array<Client::Scenes::CharacterCloth, 401> cloths;
    GameCharacterPass pass = Pass();
    pass.cloths = cloths;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    // Con 0x4000 y sin 4 ni 0x2000000, 0x50fba4 no corre: el dibujo termina sin la tela.
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_clothWorld.random.State(), 0x46d18088u);
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
}

TEST_F(GameCharacterRenderTest, TheOverloadWithoutDrawingResourcesAlsoExcludesThe401stRecord)
{
    Player(400);

    const auto result = Client::Scenes::DrawGameCharacters(m_world->Actors(), m_world->Runtime().hero,
                                                           m_world->Map().terrain.attributes->cells);

    ASSERT_TRUE(result.HasValue()) << result.Error();
}

TEST_F(GameCharacterRenderTest, TheAttachmentInitializerConsumesItsRandomBeforeAMissingItemPose)
{
    Client::Scenes::DrawnCharacter character;
    character.appearance.rightHand = Client::GameLogic::WornItem{0xc17, 0, false, false};
    character.originalRandom = &m_clothWorld.random;
    m_models.handSkeletons = false;

    Client::Scenes::DrawCharacter(m_batch, m_models, character, {});

    EXPECT_EQ(m_clothWorld.random.State(), 0xc823f683u);
    EXPECT_EQ(m_batch.Vertices().size(), 3u);
}

TEST_F(GameCharacterRenderTest, HiddenAttachmentsReturnBeforeTheInitializerAndItsRandoms)
{
    Client::Scenes::DrawnCharacter character;
    character.appearance.rightHand = Client::GameLogic::WornItem{0xc17, 0, false, false};
    character.originalRandom = &m_clothWorld.random;
    character.objectFlags = 0x4000;

    Client::Scenes::DrawCharacter(m_batch, m_models, character, {});

    EXPECT_EQ(m_clothWorld.random.State(), 1u);
    EXPECT_EQ(m_batch.Vertices().size(), 3u);
}

TEST_F(GameCharacterRenderTest, AnUnselectedLeftSwordIsNotDrawnInTheSafeZone)
{
    Player(0).hands[1].model = 0x212;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_clothWorld.random.State(), 0x46d18088u);
}

// Los vértices de la tela con el color `color` (0x455930 pone el de 0x50ba30 en cada uno).
std::size_t ClothVerticesIn(const Client::Render::ModelBatch& batch, const glm::vec3& color)
{
    std::size_t count = 0;
    for (const Client::Render::ModelVertex& vertex : batch.Vertices())
    {
        if (vertex.color.red == color.r && vertex.color.green == color.g && vertex.color.blue == color.b &&
            vertex.color.alpha == 1.0f)
            ++count;
    }
    return count;
}

// 0x50c929..0x50cc29 (CL9 hito 7, fase 2): sin duelo, la clase 1 no lleva tela y la 3 con +0x3e3 tampoco. Con el
// duelo en curso, el rival (su clave y su nombre en 0x78abadc y 0x78abae0) la lleva en (1; 0,2; 0) aunque sea de la
// clase 1 y esté en Chaos Castle (0x12), y el héroe publicado, en (0; 0,2; 1). En Lorencia y Devias.
TEST_F(GameCharacterRenderTest, TheDuelDressesTheRivalInRedAndTheHeroInBlue)
{
    constexpr glm::vec3 Rival{1.0f, 0.2f, 0.0f};
    constexpr glm::vec3 Hero{0.0f, 0.2f, 1.0f};
    for (const std::int32_t map : {0, 2, 0x12})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        ActorState& player = Player(0);
        player.characterClass = 1;
        player.key = 0x0456;
        for (std::size_t at = 0; at < player.name.size(); ++at)
            player.name[at] = at < 6 ? static_cast<std::byte>("Brezal"[at]) : std::byte{0};
        // El héroe publicado es otro (sin dibujar).
        m_world->Runtime().hero = 5;
        m_world->Actors()[5].key = 0x0021;
        Client::Scenes::DuelState& duel = m_world->Runtime().social.duel;
        duel = {};
        auto cloths = std::make_unique<std::array<Client::Scenes::CharacterCloth, 401>>();
        GameCharacterPass pass = Pass();
        pass.cloths = *cloths;
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
        EXPECT_TRUE((*cloths)[0].Cloths().empty());

        player.characterClass = 3;
        player.byte3e3 = 1;
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
        EXPECT_TRUE((*cloths)[0].Cloths().empty());

        player.characterClass = 1;
        player.byte3e3 = 0;
        duel.active = 1;
        duel.rivalKey = 0x0456;
        for (std::size_t at = 0; at < 6; ++at)
            duel.rivalName[at] = static_cast<std::byte>("Brezal"[at]);
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
        ASSERT_EQ((*cloths)[0].Cloths().size(), 1u);
        EXPECT_GT(ClothVerticesIn(m_batch, Rival), 0u);
        EXPECT_EQ(ClothVerticesIn(m_batch, Hero), 0u);

        // El mismo, publicado: la segunda comparación gana.
        m_world->Runtime().hero = 0;
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
        EXPECT_EQ(ClothVerticesIn(m_batch, Rival), 0u);
        EXPECT_GT(ClothVerticesIn(m_batch, Hero), 0u);

        // Otro nombre con la misma clave no es el rival.
        m_world->Runtime().hero = 5;
        duel.rivalName[0] = std::byte{'Z'};
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
        EXPECT_EQ(ClothVerticesIn(m_batch, Rival), 0u);
    }
}

// 0x50c972..0x50caaa (CL9 hito 8): con la guerra (0x576796d) y +0x1f2 distinto de −1, el del gremio del héroe lleva la
// tela del color de su equipo ((1; 0,2; 0) con 0x576796e en 0, (0; 0,2; 1) con 1) y el del rival (0x5767804), la del
// otro, aunque sea de la clase 1 y esté en Chaos Castle (0x12); sin la guerra o sin gremio, no. Con el héroe sin
// gremio, strcmp lee el byte 9 del nombre del miembro 74 (Q-C1281). Un índice fuera de la tabla corta. Gremios
// inventados.
TEST_F(GameCharacterRenderTest, TheGuildWarDressesBothGuildsInTheirTeamColors)
{
    constexpr glm::vec3 Red{1.0f, 0.2f, 0.0f};
    constexpr glm::vec3 Blue{0.0f, 0.2f, 1.0f};
    constexpr std::uint16_t NoGuild = 0xffff;
    for (const std::int32_t map : {0, 2, 0x12})
    {
        SCOPED_TRACE(map);
        Client::Scenes::WorldRuntime& runtime = m_world->Runtime();
        runtime.map = map;
        Client::Scenes::GuildState& guild = runtime.guild;
        guild.table[3].guildName = {'T', 'a', 'l', 'a', 's'};
        guild.table[4].guildName = {'O', 'm', 'b', 'u'};
        guild.table[7].guildName = {};
        guild.rival = {'O', 'm', 'b', 'u'};
        guild.members[74].name[9] = '\0';
        guild.warType = 0;
        guild.warTeam = 0;
        runtime.social.duel = {};
        ActorState& player = Player(0);
        player.characterClass = 1;
        player.word1f2 = 3;
        // El héroe publicado es otro (sin dibujar), del mismo gremio.
        runtime.hero = 5;
        m_world->Actors()[5].key = 0x0021;
        m_world->Actors()[5].word1f2 = 3;
        auto cloths = std::make_unique<std::array<Client::Scenes::CharacterCloth, 401>>();
        GameCharacterPass pass = Pass();
        pass.cloths = *cloths;
        // Con +0x1f2 en 3 y las claves en −1, 0x50d533 también pide la marca (0x447b00 encuentra la entrada 0).
        auto store = std::make_unique<Client::Tests::FakeTextureStore>();
        auto marks = std::make_unique<Client::Scenes::GuildMarkTextures>(*store);
        pass.guildMarks = marks.get();
        const auto draw = [&]()
        {
            m_batch.Begin(Client::Render::Pass3D{});
            return Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);
        };
        ASSERT_TRUE(draw().HasValue());
        EXPECT_TRUE((*cloths)[0].Cloths().empty());

        guild.warType = 1;
        ASSERT_TRUE(draw().HasValue());
        ASSERT_EQ((*cloths)[0].Cloths().size(), 1u);
        EXPECT_GT(ClothVerticesIn(m_batch, Red), 0u);
        EXPECT_EQ(ClothVerticesIn(m_batch, Blue), 0u);

        guild.warTeam = 1;
        ASSERT_TRUE(draw().HasValue());
        EXPECT_EQ(ClothVerticesIn(m_batch, Red), 0u);
        EXPECT_GT(ClothVerticesIn(m_batch, Blue), 0u);

        // El rival, con el equipo 1, de rojo.
        player.word1f2 = 4;
        ASSERT_TRUE(draw().HasValue());
        EXPECT_GT(ClothVerticesIn(m_batch, Red), 0u);
        EXPECT_EQ(ClothVerticesIn(m_batch, Blue), 0u);

        // Sin gremio, la clase 1 no la lleva.
        player.word1f2 = NoGuild;
        ASSERT_TRUE(draw().HasValue());
        EXPECT_EQ(ClothVerticesIn(m_batch, Red), 0u);
        EXPECT_EQ(ClothVerticesIn(m_batch, Blue), 0u);

        // El héroe sin gremio: "" (el byte 9 del miembro 74 en 0) es el nombre vacío de la entrada 7.
        m_world->Actors()[5].word1f2 = NoGuild;
        player.word1f2 = 7;
        ASSERT_TRUE(draw().HasValue());
        EXPECT_GT(ClothVerticesIn(m_batch, Blue), 0u);
        guild.members[74].name[9] = 'x';
        ASSERT_TRUE(draw().HasValue());
        EXPECT_EQ(ClothVerticesIn(m_batch, Blue), 0u);

        player.word1f2 = 2000;
        const auto cut = draw();
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x50c9b8"), std::string::npos);
        player.word1f2 = 3;
        m_world->Actors()[5].word1f2 = 2001;
        const auto heroCut = draw();
        ASSERT_FALSE(heroCut.HasValue());
        EXPECT_NE(heroCut.Error().find("0x50c9cf"), std::string::npos);
        m_world->Actors()[5].word1f2 = 3;
    }
}

TEST_F(GameCharacterRenderTest, ZeroOriginalFramesDoNotCreateFirstUseClassThreeOrFourCloths)
{
    using Client::Render::Models::OriginalFramePace;
    for (const std::uint8_t characterClass : std::array<std::uint8_t, 2>{3, 4})
    {
        for (const auto pace : {OriginalFramePace::ByTheClock, OriginalFramePace::OncePerDraw})
        {
            SCOPED_TRACE(static_cast<int>(characterClass));
            SCOPED_TRACE(static_cast<int>(pace));
            Player(0).characterClass = characterClass;
            m_clothWorld.random = Client::Render::Models::CrtRandom{1};
            std::array<Client::Scenes::CharacterCloth, 401> cloths;
            Client::World::FrameSprites sprites;
            GameCharacterPass pass = Pass();
            pass.originalFrames = 0;
            pass.pace = pace;
            pass.cloths = cloths;
            pass.sprites = &sprites;
            m_batch.Begin(Client::Render::Pass3D{});

            const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(cloths[0].Count(), 0);
            EXPECT_TRUE(cloths[0].Cloths().empty());
            EXPECT_TRUE(cloths[0].Flares().empty());
            EXPECT_FALSE(sprites.Any());
            EXPECT_EQ(m_clothWorld.random.State(), 1u);
            EXPECT_EQ(m_batch.Vertices().size(), 6u);
        }
    }
}

TEST_F(GameCharacterRenderTest, ZeroOriginalFramesDrawExistingClothsWithoutChangingPhysicsInOncePerDrawMode)
{
    for (const std::uint8_t characterClass : std::array<std::uint8_t, 2>{3, 4})
    {
        SCOPED_TRACE(static_cast<int>(characterClass));
        Player(0).characterClass = characterClass;
        std::array<Client::Scenes::CharacterCloth, 401> cloths;
        GameCharacterPass pass = Pass();
        pass.cloths = cloths;
        pass.pace = Client::Render::Models::OriginalFramePace::OncePerDraw;
        ASSERT_TRUE(Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
        const auto initial = cloths[0].Cloths();
        ASSERT_FALSE(initial.empty());
        const std::vector<Client::Render::Models::Cloth> before(initial.begin(), initial.end());
        const std::uint32_t random = m_clothWorld.random.State();
        const std::uint8_t count = cloths[0].Count();
        pass.originalFrames = 0;
        pass.frameClock = std::chrono::seconds{1};
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(cloths[0].Count(), count);
        ExpectSameCloths(cloths[0].Cloths(), before);
        EXPECT_EQ(m_clothWorld.random.State(), random);
        EXPECT_GT(m_batch.Vertices().size(), 6u);
    }
}

TEST_F(GameCharacterRenderTest, CatchUpKeepsOnlyTheLastFramesClothSpritesAndPreservesSimulation)
{
    Player(0).characterClass = 4;
    auto referenceWorld = std::make_unique<Client::Scenes::WorldState>();
    referenceWorld->Runtime().map = 0;
    referenceWorld->Actors()[0] = m_world->Actors()[0];
    referenceWorld->Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    referenceWorld->Map().terrain.attributes->cells.fill(0);
    Client::Render::Models::ClothWorld referenceClothWorld = m_clothWorld;
    std::array<Client::Scenes::CharacterCloth, 401> cloths;
    std::array<Client::Scenes::CharacterCloth, 401> referenceCloths;
    Client::World::FrameSprites sprites;
    Client::World::FrameSprites referenceSprites;
    GameCharacterPass pass = Pass();
    pass.originalFrames = 2;
    pass.frameClock = Client::Render::Models::OriginalFrameTime;
    pass.cloths = cloths;
    pass.sprites = &sprites;
    GameCharacterPass referencePass = pass;
    referencePass.originalFrames = 1;
    referencePass.clothWorld = &referenceClothWorld;
    referencePass.cloths = referenceCloths;
    referencePass.sprites = &referenceSprites;
    Client::Render::ModelBatch referenceBatch;
    for (int frame = 0; frame < 2; ++frame)
    {
        referenceBatch.Begin(Client::Render::Pass3D{});
        referenceSprites.Clear();
        ASSERT_TRUE(referenceSprites.Create(0x777, {9.0f, 8.0f, 7.0f}, 1.0f, {1.0f, 0.5f, 0.25f}, 0.0f, 0));
        referencePass.frameClock = Client::Render::Models::OriginalFrameTime * frame;
        const auto result =
            Client::Scenes::DrawGameCharacters(referenceBatch, m_models, *referenceWorld, referencePass);
        ASSERT_TRUE(result.HasValue()) << result.Error();
    }
    ASSERT_TRUE(sprites.Create(0x777, {9.0f, 8.0f, 7.0f}, 1.0f, {1.0f, 0.5f, 0.25f}, 0.0f, 0));

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto expectedSprites = LiveSprites(referenceSprites);
    ASSERT_EQ(expectedSprites.size(), 31u); // un sprite del mundo y los 30 del brillo de la tela
    ExpectSameSprites(LiveSprites(sprites), expectedSprites);
    ExpectSameGeometry(m_batch, referenceBatch);
    EXPECT_EQ(cloths[0].Count(), referenceCloths[0].Count());
    ExpectSameCloths(cloths[0].Cloths(), referenceCloths[0].Cloths());
    EXPECT_EQ(m_clothWorld.random.State(), referenceClothWorld.random.State());
}

TEST_F(GameCharacterRenderTest, CatchUpKeepsTheFirstFailedFramesShadowPartsClothAndSprites)
{
    ActorState& actor = Player(0);
    actor.characterClass = 4;
    actor.hands[0].model = 0x212;
    // 0x413fe0: +0x360 en 6 pide una capa del 6 al 13 (0x4147c0, sin portar), después del arma a la espalda.
    actor.byte360 = 6;
    auto referenceWorld = std::make_unique<Client::Scenes::WorldState>();
    referenceWorld->Runtime().map = 0;
    referenceWorld->Actors()[0] = actor;
    referenceWorld->Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    referenceWorld->Map().terrain.attributes->cells.fill(0);
    Client::Render::Models::ClothWorld referenceClothWorld = m_clothWorld;
    std::array<Client::Scenes::CharacterCloth, 401> cloths;
    std::array<Client::Scenes::CharacterCloth, 401> referenceCloths;
    Client::World::FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x777, {9.0f, 8.0f, 7.0f}, 1.0f, {1.0f, 0.5f, 0.25f}, 0.0f, 0));
    Client::World::FrameSprites referenceSprites = sprites;
    Client::Render::Pass3D originalPass;
    originalPass.viewport = {3, 5, 1024, 691};
    m_batch.Begin(originalPass);
    Client::Render::ModelDrawState precedingState;
    precedingState.blend = Client::Render::ModelBlend::Alpha;
    m_batch.ApplyOperation(precedingState, Client::Render::ModelStateOperation::Blend);
    Client::Render::ModelBatch referenceBatch = m_batch;
    GameCharacterPass pass = Pass();
    pass.originalFrames = 2;
    pass.cloths = cloths;
    pass.sprites = &sprites;
    GameCharacterPass referencePass = pass;
    referencePass.originalFrames = 1;
    referencePass.clothWorld = &referenceClothWorld;
    referencePass.cloths = referenceCloths;
    referencePass.sprites = &referenceSprites;
    Client::Scenes::FaunaDrawCarry referenceCarry;
    referencePass.poseCarry = &referenceCarry;
    const auto reference = Client::Scenes::DrawGameCharacters(referenceBatch, m_models, *referenceWorld, referencePass);
    ASSERT_FALSE(reference.HasValue());
    ASSERT_NE(reference.Error().find("0x4140d5"), std::string::npos) << reference.Error();
    ASSERT_GT(referenceBatch.Vertices().size(), 6u); // sombra, partes, tela y el arma a la espalda
    ASSERT_EQ(LiveSprites(referenceSprites).size(), 31u);

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), reference.Error());
    ExpectSameGeometry(m_batch, referenceBatch);
    EXPECT_EQ(m_batch.State(), referenceBatch.State());
    EXPECT_EQ(m_batch.Pass().viewport, originalPass.viewport);
    ExpectSameSprites(LiveSprites(sprites), LiveSprites(referenceSprites));
    ExpectSameCloths(cloths[0].Cloths(), referenceCloths[0].Cloths());
    EXPECT_EQ(m_clothWorld.random.State(), referenceClothWorld.random.State());
    EXPECT_EQ(actor.drawnLight, referenceWorld->Actors()[0].drawnLight);
}

TEST_F(GameCharacterRenderTest, MissingClothKeepsTheShadowPartsScanAndHeroLightPrefix)
{
    for (const std::uint8_t characterClass : std::array<std::uint8_t, 2>{3, 4})
    {
        SCOPED_TRACE(static_cast<int>(characterClass));
        ActorState& actor = Player(0);
        actor.characterClass = characterClass;
        m_world->OwnCharacter().speedAdjustBits = 1;
        m_world->Runtime().fullSetScan = Client::Scenes::Scan505820{123, 42};
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x50d58b"), std::string::npos) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 6u);
        ASSERT_EQ(m_batch.Commands().size(), 2u);
        EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Shadow);
        EXPECT_EQ(m_batch.Commands()[0].vertexCount, 3u);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[3].color.red, 0.9f);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[3].color.green, 0.5f);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[3].color.blue, 0.5f);
        ASSERT_TRUE(m_world->Runtime().fullSetScan.has_value());
        EXPECT_EQ(m_world->Runtime().fullSetScan->dword743a178, 0);
        EXPECT_EQ(m_world->Runtime().fullSetScan->byte743a17c, 1);
        EXPECT_EQ(actor.drawnLight, (glm::vec3{0.9f, 0.5f, 0.5f}));
        // Cierra own, pero no llega al rand incondicional de 0x50db57.
        EXPECT_EQ(m_clothWorld.random.State(), 0x65b2ab69u);
    }
}

TEST_F(GameCharacterRenderTest, EarlierSpecialBodyCutsKeepTheShadowAndTheCompletedScanAndLight)
{
    struct Case
    {
        std::int32_t subtype;
        std::uint8_t byte3e3;
        std::int32_t map;
        const char* address;
    };
    constexpr std::array cases{Case{209, 0, 0, "0x50d2dc"}};
    for (const auto& value : cases)
    {
        SCOPED_TRACE(value.address);
        ActorState& actor = Player(0);
        actor.subtype = value.subtype;
        actor.byte88 = value.subtype == 209 ? std::uint8_t{4} : std::uint8_t{1};
        actor.byte3e3 = value.byte3e3;
        m_world->Runtime().map = value.map;
        m_world->OwnCharacter().speedAdjustBits = 1;
        m_world->Runtime().fullSetScan = Client::Scenes::Scan505820{123, 42};
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find(value.address), std::string::npos) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 3u);
        ASSERT_EQ(m_batch.Commands().size(), 1u);
        EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Shadow);
        EXPECT_EQ(m_world->Runtime().fullSetScan->dword743a178, 0);
        EXPECT_EQ(m_world->Runtime().fullSetScan->byte743a17c, 1);
        EXPECT_EQ(actor.drawnLight, (glm::vec3{0.9f, 0.5f, 0.5f}));
        EXPECT_EQ(m_clothWorld.random.State(), 0x65b2ab69u);
    }
}

TEST_F(GameCharacterRenderTest, MissingDynamicLightKeepsTheShadowAndRunsThePrecedingScan)
{
    ActorState& actor = Player(0);
    actor.drawnLight = {7.0f, 8.0f, 9.0f};
    m_world->Runtime().fullSetScan = Client::Scenes::Scan505820{123, 42};
    GameCharacterPass pass = Pass();
    pass.dynamicLighting = nullptr;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x50cc55"), std::string::npos) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 3u);
    ASSERT_TRUE(m_world->Runtime().fullSetScan.has_value());
    EXPECT_EQ(m_world->Runtime().fullSetScan->dword743a178, 0);
    EXPECT_EQ(m_world->Runtime().fullSetScan->byte743a17c, 1);
    EXPECT_EQ(actor.drawnLight, (glm::vec3{7.0f, 8.0f, 9.0f}));
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}

TEST_F(GameCharacterRenderTest, MissingGlobalRandomKeepsTheShadowAndStopsAtTheScan)
{
    Player(0);
    m_world->Runtime().fullSetScan = Client::Scenes::Scan505820{123, 42};
    GameCharacterPass pass = Pass();
    pass.clothWorld = nullptr;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x50c91e"), std::string::npos) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 3u);
    ASSERT_TRUE(m_world->Runtime().fullSetScan.has_value());
    EXPECT_EQ(m_world->Runtime().fullSetScan->dword743a178, 123);
    EXPECT_EQ(m_world->Runtime().fullSetScan->byte743a17c, 42);
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}
TEST_F(GameCharacterRenderTest, WithPlus3e3ThePlayerDrawsNoPartsAndGoesOnToTheCloth)
{
    Player(7).byte3e3 = 1;
    m_world->Runtime().hero = 42;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Commands().size(), 1u);
    EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Shadow);
    EXPECT_EQ(m_clothWorld.random.State(), 0x0029e2c0u); // el rand de 0x50db57 sigue
}

TEST_F(GameCharacterRenderTest, InChaosCastleAPlayerWearsTheAngelAndOnlyTheHeroAddsItsChrome)
{
    m_world->Runtime().map = 18;
    for (const std::size_t slot : {std::size_t{7}, std::size_t{0}})
    {
        SCOPED_TRACE(slot);
        m_world->Actors()[7].alive = false;
        m_world->Actors()[0].alive = false;
        ActorState& actor = Player(slot);
        actor.byte88 = 1;
        m_world->Runtime().hero = slot == 0 ? 0 : 42;
        m_clothWorld.random = Client::Render::Models::CrtRandom{1};
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        const auto commands = m_batch.Commands();
        ASSERT_EQ(commands.size(), slot == 0 ? 4u : 2u);
        EXPECT_EQ(commands[0].state.blend, Client::Render::ModelBlend::Shadow);
        // 0x4ef0c0(2) sobre las dos mallas del Angel, no las botas (una malla), con la luz como color.
        EXPECT_EQ(commands[1].state.blend, Client::Render::ModelBlend::Opaque);
        EXPECT_EQ(commands[1].vertexCount, 6u);
        EXPECT_EQ(commands[1].state.texture, Client::Render::TextureSlot{0x300});
        const auto light = m_batch.Vertices()[3].color;
        const glm::vec3 expected = slot == 0 ? glm::vec3{1.0f} : glm::vec3{0.3f, 0.6f, 0.9f};
        EXPECT_FLOAT_EQ(light.red, expected.x);
        EXPECT_FLOAT_EQ(light.green, expected.y);
        EXPECT_FLOAT_EQ(light.blue, expected.z);
        if (slot != 0)
            continue;
        // 0x609640, caso 0x1f7, con 6: la malla 0 con 0x1040 (0x620) a (0,75; 0,55; 0,5) y con 0x46 (0x5c0) a 1.
        EXPECT_EQ(commands[2].state.blend, Client::Render::ModelBlend::Additive);
        EXPECT_EQ(commands[2].state.texture, Client::Render::TextureSlot{0x620});
        EXPECT_EQ(commands[2].vertexCount, 3u);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[9].color.red, 0.75f);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[9].color.green, 0.55f);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[9].color.blue, 0.5f);
        EXPECT_EQ(commands[3].state.blend, Client::Render::ModelBlend::Additive);
        EXPECT_EQ(commands[3].state.texture, Client::Render::TextureSlot{0x5c0});
        EXPECT_EQ(commands[3].vertexCount, 3u);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[12].color.red, 1.0f);
    }
}

TEST_F(GameCharacterRenderTest, TheChaosCastleOutlineScalesWithTheObjectAndHasItsOwnColors)
{
    m_world->Runtime().map = 18;
    ActorState& actor = Player(7);
    actor.byte88 = 1;
    actor.scale = 2.0f;
    m_world->Runtime().hero = 42;
    GameCharacterPass pass = Pass();
    pass.hovered = 7;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto commands = m_batch.Commands();
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(commands[1].state.blend, Client::Render::ModelBlend::Additive);
    ASSERT_EQ(commands[1].vertexCount, 12u);
    EXPECT_EQ(commands[2].state.blend, Client::Render::ModelBlend::Opaque);
    // 0x60d09f: 0,1f / 2 + 1 en el x87, guardado: 0x3f866666. 0x60d113: 0,04f / 2 + 1,02f − 0,02f: 1,02f.
    const auto vertices = m_batch.Vertices();
    const float outer = std::bit_cast<float>(0x3f866666u);
    const float inner = std::bit_cast<float>(0x3f828f5cu);
    EXPECT_EQ(vertices[3 + 1].position.x, outer * 2.0f + 150.0f);
    EXPECT_EQ(vertices[3 + 6 + 1].position.x, inner * 2.0f + 150.0f);
    EXPECT_FLOAT_EQ(vertices[3].color.red, 0.1f);
    EXPECT_FLOAT_EQ(vertices[3].color.green, 0.01f);
    EXPECT_FLOAT_EQ(vertices[3].color.blue, 0.0f);
    EXPECT_FLOAT_EQ(vertices[9].color.red, 0.7f);
    EXPECT_FLOAT_EQ(vertices[9].color.green, 0.07f);
    EXPECT_FLOAT_EQ(vertices[9].color.blue, 0.0f);
}

TEST_F(GameCharacterRenderTest, AnNpcKindOutlinesItsPartsWithTheGreenOfTheNpcs)
{
    ActorState& actor = Player(7);
    actor.byte88 = 4;
    m_world->Runtime().hero = 42;
    GameCharacterPass pass = Pass();
    pass.hovered = 7;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto vertices = m_batch.Vertices();
    ASSERT_GE(vertices.size(), 9u);
    // 0x60d069 y 0x60d0dd: (0,02; 0,1; 0) y (0,16; 0,7; 0), con las escalas de siempre.
    EXPECT_FLOAT_EQ(vertices[3].color.red, 0.02f);
    EXPECT_FLOAT_EQ(vertices[3].color.green, 0.1f);
    EXPECT_FLOAT_EQ(vertices[6].color.red, 0.16f);
    EXPECT_FLOAT_EQ(vertices[6].color.green, 0.7f);
    EXPECT_EQ(vertices[3 + 1].position.x, 1.2f + 150.0f);
}

// 0x50d4fb..0x50d588 (CL9 hito 8, fase 2): con +0x1f2 en 5 (su clave en −1, que 0x447b00 encuentra en la entrada 0
// con la cuenta en 0) y sin la tabla de las marcas, la marca corta en 0x50d575 después de las partes; en Chaos Castle
// (18) no hay marca.
TEST_F(GameCharacterRenderTest, AGuildIndexStopsAfterThePartsExceptInChaosCastle)
{
    for (const std::int32_t map : {0, 18})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        ActorState& actor = Player(7);
        actor.byte88 = 1;
        actor.word1f2 = 5;
        m_world->Runtime().hero = 42;
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        if (map == 18)
        {
            ASSERT_TRUE(result.HasValue()) << result.Error();
            continue;
        }
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x50d575"), std::string::npos) << result.Error();
        // La sombra y las botas ya están.
        ASSERT_EQ(m_batch.Commands().size(), 2u);
        EXPECT_EQ(m_batch.Commands()[1].vertexCount, 3u);
    }
}
} // namespace
