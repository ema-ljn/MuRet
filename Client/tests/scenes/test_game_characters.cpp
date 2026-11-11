#include "Scenes/GameCharacters.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
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

// 0x510ea9..0x510eb3 y 0x42afe0: después de dibujar el 0x20a, en el 30, con el asedio y con +0x16c, 0x42ddb0
// (que no está portado); sin el asedio, el personaje se dibuja como en otro mapa.
TEST_F(UnarmedGameCharacterTest, InTheCastleSiegeOnlyTheSiegeStopsAfterTheCharacter)
{
    m_world->Runtime().map = 30;
    const auto quiet = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());
    ASSERT_TRUE(quiet.HasValue()) << quiet.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);

    m_world->Runtime().siegeActive = true;
    m_batch.Begin({});
    const auto siege = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, Pass());
    ASSERT_FALSE(siege.HasValue());
    EXPECT_NE(siege.Error().find("0x42b00b"), std::string::npos) << siege.Error();
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
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

TEST_F(UnarmedGameCharacterTest, TheProjectedPoseCanBePublishedWithoutConsumingAnOriginalFrame)
{
    auto pass = Pass();
    pass.originalFrames = 0;
    std::vector<Client::Render::Models::BoneTransform> projected;
    std::optional<Client::Render::Models::ObjectTransform> object;
    pass.onPose = [&](std::uint16_t model, std::span<const Client::Render::Models::BoneTransform> bones,
                      const Client::Render::Models::ObjectTransform& transform, bool scaled)
    {
        if (model == Client::Data::Models::PlayerSkeletonModel)
        {
            projected.assign(bones.begin(), bones.end());
            object = transform;
            EXPECT_FALSE(scaled);
        }
    };
    // La ruta por defecto no publica los frames intermedios a la historia durable.
    ASSERT_TRUE(Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass).HasValue());
    EXPECT_TRUE(projected.empty());
    pass.publishProjectedPose = true;

    const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(projected.size(), 1u);
    ASSERT_TRUE(object.has_value());
    EXPECT_EQ(object->position, Actor().position);
    EXPECT_FLOAT_EQ(projected[0][0][0], 1.0f);
    EXPECT_EQ(m_cloth.random.State(), 1u);
    EXPECT_EQ(Actor().byte38eAccesses, 0);
}

TEST_F(UnarmedGameCharacterTest, BodyGeometryAndPublishedPoseUseTheSameRetainedDummyMatrices)
{
    m_models.RetainTheDummyRoot();
    std::array<Client::Render::Models::BoneTransform, 1> cached{Client::Render::Models::BoneTransform(1.0f)};
    cached[0][3] = {7.0f, 11.0f, 13.0f};
    for (const int frames : {0, 1})
    {
        SCOPED_TRACE(frames);
        m_cloth.random = Client::Render::Models::CrtRandom(1);
        m_batch.Begin({});
        auto pass = Pass();
        pass.originalFrames = frames;
        pass.publishProjectedPose = true;
        pass.poseBones = [&](std::uint16_t model) -> std::span<const Client::Render::Models::BoneTransform>
        {
            return model == Client::Data::Models::PlayerSkeletonModel
                       ? std::span<const Client::Render::Models::BoneTransform>(cached)
                       : std::span<const Client::Render::Models::BoneTransform>{};
        };
        std::optional<Client::Render::Models::BoneTransform> published;
        pass.onPose = [&](std::uint16_t model, std::span<const Client::Render::Models::BoneTransform> bones,
                          const Client::Render::Models::ObjectTransform&, bool)
        {
            if (model == Client::Data::Models::PlayerSkeletonModel && !bones.empty())
                published = bones[0];
        };

        const auto result = Client::Scenes::DrawGameCharacters(m_batch, m_models, *m_world, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_TRUE(published.has_value());
        EXPECT_EQ(*published, cached[0]);
        ASSERT_EQ(m_batch.Vertices().size(), 6u);
        // La bota empieza después del triángulo de la sombra: vértice0 + dummy + posición del actor.
        EXPECT_EQ(m_batch.Vertices()[3].position, (glm::vec3{157.0f, 161.0f, 23.0f}));
        EXPECT_EQ(m_cloth.random.State(), frames == 0 ? 1u : 0x18be873au);
        EXPECT_EQ(Actor().byte38eAccesses, 0);
    }
}
} // namespace
