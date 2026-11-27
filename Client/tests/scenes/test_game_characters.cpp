#include "Models/IModelLibrary.h"
#include "Actors/Drawing/GameCharacters.h"

#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Render/ModelVertex.h"
#include "Math/OriginalRotation.h"
#include "Render/TextureSlot.h"
#include "Actors/ActorAppearance.h"
#include "Kernel/ActorState.h"
#include "Actors/ActorPickingBox.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/FullSetScan.h"
#include "Scenes/GuildMarks.h"
#include "Scenes/GuildState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Terrain/TerrainLighting.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "Terrain/WorldTerrain.h"

#include "support/FakeTextureStore.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Client::Actors::Drawing::DrawGameCharacters (0x510d00 in scene 5; 06 § El dibujo del juego): the hero that is not
// drawn, the stop at the first character alive and seen (0x50ba30) and the TerrainWall of 0x42da60 at the end.
namespace
{
using Client::Kernel::ActorState;

constexpr std::size_t Hero = 42;

// A TerrainWall with 0x04 in the four cells under the gate (119, 114) and in its row.
std::unique_ptr<Client::Data::Terrain::TerrainLayer<std::uint16_t>> GateWalls()
{
    auto walls = std::make_unique<Client::Data::Terrain::TerrainLayer<std::uint16_t>>();
    walls->fill(0);
    for (std::uint8_t x = 118; x <= 121; ++x)
    {
        (*walls)[Client::Data::Terrain::CellIndex(x, 115)] = 0x05;
    }
    return walls;
}

// 0x510e75: without a character alive and seen, none goes to 0x50ba30; the hero without +0x1d8 & 4 is not touched
// either. At the end, 0x42da40 opens the TerrainWall's gates.
TEST(GameCharactersTest, WithoutASeenCharacterThePassOpensTheGates)
{
    auto actors = std::make_unique<std::array<ActorState, 400>>();
    (*actors)[Hero].alive = true;
    (*actors)[7].seen = true;
    auto walls = GateWalls();

    const auto drawn = Client::Actors::Drawing::DrawGameCharacters(*actors, Hero, *walls);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    for (std::uint8_t x = 118; x <= 121; ++x)
    {
        EXPECT_EQ((*walls)[Client::Data::Terrain::CellIndex(x, 115)], 0x01) << static_cast<int>(x);
    }
    EXPECT_EQ((*actors)[Hero].field13c, 0.0f);
}

// 0x510e40..0x510e73: the hero with +0x1d8 & 4 is not drawn even if it is alive and seen; in scene 5 it is left with
// +0x13c at 1,000 and +0x148, +0x158 and +0x168 at 1. Another with that bit is not the hero and stops.
TEST(GameCharactersTest, TheHeroWithControlBitFourIsNotDrawn)
{
    auto actors = std::make_unique<std::array<ActorState, 400>>();
    ActorState& hero = (*actors)[Hero];
    hero.alive = true;
    hero.seen = true;
    hero.controlCode = Client::Actors::Drawing::UndrawnHeroControlBit;
    auto walls = GateWalls();

    ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacters(*actors, Hero, *walls).HasValue());
    EXPECT_EQ(hero.field13c, 1000.0f);
    EXPECT_EQ(hero.field148, 1.0f);
    EXPECT_EQ(hero.field158, 1.0f);
    EXPECT_EQ(hero.field168, 1.0f);

    const auto other = Client::Actors::Drawing::DrawGameCharacters(*actors, Hero + 1, *walls);
    ASSERT_FALSE(other.HasValue());
    EXPECT_NE(other.Error().find("0x510ea1"), std::string::npos) << other.Error();
}

TEST(GameCharactersTest, ProjectedHeroSuppressionWritesOnlyTheProjectionBox)
{
    auto actors = std::make_unique<std::array<ActorState, 400>>();
    ActorState& hero = (*actors)[Hero];
    hero.alive = true;
    hero.seen = true;
    hero.controlCode = Client::Actors::Drawing::UndrawnHeroControlBit;
    hero.field13c = 10.0f;
    hero.field140 = 20.0f;
    hero.field144 = 30.0f;
    hero.field148 = 40.0f;
    hero.field158 = 50.0f;
    hero.field168 = 60.0f;
    std::array<Client::Actors::ActorPickingBox, 400> boxes{};
    boxes[Hero] = {.origin = {1.0f, 2.0f, 3.0f},
                   .axisX = {4.0f, 5.0f, 6.0f},
                   .axisY = {7.0f, 8.0f, 9.0f},
                   .axisZ = {10.0f, 11.0f, 12.0f}};
    auto walls = GateWalls();

    ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacters(*actors, Hero, *walls, nullptr, boxes).HasValue());

    EXPECT_EQ(hero.field13c, 10.0f);
    EXPECT_EQ(hero.field140, 20.0f);
    EXPECT_EQ(hero.field144, 30.0f);
    EXPECT_EQ(hero.field148, 40.0f);
    EXPECT_EQ(hero.field158, 50.0f);
    EXPECT_EQ(hero.field168, 60.0f);
    EXPECT_EQ(boxes[Hero].origin, (glm::vec3{1000.0f, 2.0f, 3.0f}));
    EXPECT_EQ(boxes[Hero].axisX, (glm::vec3{1.0f, 5.0f, 6.0f}));
    EXPECT_EQ(boxes[Hero].axisY, (glm::vec3{7.0f, 1.0f, 9.0f}));
    EXPECT_EQ(boxes[Hero].axisZ, (glm::vec3{10.0f, 11.0f, 1.0f}));
}

