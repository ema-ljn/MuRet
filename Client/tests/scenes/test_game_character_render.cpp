#include "Models/IModelLibrary.h"
#include "Actors/Drawing/GameCharacters.h"

#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Kernel/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Pets/DarkSpirit.h"
#include "Events/EventGlobals.h"
#include "Kernel/PoseBuffer.h"
#include "Actors/FullSetScan.h"
#include "Guild/GuildMarks.h"
#include "Guild/GuildState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Terrain/TerrainLighting.h"
#include "Particles/Sprites.h"
#include "Terrain/WorldTerrain.h"

#include "support/FakeTextureStore.h"
#include "Effects/ActorEffectHooks.h"
#include "Session/CharacterDrawHooks.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

// 0x50cc29..0x50d2aa and 0x50db57..0x50dc5e: literal vectors from the c32bbd5f ASM, without running the original. A
// synthetic boot and shadow make the color and the outline observable.
namespace
{
using Client::Actors::Drawing::GameCharacterPass;
using Client::Kernel::ActorState;

constexpr std::uint16_t BootsModel = 0x1800;
constexpr std::uint16_t AngelModel = 0x1f7;
// Skill\\Skeleton02 (0x615e36): the body of subtype 0xd2. 0xd1 is not loaded.
constexpr std::uint16_t SkeletonBodyModel = 0xd2;
constexpr std::uint16_t UnloadedSkeletonBody = 0xd1;

// The CRT rand() state with seed 1 after `count` calls.
std::uint32_t AfterRandoms(std::size_t count)
{
    Client::Crt::CrtRandom random{1};
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random.State();
}

// Closing the hero's own record spends 3848 rand() (0x50ccb5..0x50d219).
constexpr std::size_t OwnRecordRandoms = 3848;

void ExpectSameCloths(std::span<const Client::Models::Cloth> actual, std::span<const Client::Models::Cloth> expected)
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

std::vector<Client::Particles::Sprite> LiveSprites(Client::Particles::FrameSprites& table)
{
    std::vector<Client::Particles::Sprite> live;
    for (const auto& sprite : table.Sprites())
    {
        if (sprite.alive)
            live.push_back(sprite);
    }
    return live;
}

void ExpectSameSprites(std::span<const Client::Particles::Sprite> actual,
                       std::span<const Client::Particles::Sprite> expected)
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

class CharacterModels final : public Client::Models::IModelLibrary
{
public:
    CharacterModels()
    {
        using namespace Client::Math;
        using namespace Client::Models;
        SkeletonBone bone;
        bone.parent = -1;
        bone.actions = {{BoneKey{}}};
        bone.actions[0][0].rotation = AngleQuaternion({0.0f, 0.0f, 0.0f});
        // Bone k at (0, 0, k): what hangs shows which body bone it comes from. The meshes only use 0.
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
        // The Chaos Castle Angel with two meshes: its pass is told apart from the boots' by the vertices.
        m_angel.meshes = {mesh, mesh};
        // A skeleton body with three: neither the boots nor the Angel.
        m_skeletonBody.meshes = {mesh, mesh, mesh};
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId model) const override
    {
        if (model.Value() == AngelModel)
            return &m_angel;
        if (model.Value() == SkeletonBodyModel)
            return &m_skeletonBody;
        return model.Value() == BootsModel || model.Value() == 0x20b || HasHand(model.Value()) ? &m_model : nullptr;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return model.Value() == 0x20a || (handSkeletons && HasHand(model.Value())) ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return m_texture;
    }

    bool handSkeletons = true;

private:
    static bool HasHand(std::uint16_t model)
    {
        return model == 0x212 || model == 0x220 || model == 0xc19 || model == 0xc17 || model == 0xe20;
    }

    Client::Models::Skeleton m_skeleton;
    Client::Data::Models::Model m_model;
    Client::Data::Models::Model m_angel;
    Client::Data::Models::Model m_skeletonBody;
    // One per mesh of the model with the most: the skeleton's three.
    std::array<Client::Render::TextureSlot, 3> m_texture{
        Client::Render::TextureSlot{0x300}, Client::Render::TextureSlot{0x300}, Client::Render::TextureSlot{0x300}};
};

class GameCharacterRenderTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_world = std::make_unique<Client::Kernel::WorldState>();
        m_world->Runtime().map = 0;
        m_terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        m_terrain.attributes->cells.fill(0);
        m_light = std::make_unique<Client::Terrain::TerrainLighting>();
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
        // 0x511290: no guild (+0x1f2 at −1).
        actor.word1f2 = 0xffff;
        actor.scale = 1.0f;
        actor.alpha = 1.0f;
        // 0x511290: no blend or hidden mesh (+0x64 and +0x58 at −1) and the blend light (+0x68) at 1.
        actor.dword58 = -1;
        actor.dword64 = -1;
        actor.blendMeshLight = 1.0f;
        actor.safeZone = 1;
        actor.position = {150.0f, 150.0f, 10.0f};
        actor.light = {0.1f, 0.2f, 0.3f};
        actor.wings.model = 0xffff;
        actor.pet.model = 0xffff;
        for (auto& part : actor.body)
            part.model = 0xffff;
        for (auto& part : actor.hands)
            part.model = 0xffff;
        // 0x5126b2..0x5126d5: a 0x20a's hands hang from bones 33 and 42.
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
                .worldModels = &m_models,
                .terrain = &m_terrain,
                .hooks = &m_drawHooks,
                .effectHooks = &m_effectHooks};
    }

    std::unique_ptr<Client::Kernel::WorldState> m_world;
    Client::Terrain::WorldTerrain m_terrain;
    Client::Session::CharacterDrawHooks m_drawHooks;
    Client::Effects::ActorEffectHooks m_effectHooks;
    std::unique_ptr<Client::Terrain::TerrainLighting> m_light;
    Client::Models::ClothWorld m_clothWorld;
    Client::Particles::FrameSprites m_sprites;
    Client::Kernel::PoseBuffer m_carry;
    CharacterModels m_models;
    Client::Render::ModelBatch m_batch;
};

