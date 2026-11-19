#include "Scenes/GameCharacters.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/ModelVertex.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorAppearance.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorPickingBox.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GuildMarks.h"
#include "Scenes/GuildState.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/TerrainLighting.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/WorldTerrain.h"

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

// Client::Scenes::DrawGameCharacters (0x510d00 en la escena 5; 06 § El dibujo del juego): el héroe que no se
// dibuja, el corte en el primer personaje vivo y visto (0x50ba30) y el TerrainWall de 0x42da60 al final.
namespace
{
using Client::Scenes::ActorState;

constexpr std::size_t Hero = 42;

// Un TerrainWall con el 0x04 en las cuatro celdas debajo de la puerta (119, 114) y en su fila.
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

// 0x510e75: sin un personaje vivo y visto, ninguno va a 0x50ba30; el héroe sin +0x1d8 & 4 tampoco se toca. Al
// final, 0x42da40 abre las puertas del TerrainWall.
TEST(GameCharactersTest, WithoutASeenCharacterThePassOpensTheGates)
{
    auto actors = std::make_unique<std::array<ActorState, 400>>();
    (*actors)[Hero].alive = true;
    (*actors)[7].seen = true;
    auto walls = GateWalls();

    const auto drawn = Client::Scenes::DrawGameCharacters(*actors, Hero, *walls);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    for (std::uint8_t x = 118; x <= 121; ++x)
    {
        EXPECT_EQ((*walls)[Client::Data::Terrain::CellIndex(x, 115)], 0x01) << static_cast<int>(x);
    }
    EXPECT_EQ((*actors)[Hero].field13c, 0.0f);
}

// 0x510e40..0x510e73: el héroe con +0x1d8 & 4 no se dibuja aunque esté vivo y visto; en la escena 5 le quedan
// +0x13c en 1.000 y +0x148, +0x158 y +0x168 en 1. Otro con ese bit no es el héroe y corta.
TEST(GameCharactersTest, TheHeroWithControlBitFourIsNotDrawn)
{
    auto actors = std::make_unique<std::array<ActorState, 400>>();
    ActorState& hero = (*actors)[Hero];
    hero.alive = true;
    hero.seen = true;
    hero.controlCode = Client::Scenes::UndrawnHeroControlBit;
    auto walls = GateWalls();

    ASSERT_TRUE(Client::Scenes::DrawGameCharacters(*actors, Hero, *walls).HasValue());
    EXPECT_EQ(hero.field13c, 1000.0f);
    EXPECT_EQ(hero.field148, 1.0f);
    EXPECT_EQ(hero.field158, 1.0f);
    EXPECT_EQ(hero.field168, 1.0f);

    const auto other = Client::Scenes::DrawGameCharacters(*actors, Hero + 1, *walls);
    ASSERT_FALSE(other.HasValue());
    EXPECT_NE(other.Error().find("0x510ea1"), std::string::npos) << other.Error();
}

TEST(GameCharactersTest, ProjectedHeroSuppressionWritesOnlyTheProjectionBox)
{
    auto actors = std::make_unique<std::array<ActorState, 400>>();
    ActorState& hero = (*actors)[Hero];
    hero.alive = true;
    hero.seen = true;
    hero.controlCode = Client::Scenes::UndrawnHeroControlBit;
    hero.field13c = 10.0f;
    hero.field140 = 20.0f;
    hero.field144 = 30.0f;
    hero.field148 = 40.0f;
    hero.field158 = 50.0f;
    hero.field168 = 60.0f;
    std::array<Client::Scenes::ActorPickingBox, 400> boxes{};
    boxes[Hero] = {.origin = {1.0f, 2.0f, 3.0f},
                   .axisX = {4.0f, 5.0f, 6.0f},
                   .axisY = {7.0f, 8.0f, 9.0f},
                   .axisZ = {10.0f, 11.0f, 12.0f}};
    auto walls = GateWalls();

    ASSERT_TRUE(Client::Scenes::DrawGameCharacters(*actors, Hero, *walls, nullptr, boxes).HasValue());

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

// 0x510ea1: el primer personaje vivo y visto, por lugar, va a 0x50ba30, que MuRet no tiene en el juego: corta
// con su lugar, sin abrir las puertas (0x42da60 va después del lazo).
TEST(GameCharactersTest, TheFirstSeenCharacterStopsThePass)
{
    auto actors = std::make_unique<std::array<ActorState, 400>>();
    (*actors)[Hero].alive = true;
    (*actors)[Hero].seen = true;
    (*actors)[300].alive = true;
    (*actors)[300].seen = true;
    auto walls = GateWalls();

    const auto drawn = Client::Scenes::DrawGameCharacters(*actors, Hero, *walls);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x510ea1"), std::string::npos) << drawn.Error();
    EXPECT_NE(drawn.Error().find("personaje 42 "), std::string::npos) << drawn.Error();
    EXPECT_EQ((*walls)[Client::Data::Terrain::CellIndex(119, 115)], 0x05);
}

constexpr std::uint16_t Boots = Client::Data::Models::ItemModel(11, 0);

class UnarmedModels final : public Client::Scenes::IModelLibrary
{
public:
    UnarmedModels()
    {
        Client::Render::Models::SkeletonBone bone;
        bone.actions.assign(Client::Tests::PlayerActionCount, {Client::Render::Models::BoneKey{}});
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
        return model == Boots || model == Client::Data::Models::PlayerShadowModel ? &m_model : nullptr;
    }
    [[nodiscard]] const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == Client::Data::Models::PlayerSkeletonModel ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return m_textures;
    }
    void RetainTheDummyRoot()
    {
        m_skeleton.bones[0].dummy = true;
    }
    // Más huesos como el primero, hasta `count`: el 26 es el de la marca del gremio (0x5091e0).
    void GrowTo(std::size_t count)
    {
        while (m_skeleton.bones.size() < count)
            m_skeleton.bones.push_back(m_skeleton.bones.front());
    }

private:
    Client::Data::Models::Model m_model;
    Client::Render::Models::Skeleton m_skeleton;
    std::array<Client::Render::TextureSlot, 1> m_textures{Client::Render::TextureSlot{0x300}};
};

class UnarmedGameCharacterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_world = std::make_unique<Client::Scenes::WorldState>();
        m_world->Runtime().map = 2;
        m_world->Runtime().hero = Hero;
        m_world->Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        m_world->Map().terrain.attributes->cells = *GateWalls();
        m_light = std::make_unique<Client::World::TerrainLighting>();
        m_light->cells.fill({0.2f, 0.4f, 0.6f});
        ActorState& actor = Actor();
        actor = Client::Tests::UnarmedPlayer(1);
        actor.alive = true;
        actor.seen = true;
        actor.word376 = 0xffff;
        actor.head = 0xffff;
        for (auto& part : actor.body)
            part.model = Client::Scenes::NoItemModel;
        actor.body.back().model = Boots;
        actor.position = {150.0f, 150.0f, 10.0f};
        actor.scale = 1.0f;
        actor.alpha = 1.0f;
        actor.pk = 3;
        actor.animation.action = 0x0d; // 0x4f3a60: caminar sin arma para las clases 0/1
        m_batch.Begin({});
    }

    [[nodiscard]] ActorState& Actor()
    {
        return m_world->Actors()[7];
    }
    [[nodiscard]] Client::Scenes::GameCharacterPass Pass()
    {
        return {.dynamicLighting = m_light.get(), .clothWorld = &m_cloth};
    }

    std::unique_ptr<Client::Scenes::WorldState> m_world;
    std::unique_ptr<Client::World::TerrainLighting> m_light;
    UnarmedModels m_models;
    Client::Render::Models::ClothWorld m_cloth;
    Client::Render::ModelBatch m_batch;
};

