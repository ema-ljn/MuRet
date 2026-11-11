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
#include "Scenes/FullSetScan.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/TerrainLighting.h"
#include "World/Sprites.h"
#include "World/WorldTerrain.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
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
        m_skeleton.bones.assign(48, bone);
        m_skeleton.actions = {ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "boot.jpg";
        m_model.meshes = {mesh};
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t model) const override
    {
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
    std::array<Client::Render::TextureSlot, 1> m_texture{Client::Render::TextureSlot{0x300}};
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
        actor.body.back().model = BootsModel;
        return actor;
    }

    GameCharacterPass Pass()
    {
        return {.dynamicLighting = m_light.get(),
                .clothWorld = &m_clothWorld,
                .frameClock = Client::Core::Duration{},
                .originalFrames = 1};
    }

    std::unique_ptr<Client::Scenes::WorldState> m_world;
    std::unique_ptr<Client::World::TerrainLighting> m_light;
    Client::Render::Models::ClothWorld m_clothWorld;
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

TEST_F(GameCharacterRenderTest, ABackWeaponStopsAtItsUnportedPlacementAfterTheBodyAndTheRandom)
{
    Player(0).hands[0].model = 0x212;
    auto& walls = m_world->Map().terrain.attributes->cells;
    walls[Client::Data::Terrain::CellIndex(118, 115)] = 5;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x50de55"), std::string::npos) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_clothWorld.random.State(), 0x46d18088u);
    EXPECT_EQ(walls[Client::Data::Terrain::CellIndex(118, 115)], 5);
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

TEST_F(GameCharacterRenderTest, OutsideTheSafeZoneTheUnportedWeaponStateDoesNotFallBackToTheHand)
{
    Player(0).safeZone = 0;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x50e298"), std::string::npos) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_clothWorld.random.State(), 0x46d18088u);
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

TEST_F(GameCharacterRenderTest, TheRavenOwnerPointerIsRejectedEvenAfterTheHandItemWasRemoved)
{
    Player(0).darkSpirit.emplace();

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x420df0"), std::string::npos) << result.Error();
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

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x50fba4"), std::string::npos) << result.Error();
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
    const auto reference = Client::Scenes::DrawGameCharacters(referenceBatch, m_models, *referenceWorld, referencePass);
    ASSERT_FALSE(reference.HasValue());
    ASSERT_NE(reference.Error().find("0x50de55"), std::string::npos) << reference.Error();
    ASSERT_GT(referenceBatch.Vertices().size(), 6u); // sombra, partes y tela antes del arma a espalda
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
    constexpr std::array cases{Case{209, 0, 0, "0x50d2dc"}, Case{0, 1, 0, "0x50d363"}, Case{0, 0, 18, "0x50d363"}};
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
} // namespace