TEST_F(GameCharacterRenderTest, OtherPlayersUseTheSuppliedDynamicTerrainLightAndTheirOwnLight)
{
    ActorState& actor = Player(7);
    m_world->Runtime().hero = 42;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    const auto color = m_batch.Vertices()[3].color;
    EXPECT_FLOAT_EQ(color.red, 0.3f);
    EXPECT_FLOAT_EQ(color.green, 0.6f);
    EXPECT_FLOAT_EQ(color.blue, 0.9f);
    EXPECT_FLOAT_EQ(actor.drawnLight.x, 0.3f);
    EXPECT_FLOAT_EQ(actor.drawnLight.y, 0.6f);
    EXPECT_FLOAT_EQ(actor.drawnLight.z, 0.9f);
    EXPECT_EQ(m_clothWorld.random.State(), 0x0029e2c0u); // seed 1, one rand of 0x50db57
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
        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 6u);
        const auto color = m_batch.Vertices()[3].color;
        EXPECT_FLOAT_EQ(color.red, value.expected.x);
        EXPECT_FLOAT_EQ(color.green, value.expected.y);
        EXPECT_FLOAT_EQ(color.blue, value.expected.z);
        // The persistent RGB goes back to the terrain's before what hangs, not to the hero's body.
        EXPECT_FLOAT_EQ(hero.drawnLight.x, 0.3f);
        EXPECT_FLOAT_EQ(hero.drawnLight.y, 0.6f);
        EXPECT_FLOAT_EQ(hero.drawnLight.z, 0.9f);
    }
}

TEST_F(GameCharacterRenderTest, TheHeroClosesItsOwnRecordBeforeTheUnconditionalAttachmentRandom)
{
    Player(0);

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_world->Runtime().ownRecordDepth, 0);
    EXPECT_EQ(m_clothWorld.random.State(), 0x46d18088u); // semilla 1, 3848 + 1 rand
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(OwnRecordRandoms + 1));
}

TEST_F(GameCharacterRenderTest, AnOuterOwnRecordAccessSuppressesOnlyThe3848CloseRandoms)
{
    Player(0);
    m_world->Runtime().ownRecordDepth = 1;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

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
        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);
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

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 3u);
    ASSERT_EQ(m_batch.Commands().size(), 1u);
    EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Alpha);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[0].color.alpha, 0.25f);
}

// 0x50ddde..0x50de60 in the safe zone: the right sword hangs from the wings bone (47) along the flag path, with the
// root of 0x5fc8f0 and 0x5fca90 (70, 0, 90 and −20, 5, 40), the origin on the character and the hanging color
// (+0x3b4). The rand(): 0x50db57's, +0x38e's close in 0x509340 and 0x509a92's light. On the back the hands loop does
// not run.
TEST_F(GameCharacterRenderTest, ABackSwordHangsFromTheWingsBoneWithTheFlagPlacement)
{
    ActorState& actor = Player(0);
    actor.hands[0].model = 0x212;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 9u);
    // The sword's vertex 0 is at its root: (−20, 5, 40) on bone 47, at (0, 0, 47).
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
    Client::Actors::Drawing::DrawnCharacter character;
    character.appearance.bodyParts.back().model = BootsModel;
    character.appearance.rightHand = Client::Kernel::WornItem{0xc17, 0, false, false};
    character.light = {0.1f, 0.2f, 0.3f};
    character.hangingLight = glm::vec3{0.8f, 0.7f, 0.6f};
    character.originalRandom = &m_clothWorld.random;

    Client::Actors::Drawing::DrawCharacter(m_batch, m_models, character, {});

    ASSERT_EQ(m_batch.Vertices().size(), 9u);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[3].color.red, 0.1f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[3].color.green, 0.2f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[3].color.blue, 0.3f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[6].color.red, 0.8f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[6].color.green, 0.7f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[6].color.blue, 0.6f);
    EXPECT_EQ(m_clothWorld.random.State(), 0xc823f683u); // seed 1, the two rand of 0x607b60
}