// Los vértices de los tramos dibujados con `slot`.
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

// 0x50d4fb..0x50d588 y 0x5091e0: otro jugador con gremio lleva la marca (0x5f16a0 con el fondo transparente) en el
// hueso 26: el quad de 0x621320(5, 7) por R_ConcatTransforms(hueso, AngleMatrix(giro + (80, 45, 135)) con (20, −5,
// −10)) más la posición, uv (0, 1), (1, 1), (1, 0), (0, 0). Con la armadura 0x1226, z en −18. Sin rand() propios. En
// Lorencia, en Devias y en Chaos Castle (18), donde no va; tampoco con la bandera 0x4000 de +0x78, ni con un índice
// cuya clave la tabla no encuentra hasta la cuenta (0x447b30), salvo el 0x7cf, que no la busca.
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
        m_cloth.random = Client::Render::Models::CrtRandom(1);
        m_batch.Begin({});
        auto pass = Pass();
        pass.guildMarks = &marks;
        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);
        EXPECT_TRUE(result.HasValue()) << result.Error();
        return VerticesWith(m_batch, drawn);
    };
    const auto expectedQuad = [&](float lowZ)
    {
        const Client::Render::Models::OriginalMatrix local = [&]
        {
            auto turn = Client::Render::Models::OriginalAngleMatrix(
                {Actor().angles.x + 80.0f, Actor().angles.y + 45.0f, Actor().angles.z + 135.0f});
            turn[3] = 20.0f;
            turn[7] = -5.0f;
            turn[11] = lowZ;
            return turn;
        }();
        const auto matrix = Client::Render::Models::OriginalConcatTransforms(
            Client::Render::Models::OriginalMatrixOf(Actor().bodyPose.at(26)), local);
        const std::array<glm::vec3, 4> corners{{{-5, -5, -7}, {5, 5, -7}, {5, 5, 7}, {-5, -5, 7}}};
        std::array<glm::vec3, 4> points{};
        for (std::size_t i = 0; i < corners.size(); ++i)
            points[i] = Actor().position + Client::Render::Models::OriginalVectorTransform(corners[i], matrix);
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
        // Las dos mitades del quad: (0, 1, 2) y (0, 2, 3).
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
    Actor().body[1].model = Client::Scenes::NoItemModel;

    Actor().flags78 |= 0x4000;
    EXPECT_TRUE(draw(2, slot).empty());
    Actor().flags78 &= ~0x4000u;
    // En Chaos Castle, sin el Angel (+0x88 distinto de 1), el cuerpo de siempre y ninguna marca.
    const std::uint8_t kind = Actor().byte88;
    Actor().byte88 = 0;
    EXPECT_TRUE(draw(18, slot).empty());
    Actor().byte88 = kind;
    // La entrada 10 está después de la cuenta (4): 0x447b00 no encuentra su clave.
    Actor().word1f2 = 10;
    guild.table[10].key = 99;
    const Client::Render::TextureSlot tenth = Client::Scenes::GuildMarkTextures::SlotOf(10, true);
    EXPECT_TRUE(draw(2, tenth).empty());
    EXPECT_FALSE(store.Find(tenth).has_value());
    Actor().word1f2 = 1999;
    guild.table[1999].mark.fill(2);
    EXPECT_EQ(draw(2, Client::Scenes::GuildMarkTextures::SlotOf(1999, true)).size(), 6u);
}