// 0x510ea1: the first character alive and seen, by slot, goes to 0x50ba30, which MuRet does not have in the game: it
// stops with its slot, without opening the gates (0x42da60 comes after the loop).
TEST(GameCharactersTest, TheFirstSeenCharacterStopsThePass)
{
    auto actors = std::make_unique<std::array<ActorState, 400>>();
    (*actors)[Hero].alive = true;
    (*actors)[Hero].seen = true;
    (*actors)[300].alive = true;
    (*actors)[300].seen = true;
    auto walls = GateWalls();

    const auto drawn = Client::Actors::Drawing::DrawGameCharacters(*actors, Hero, *walls);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x510ea1"), std::string::npos) << drawn.Error();
    EXPECT_NE(drawn.Error().find("personaje 42 "), std::string::npos) << drawn.Error();
    EXPECT_EQ((*walls)[Client::Data::Terrain::CellIndex(119, 115)], 0x05);
}

constexpr std::uint16_t Boots = Client::Tables::ItemModel(11, 0);

class UnarmedModels final : public Client::Models::IModelLibrary
{
public:
    UnarmedModels()
    {
        Client::Models::SkeletonBone bone;
        bone.actions.assign(Client::Tests::PlayerActionCount, {Client::Models::BoneKey{}});
        m_skeleton.bones.push_back(bone);
        m_skeleton.actions.assign(Client::Tests::PlayerActionCount, {.keyCount = 1});
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "boot.jpg";
        m_model.meshes.push_back(mesh);
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t model) const override
    {
        return model == Boots || model == Client::Tables::PlayerShadowModel ? &m_model : nullptr;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == Client::Tables::PlayerSkeletonModel ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return m_textures;
    }
    void RetainTheDummyRoot()
    {
        m_skeleton.bones[0].dummy = true;
    }
    // More bones like the first, up to `count`: 26 is the guild mark's (0x5091e0).
    void GrowTo(std::size_t count)
    {
        while (m_skeleton.bones.size() < count)
            m_skeleton.bones.push_back(m_skeleton.bones.front());
    }

private:
    Client::Data::Models::Model m_model;
    Client::Models::Skeleton m_skeleton;
    std::array<Client::Render::TextureSlot, 1> m_textures{Client::Render::TextureSlot{0x300}};
};

class UnarmedGameCharacterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_world = std::make_unique<Client::Kernel::WorldState>();
        m_world->Runtime().map = 2;
        m_world->Runtime().hero = Hero;
        m_terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        m_terrain.attributes->cells = *GateWalls();
        m_light = std::make_unique<Client::Terrain::TerrainLighting>();
        m_light->cells.fill({0.2f, 0.4f, 0.6f});
        ActorState& actor = Actor();
        actor = Client::Tests::UnarmedPlayer(1);
        actor.alive = true;
        actor.seen = true;
        actor.word376 = 0xffff;
        actor.head = 0xffff;
        for (auto& part : actor.body)
            part.model = Client::Kernel::NoItemModel;
        actor.body.back().model = Boots;
        actor.position = {150.0f, 150.0f, 10.0f};
        actor.scale = 1.0f;
        actor.alpha = 1.0f;
        actor.pk = 3;
        actor.animation.action = 0x0d; // 0x4f3a60: walking unarmed for classes 0 and 1
        m_batch.Begin({});
    }

    [[nodiscard]] ActorState& Actor()
    {
        return m_world->Actors()[7];
    }
    [[nodiscard]] Client::Actors::Drawing::GameCharacterPass Pass()
    {
        return {.dynamicLighting = m_light.get(), .clothWorld = &m_cloth, .terrain = &m_terrain};
    }

    std::unique_ptr<Client::Kernel::WorldState> m_world;
    Client::Terrain::WorldTerrain m_terrain;
    std::unique_ptr<Client::Terrain::TerrainLighting> m_light;
    UnarmedModels m_models;
    Client::Models::ClothWorld m_cloth;
    Client::Render::ModelBatch m_batch;
};

// The vertices of the stretches drawn with `slot`.
std::vector<Client::Render::ModelVertex> VerticesWith(const Client::Render::ModelBatch& batch,
                                                      Client::Render::TextureSlot slot)
{
    std::vector<Client::Render::ModelVertex> found;
    const auto vertices = batch.Vertices();
    for (const Client::Render::ModelDrawCommand& command : batch.Commands())
    {
        if (command.operation != Client::Render::ModelStateOperation::Draw || command.state.texture != slot)
            continue;
        for (std::size_t i = 0; i < command.vertexCount; ++i)
            found.push_back(vertices[command.firstVertex + i]);
        EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Alpha);
        EXPECT_EQ(command.state.cullEnabled, std::optional<bool>(true));
    }
    return found;
}