TEST_F(GameCharacterRenderTest, BetweenOriginalFramesTheDrawDoesNotRepeatTheRecordOrAttachmentRandoms)
{
    Player(0);
    GameCharacterPass pass = Pass();
    pass.originalFrames = 0;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
}

TEST_F(GameCharacterRenderTest, MultipleOriginalFramesKeepOnlyTheLastGeometryButConsumeBothFrames)
{
    Player(0);
    GameCharacterPass pass = Pass();
    pass.originalFrames = 2;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_clothWorld.random.State(), 0x5fc9f4d3u); // semilla 1, dos veces 3848 + 1
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
}

TEST_F(GameCharacterRenderTest, The401stPoolRecordIsNotPartOfThe400CharacterDraw)
{
    Player(400);

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}

// Outside the safe zone the hands loop runs (0x50e298): the right sword hangs from bone 33 with the origin on the bone
// (0x4ec190). The rand(): 0x50db57, the +0x38e before each hand (two), 0x509340's and 0x509a92's light.
TEST_F(GameCharacterRenderTest, OutsideTheSafeZoneTheSwordHangsFromTheRightHandBone)
{
    ActorState& actor = Player(0);
    actor.safeZone = 0;
    actor.hands[0].model = 0x212;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 9u);
    EXPECT_EQ(m_batch.Vertices()[6].position, glm::vec3(150.0f, 150.0f, 43.0f));
    EXPECT_EQ(actor.hands[0].animation.action, 0);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(OwnRecordRandoms + 5));
}

TEST_F(GameCharacterRenderTest, BetweenOriginalFramesTheHangingIsDrawnWithoutChangingTheActor)
{
    ActorState& actor = Player(0);
    actor.hands[0].model = 0x212;
    GameCharacterPass pass = Pass();
    pass.originalFrames = 0;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 9u);
    EXPECT_EQ(m_batch.Vertices()[6].position, glm::vec3(130.0f, 155.0f, 97.0f));
    EXPECT_EQ(actor.wings.bone, 0);
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}

// Without the global pose buffer, what hangs stops in 0x509340 with the body already drawn.
TEST_F(GameCharacterRenderTest, AHangingItemWithoutThePoseBufferStopsAfterTheBody)
{
    Player(0).hands[0].model = 0x212;
    GameCharacterPass pass = Pass();
    pass.poseCarry = nullptr;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x509340"), std::string::npos) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_clothWorld.random.State(), AfterRandoms(OwnRecordRandoms + 1));
}

TEST_F(GameCharacterRenderTest, AHiddenHeroReturnsBeforeTheRecordAndTheUnconditionalRandom)
{
    Player(0).flags7c = 0x10000000;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}

TEST_F(GameCharacterRenderTest, ACalledHeroWithControlBitFourKeepsTheOriginalUndrawnMarks)
{
    ActorState& hero = Player(0);
    hero.controlCode = 4;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

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

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

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
        Client::Actors::Drawing::DrawnCharacter character;
        character.appearance.rightHand = Client::Kernel::WornItem{value.model, 0, false, false};
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        character.originalRandom = &m_clothWorld.random;
        m_batch.Begin(Client::Render::Pass3D{});

        Client::Actors::Drawing::DrawCharacter(m_batch, m_models, character, {});

        EXPECT_EQ(m_clothWorld.random.State(), value.state);
    }
}

TEST_F(GameCharacterRenderTest, TheClothOnTheMeshStopsBeforeThePlayerRecordAndItsRandoms)
{
    Player(0).word376 = 0x59;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x50bae7"), std::string::npos) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_clothWorld.random.State(), 1u);
}

// 0x50c951: monster 55 is a 0x20a with +0x376 0x37; without class 3 or 4, bVar23 turns it on and 0x50d812 builds
// ClothMonster's cloth (10 by 10, texture 0x252), except on maps 0x12 to 0x17.
TEST_F(GameCharacterRenderTest, TheMonster55WearsTheClothOfThe0x37)
{
    for (const std::int32_t map : {0, 2, 0x12})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        Player(0).word376 = 0x37;
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        std::array<Client::Actors::Drawing::CharacterCloth, 401> cloths;
        GameCharacterPass pass = Pass();
        pass.cloths = cloths;
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        if (map == 0x12)
        {
            EXPECT_TRUE(cloths[0].Cloths().empty());
            continue;
        }
        ASSERT_EQ(cloths[0].Cloths().size(), 1u);
        EXPECT_EQ(cloths[0].Count(), 1);
        EXPECT_EQ(cloths[0].Cloths()[0].Particles().size(), 100u);
        EXPECT_EQ(cloths[0].Cloths()[0].Front(), Client::Render::TextureSlot{0x252});
        EXPECT_GT(m_batch.Vertices().size(), 6u);
    }
}

TEST_F(GameCharacterRenderTest, TheHiddenClothFlagDoesNotRunTheClothSimulationBeforeItsBoundary)
{
    ActorState& hero = Player(0);
    hero.characterClass = 4;
    hero.flags78 = 0x4000;
    std::array<Client::Actors::Drawing::CharacterCloth, 401> cloths;
    GameCharacterPass pass = Pass();
    pass.cloths = cloths;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    // With 0x4000 and without 4 or 0x2000000, 0x50fba4 does not run: the draw ends without the cloth.
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_clothWorld.random.State(), 0x46d18088u);
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
}