// 50e298 abre +38e; 50e503 lee; 50e68a cierra con un rand; 50e746 prueba NoItem después.
// El lazo repite para ambas manos (50e9ea..f4), aunque las dos estén vacías.
TEST_F(UnarmedGameCharacterTest, WalkingUnarmedClosesBothHandBytesAfterTheBodyRandom)
{
    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_cloth.random.State(), 0x18be873au); // semilla1: 50db57 y dos cierres de un byte
    EXPECT_EQ(Actor().byte38eAccesses, 0);
    EXPECT_EQ(m_world->Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(119, 115)], 1);
}

// 510ea9..510eb3 y42afe0: después del0x20a,42ddb0 emite638 en un defensor cerca de una marca cada10pasos.
// Los tres CRT del cuerpo/manos preceden a los dos del jitter de la bendición (53fe70).
TEST_F(UnarmedGameCharacterTest, TheCastleSiegeBlessingFollowsTheCompletedCharacterAndItsRandoms)
{
    m_world->Runtime().map = 30;
    Actor().position = {8200.0f, 13000.0f, 10.0f};
    Actor().flags78 |= 0x40000;
    auto pass = Pass();
    pass.particles = &m_world->Particles();
    const auto quiet = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);
    ASSERT_TRUE(quiet.HasValue()) << quiet.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_cloth.random.State(), 0x18be873au);
    EXPECT_FALSE(m_world->Particles().All().front().alive);

    m_world->Runtime().siegeActive = true;
    m_cloth.random = Client::Render::Models::CrtRandom(1);
    m_batch.Begin({});
    const auto siege = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);
    ASSERT_TRUE(siege.HasValue()) << siege.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    const auto& blessing = m_world->Particles().All().front();
    ASSERT_TRUE(blessing.alive);
    EXPECT_EQ(blessing.type, Client::World::SiegeBlessingParticle);
    EXPECT_EQ(blessing.life, 20);
    EXPECT_EQ(blessing.position, (glm::vec3{8195.0f, 13014.0f, 250.0f}));
    EXPECT_EQ(blessing.light, Actor().light);
    Client::Render::Models::CrtRandom expected(1);
    expected.Discard(5);
    EXPECT_EQ(m_cloth.random.State(), expected.State());
    EXPECT_EQ(m_world->Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(119, 115)], 1);
}