// 0x50d4fb..0x50d588 and 0x5091e0: another player with a guild wears the mark (0x5f16a0 with the transparent
// background) on bone 26: the quad of 0x621320(5, 7) by R_ConcatTransforms(bone, AngleMatrix(turn + (80, 45, 135)) with
// (20, −5, −10)) plus the position, uv (0, 1), (1, 1), (1, 0), (0, 0). With the armor 0x1226, z at −18. No rand() of
// its own. In Lorencia, in Devias and in Chaos Castle (18), where it does not go; nor with the 0x4000 flag of +0x78,
// nor with an index whose key the table does not find up to the count (0x447b30), except 0x7cf, which does not look for
// it.
TEST_F(UnarmedGameCharacterTest, AnotherPlayerWithAGuildWearsItsMarkOnBoneTwentySix)
{
    m_models.GrowTo(27);
    Client::Tests::FakeTextureStore store;
    Client::Scenes::GuildMarkTextures marks(store);
    Client::Scenes::GuildState& guild = m_world->Runtime().guild;
    guild.tableCount = 4;
    guild.table[3].key = 77;
    guild.table[3].mark.fill(4);
    const Client::Render::TextureSlot slot = Client::Scenes::GuildMarkTextures::SlotOf(3, true);
    const auto draw = [&](std::int32_t map, Client::Render::TextureSlot drawn)
    {
        m_world->Runtime().map = map;
        m_cloth.random = Client::Crt::CrtRandom(1);
        m_batch.Begin({});
        auto pass = Pass();
        pass.guildMarks = &marks;
        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);
        EXPECT_TRUE(result.HasValue()) << result.Error();
        return VerticesWith(m_batch, drawn);
    };
    const auto expectedQuad = [&](float lowZ)
    {
        const Client::Math::OriginalMatrix local = [&]
        {
            auto turn = Client::Math::OriginalAngleMatrix(
                {Actor().angles.x + 80.0f, Actor().angles.y + 45.0f, Actor().angles.z + 135.0f});
            turn[3] = 20.0f;
            turn[7] = -5.0f;
            turn[11] = lowZ;
            return turn;
        }();
        const auto matrix =
            Client::Math::OriginalConcatTransforms(Client::Math::OriginalMatrixOf(Actor().bodyPose.at(26)), local);
        const std::array<glm::vec3, 4> corners{{{-5, -5, -7}, {5, 5, -7}, {5, 5, 7}, {-5, -5, 7}}};
        std::array<glm::vec3, 4> points{};
        for (std::size_t i = 0; i < corners.size(); ++i)
            points[i] = Actor().position + Client::Math::OriginalVectorTransform(corners[i], matrix);
        return points;
    };
    Actor().word1f2 = 3;
    Actor().angles = {0.0f, 0.0f, 90.0f};
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto vertices = draw(map, slot);
        ASSERT_EQ(vertices.size(), 6u);
        EXPECT_EQ(m_cloth.random.State(), 0x18be873au);
        const auto points = expectedQuad(-10.0f);
        // The quad's two halves: (0, 1, 2) and (0, 2, 3).
        const std::array<std::size_t, 6> order{0, 1, 2, 0, 2, 3};
        const std::array<glm::vec2, 4> uv{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
        for (std::size_t i = 0; i < order.size(); ++i)
        {
            SCOPED_TRACE(i);
            EXPECT_EQ(vertices[i].position, points[order[i]]);
            EXPECT_EQ(vertices[i].uv, uv[order[i]]);
        }
    }
    ASSERT_TRUE(store.Find(slot).has_value());

    Actor().body[1].model = 0x1226;
    auto low = draw(2, slot);
    ASSERT_EQ(low.size(), 6u);
    EXPECT_EQ(low[0].position, expectedQuad(-18.0f)[0]);
    Actor().body[1].model = Client::Kernel::NoItemModel;

    Actor().flags78 |= 0x4000;
    EXPECT_TRUE(draw(2, slot).empty());
    Actor().flags78 &= ~0x4000u;
    // In Chaos Castle, without the Angel (+0x88 other than 1), the usual body and no mark.
    const std::uint8_t kind = Actor().byte88;
    Actor().byte88 = 0;
    EXPECT_TRUE(draw(18, slot).empty());
    Actor().byte88 = kind;
    // Entry 10 is past the count (4): 0x447b00 does not find its key.
    Actor().word1f2 = 10;
    guild.table[10].key = 99;
    const Client::Render::TextureSlot tenth = Client::Scenes::GuildMarkTextures::SlotOf(10, true);
    EXPECT_TRUE(draw(2, tenth).empty());
    EXPECT_FALSE(store.Find(tenth).has_value());
    Actor().word1f2 = 1999;
    guild.table[1999].mark.fill(2);
    EXPECT_EQ(draw(2, Client::Scenes::GuildMarkTextures::SlotOf(1999, true)).size(), 6u);
}

// 0x50e298 opens +0x38e; 0x50e503 reads; 0x50e68a closes with a rand; 0x50e746 tests NoItem afterwards. The loop
// repeats for both hands (0x50e9ea..0x50e9f4), even if both are empty.
TEST_F(UnarmedGameCharacterTest, WalkingUnarmedClosesBothHandBytesAfterTheBodyRandom)
{
    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_cloth.random.State(), 0x18be873au); // seed 1: 0x50db57 and two one-byte closes
    EXPECT_EQ(Actor().byte38eAccesses, 0);
    EXPECT_EQ(m_terrain.attributes->cells[Client::Data::Terrain::CellIndex(119, 115)], 1);
}

// 0x510ea9..0x510eb3 and 0x42afe0: after the 0x20a, 0x42ddb0 emits 0x638 on a defender near a mark every 10 steps. The
// three CRT calls of the body and the hands come before the two of the blessing's jitter (0x53fe70).
TEST_F(UnarmedGameCharacterTest, TheCastleSiegeBlessingFollowsTheCompletedCharacterAndItsRandoms)
{
    m_world->Runtime().map = 30;
    Actor().position = {8200.0f, 13000.0f, 10.0f};
    Actor().flags78 |= 0x40000;
    auto pass = Pass();
    pass.particles = &m_world->Particles();
    const auto quiet = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);
    ASSERT_TRUE(quiet.HasValue()) << quiet.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_cloth.random.State(), 0x18be873au);
    EXPECT_FALSE(m_world->Particles().All().front().alive);

    m_world->Runtime().castleSiege.siegeActive = true;
    m_cloth.random = Client::Crt::CrtRandom(1);
    m_batch.Begin({});
    const auto siege = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);
    ASSERT_TRUE(siege.HasValue()) << siege.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    const auto& blessing = m_world->Particles().All().front();
    ASSERT_TRUE(blessing.alive);
    EXPECT_EQ(blessing.type, Client::World::SiegeBlessingParticle);
    EXPECT_EQ(blessing.life, 20);
    EXPECT_EQ(blessing.position, (glm::vec3{8195.0f, 13014.0f, 250.0f}));
    EXPECT_EQ(blessing.light, Actor().light);
    Client::Crt::CrtRandom expected(1);
    expected.Discard(5);
    EXPECT_EQ(m_cloth.random.State(), expected.State());
    EXPECT_EQ(m_terrain.attributes->cells[Client::Data::Terrain::CellIndex(119, 115)], 1);
}