TEST_F(GameCharacterRenderTest, TheOverloadWithoutDrawingResourcesAlsoExcludesThe401stRecord)
{
    Player(400);

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_world->Actors(), m_world->Runtime().hero,
                                                                    m_terrain.attributes->cells);

    ASSERT_TRUE(result.HasValue()) << result.Error();
}

TEST_F(GameCharacterRenderTest, TheAttachmentInitializerConsumesItsRandomBeforeAMissingItemPose)
{
    Client::Actors::Drawing::DrawnCharacter character;
    character.appearance.rightHand = Client::Kernel::WornItem{0xc17, 0, false, false};
    character.originalRandom = &m_clothWorld.random;
    m_models.handSkeletons = false;

    Client::Actors::Drawing::DrawCharacter(m_batch, m_models, character, {});

    EXPECT_EQ(m_clothWorld.random.State(), 0xc823f683u);
    EXPECT_EQ(m_batch.Vertices().size(), 3u);
}

TEST_F(GameCharacterRenderTest, HiddenAttachmentsReturnBeforeTheInitializerAndItsRandoms)
{
    Client::Actors::Drawing::DrawnCharacter character;
    character.appearance.rightHand = Client::Kernel::WornItem{0xc17, 0, false, false};
    character.originalRandom = &m_clothWorld.random;
    character.objectFlags = 0x4000;

    Client::Actors::Drawing::DrawCharacter(m_batch, m_models, character, {});

    EXPECT_EQ(m_clothWorld.random.State(), 1u);
    EXPECT_EQ(m_batch.Vertices().size(), 3u);
}

TEST_F(GameCharacterRenderTest, AnUnselectedLeftSwordIsNotDrawnInTheSafeZone)
{
    Player(0).hands[1].model = 0x212;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_clothWorld.random.State(), 0x46d18088u);
}

// The cloth vertices with color `color` (0x455930 puts 0x50ba30's on each one).
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

// 0x50c929..0x50cc29 (CL9 milestone 7, phase 2): without a duel, class 1 wears no cloth and neither does 3 with
// +0x3e3. With a duel on, the rival (its key and name at 0x78abadc and 0x78abae0) wears it in (1, 0.2, 0) even in class
// 1 and in Chaos Castle (0x12), and the published hero, in (0, 0.2, 1).
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
        // The published hero is another one (not drawn).
        m_world->Runtime().hero = 5;
        m_world->Actors()[5].key = 0x0021;
        Client::Social::DuelState& duel = m_world->Runtime().social.duel;
        duel = {};
        auto cloths = std::make_unique<std::array<Client::Actors::Drawing::CharacterCloth, 401>>();
        GameCharacterPass pass = Pass();
        pass.cloths = *cloths;
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
        EXPECT_TRUE((*cloths)[0].Cloths().empty());

        player.characterClass = 3;
        player.byte3e3 = 1;
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
        EXPECT_TRUE((*cloths)[0].Cloths().empty());

        player.characterClass = 1;
        player.byte3e3 = 0;
        duel.active = 1;
        duel.rivalKey = 0x0456;
        for (std::size_t at = 0; at < 6; ++at)
            duel.rivalName[at] = static_cast<std::byte>("Brezal"[at]);
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
        ASSERT_EQ((*cloths)[0].Cloths().size(), 1u);
        EXPECT_GT(ClothVerticesIn(m_batch, Rival), 0u);
        EXPECT_EQ(ClothVerticesIn(m_batch, Hero), 0u);

        // The same, published: the second comparison wins.
        m_world->Runtime().hero = 0;
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
        EXPECT_EQ(ClothVerticesIn(m_batch, Rival), 0u);
        EXPECT_GT(ClothVerticesIn(m_batch, Hero), 0u);

        // Another name with the same key is not the rival.
        m_world->Runtime().hero = 5;
        duel.rivalName[0] = std::byte{'Z'};
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
        EXPECT_EQ(ClothVerticesIn(m_batch, Rival), 0u);
    }
}