TEST_F(UnarmedGameCharacterTest, TheCastleSiegeRequiresTheParticlePoolAfterTheBodyPrefix)
{
    m_world->Runtime().map = 30;
    m_world->Runtime().siegeActive = true;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x42de7f"), std::string::npos) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_cloth.random.State(), 0x18be873au);
    // No llega a42da60: el bit04 de las puertas sigue cerrado.
    EXPECT_EQ(m_world->Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(119, 115)], 5);
}

TEST_F(UnarmedGameCharacterTest, HeldOrWrappedHandByteDepthDoesNotConsumeClosingRandoms)
{
    for (const std::uint8_t depth : std::array<std::uint8_t, 5>{0, 1, 2, 254, 255})
    {
        SCOPED_TRACE(static_cast<int>(depth));
        Actor().byte38eAccesses = depth;
        m_cloth.random = Client::Render::Models::CrtRandom(1);
        m_batch.Begin({});

        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

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
    // 50ccb5..50d219: 3.848 rand del registro propio; después 50db57 y los dos cierres de +38e.
    constexpr std::array cases{Case{0, 0, 0x059c5342}, Case{1, 0, 0x18be873a}, Case{0, 1, 0x46d18088},
                               Case{1, 1, 0x0029e2c0}, Case{255, 255, 0x0029e2c0}};
    m_world->Runtime().hero = 7;
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.ownDepth));
        SCOPED_TRACE(static_cast<int>(probe.handDepth));
        m_world->Runtime().ownRecordDepth = probe.ownDepth;
        Actor().byte38eAccesses = probe.handDepth;
        m_cloth.random = Client::Render::Models::CrtRandom(1);
        m_batch.Begin({});

        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

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
    // 0x4f2250: parado1 salvo Elf2; 0x4f3a60: caminar0xd salvo Elf0xe, correr0x16.
    constexpr std::array cases{Case{0, 1},    Case{1, 1},    Case{2, 2},    Case{0, 0x0d}, Case{1, 0x0d},
                               Case{2, 0x0e}, Case{0, 0x16}, Case{1, 0x16}, Case{2, 0x16}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.characterClass));
        SCOPED_TRACE(static_cast<int>(probe.action));
        Actor().characterClass = probe.characterClass;
        Actor().animation.action = probe.action;
        m_cloth.random = Client::Render::Models::CrtRandom(1);
        m_batch.Begin({});

        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

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
        m_cloth.random = Client::Render::Models::CrtRandom(1);
        m_batch.Begin({});

        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

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

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

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

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_cloth.random.State(), 0x3d6c1037u); // seis rand: (cuerpo + dos manos) por dos frames
    EXPECT_EQ(Actor().byte38eAccesses, 0);
    // 0x509340 deja la pose del cuerpo en los huesos propios (+0x120) para el paso siguiente (0x506d25).
    EXPECT_EQ(Actor().bodyPose.size(), m_models.SkeletonOf(Client::Data::Models::PlayerSkeletonModel)->bones.size());
}