TEST_F(UnarmedGameCharacterTest, TheCastleSiegeRequiresTheParticlePoolAfterTheBodyPrefix)
{
    m_world->Runtime().map = 30;
    m_world->Runtime().castleSiege.siegeActive = true;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x42de7f"), std::string::npos) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_cloth.random.State(), 0x18be873au);
    // It does not reach 0x42da60: the gates' bit 0x04 stays closed.
    EXPECT_EQ(m_terrain.attributes->cells[Client::Data::Terrain::CellIndex(119, 115)], 5);
}

TEST_F(UnarmedGameCharacterTest, HeldOrWrappedHandByteDepthDoesNotConsumeClosingRandoms)
{
    for (const std::uint8_t depth : std::array<std::uint8_t, 5>{0, 1, 2, 254, 255})
    {
        SCOPED_TRACE(static_cast<int>(depth));
        Actor().byte38eAccesses = depth;
        m_cloth.random = Client::Crt::CrtRandom(1);
        m_batch.Begin({});

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(Actor().byte38eAccesses, depth);
        EXPECT_EQ(m_cloth.random.State(), depth == 0 ? 0x18be873au : 0x0029e2c0u);
    }
}

TEST_F(UnarmedGameCharacterTest, TheHeroClosesItsOwnRecordBeforeTheBodyAndTheTwoHandBytes)
{
    struct Case
    {
        std::uint8_t ownDepth;
        std::uint8_t handDepth;
        std::uint32_t random;
    };
    // 0x50ccb5..0x50d219: 3,848 rand calls of the own record; then 0x50db57 and the two closes of +0x38e.
    constexpr std::array cases{Case{0, 0, 0x059c5342}, Case{1, 0, 0x18be873a}, Case{0, 1, 0x46d18088},
                               Case{1, 1, 0x0029e2c0}, Case{255, 255, 0x0029e2c0}};
    m_world->Runtime().hero = 7;
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.ownDepth));
        SCOPED_TRACE(static_cast<int>(probe.handDepth));
        m_world->Runtime().ownRecordDepth = probe.ownDepth;
        Actor().byte38eAccesses = probe.handDepth;
        m_cloth.random = Client::Crt::CrtRandom(1);
        m_batch.Begin({});

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_cloth.random.State(), probe.random);
        EXPECT_EQ(m_world->Runtime().ownRecordDepth, probe.ownDepth);
        EXPECT_EQ(Actor().byte38eAccesses, probe.handDepth);
        EXPECT_EQ(m_batch.Vertices().size(), 6u);
    }
}

TEST_F(UnarmedGameCharacterTest, NativeUnarmedStandingWalkingAndRunningActionsReachTheSameHandLoop)
{
    struct Case
    {
        std::uint8_t characterClass;
        std::uint8_t action;
    };
    // 0x4f2250: standing 1 except the Elf's 2; 0x4f3a60: walking 0xd except the Elf's 0xe, running 0x16.
    constexpr std::array cases{Case{0, 1},    Case{1, 1},    Case{2, 2},    Case{0, 0x0d}, Case{1, 0x0d},
                               Case{2, 0x0e}, Case{0, 0x16}, Case{1, 0x16}, Case{2, 0x16}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.characterClass));
        SCOPED_TRACE(static_cast<int>(probe.action));
        Actor().characterClass = probe.characterClass;
        Actor().animation.action = probe.action;
        m_cloth.random = Client::Crt::CrtRandom(1);
        m_batch.Begin({});

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_batch.Vertices().size(), 6u);
        EXPECT_EQ(m_cloth.random.State(), 0x18be873au);
    }
}

TEST_F(UnarmedGameCharacterTest, OnlyTheNativeBackCasesSkipTheTwoHandByteReads)
{
    struct Case
    {
        int map;
        std::uint8_t safeZone;
        std::uint8_t action;
        std::uint32_t random;
    };
    constexpr std::array cases{
        Case{2, 0, 0, 0x18be873a},    Case{2, 0, 2, 0x18be873a},    Case{2, 0, 0x3d, 0x18be873a},
        Case{2, 0, 0x3e, 0x18be873a}, Case{2, 0, 0x98, 0x18be873a}, Case{2, 0, 0x99, 0x0029e2c0},
        Case{2, 0, 0xb8, 0x0029e2c0}, Case{2, 0, 0xb9, 0x18be873a}, Case{2, 1, 2, 0x0029e2c0},
        Case{11, 1, 2, 0x18be873a},   Case{7, 0, 0x15, 0x0029e2c0}, Case{24, 0, 0x1d, 0x0029e2c0},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.map);
        SCOPED_TRACE(static_cast<int>(probe.action));
        m_world->Runtime().map = probe.map;
        Actor().safeZone = probe.safeZone;
        Actor().animation.action = probe.action;
        m_cloth.random = Client::Crt::CrtRandom(1);
        m_batch.Begin({});

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_cloth.random.State(), probe.random);
    }
}