// 0x50c972..0x50caaa (CL9 milestone 8): with the war (0x576796d) and +0x1f2 other than −1, the hero's guild wears the
// cloth of its team's color ((1, 0.2, 0) with 0x576796e at 0, (0, 0.2, 1) with 1) and the rival's (0x5767804) the
// other one, even in class 1 and in Chaos Castle (0x12); without the war or without a guild, not. With the hero
// guildless, strcmp reads byte 9 of member 74's name (Q-C1281). An index outside the table stops. Invented guilds.
TEST_F(GameCharacterRenderTest, TheGuildWarDressesBothGuildsInTheirTeamColors)
{
    constexpr glm::vec3 Red{1.0f, 0.2f, 0.0f};
    constexpr glm::vec3 Blue{0.0f, 0.2f, 1.0f};
    constexpr std::uint16_t NoGuild = 0xffff;
    for (const std::int32_t map : {0, 2, 0x12})
    {
        SCOPED_TRACE(map);
        Client::Kernel::WorldRuntime& runtime = m_world->Runtime();
        runtime.map = map;
        Client::Guild::GuildState& guild = runtime.guild;
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
        // The published hero is another one (not drawn), from the same guild.
        runtime.hero = 5;
        m_world->Actors()[5].key = 0x0021;
        m_world->Actors()[5].word1f2 = 3;
        auto cloths = std::make_unique<std::array<Client::Actors::Drawing::CharacterCloth, 401>>();
        GameCharacterPass pass = Pass();
        pass.cloths = *cloths;
        // With +0x1f2 at 3 and the keys at −1, 0x50d533 also asks for the mark (0x447b00 finds entry 0).
        auto store = std::make_unique<Client::Tests::FakeTextureStore>();
        auto marks = std::make_unique<Client::Guild::GuildMarkTextures>(*store);
        pass.guildMarks = marks.get();
        const auto draw = [&]()
        {
            m_batch.Begin(Client::Render::Pass3D{});
            return Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);
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

        // The rival, with team 1, in red.
        player.word1f2 = 4;
        ASSERT_TRUE(draw().HasValue());
        EXPECT_GT(ClothVerticesIn(m_batch, Red), 0u);
        EXPECT_EQ(ClothVerticesIn(m_batch, Blue), 0u);

        // Without a guild, class 1 does not wear it.
        player.word1f2 = NoGuild;
        ASSERT_TRUE(draw().HasValue());
        EXPECT_EQ(ClothVerticesIn(m_batch, Red), 0u);
        EXPECT_EQ(ClothVerticesIn(m_batch, Blue), 0u);

        // The guildless hero: "" (member 74's byte 9 at 0) is entry 7's empty name.
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

// 0x50caaa..0x50cb5c: Soccer overrides the war colors, and the second name wins if both are equal.
TEST_F(GameCharacterRenderTest, SoccerDressesTheCurrentGuildNamesAndTheSecondTeamWins)
{
    auto& runtime = m_world->Runtime();
    runtime.map = 0x12;
    runtime.hero = 5;
    m_world->Actors()[5].word1f2 = 3;
    runtime.guild.warType = 1;
    runtime.guild.warTeam = 1;
    auto& player = Player(0);
    player.characterClass = 1;
    player.word1f2 = 3;
    runtime.guild.table[3].guildName = {'T', 'a', 'l', 'a', 's'};
    auto& soccer = runtime.events.soccer;
    soccer.active = true;
    soccer.firstName = {'T', 'a', 'l', 'a', 's'};
    soccer.secondName = {'O', 'm', 'b', 'u'};
    auto cloths = std::make_unique<std::array<Client::Actors::Drawing::CharacterCloth, 401>>();
    auto store = std::make_unique<Client::Tests::FakeTextureStore>();
    auto marks = std::make_unique<Client::Guild::GuildMarkTextures>(*store);
    GameCharacterPass pass = Pass();
    pass.cloths = *cloths;
    pass.guildMarks = marks.get();
    const auto draw = [&]()
    {
        m_batch.Begin(Client::Render::Pass3D{});
        return Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);
    };
    ASSERT_TRUE(draw().HasValue());
    EXPECT_GT(ClothVerticesIn(m_batch, {1, 0.2f, 0}), 0u);
    runtime.guild.table[3].guildName = {'O', 'm', 'b', 'u'};
    ASSERT_TRUE(draw().HasValue());
    EXPECT_GT(ClothVerticesIn(m_batch, {0, 0.2f, 1}), 0u);
    soccer.firstName = soccer.secondName;
    ASSERT_TRUE(draw().HasValue());
    EXPECT_GT(ClothVerticesIn(m_batch, {0, 0.2f, 1}), 0u);
    EXPECT_EQ(ClothVerticesIn(m_batch, {1, 0.2f, 0}), 0u);
    player.word1f2 = 0xffff;
    runtime.guild.warType = 0;
    soccer.firstName = {};
    soccer.secondName = {};
    runtime.guild.members[74].name[9] = '\0';
    ASSERT_TRUE(draw().HasValue());
    EXPECT_GT(ClothVerticesIn(m_batch, {0, 0.2f, 1}), 0u);
    player.word1f2 = 2000;
    const auto cut = draw();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x50cac6"), std::string::npos);
}

TEST_F(GameCharacterRenderTest, ZeroOriginalFramesDoNotCreateFirstUseClassThreeOrFourCloths)
{
    using Client::Models::OriginalFramePace;
    for (const std::uint8_t characterClass : std::array<std::uint8_t, 2>{3, 4})
    {
        for (const auto pace : {OriginalFramePace::ByTheClock, OriginalFramePace::OncePerDraw})
        {
            SCOPED_TRACE(static_cast<int>(characterClass));
            SCOPED_TRACE(static_cast<int>(pace));
            Player(0).characterClass = characterClass;
            m_clothWorld.random = Client::Crt::CrtRandom{1};
            std::array<Client::Actors::Drawing::CharacterCloth, 401> cloths;
            Client::Particles::FrameSprites sprites;
            GameCharacterPass pass = Pass();
            pass.originalFrames = 0;
            pass.pace = pace;
            pass.cloths = cloths;
            pass.sprites = &sprites;
            m_batch.Begin(Client::Render::Pass3D{});

            const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

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
        std::array<Client::Actors::Drawing::CharacterCloth, 401> cloths;
        GameCharacterPass pass = Pass();
        pass.cloths = cloths;
        pass.pace = Client::Models::OriginalFramePace::OncePerDraw;
        ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
        const auto initial = cloths[0].Cloths();
        ASSERT_FALSE(initial.empty());
        const std::vector<Client::Models::Cloth> before(initial.begin(), initial.end());
        const std::uint32_t random = m_clothWorld.random.State();
        const std::uint8_t count = cloths[0].Count();
        pass.originalFrames = 0;
        pass.frameClock = std::chrono::seconds{1};
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

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
    auto referenceWorld = std::make_unique<Client::Kernel::WorldState>();
    referenceWorld->Runtime().map = 0;
    referenceWorld->Actors()[0] = m_world->Actors()[0];
    Client::Terrain::WorldTerrain referenceTerrain;
    referenceTerrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    referenceTerrain.attributes->cells.fill(0);
    Client::Models::ClothWorld referenceClothWorld = m_clothWorld;
    std::array<Client::Actors::Drawing::CharacterCloth, 401> cloths;
    std::array<Client::Actors::Drawing::CharacterCloth, 401> referenceCloths;
    Client::Particles::FrameSprites sprites;
    Client::Particles::FrameSprites referenceSprites;
    GameCharacterPass pass = Pass();
    pass.originalFrames = 2;
    pass.frameClock = Client::Models::OriginalFrameTime;
    pass.cloths = cloths;
    pass.sprites = &sprites;
    GameCharacterPass referencePass = pass;
    referencePass.originalFrames = 1;
    referencePass.terrain = &referenceTerrain;
    referencePass.clothWorld = &referenceClothWorld;
    referencePass.cloths = referenceCloths;
    referencePass.sprites = &referenceSprites;
    Client::Render::ModelBatch referenceBatch;
    for (int frame = 0; frame < 2; ++frame)
    {
        referenceBatch.Begin(Client::Render::Pass3D{});
        referenceSprites.Clear();
        ASSERT_TRUE(referenceSprites.Create(0x777, {9.0f, 8.0f, 7.0f}, 1.0f, {1.0f, 0.5f, 0.25f}, 0.0f, 0));
        referencePass.frameClock = Client::Models::OriginalFrameTime * frame;
        const auto result =
            Client::Actors::Drawing::DrawGameCharacters(referenceBatch, m_models, *referenceWorld, referencePass);
        ASSERT_TRUE(result.HasValue()) << result.Error();
    }
    ASSERT_TRUE(sprites.Create(0x777, {9.0f, 8.0f, 7.0f}, 1.0f, {1.0f, 0.5f, 0.25f}, 0.0f, 0));

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto expectedSprites = LiveSprites(referenceSprites);
    ASSERT_EQ(expectedSprites.size(), 31u); // one world sprite and the cloth glow's 30
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
    // 0x414470: the synthetic library has no MuSign. Layer 5 fails after shadow/parts/cloth/weapon; siege layers 6..13
    // are already ported and are not a valid failure point for this regression.
    actor.byte360 = 5;
    auto referenceWorld = std::make_unique<Client::Kernel::WorldState>();
    referenceWorld->Runtime().map = 0;
    referenceWorld->Actors()[0] = actor;
    Client::Terrain::WorldTerrain referenceTerrain;
    referenceTerrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    referenceTerrain.attributes->cells.fill(0);
    Client::Models::ClothWorld referenceClothWorld = m_clothWorld;
    std::array<Client::Actors::Drawing::CharacterCloth, 401> cloths;
    std::array<Client::Actors::Drawing::CharacterCloth, 401> referenceCloths;
    Client::Particles::FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x777, {9.0f, 8.0f, 7.0f}, 1.0f, {1.0f, 0.5f, 0.25f}, 0.0f, 0));
    Client::Particles::FrameSprites referenceSprites = sprites;
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
    referencePass.terrain = &referenceTerrain;
    referencePass.clothWorld = &referenceClothWorld;
    referencePass.cloths = referenceCloths;
    referencePass.sprites = &referenceSprites;
    Client::Kernel::PoseBuffer referenceCarry;
    referencePass.poseCarry = &referenceCarry;
    const auto reference =
        Client::Actors::Drawing::DrawGameCharacters(referenceBatch, m_models, *referenceWorld, referencePass);
    ASSERT_FALSE(reference.HasValue());
    ASSERT_NE(reference.Error().find("0x414470"), std::string::npos) << reference.Error();
    ASSERT_GT(referenceBatch.Vertices().size(), 6u); // shadow, parts, cloth and the back weapon
    ASSERT_EQ(LiveSprites(referenceSprites).size(), 31u);

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

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
        m_world->Runtime().fullSetScan = Client::Actors::Scan505820{123, 42};
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

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
        // It closes the own record but does not reach 0x50db57's unconditional rand.
        EXPECT_EQ(m_clothWorld.random.State(), 0x65b2ab69u);
    }
}