// Q-C412: 0x6001b0 copia la escala y la posición del personaje al modelo compartido de Player.bmd antes de posar,
// con el alpha desde 0,01 (0x6b88d4); el cuervo de otro las lee en la zona segura. Un dibujo intermedio no las
// toca.
TEST_F(UnarmedGameCharacterTest, TheBodyLeavesItsScaleAndPositionInThePlayerModel)
{
    Actor().scale = 1.3f;
    auto pass = Pass();
    pass.originalFrames = 0;
    ASSERT_TRUE(Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
    EXPECT_EQ(m_world->PlayerModel().scale, 0.0f);

    pass.originalFrames = 1;
    const auto drawn = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(m_world->PlayerModel().scale, 1.3f);
    EXPECT_EQ(m_world->PlayerModel().origin, Actor().position);

    m_world->PlayerModel() = {};
    Actor().alpha = 0.009f;
    ASSERT_TRUE(Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
    EXPECT_EQ(m_world->PlayerModel().scale, 0.0f);
}

// Los vértices de las botas (la textura 0x300 de UnarmedModels), opacas.
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

// La z de un dibujo: dos NaN son la misma; si no, el mismo valor y el mismo signo (−0 no es +0).
bool SameZ(float drawn, float expected)
{
    if (std::isnan(expected))
        return std::isnan(drawn);
    return drawn == expected && std::signbit(drawn) == std::signbit(expected);
}

// 0x50bc77..0x50bcbd: con la sombra, en Blood Castle (los mapas 0xb a 0x11), el que muere empujado (+0x1a1 y +0x389)
// baja a la altura del terreno (0x5f93c0) antes de 0x60cf70 (0x50bcec), que copia ese origen al 0x20b; el cuerpo se
// dibuja ahí. El fcom con `test ah, 1` escribe también con la z o la altura en NaN; con el terreno a la altura de la z
// o arriba no escribe: con la z en −0 y el terreno en +0, la z queda en −0 (un `>` la pisaría con +0). Fuera de esos
// mapas (Devias y Chaos Castle), sin +0x1a1 o sin morirse, la z queda. El suelo va detrás de las guardas de la sombra
// (0x50bc05..0x50bc71): con el alpha por debajo de 0,5 (en 0,5 baja), con +0x3e4 o con una mascota que se monta
// (0x1c14, 0x1c15) fuera de la zona segura (+0x3e2), ni sombra ni suelo. Sin las alturas, corta en la llamada a
// 0x5f93c0.
TEST_F(UnarmedGameCharacterTest, TheDyingOfBloodCastleSinkToTheGroundUnderTheirShadow)
{
    m_world->Map().terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
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
        m_world->Map().terrain.heights->cells.fill(probe.height);
        m_world->Runtime().map = probe.map;
        // En Chaos Castle, sin el Angel (+0x88 distinto de 1).
        Actor().byte88 = probe.map == 0x12 ? std::uint8_t{0} : kind;
        Actor().byte1a1 = probe.byte1a1;
        Actor().dying = probe.dying;
        Actor().position = {150.0f, 150.0f, probe.z};
        m_world->PlaceModel(Client::Data::Models::PlayerShadowModel, {});
        m_cloth.random = Client::Render::Models::CrtRandom(1);
        m_batch.Begin({});

        const auto drawn = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(Actor().position.x, 150.0f);
        EXPECT_EQ(Actor().position.y, 150.0f);
        EXPECT_TRUE(SameZ(Actor().position.z, probe.grounded)) << Actor().position.z;
        const Client::Scenes::SharedModelPlacement shadow =
            m_world->ModelPlacement(Client::Data::Models::PlayerShadowModel);
        EXPECT_TRUE(SameZ(shadow.origin.z, probe.grounded)) << shadow.origin.z;
        EXPECT_EQ(shadow.scale, 1.0f);
        const auto boots = BootVertices(m_batch);
        ASSERT_EQ(boots.size(), 3u);
        // El vértice suma el origen a su 0: con −0 sale +0.
        const float boot = boots[0].position.z;
        EXPECT_TRUE(std::isnan(probe.grounded) ? std::isnan(boot) : boot == probe.grounded) << boot;
    }
    Actor().byte88 = kind;

    // Detrás de las guardas de la sombra: sin ella, la z queda y el 0x20b no se toca.
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
    m_world->Map().terrain.heights->cells.fill(4.0f);
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
        m_world->PlaceModel(Client::Data::Models::PlayerShadowModel, {});
        m_batch.Begin({});

        const auto drawn = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(Actor().position.z, guard.grounded);
        const Client::Scenes::SharedModelPlacement shadow =
            m_world->ModelPlacement(Client::Data::Models::PlayerShadowModel);
        EXPECT_EQ(shadow.scale, guard.grounded == 4.0f ? 1.0f : 0.0f);
        const auto boots = BootVertices(m_batch);
        ASSERT_EQ(boots.size(), 3u);
        EXPECT_EQ(boots[0].position.z, guard.grounded);
    }
    Actor().alpha = 1.0f;
    Actor().byte3e4 = 0;
    Actor().pet.model = noPet;
    Actor().safeZone = 0;

    // Entre frames del original, el cuerpo va al suelo pero la z no queda ni el 0x20b se toca.
    Actor().position = {150.0f, 150.0f, 10.0f};
    m_world->PlaceModel(Client::Data::Models::PlayerShadowModel, {});
    m_batch.Begin({});
    auto between = Pass();
    between.originalFrames = 0;
    const auto projected = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, between);
    ASSERT_TRUE(projected.HasValue()) << projected.Error();
    EXPECT_EQ(Actor().position.z, 10.0f);
    EXPECT_EQ(m_world->ModelPlacement(Client::Data::Models::PlayerShadowModel).scale, 0.0f);
    const auto boots = BootVertices(m_batch);
    ASSERT_EQ(boots.size(), 3u);
    EXPECT_EQ(boots[0].position.z, 4.0f);

    m_world->Map().terrain.heights.reset();
    m_batch.Begin({});
    const auto blind = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());
    ASSERT_FALSE(blind.HasValue());
    EXPECT_NE(blind.Error().find("0x50bc9d"), std::string::npos) << blind.Error();
    EXPECT_EQ(Actor().position.z, 10.0f);
}