TEST_F(UnarmedGameCharacterTest, AnIntermediateDrawDoesNotOpenTheHandBytesOrAdvanceTheActor)
{
    m_world->Runtime().hero = 7;
    auto pass = Pass();
    pass.originalFrames = 0;
    const ActorState before = Actor();

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_cloth.random.State(), 1u);
    EXPECT_EQ(Actor().byte38eAccesses, 0);
    EXPECT_EQ(Actor().position, before.position);
    EXPECT_EQ(Actor().animation.frame, before.animation.frame);
    EXPECT_EQ(Actor().animation.action, before.animation.action);
    EXPECT_TRUE(Actor().bodyPose.empty());
}

TEST_F(UnarmedGameCharacterTest, CatchUpSpendsTwoHandByteClosesPerOriginalFrame)
{
    auto pass = Pass();
    pass.originalFrames = 2;

    const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_cloth.random.State(), 0x3d6c1037u); // six rand calls: (body + two hands) times two frames
    EXPECT_EQ(Actor().byte38eAccesses, 0);
    // 0x509340 leaves the body's pose in the own bones (+0x120) for the next step (0x506d25).
    EXPECT_EQ(Actor().bodyPose.size(), m_models.SkeletonOf(Client::Tables::PlayerSkeletonModel)->bones.size());
}

// Q-C412: 0x6001b0 copies the character's scale and position to Player.bmd's shared model before posing, with the alpha
// from 0.01 (0x6b88d4); another's raven reads them in the safe zone. An intermediate draw does not touch them.
TEST_F(UnarmedGameCharacterTest, TheBodyLeavesItsScaleAndPositionInThePlayerModel)
{
    Actor().scale = 1.3f;
    auto pass = Pass();
    pass.originalFrames = 0;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
    EXPECT_EQ(m_world->PlayerModel().scale, 0.0f);

    pass.originalFrames = 1;
    const auto drawn = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(m_world->PlayerModel().scale, 1.3f);
    EXPECT_EQ(m_world->PlayerModel().origin, Actor().position);

    m_world->PlayerModel() = {};
    Actor().alpha = 0.009f;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
    EXPECT_EQ(m_world->PlayerModel().scale, 0.0f);
}

// The boots' vertices (the texture 0x300 of UnarmedModels), opaque.
std::vector<Client::Render::ModelVertex> BootVertices(const Client::Render::ModelBatch& batch)
{
    std::vector<Client::Render::ModelVertex> found;
    const auto vertices = batch.Vertices();
    for (const Client::Render::ModelDrawCommand& command : batch.Commands())
    {
        if (command.operation != Client::Render::ModelStateOperation::Draw ||
            command.state.texture != Client::Render::TextureSlot{0x300})
            continue;
        for (std::size_t i = 0; i < command.vertexCount; ++i)
            found.push_back(vertices[command.firstVertex + i]);
    }
    return found;
}

// The z of a draw: two NaNs are the same; otherwise, the same value and the same sign (−0 is not +0).
bool SameZ(float drawn, float expected)
{
    if (std::isnan(expected))
        return std::isnan(drawn);
    return drawn == expected && std::signbit(drawn) == std::signbit(expected);
}