TEST_F(GameCharacterRenderTest, MissingDynamicLightKeepsTheShadowAndRunsThePrecedingScan)
{
    ActorState& actor = Player(0);
    actor.drawnLight = {7.0f, 8.0f, 9.0f};
    m_world->Runtime().fullSetScan = Client::Actors::Scan505820{123, 42};
    GameCharacterPass pass = Pass();
    pass.dynamicLighting = nullptr;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

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
    m_world->Runtime().fullSetScan = Client::Actors::Scan505820{123, 42};
    GameCharacterPass pass = Pass();
    pass.clothWorld = nullptr;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

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

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Commands().size(), 1u);
    EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Shadow);
    EXPECT_EQ(m_clothWorld.random.State(), 0x0029e2c0u); // 0x50db57's rand still runs
}

// 0x50d316..0x50d35e: a 0x20a with subtype 0xd1..0xd3 wears neither its parts nor the guild mark: 0x60cf70(c,
// subtype, NULL, &c+0x3b4, c+0x174, 0, 0, 0, 0, 0, 1, mode, 2) with the skeleton's whole body (Skill\\Skeleton0N), and
// jumps to the cloth (0x50d58b). Without the level, the color is the light.
TEST_F(GameCharacterRenderTest, TheSkeletonSubtypesWearTheirWholeBodyInsteadOfTheParts)
{
    ActorState& actor = Player(7);
    actor.subtype = SkeletonBodyModel;
    m_world->Runtime().hero = 42;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto commands = m_batch.Commands();
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[0].state.blend, Client::Render::ModelBlend::Shadow);
    // The skeleton's three meshes, not the boot.
    EXPECT_EQ(commands[1].state.blend, Client::Render::ModelBlend::Opaque);
    EXPECT_EQ(commands[1].vertexCount, 9u);
    const auto light = m_batch.Vertices()[3].color;
    EXPECT_FLOAT_EQ(light.red, 0.3f);
    EXPECT_FLOAT_EQ(light.green, 0.6f);
    EXPECT_FLOAT_EQ(light.blue, 0.9f);
    EXPECT_EQ(m_clothWorld.random.State(), 0x0029e2c0u); // 0x50db57's rand still runs
}