// 0x50bbf7: 0x6001b0 copia la posición al registro del 0x20a (+0x6c..+0x74, 0x600247..0x600256) antes de las guardas y
// del suelo de la sombra (0x50bcaf), y nada la vuelve a escribir en 0x50ba30 ([ebp−0x14] se asigna sólo en 0x50ba7f).
// En Blood Castle, el que muere con la z en 10 y el terreno en 4 deja el origen 10 en el modelo compartido (Q-C412), y
// los sprites de la piel del mago (0x50eab0: 0x4ec110 con [ebp−0x14] desde 0x50ea70, el hueso 19 con (−4, 11, 0), el
// 0x5c6 y el 0x5fd) salen desde 10, en (146, 161, 10); las botas y la sombra, en 4. Entre frames del original, lo
// mismo, sin tocar la z del personaje ni el modelo compartido.
TEST_F(UnarmedGameCharacterTest, TheTailOfTheDyingOfBloodCastleHangsFromWhereTheyStoodBeforeTheGround)
{
    m_models.GrowTo(20);
    m_world->Map().terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    m_world->Map().terrain.heights->cells.fill(4.0f);
    m_world->Runtime().map = 0xb;
    ActorState& actor = Actor();
    actor.characterClass = 0; // el mago: 0x50ea38 mira clase & 7 en 0
    actor.skin = 8;           // 0x50ea51: la piel & 0xf8 en 8
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

    const auto drawn = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(actor.position.z, 4.0f);
    EXPECT_EQ(m_world->PlayerModel().origin, (glm::vec3{150.0f, 150.0f, 10.0f}));
    EXPECT_EQ(m_world->ModelPlacement(Client::Data::Models::PlayerShadowModel).origin,
              (glm::vec3{150.0f, 150.0f, 4.0f}));
    const auto boots = BootVertices(m_batch);
    ASSERT_EQ(boots.size(), 3u);
    EXPECT_EQ(boots[0].position.z, 4.0f);
    EXPECT_EQ(sparksAt(), fromTen);

    sprites.Clear();
    m_world->PlayerModel() = {};
    actor.position.z = 10.0f;
    m_batch.Begin({});
    pass.originalFrames = 0;
    const auto projected = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);
    ASSERT_TRUE(projected.HasValue()) << projected.Error();
    EXPECT_EQ(actor.position.z, 10.0f);
    EXPECT_EQ(m_world->PlayerModel().scale, 0.0f);
    const auto between = BootVertices(m_batch);
    ASSERT_EQ(between.size(), 3u);
    EXPECT_EQ(between[0].position.z, 4.0f);
    EXPECT_EQ(sparksAt(), fromTen);
}