// 0x50bc77..0x50bcbd: with the shadow, in Blood Castle (maps 0xb to 0x11), the one that dies pushed (+0x1a1 and +0x389)
// drops to the terrain height (0x5f93c0) before 0x60cf70 (0x50bcec), which copies that origin to the 0x20b; the body is
// drawn there. The fcom with `test ah, 1` also writes with the z or the height at NaN; with the terrain at the z's
// height or above it does not write: with the z at −0 and the terrain at +0, the z stays at −0 (a `>` would overwrite
// it with +0). Outside those maps (Devias and Chaos Castle), without +0x1a1 or without dying, the z stays. The floor
// goes behind the shadow's guards (0x50bc05..0x50bc71): with the alpha below 0.5 (at 0.5 it drops), with +0x3e4 or with
// a pet that is ridden (0x1c14, 0x1c15) outside the safe zone (+0x3e2), neither shadow nor floor. Without the heights,
// it stops at the call to 0x5f93c0.
TEST_F(UnarmedGameCharacterTest, TheDyingOfBloodCastleSinkToTheGroundUnderTheirShadow)
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
    const std::uint8_t kind = Actor().byte88;
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << probe.map << ", +0x1a1 " << int{probe.byte1a1} << ", +0x389 "
                                          << int{probe.dying} << ", z " << probe.z << ", terreno " << probe.height);
        m_terrain.heights->cells.fill(probe.height);
        m_world->Runtime().map = probe.map;
        // In Chaos Castle, without the Angel (+0x88 other than 1).
        Actor().byte88 = probe.map == 0x12 ? std::uint8_t{0} : kind;
        Actor().byte1a1 = probe.byte1a1;
        Actor().dying = probe.dying;
        Actor().position = {150.0f, 150.0f, probe.z};
        m_world->PlaceModel(Client::Tables::PlayerShadowModel, {});
        m_cloth.random = Client::Crt::CrtRandom(1);
        m_batch.Begin({});

        const auto drawn = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(Actor().position.x, 150.0f);
        EXPECT_EQ(Actor().position.y, 150.0f);
        EXPECT_TRUE(SameZ(Actor().position.z, probe.grounded)) << Actor().position.z;
        const Client::Kernel::SharedModelPlacement shadow = m_world->ModelPlacement(Client::Tables::PlayerShadowModel);
        EXPECT_TRUE(SameZ(shadow.origin.z, probe.grounded)) << shadow.origin.z;
        EXPECT_EQ(shadow.scale, 1.0f);
        const auto boots = BootVertices(m_batch);
        ASSERT_EQ(boots.size(), 3u);
        // The vertex adds the origin to its 0: with −0 it gives +0.
        const float boot = boots[0].position.z;
        EXPECT_TRUE(std::isnan(probe.grounded) ? std::isnan(boot) : boot == probe.grounded) << boot;
    }
    Actor().byte88 = kind;

    // Behind the shadow's guards: without it, the z stays and the 0x20b is not touched.
    struct Guard
    {
        float alpha;
        std::uint8_t portrait;
        std::uint16_t pet;
        std::uint8_t safeZone;
        float grounded;
    };
    const std::uint16_t noPet = Actor().pet.model;
    const std::array guards{Guard{0.49f, 0, noPet, 0, 10.0f}, Guard{0.5f, 0, noPet, 0, 4.0f},
                            Guard{1.0f, 1, noPet, 0, 10.0f},  Guard{1.0f, 0, 0x1c14, 0, 10.0f},
                            Guard{1.0f, 0, 0x1c15, 0, 10.0f}, Guard{1.0f, 0, 0x1c14, 1, 4.0f}};
    m_terrain.heights->cells.fill(4.0f);
    m_world->Runtime().map = 0xb;
    Actor().byte1a1 = 1;
    Actor().dying = 1;
    for (const Guard& guard : guards)
    {
        SCOPED_TRACE(::testing::Message() << "alpha " << guard.alpha << ", +0x3e4 " << int{guard.portrait}
                                          << ", mascota " << guard.pet << ", +0x3e2 " << int{guard.safeZone});
        Actor().alpha = guard.alpha;
        Actor().byte3e4 = guard.portrait;
        Actor().pet.model = guard.pet;
        Actor().safeZone = guard.safeZone;
        Actor().position = {150.0f, 150.0f, 10.0f};
        m_world->PlaceModel(Client::Tables::PlayerShadowModel, {});
        m_batch.Begin({});

        const auto drawn = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(Actor().position.z, guard.grounded);
        const Client::Kernel::SharedModelPlacement shadow = m_world->ModelPlacement(Client::Tables::PlayerShadowModel);
        EXPECT_EQ(shadow.scale, guard.grounded == 4.0f ? 1.0f : 0.0f);
        const auto boots = BootVertices(m_batch);
        ASSERT_EQ(boots.size(), 3u);
        EXPECT_EQ(boots[0].position.z, guard.grounded);
    }
    Actor().alpha = 1.0f;
    Actor().byte3e4 = 0;
    Actor().pet.model = noPet;
    Actor().safeZone = 0;

    // Between original frames, the body goes to the floor but the z does not stay and the 0x20b is not touched.
    Actor().position = {150.0f, 150.0f, 10.0f};
    m_world->PlaceModel(Client::Tables::PlayerShadowModel, {});
    m_batch.Begin({});
    auto between = Pass();
    between.originalFrames = 0;
    const auto projected = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, between);
    ASSERT_TRUE(projected.HasValue()) << projected.Error();
    EXPECT_EQ(Actor().position.z, 10.0f);
    EXPECT_EQ(m_world->ModelPlacement(Client::Tables::PlayerShadowModel).scale, 0.0f);
    const auto boots = BootVertices(m_batch);
    ASSERT_EQ(boots.size(), 3u);
    EXPECT_EQ(boots[0].position.z, 4.0f);

    m_terrain.heights.reset();
    m_batch.Begin({});
    const auto blind = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());
    ASSERT_FALSE(blind.HasValue());
    EXPECT_NE(blind.Error().find("0x50bc9d"), std::string::npos) << blind.Error();
    EXPECT_EQ(Actor().position.z, 10.0f);
}

// 0x50bbf7: 0x6001b0 copies the position to the 0x20a's record (+0x6c..+0x74, 0x600247..0x600256) before the guards and
// the shadow's floor (0x50bcaf), and nothing writes it again in 0x50ba30 ([ebp−0x14] is only assigned at 0x50ba7f). In
// Blood Castle, the one that dies with the z at 10 and the terrain at 4 leaves the origin 10 in the shared model
// (Q-C412), and the sprites of the wizard's skin (0x50eab0: 0x4ec110 with [ebp−0x14] from 0x50ea70, bone 19 with (−4,
// 11, 0), the 0x5c6 and the 0x5fd) come out from 10, at (146, 161, 10); the boots and the shadow, at 4. Between
// original frames, the same, without touching the character's z or the shared model.
TEST_F(UnarmedGameCharacterTest, TheTailOfTheDyingOfBloodCastleHangsFromWhereTheyStoodBeforeTheGround)
{
    m_models.GrowTo(20);
    m_terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    m_terrain.heights->cells.fill(4.0f);
    m_world->Runtime().map = 0xb;
    ActorState& actor = Actor();
    actor.characterClass = 0; // the wizard: 0x50ea38 looks for class & 7 at 0
    actor.skin = 8;           // 0x50ea51: the skin & 0xf8 at 8
    actor.byte1a1 = 1;
    actor.dying = 1;
    actor.position = {150.0f, 150.0f, 10.0f};
    Client::World::FrameSprites sprites;
    auto pass = Pass();
    pass.sprites = &sprites;
    const auto sparksAt = [&sprites]()
    {
        std::vector<glm::vec3> found;
        for (const Client::World::Sprite& sprite : sprites.Sprites())
        {
            if (sprite.alive)
                found.push_back(sprite.position);
        }
        return found;
    };
    const std::vector<glm::vec3> fromTen(2, glm::vec3{146.0f, 161.0f, 10.0f});

    const auto drawn = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(actor.position.z, 4.0f);
    EXPECT_EQ(m_world->PlayerModel().origin, (glm::vec3{150.0f, 150.0f, 10.0f}));
    EXPECT_EQ(m_world->ModelPlacement(Client::Tables::PlayerShadowModel).origin, (glm::vec3{150.0f, 150.0f, 4.0f}));
    const auto boots = BootVertices(m_batch);
    ASSERT_EQ(boots.size(), 3u);
    EXPECT_EQ(boots[0].position.z, 4.0f);
    EXPECT_EQ(sparksAt(), fromTen);

    sprites.Clear();
    m_world->PlayerModel() = {};
    actor.position.z = 10.0f;
    m_batch.Begin({});
    pass.originalFrames = 0;
    const auto projected = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);
    ASSERT_TRUE(projected.HasValue()) << projected.Error();
    EXPECT_EQ(actor.position.z, 10.0f);
    EXPECT_EQ(m_world->PlayerModel().scale, 0.0f);
    const auto between = BootVertices(m_batch);
    ASSERT_EQ(between.size(), 3u);
    EXPECT_EQ(between[0].position.z, 4.0f);
    EXPECT_EQ(sparksAt(), fromTen);
}