// 0x50d2aa..0x50d311: with +0x88 at 4 (an NPC) and map [0x6c9dd0] at 0 (Lorencia), the level is word +0x1d6 · 8, which
// 0x60b3d0 reads with >> 3 and & 0xf: with 7, the color times 0.8 and the 0x44 pass (Chrome). On another map, or with
// another +0x88, 0.
TEST_F(GameCharacterRenderTest, TheSkeletonNpcOfLorenciaGlowsWithItsWordOf1d6)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t kind : {std::uint8_t{4}, std::uint8_t{1}})
        {
            SCOPED_TRACE(std::to_string(map) + " " + std::to_string(kind));
            m_world->Runtime().map = map;
            m_world->Runtime().hero = 42;
            ActorState& actor = Player(7);
            actor.byte88 = kind;
            actor.subtype = SkeletonBodyModel;
            actor.level = 7;
            m_clothWorld.random = Client::Crt::CrtRandom{1};
            m_batch.Begin(Client::Render::Pass3D{});

            const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

            ASSERT_TRUE(result.HasValue()) << result.Error();
            const bool glows = kind == 4 && map == 0;
            const auto commands = m_batch.Commands();
            ASSERT_EQ(commands.size(), glows ? 3u : 2u);
            EXPECT_EQ(commands[1].vertexCount, 9u);
            const float factor = glows ? 0.8f : 1.0f;
            const auto light = m_batch.Vertices()[3].color;
            EXPECT_FLOAT_EQ(light.red, 0.3f * factor);
            EXPECT_FLOAT_EQ(light.green, 0.6f * factor);
            EXPECT_FLOAT_EQ(light.blue, 0.9f * factor);
            if (glows)
            {
                EXPECT_EQ(commands[2].state.blend, Client::Render::ModelBlend::Additive);
            }
        }
    }
}

// 0x60b573: with the options' glow detail (0x6c9dcc) at 1, the level 13 skeleton of Lorencia draws as a level 7 one:
// its body and the 0x44 pass.
TEST_F(GameCharacterRenderTest, TheGlowDetailOfTheOptionsTrimsTheLevelOfTheSkeletonBody)
{
    for (const std::uint8_t detail : {std::uint8_t{4}, std::uint8_t{1}})
    {
        SCOPED_TRACE(static_cast<int>(detail));
        m_world->Runtime().map = 0;
        m_world->Runtime().hero = 42;
        ActorState& actor = Player(7);
        actor.byte88 = 4;
        actor.subtype = SkeletonBodyModel;
        actor.level = 13;
        m_batch.Begin(Client::Render::Pass3D{});
        Client::Actors::Drawing::ItemDrawContext items;
        items.glowDetail = detail;
        GameCharacterPass pass = Pass();
        pass.itemContext = &items;

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        // The shadow, the body and its passes.
        EXPECT_EQ(m_batch.Commands().size(), detail == 4 ? 5u : 3u);
    }
}