// 0x4959d5: 0x50ba30(personaje, personaje, 0) con el personaje del retrato de las cartas, fuera del pool y con su tela
// propia. Con +0x3e4 en 1 (0x4959a1) no hay sombra (0x50bc16) ni 0x505820 (0x50c913): el cuerpo sin los 3 vértices de
// la sombra, con los mismos rand() (el del cuerpo y los dos cierres de las manos). El pool no se toca.
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
            Client::Scenes::CharacterCloth cloth;
            m_cloth.random = Client::Render::Models::CrtRandom(1);
            m_batch.Begin({});

            const auto drawn =
                Client::Scenes::DrawCharacterOutsideThePool(m_batch, m_models, *m_world, outside, cloth, Pass(), true);

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
    // 50e060 omite la espalda; 50e06b..50e0ba elige el efecto antes del primer acceso a +38e. Sin el
    // efecto, +0x78 en 0x800 o 0x4800 no tiene ni 4 ni 0x2000000: 0x50fba4 no corre y el dibujo termina.
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
        m_cloth.random = Client::Render::Models::CrtRandom(1);
        Actor().byte88 = probe.byte88;
        Actor().flags78 = probe.flags;
        m_batch.Begin({});

        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());

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
        // Sin corte, 0x42da60 abre las puertas al final del pase: la celda pierde el 0x04.
        EXPECT_EQ(m_world->Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(119, 115)],
                  probe.cut == nullptr ? 1 : 5);
        m_world->Map().terrain.attributes->cells = *GateWalls();
    }
}

// 0x511290 le da al personaje su buffer de huesos (+0x11c en 1 en 0x511bf7, +0x120): el cuerpo no pasa por el
// buffer global (0x68b181c) ni en un frame del original ni en uno proyectado, y sólo el frame del original lo
// guarda en el propio.
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
        pass.onPose = [&](std::uint16_t model, std::span<const Client::Render::Models::BoneTransform>,
                          const Client::Render::Models::ObjectTransform&, bool) { published.push_back(model); };

        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

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

// El cuerpo arranca de su buffer propio: un hueso dummy (0x4eb973 no lo escribe) conserva lo que dejó el dibujo
// anterior del personaje, no lo que haya en el global.
TEST_F(UnarmedGameCharacterTest, TheBodyKeepsTheDummyMatricesOfItsOwnBones)
{
    m_models.RetainTheDummyRoot();
    Client::Render::Models::BoneTransform own(1.0f);
    own[3] = {7.0f, 11.0f, 13.0f};
    std::array<Client::Render::Models::BoneTransform, 1> global{Client::Render::Models::BoneTransform(1.0f)};
    global[0][3] = {70.0f, 110.0f, 130.0f};
    for (const int frames : {0, 1})
    {
        SCOPED_TRACE(frames);
        Actor().bodyPose.assign(1, own);
        m_cloth.random = Client::Render::Models::CrtRandom(1);
        m_batch.Begin({});
        auto pass = Pass();
        pass.originalFrames = frames;
        pass.poseBones = [&](std::uint16_t) { return std::span<const Client::Render::Models::BoneTransform>(global); };

        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 6u);
        // La bota empieza después del triángulo de la sombra: vértice0 + dummy propio + posición del actor.
        EXPECT_EQ(m_batch.Vertices()[3].position, (glm::vec3{157.0f, 161.0f, 23.0f}));
        ASSERT_EQ(Actor().bodyPose.size(), 1u);
        EXPECT_EQ(Actor().bodyPose[0], own);
        EXPECT_EQ(m_cloth.random.State(), frames == 0 ? 1u : 0x18be873au);
        EXPECT_EQ(Actor().byte38eAccesses, 0);
    }
}
// El personaje como lo arma el caller del login o de la selección: el cuerpo de su paso, sin lo que cuelga.
Client::Scenes::DrawnCharacter Drawn(const ActorState& actor)
{
    Client::Scenes::DrawnCharacter character;
    character.appearance = Client::Scenes::AppearanceOfActor(actor);
    character.body = actor.animation;
    character.object = {.position = actor.position, .angles = actor.angles, .scale = actor.scale};
    return character;
}