// 0x4959d5: 0x50ba30(character, character, 0) with the character of the cards' portrait, outside the pool and with its
// own cloth. With +0x3e4 at 1 (0x4959a1) there is no shadow (0x50bc16) nor 0x505820 (0x50c913): the body without the
// shadow's 3 vertices, with the same rand() calls (the body's and the two closes of the hands). The pool is not
// touched.
TEST_F(UnarmedGameCharacterTest, OutsideThePoolTheLetterPortraitDrawsWithoutItsShadow)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        const ActorState pooled = Actor();
        for (const std::uint8_t portrait : {std::uint8_t{0}, std::uint8_t{1}})
        {
            SCOPED_TRACE(static_cast<int>(portrait));
            ActorState outside = pooled;
            outside.byte3e4 = portrait;
            Client::Actors::Drawing::CharacterCloth cloth;
            m_cloth.random = Client::Crt::CrtRandom(1);
            m_batch.Begin({});

            const auto drawn = Client::Actors::Drawing::DrawCharacterOutsideThePool(m_batch, m_models, *m_world,
                                                                                    outside, cloth, Pass(), true);

            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            EXPECT_EQ(m_batch.Vertices().size(), portrait != 0 ? 3u : 6u);
            EXPECT_EQ(m_cloth.random.State(), 0x18be873au);
            EXPECT_EQ(outside.drawnLight, glm::vec3(0.2f, 0.4f, 0.6f) + outside.light);
            EXPECT_EQ(Actor().drawnLight, pooled.drawnLight);
            EXPECT_EQ(Actor().animation.frame, pooled.animation.frame);
        }
    }
}

TEST_F(UnarmedGameCharacterTest, TheUnportedPeriodicStatusEffectStopsBeforeTheHandByteReads)
{
    struct Case
    {
        std::int32_t counter;
        std::uint8_t byte88;
        std::uint32_t flags;
        const char* cut;
        std::uint32_t random;
    };
    // 0x50e060 skips the back; 0x50e06b..0x50e0ba chooses the effect before the first access to +0x38e. Without the
    // effect, +0x78 at 0x800 or 0x4800 has neither 4 nor 0x2000000: 0x50fba4 does not run and the draw ends.
    constexpr std::array cases{
        Case{30, 1, 0x800, "0x50e0c0", 0x0029e2c0},
        Case{31, 1, 0x800, nullptr, 0x18be873a},
        Case{30, 0, 0x800, nullptr, 0x18be873a},
        Case{30, 1, 0x4800, nullptr, 0x18be873a},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << probe.counter << ' ' << int{probe.byte88} << ' ' << probe.flags);
        m_cloth.logicSteps = probe.counter;
        m_cloth.random = Client::Crt::CrtRandom(1);
        Actor().byte88 = probe.byte88;
        Actor().flags78 = probe.flags;
        m_batch.Begin({});

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        if (probe.cut == nullptr)
        {
            ASSERT_TRUE(result.HasValue()) << result.Error();
        }
        else
        {
            ASSERT_FALSE(result.HasValue());
            EXPECT_NE(result.Error().find(probe.cut), std::string::npos) << result.Error();
        }
        EXPECT_EQ(m_cloth.random.State(), probe.random);
        EXPECT_EQ(Actor().byte38eAccesses, 0);
        // Without a stop, 0x42da60 opens the gates at the end of the pass: the cell loses the 0x04.
        EXPECT_EQ(m_terrain.attributes->cells[Client::Data::Terrain::CellIndex(119, 115)],
                  probe.cut == nullptr ? 1 : 5);
        m_terrain.attributes->cells = *GateWalls();
    }
}

// 0x511290 gives the character its bone buffer (+0x11c at 1 at 0x511bf7, +0x120): the body does not go through the
// global buffer (0x68b181c) in an original frame or in a projected one, and only the original frame stores it in its
// own.
TEST_F(UnarmedGameCharacterTest, TheBodyGoesToItsOwnBonesAndNeverToTheGlobalPose)
{
    for (const int frames : {0, 1})
    {
        SCOPED_TRACE(frames);
        Actor().bodyPose.clear();
        m_batch.Begin({});
        auto pass = Pass();
        pass.originalFrames = frames;
        pass.publishProjectedPose = true;
        std::vector<std::uint16_t> published;
        pass.onPose = [&](std::uint16_t model, std::span<const Client::Math::BoneTransform>,
                          const Client::Models::ObjectTransform&, bool) { published.push_back(model); };

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(published.empty());
        if (frames == 0)
        {
            EXPECT_TRUE(Actor().bodyPose.empty());
        }
        else
        {
            ASSERT_EQ(Actor().bodyPose.size(), 1u);
            EXPECT_FLOAT_EQ(Actor().bodyPose[0][0][0], 1.0f);
        }
        EXPECT_EQ(Actor().byte38eAccesses, 0);
    }
}