// 0x50d2aa: the skeleton branch comes before looking at +0x3e3 (0x50d363): with +0x3e3 the whole body is drawn the
// same.
TEST_F(GameCharacterRenderTest, WithPlus3e3TheSkeletonStillWearsItsWholeBody)
{
    ActorState& actor = Player(7);
    actor.subtype = SkeletonBodyModel;
    actor.byte3e3 = 1;
    m_world->Runtime().hero = 42;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Commands().size(), 2u);
    EXPECT_EQ(m_batch.Commands()[1].vertexCount, 9u);
}

// 0x60cf70 with an unloaded model (0x60dbe0's record at 0): no meshes to draw; it publishes the picking box
// (0x4ebc80) anyway.
TEST_F(GameCharacterRenderTest, AnUnloadedSkeletonBodyOnlyPublishesThePickingBox)
{
    ActorState& actor = Player(7);
    actor.subtype = UnloadedSkeletonBody;
    m_world->Runtime().hero = 42;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Commands().size(), 1u);
    EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Shadow);
    EXPECT_EQ(actor.field13c, actor.position.x + actor.boundsMin.x);
    EXPECT_EQ(actor.field168, actor.boundsTop - actor.boundsMin.z);
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
        m_clothWorld.random = Client::Crt::CrtRandom{1};
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(actor.field13c, actor.position.x + actor.boundsMin.x);
        EXPECT_EQ(actor.field140, actor.position.y + actor.boundsMin.y);
        EXPECT_EQ(actor.field144, actor.position.z + actor.boundsMin.z);
        EXPECT_EQ(actor.field148, actor.boundsMaxX - actor.boundsMin.x);
        EXPECT_EQ(actor.field158, actor.boundsMaxY - actor.boundsMin.y);
        EXPECT_EQ(actor.field168, actor.boundsTop - actor.boundsMin.z);
        const auto commands = m_batch.Commands();
        ASSERT_EQ(commands.size(), slot == 0 ? 4u : 2u);
        EXPECT_EQ(commands[0].state.blend, Client::Render::ModelBlend::Shadow);
        // 0x4ef0c0(2) on the Angel's two meshes, not the boots (one mesh), with the light as color.
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
        // 0x609640, case 0x1f7, with 6: mesh 0 with 0x1040 (0x620) at (0.75, 0.55, 0.5) and with 0x46 (0x5c0) at 1.
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

TEST_F(GameCharacterRenderTest, ChaosCastleAngelPartsSkipDrawingAtTheAlphaBoundaryAndForNan)
{
    m_world->Runtime().map = 18;
    ActorState& actor = Player(7);
    actor.byte88 = 1;
    m_world->Runtime().hero = 42;
    actor.field13c = 123.0f;
    GameCharacterPass pass = Pass();
    pass.hovered = 7;

    for (const float alpha : {0.01f, std::numeric_limits<float>::quiet_NaN()})
    {
        SCOPED_TRACE(alpha);
        actor.alpha = alpha;
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(m_batch.Commands().empty());
        EXPECT_EQ(actor.field13c, 123.0f);
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

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto commands = m_batch.Commands();
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(commands[1].state.blend, Client::Render::ModelBlend::Additive);
    ASSERT_EQ(commands[1].vertexCount, 12u);
    EXPECT_EQ(commands[2].state.blend, Client::Render::ModelBlend::Opaque);
    // 0x60d09f: 0.1f / 2 + 1 on the x87, stored: 0x3f866666. 0x60d113: 0.04f / 2 + 1.02f − 0.02f: 1.02f.
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

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto vertices = m_batch.Vertices();
    ASSERT_GE(vertices.size(), 9u);
    // 0x60d069 and 0x60d0dd: (0.02, 0.1, 0) and (0.16, 0.7, 0), with the usual scales.
    EXPECT_FLOAT_EQ(vertices[3].color.red, 0.02f);
    EXPECT_FLOAT_EQ(vertices[3].color.green, 0.1f);
    EXPECT_FLOAT_EQ(vertices[6].color.red, 0.16f);
    EXPECT_FLOAT_EQ(vertices[6].color.green, 0.7f);
    EXPECT_EQ(vertices[3 + 1].position.x, 1.2f + 150.0f);
}

// 0x50d4fb..0x50d588 (CL9 milestone 8, phase 2): with +0x1f2 at 5 (its key at −1, which 0x447b00 finds in entry 0
// with the count at 0) and without the marks table, the mark stops at 0x50d575 after the parts; in Chaos Castle (18)
// there is no mark.
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

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        if (map == 18)
        {
            ASSERT_TRUE(result.HasValue()) << result.Error();
            continue;
        }
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x50d575"), std::string::npos) << result.Error();
        // The shadow and the boots are already there.
        ASSERT_EQ(m_batch.Commands().size(), 2u);
        EXPECT_EQ(m_batch.Commands()[1].vertexCount, 3u);
    }
}
} // namespace