// 0x50db57..0x510a7a en el login y la selección, que corren el mismo 0x50ba30 que el juego: el rand() de 0x50db57 y
// los dos cierres de +0x38e de las manos, como en DrawGameCharacters, y el cuerpo en sus huesos propios. Sin
// 0x510ef0, que sólo corre en la escena 5 (0x510a80): con +0x369 en 1 el juego crea el 0x140 (aquí, sin la tabla de
// efectos ni las partículas en el pase, corta en 0x510f4d) y esto no.
TEST_F(UnarmedGameCharacterTest, OutsideTheGameTheTailSpendsTheSameRandomsWithoutTheSceneFiveEffect)
{
    Actor().byte88 = 1;
    Actor().byte369 = 1;
    const auto game = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());
    ASSERT_FALSE(game.HasValue());
    EXPECT_NE(game.Error().find("0x510f4d"), std::string::npos) << game.Error();

    m_cloth.random = Client::Render::Models::CrtRandom(1);
    Actor().bodyPose.clear();
    m_batch.Begin({});
    const auto tail = Client::Scenes::DrawCharacterTail(m_batch, m_models, *m_world, Actor(), 7, Drawn(Actor()), Pass(),
                                                        Client::Scenes::FullSetScan{}, true);

    ASSERT_TRUE(tail.HasValue()) << tail.Error();
    EXPECT_EQ(m_cloth.random.State(), 0x18be873au);
    EXPECT_EQ(Actor().byte38eAccesses, 0);
    EXPECT_EQ(Actor().bodyPose.size(), m_models.SkeletonOf(Client::Data::Models::PlayerSkeletonModel)->bones.size());
}

// Entre frames del original, sobre copias: ni el rand(), ni los huesos propios, ni el modelo compartido.
TEST_F(UnarmedGameCharacterTest, OutsideTheGameAnIntermediateTailSpendsNothing)
{
    const auto tail = Client::Scenes::DrawCharacterTail(m_batch, m_models, *m_world, Actor(), 7, Drawn(Actor()), Pass(),
                                                        Client::Scenes::FullSetScan{}, false);

    ASSERT_TRUE(tail.HasValue()) << tail.Error();
    EXPECT_EQ(m_cloth.random.State(), 1u);
    EXPECT_TRUE(Actor().bodyPose.empty());
    EXPECT_EQ(m_world->PlayerModel().scale, 0.0f);
}

// 0x510abd: el cuervo corre en todas las escenas. Sin el trapecio, el juego corta en 0x418320; fuera del juego lo
// dibuja el caller (el login), y la cola no lo toca.
TEST_F(UnarmedGameCharacterTest, OutsideTheGameWithoutTheTrapezoidTheCallerDrawsTheRaven)
{
    Actor().darkSpirit.emplace();
    const auto game = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());
    ASSERT_FALSE(game.HasValue());
    EXPECT_NE(game.Error().find("0x418320"), std::string::npos) << game.Error();

    m_batch.Begin({});
    const auto tail = Client::Scenes::DrawCharacterTail(m_batch, m_models, *m_world, Actor(), 7, Drawn(Actor()), Pass(),
                                                        Client::Scenes::FullSetScan{}, true);

    ASSERT_TRUE(tail.HasValue()) << tail.Error();
}

// Sin el flujo global de rand, no hay cola.
TEST_F(UnarmedGameCharacterTest, TheTailNeedsTheGlobalRandom)
{
    auto pass = Pass();
    pass.clothWorld = nullptr;
    const auto tail = Client::Scenes::DrawCharacterTail(m_batch, m_models, *m_world, Actor(), 7, Drawn(Actor()), pass,
                                                        Client::Scenes::FullSetScan{}, true);

    ASSERT_FALSE(tail.HasValue());
    EXPECT_NE(tail.Error().find("0x50db57"), std::string::npos) << tail.Error();
}
} // namespace