// The body starts from its own buffer: a dummy bone (0x4eb973 does not write it) keeps what the character's previous
// draw left, not what is in the global one.
TEST_F(UnarmedGameCharacterTest, TheBodyKeepsTheDummyMatricesOfItsOwnBones)
{
    m_models.RetainTheDummyRoot();
    Client::Math::BoneTransform own(1.0f);
    own[3] = {7.0f, 11.0f, 13.0f};
    std::array<Client::Math::BoneTransform, 1> global{Client::Math::BoneTransform(1.0f)};
    global[0][3] = {70.0f, 110.0f, 130.0f};
    for (const int frames : {0, 1})
    {
        SCOPED_TRACE(frames);
        Actor().bodyPose.assign(1, own);
        m_cloth.random = Client::Crt::CrtRandom(1);
        m_batch.Begin({});
        auto pass = Pass();
        pass.originalFrames = frames;
        pass.poseBones = [&](std::uint16_t) { return std::span<const Client::Math::BoneTransform>(global); };

        const auto result = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 6u);
        // The boot starts after the shadow's triangle: vertex 0 + own dummy + the actor's position.
        EXPECT_EQ(m_batch.Vertices()[3].position, (glm::vec3{157.0f, 161.0f, 23.0f}));
        ASSERT_EQ(Actor().bodyPose.size(), 1u);
        EXPECT_EQ(Actor().bodyPose[0], own);
        EXPECT_EQ(m_cloth.random.State(), frames == 0 ? 1u : 0x18be873au);
        EXPECT_EQ(Actor().byte38eAccesses, 0);
    }
}
// The character as the caller of the login or the selection sets it up: the body of its step, without what hangs.
Client::Actors::Drawing::DrawnCharacter Drawn(const ActorState& actor)
{
    Client::Actors::Drawing::DrawnCharacter character;
    character.appearance = Client::Actors::AppearanceOfActor(actor);
    character.body = actor.animation;
    character.object = {.position = actor.position, .angles = actor.angles, .scale = actor.scale};
    return character;
}

// 0x50db57..0x510a7a in the login and the selection, which run the same 0x50ba30 as the game: the rand() of 0x50db57
// and the two closes of +0x38e of the hands, as in DrawGameCharacters, and the body in its own bones. Without 0x510ef0,
// which only runs in scene 5 (0x510a80): with +0x369 at 1 the game creates the 0x140 (here, without the effects table
// or the particles in the pass, it stops at 0x510f4d) and this does not.
TEST_F(UnarmedGameCharacterTest, OutsideTheGameTheTailSpendsTheSameRandomsWithoutTheSceneFiveEffect)
{
    Actor().byte88 = 1;
    Actor().byte369 = 1;
    const auto game = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());
    ASSERT_FALSE(game.HasValue());
    EXPECT_NE(game.Error().find("0x510f4d"), std::string::npos) << game.Error();

    m_cloth.random = Client::Crt::CrtRandom(1);
    Actor().bodyPose.clear();
    m_batch.Begin({});
    const auto tail = Client::Actors::Drawing::DrawCharacterTail(
        m_batch, m_models, *m_world, Actor(), 7, Drawn(Actor()), Pass(), Client::Actors::FullSetScan{}, true);

    ASSERT_TRUE(tail.HasValue()) << tail.Error();
    EXPECT_EQ(m_cloth.random.State(), 0x18be873au);
    EXPECT_EQ(Actor().byte38eAccesses, 0);
    EXPECT_EQ(Actor().bodyPose.size(), m_models.SkeletonOf(Client::Tables::PlayerSkeletonModel)->bones.size());
}

// Between original frames, on copies: neither the rand(), nor the own bones, nor the shared model.
TEST_F(UnarmedGameCharacterTest, OutsideTheGameAnIntermediateTailSpendsNothing)
{
    const auto tail = Client::Actors::Drawing::DrawCharacterTail(
        m_batch, m_models, *m_world, Actor(), 7, Drawn(Actor()), Pass(), Client::Actors::FullSetScan{}, false);

    ASSERT_TRUE(tail.HasValue()) << tail.Error();
    EXPECT_EQ(m_cloth.random.State(), 1u);
    EXPECT_TRUE(Actor().bodyPose.empty());
    EXPECT_EQ(m_world->PlayerModel().scale, 0.0f);
}

// 0x510abd: the raven runs in every scene. Without the trapezoid, the game stops at 0x418320; outside the game the
// caller draws it (the login), and the tail does not touch it.
TEST_F(UnarmedGameCharacterTest, OutsideTheGameWithoutTheTrapezoidTheCallerDrawsTheRaven)
{
    Actor().darkSpirit.emplace();
    const auto game = Client::Actors::Drawing::DrawGameCharacters(m_batch, m_models, *m_world, Pass());
    ASSERT_FALSE(game.HasValue());
    EXPECT_NE(game.Error().find("0x418320"), std::string::npos) << game.Error();

    m_batch.Begin({});
    const auto tail = Client::Actors::Drawing::DrawCharacterTail(
        m_batch, m_models, *m_world, Actor(), 7, Drawn(Actor()), Pass(), Client::Actors::FullSetScan{}, true);

    ASSERT_TRUE(tail.HasValue()) << tail.Error();
}

// Without the global rand flow, there is no tail.
TEST_F(UnarmedGameCharacterTest, TheTailNeedsTheGlobalRandom)
{
    auto pass = Pass();
    pass.clothWorld = nullptr;
    const auto tail = Client::Actors::Drawing::DrawCharacterTail(
        m_batch, m_models, *m_world, Actor(), 7, Drawn(Actor()), pass, Client::Actors::FullSetScan{}, true);

    ASSERT_FALSE(tail.HasValue());
    EXPECT_NE(tail.Error().find("0x50db57"), std::string::npos) << tail.Error();
}
} // namespace
