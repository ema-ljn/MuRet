#include "support/CheckedWorldObjectProjection.h"

#include "Scenes/WorldObjectsDrawing.h"

#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Render/ModelBatch.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/TextureMetadata.h"
#include "Data/Textures/Texture.h"
#include "World/TerrainCulling.h"
#include "Render/Billboard.h"
#include "Render/Models/Cloth.h"
#include "World/ParticleDrawing.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "Render/Models/Skinning.h"
#include "World/TerrainLighting.h"
#include "World/ModelEffects.h"
#include "World/WorldObjects.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <array>
#include <memory>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Client::Scenes::StepWorldObjects y DrawWorldObjects (06 § El mundo del login): qué objetos avanzan,
// en qué orden se dibujan y cómo van los remolinos del mapa 55, con modelos de mentira.
namespace
{
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::TextureSlot;
using Client::Render::Models::Skeleton;
using Client::Scenes::DrawWorldObjects;
using Client::Scenes::StepWorldObjects;
using Client::Scenes::TextureMetadata;
using Client::World::WorldObject;

constexpr std::uint8_t Login = 55;
constexpr std::uint8_t Lorencia = 0;
constexpr std::int16_t Bridge = 0x50;
constexpr std::int16_t RedWhirlpool = 0x56;
constexpr std::int16_t DarkWhirlpool = 0x5a;
constexpr std::int16_t Rock = 1;

// Cualquier modelo: dos mallas de un triángulo del hueso 0 (texturas 0x41c y 0x41d) y un esqueleto de
// un hueso, sin animación.
class TwoMeshModels final : public Client::Scenes::IModelLibrary
{
public:
    TwoMeshModels()
    {
        Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "a.jpg";
        Mesh second = mesh;
        second.textureName = "b.jpg";
        m_model.meshes = {mesh, second};
        Client::Render::Models::SkeletonBone bone;
        bone.parent = -1;
        Client::Render::Models::BoneKey key;
        key.rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
        bone.actions = {{key}};
        m_skeleton.bones = {bone};
        m_skeleton.actions = {
            Client::Render::Models::ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
    }

    [[nodiscard]] const Model* Find(std::uint16_t) const override
    {
        return &m_model;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &m_skeleton;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return m_textures;
    }

private:
    Model m_model;
    Skeleton m_skeleton;
    std::array<TextureSlot, 2> m_textures{TextureSlot{0x41c}, TextureSlot{0x41d}};
};

WorldObject ObjectAt(std::int16_t type, float x, float y, std::uint8_t map = Login,
                     Client::World::ObjectScene scene = Client::World::ObjectScene::Login)
{
    Client::Data::Terrain::TerrainObject record;
    record.type = type;
    record.position = {x, y, 0.0f};
    record.scale = 1.0f;
    const auto objects = Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), map, scene);
    return objects.front();
}

const Client::World::TerrainPolygon Everything{.everything = true};

std::size_t VerticesWith(const ModelBatch& batch, ModelBlend blend)
{
    std::size_t count = 0;
    for (const auto& command : batch.Commands())
    {
        if (command.state.blend == blend)
        {
            count += command.vertexCount;
        }
    }
    return count;
}

void Draw(ModelBatch& batch, const TwoMeshModels& models, std::span<WorldObject> objects,
          const Client::World::TerrainPolygon& polygon = Everything, std::uint8_t map = Login)
{
    batch.Begin(Client::Render::Pass3D{});
    DrawWorldObjects(batch, models, objects, polygon, nullptr, map, {}, 0.0f);
}

// 0x606560 solo avanza los que se vieron en el último dibujo (+0x16c): recién creado, el alpha no se
// mueve; después de un dibujo, un décimo hacia su meta (Q-C135).
TEST(WorldObjectsDrawingTest, OnlyTheObjectsDrawnLastFrameStep)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 1000.0f, 1000.0f)};
    objects[0].alpha = 0.0f;
    StepWorldObjects(objects, Login, models);
    EXPECT_EQ(objects[0].alpha, 0.0f);
    EXPECT_FALSE(objects[0].seen);

    ModelBatch batch;
    Draw(batch, models, objects);
    EXPECT_TRUE(objects[0].seen);
    StepWorldObjects(objects, Login, models);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(objects[0].alpha), 0x3DCCCCCDu) << "0,1";
}

// 0x6046a0: los sectores por x y después por y, y en cada uno el orden del archivo. El objeto del
// sector (0, 3) va antes que el del (5, 2), y los dos del (0, 3) en su orden.
TEST(WorldObjectsDrawingTest, ObjectsGoSectorBySector)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 8000.0f, 3500.0f), ObjectAt(Rock, 100.0f, 5000.0f),
                                     ObjectAt(Rock, 200.0f, 5100.0f)};
    ModelBatch batch;
    Draw(batch, models, objects);
    ASSERT_EQ(batch.Vertices().size(), 3u * 6u);
    // La primera esquina de la malla 0 de cada uno, en el orden del lote.
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 100.0f);
    EXPECT_FLOAT_EQ(batch.Vertices()[6].position.x, 200.0f);
    EXPECT_FLOAT_EQ(batch.Vertices()[12].position.x, 8000.0f);
}

// Un objeto fuera del trapecio del juego no se ve ni avanza: el de un sector lejano, y el del sector
// del héroe (9, 7) que queda afuera en su lugar.
TEST(WorldObjectsDrawingTest, AnObjectOutsideThePolygonIsNotSeen)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 14400.0f, 12100.0f), ObjectAt(Rock, 100.0f, 100.0f),
                                     ObjectAt(Rock, 15900.0f, 11300.0f)};
    const auto polygon =
        Client::World::TerrainPolygonOf(Client::World::CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    ModelBatch batch;
    Draw(batch, models, objects, polygon);
    EXPECT_TRUE(objects[0].seen);
    EXPECT_FALSE(objects[1].seen);
    EXPECT_EQ(Client::World::SectorX(objects[2]), 9);
    EXPECT_EQ(Client::World::SectorY(objects[2]), 7);
    EXPECT_FALSE(objects[2].seen);
    EXPECT_EQ(batch.Vertices().size(), 6u);
}

// 0x6069bd: el puente del mapa 0 usa −50 en +0xdc; 0x604744..0x60476b pasa ese margen a
// 0x5fba90. El sector (0, 0), centro (8, 8), pasa; (14, 5) da −40 en el lado derecho.
TEST(WorldObjectsDrawingTest, TheLorenciaBridgeExtendsItsOwnCullingMargin)
{
    const TwoMeshModels models;
    const Client::World::TerrainPolygon polygon{.x = {0.0f, 0.0f, 10.0f, 10.0f}, .y = {0.0f, 10.0f, 10.0f, 0.0f}};
    for (const auto scene : {Client::World::ObjectScene::Other, Client::World::ObjectScene::Login,
                             Client::World::ObjectScene::CharacterSelect})
    {
        SCOPED_TRACE(static_cast<int>(scene));
        std::array objects{ObjectAt(Bridge, 1400.0f, 500.0f, Lorencia, scene),
                           ObjectAt(0x51, 1400.0f, 500.0f, Lorencia, scene)};
        EXPECT_EQ(objects[0].sectorX, 0);
        EXPECT_EQ(objects[0].sectorY, 0);
        ModelBatch batch;
        Draw(batch, models, objects, polygon, Lorencia);
        EXPECT_TRUE(objects[0].seen);
        EXPECT_FALSE(objects[1].seen);
        ASSERT_EQ(batch.Vertices().size(), 6u);
        EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 1400.0f);
    }
}

// El borde de 0x5fba90 es estricto: (15, 5) da −50. Al salir se borra seen y el lote del frame.
TEST(WorldObjectsDrawingTest, TheBridgeIsCulledExactlyAtItsMargin)
{
    const TwoMeshModels models;
    const Client::World::TerrainPolygon polygon{.x = {0.0f, 0.0f, 10.0f, 10.0f}, .y = {0.0f, 10.0f, 10.0f, 0.0f}};
    std::array objects{ObjectAt(Bridge, 1400.0f, 500.0f, Lorencia)};
    ModelBatch batch;
    Draw(batch, models, objects, polygon, Lorencia);
    ASSERT_TRUE(objects[0].seen);
    ASSERT_EQ(batch.Vertices().size(), 6u);

    objects[0].transform.position.x = 1500.0f;
    Draw(batch, models, objects, polygon, Lorencia);
    EXPECT_FALSE(objects[0].seen);
    EXPECT_TRUE(batch.Vertices().empty());
}

// El consumidor usa el campo del objeto, sin volver a deducirlo de tipo, mapa o escena. El helper
// devuelve una copia y el array la copia otra vez, como los contenedores de las escenas.
TEST(WorldObjectsDrawingTest, CullingUsesTheStoredMarginAfterCopyingAnObject)
{
    const TwoMeshModels models;
    const Client::World::TerrainPolygon polygon{.x = {0.0f, 0.0f, 10.0f, 10.0f}, .y = {0.0f, 10.0f, 10.0f, 0.0f}};
    WorldObject extended = ObjectAt(Rock, 1400.0f, 500.0f);
    extended.cullingMargin = -50.0f;
    std::array objects{extended, ObjectAt(Rock, 1400.0f, 500.0f)};
    ModelBatch batch;
    Draw(batch, models, objects, polygon);
    EXPECT_TRUE(objects[0].seen);
    EXPECT_FALSE(objects[1].seen);
    EXPECT_EQ(batch.Vertices().size(), 6u);
}

// 0x602ed0 saca un rand() de la CRT por cada objeto que llega a él (0x602ef4, antes de mirar la escena o el
// tipo): los que se ven, aunque no se dibujen (el de alpha 0 y el de +0x58 en −2). El del sector lejano no
// llega.
TEST(WorldObjectsDrawingTest, EachSeenObjectTakesARand)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 14400.0f, 12100.0f), ObjectAt(Rock, 100.0f, 100.0f),
                                     ObjectAt(Rock, 14450.0f, 12150.0f), ObjectAt(Rock, 14420.0f, 12120.0f)};
    objects[2].alpha = 0.0f;
    objects[3].state.hiddenMesh = Client::World::HiddenObject;
    const auto polygon =
        Client::World::TerrainPolygonOf(Client::World::CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    Client::Render::Models::CrtRandom random(7);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawWorldObjects(batch, models, objects, polygon, nullptr, Login, {}, 0.0f,
                     Client::Scenes::ObjectFrameEffects{.random = &random});
    Client::Render::Models::CrtRandom expected(7);
    for (int k = 0; k < 3; ++k)
    {
        (void)expected.Next();
    }
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(batch.Vertices().size(), 6u) << "el de alpha 0 no se dibuja";
}

TEST(WorldObjectsDrawingTest, DrawingStillUpdatesGeometryAndSeenWithoutAnOriginalFrame)
{
    const TwoMeshModels models;
    std::array objects{ObjectAt(Rock, 1000.0f, 1000.0f)};
    Client::Render::Models::CrtRandom random(4);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawWorldObjects(batch, models, objects, Everything, nullptr, Login, {}, 0.0f,
                     Client::Scenes::ObjectFrameEffects{.random = &random, .frames = 0});
    EXPECT_TRUE(objects[0].seen);
    EXPECT_EQ(batch.Vertices().size(), 6u);
    EXPECT_EQ(random.State(), 4u);
}

// Modelos de diez huesos de raíz, sin animación, el k en (100 · k, 10 · k, 0) por la base de su tipo; el
// del tipo 2 con el 8 dummy. El farol (0x3c), de un hueso, como Object61.bmd.
class TenBoneModels final : public Client::Scenes::IModelLibrary
{
public:
    TenBoneModels()
    {
        const auto skeletonOf = [](std::size_t bones, float base, std::optional<std::size_t> dummy)
        {
            Skeleton skeleton;
            for (std::size_t k = 0; k < bones; ++k)
            {
                Client::Render::Models::SkeletonBone bone;
                bone.parent = -1;
                if (dummy == k)
                {
                    bone.dummy = true;
                    skeleton.bones.push_back(bone);
                    continue;
                }
                Client::Render::Models::BoneKey key;
                key.position = {base + (100.0f * static_cast<float>(k)), 10.0f * static_cast<float>(k), 0.0f};
                key.rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
                bone.actions = {{key}};
                skeleton.bones.push_back(bone);
            }
            skeleton.actions = {Client::Render::Models::ActionInfo{
                .repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
            return skeleton;
        };
        m_horse = skeletonOf(10, 0.0f, std::nullopt);
        m_rock = skeletonOf(10, 5000.0f, 8);
        m_lantern = skeletonOf(1, 0.0f, std::nullopt);
    }

    [[nodiscard]] const Model* Find(std::uint16_t) const override
    {
        return &m_model;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == Lantern)
        {
            return &m_lantern;
        }
        return model == SecondRock ? &m_rock : &m_horse;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }

    static constexpr std::uint16_t Lantern = 0x3c;
    static constexpr std::uint16_t SecondRock = 2;

private:
    Model m_model;
    Skeleton m_horse;
    Skeleton m_rock;
    Skeleton m_lantern;
};

struct LanternGlow
{
    std::vector<Client::World::Sprite> sprites;
    std::optional<glm::vec3> horse8;
    std::optional<glm::vec3> horse9;
    std::optional<glm::vec3> rock9;
};

// El caballo posado antes de los objetos (0x4453b0) en (1.000; 2.000; 300) y `objects` dibujados en su
// orden, con el farol al final.
LanternGlow GlowOf(std::vector<WorldObject>& objects, Client::World::ObjectScene scene, std::uint32_t seed)
{
    const TenBoneModels models;
    Client::Scenes::DrawnObject horse;
    horse.model = 1;
    horse.transform.position = {1000.0f, 2000.0f, 300.0f};
    horse.transform.scale = 1.5f;
    const std::array<Client::Scenes::PosedInTheBuffer, 1> before{{{&models, horse}}};
    Client::Render::Models::CrtRandom random(seed);
    Client::World::FrameSprites sprites;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawWorldObjects(batch, models, objects, Everything, nullptr, Login, {}, 0.0f,
                     Client::Scenes::ObjectFrameEffects{
                         .random = &random, .sprites = &sprites, .posedBefore = before, .scene = scene});
    LanternGlow glow;
    for (const Client::World::Sprite& sprite : sprites.Sprites())
    {
        if (sprite.alive)
        {
            glow.sprites.push_back(sprite);
        }
    }
    glow.horse8 = Client::Scenes::PosedBonePoint(models, horse, 8, {});
    glow.horse9 = Client::Scenes::PosedBonePoint(models, horse, 9, {});
    Client::Scenes::DrawnObject rock;
    rock.model = TenBoneModels::SecondRock;
    rock.transform = objects.front().transform;
    glow.rock9 = Client::Scenes::PosedBonePoint(models, rock, 9, {});
    return glow;
}

TEST(WorldObjectsDrawingTest, AZeroFrameDrawReplaysTheLanternWithoutConsumingRandomness)
{
    const TenBoneModels models;
    std::array objects{ObjectAt(TenBoneModels::Lantern, 300.0f, 300.0f)};
    Client::World::FrameSprites sprites;
    Client::Render::Models::CrtRandom random(4);
    std::array<std::optional<int>, 1> lastRandoms;
    Client::Scenes::DrawnObject horse;
    horse.model = 1;
    horse.transform.position = {1000.0f, 2000.0f, 300.0f};
    std::array<Client::Scenes::PosedInTheBuffer, 1> before{{{&models, horse}}};
    ModelBatch batch;
    const auto draw = [&](int frames)
    {
        batch.Begin(Client::Render::Pass3D{});
        DrawWorldObjects(batch, models, objects, Everything, nullptr, Login, {}, 0.0f,
                         Client::Scenes::ObjectFrameEffects{.random = &random,
                                                            .sprites = &sprites,
                                                            .posedBefore = before,
                                                            .scene = Client::World::ObjectScene::Login,
                                                            .frames = frames,
                                                            .lastRandoms = lastRandoms});
    };
    draw(1);
    ASSERT_TRUE(sprites.Sprites()[0].alive);
    const glm::vec3 light = sprites.Sprites()[0].light;
    const std::uint32_t state = random.State();
    EXPECT_EQ(sprites.Sprites()[0].position, glm::vec3(1800.0f, 2080.0f, 300.0f));
    before[0].object.transform.position.x += 100.0f;
    sprites.Clear();
    draw(0);
    EXPECT_EQ(random.State(), state);
    EXPECT_TRUE(sprites.Sprites()[0].alive);
    EXPECT_TRUE(sprites.Sprites()[1].alive);
    EXPECT_EQ(sprites.Sprites()[0].light, light);
    EXPECT_EQ(sprites.Sprites()[0].position, glm::vec3(1900.0f, 2080.0f, 300.0f)) << "la pose del dibujo actual";
    sprites.Clear();
    lastRandoms[0].reset();
    draw(0);
    EXPECT_FALSE(sprites.Any()) << "recién visible, todavía no tiene un sorteo previo";
}

TEST(WorldObjectsDrawingTest, ANotSeenLanternLosesItsSampleOnlyAfterAnOriginalFrame)
{
    const TenBoneModels models;
    std::array objects{ObjectAt(TenBoneModels::Lantern, 300.0f, 300.0f)};
    Client::World::FrameSprites sprites;
    Client::Render::Models::CrtRandom random(4);
    std::array<std::optional<int>, 1> lastRandoms;
    const auto outside =
        Client::World::TerrainPolygonOf(Client::World::CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    ModelBatch batch;
    const auto draw = [&](int frames, const Client::World::TerrainPolygon& polygon)
    {
        sprites.Clear();
        batch.Begin(Client::Render::Pass3D{});
        DrawWorldObjects(batch, models, objects, polygon, nullptr, Login, {}, 0.0f,
                         Client::Scenes::ObjectFrameEffects{.random = &random,
                                                            .sprites = &sprites,
                                                            .scene = Client::World::ObjectScene::Login,
                                                            .frames = frames,
                                                            .lastRandoms = lastRandoms});
    };
    draw(1, Everything);
    ASSERT_TRUE(lastRandoms[0].has_value());
    const glm::vec3 light = sprites.Sprites()[0].light;
    const std::uint32_t state = random.State();
    draw(0, outside);
    EXPECT_FALSE(objects[0].seen);
    EXPECT_TRUE(lastRandoms[0].has_value());
    draw(0, Everything);
    ASSERT_TRUE(sprites.Any());
    EXPECT_EQ(sprites.Sprites()[0].light, light);
    draw(1, outside);
    EXPECT_FALSE(objects[0].seen);
    EXPECT_FALSE(lastRandoms[0].has_value());
    draw(0, Everything);
    EXPECT_TRUE(objects[0].seen);
    EXPECT_FALSE(sprites.Any());
    EXPECT_EQ(random.State(), state);
}

// 0x602ed0 en la escena 2 con el farol (0x3c, 0x602f21): dos sprites de flare01 (0x5ac), de escala 1,
// aditivos y sin giro, en las traslaciones de los huesos 8 y 9 del buffer global de la pose (0x68b199c y
// 0x68b19cc), con la luz del rand() de su objeto. El farol tiene un hueso: los dos son los del último
// modelo posado que los tiene sin ser dummy. Sin otro objeto antes, los del caballo.
TEST(WorldObjectsDrawingTest, TheLanternGlowsAtTheBonesThatTheHorseLeft)
{
    std::vector<WorldObject> objects{ObjectAt(TenBoneModels::Lantern, 300.0f, 300.0f)};
    const LanternGlow glow = GlowOf(objects, Client::World::ObjectScene::Login, 11);
    ASSERT_EQ(glow.sprites.size(), 2u);
    Client::Render::Models::CrtRandom random(11);
    const glm::vec3 light = Client::World::StreetLightGlowLight(random.Next());
    ASSERT_TRUE(glow.horse8.has_value());
    ASSERT_TRUE(glow.horse9.has_value());
    EXPECT_EQ(glow.sprites[0].position, *glow.horse8);
    EXPECT_EQ(glow.sprites[1].position, *glow.horse9);
    for (const Client::World::Sprite& sprite : glow.sprites)
    {
        EXPECT_EQ(sprite.texture, 0x5ac);
        EXPECT_EQ(sprite.scale, 1.0f);
        EXPECT_EQ(sprite.blend, 0);
        EXPECT_EQ(sprite.rotation, 0.0f);
        EXPECT_EQ(sprite.light, light);
    }
}

// Un objeto de diez huesos dibujado antes del farol pisa el 9; su 8 es dummy (0x4eb973 no lo escribe) y
// queda el del caballo. Con el alpha por debajo de 0,01, 0x5ffdf0 sale antes de posarlo y quedan los dos
// del caballo; justo en 0,01, lo posa. Con +0x58 en −2 lo posa (0x6002e0 es el que no lo dibuja).
TEST(WorldObjectsDrawingTest, TheLanternGlowsAtTheBonesOfTheLastPosedModel)
{
    std::vector<WorldObject> objects{ObjectAt(static_cast<std::int16_t>(TenBoneModels::SecondRock), 100.0f, 100.0f),
                                     ObjectAt(TenBoneModels::Lantern, 300.0f, 300.0f)};
    const LanternGlow posed = GlowOf(objects, Client::World::ObjectScene::Login, 3);
    ASSERT_EQ(posed.sprites.size(), 2u);
    EXPECT_EQ(posed.sprites[0].position, *posed.horse8);
    ASSERT_TRUE(posed.rock9.has_value());
    EXPECT_EQ(posed.sprites[1].position, *posed.rock9);

    objects[0].state.hiddenMesh = Client::World::HiddenObject;
    const LanternGlow hidden = GlowOf(objects, Client::World::ObjectScene::Login, 3);
    ASSERT_EQ(hidden.sprites.size(), 2u);
    EXPECT_EQ(hidden.sprites[1].position, *hidden.rock9) << "posado aunque no se dibuje";

    objects[0].state.hiddenMesh = Client::Render::Models::NoHiddenMesh;
    objects[0].alpha = 0.0f;
    const LanternGlow faded = GlowOf(objects, Client::World::ObjectScene::Login, 3);
    ASSERT_EQ(faded.sprites.size(), 2u);
    EXPECT_EQ(faded.sprites[1].position, *faded.horse9);

    // `fcomp` con 0,01f y `test ah, 1`: con el alpha justo en 0,01 no sale.
    objects[0].alpha = std::bit_cast<float>(0x3C23D70Au);
    const LanternGlow edge = GlowOf(objects, Client::World::ObjectScene::Login, 3);
    ASSERT_EQ(edge.sprites.size(), 2u);
    EXPECT_EQ(edge.sprites[1].position, *edge.rock9) << "posado con el alpha en 0,01";
}

// El caso del farol es de la escena 2 (0x602f09): en otra, el rand() se saca igual pero no hay sprites.
TEST(WorldObjectsDrawingTest, TheLanternGlowsOnlyInTheLogin)
{
    std::vector<WorldObject> objects{ObjectAt(TenBoneModels::Lantern, 300.0f, 300.0f)};
    EXPECT_TRUE(GlowOf(objects, Client::World::ObjectScene::CharacterSelect, 11).sprites.empty());
}

// Los dos remolinos rojos (86) del .obj están en el mismo lugar, con el mismo giro y aditivos desde su
// primer paso: se suman dos veces, las dos mallas de cada uno.
TEST(WorldObjectsDrawingTest, TheTwoRedWhirlpoolsAddUp)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(RedWhirlpool, 0.0f, 0.0f), ObjectAt(RedWhirlpool, 0.0f, 0.0f)};
    ModelBatch batch;
    Draw(batch, models, objects);
    StepWorldObjects(objects, Login, models);
    Draw(batch, models, objects);
    EXPECT_EQ(VerticesWith(batch, ModelBlend::Additive), 2u * 2u * 3u);
    EXPECT_EQ(batch.Vertices().size(), 2u * 2u * 3u);
}

// El remolino 90: antes de su primer paso no se dibuja (sin el blendMesh, Q-C135); después, solo su
// malla 0, sustractiva y en blanco.
TEST(WorldObjectsDrawingTest, TheDarkWhirlpoolGoesSubtractive)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(DarkWhirlpool, 0.0f, 0.0f)};
    ModelBatch batch;
    Draw(batch, models, objects);
    EXPECT_TRUE(batch.Vertices().empty());
    StepWorldObjects(objects, Login, models);
    Draw(batch, models, objects);
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Subtractive);
    EXPECT_EQ(batch.Commands()[0].state.texture, TextureSlot{0x41c});
    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 1.0f);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.green, 1.0f);
}

// 0x6046a0 guarda en +0x16c el resultado del test de cada objeto, y 0x606560 apaga los de un sector que
// no pasó: uno que sale de la vista deja de avanzar.
TEST(WorldObjectsDrawingTest, AnObjectThatLeavesTheViewStopsStepping)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 100.0f, 100.0f)};
    objects[0].alpha = 0.0f;
    ModelBatch batch;
    Draw(batch, models, objects);
    ASSERT_TRUE(objects[0].seen);
    const auto away =
        Client::World::TerrainPolygonOf(Client::World::CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    Draw(batch, models, objects, away);
    EXPECT_FALSE(objects[0].seen);
    StepWorldObjects(objects, Login, models);
    EXPECT_EQ(objects[0].alpha, 0.0f) << "no avanzó";
}

// 0x606560 recorre los sectores como el dibujo (x afuera, y adentro) y, en cada uno, el orden en que se
// crearon: el orden importa cuando los pasos sacan rand().
TEST(WorldObjectsDrawingTest, TheObjectsStepSectorBySector)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 8000.0f, 3500.0f), ObjectAt(Rock, 100.0f, 5000.0f),
                                     ObjectAt(Rock, 200.0f, 5100.0f)};
    ModelBatch batch;
    Draw(batch, models, objects);
    std::vector<float> order;
    StepWorldObjects(objects, Login, models,
                     [&order](WorldObject& object)
                     {
                         order.push_back(object.transform.position.x);
                         return false;
                     });
    EXPECT_EQ(order, (std::vector<float>{100.0f, 200.0f, 8000.0f}));
}

// El que se encarga de uno (el logo del login) lo avanza él, y StepWorldObject no lo toca.
TEST(WorldObjectsDrawingTest, ASpecialObjectIsSteppedByItsOwner)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 100.0f, 100.0f)};
    objects[0].alpha = 0.0f;
    ModelBatch batch;
    Draw(batch, models, objects);
    int calls = 0;
    StepWorldObjects(objects, Login, models,
                     [&calls](WorldObject&)
                     {
                         ++calls;
                         return true;
                     });
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(objects[0].alpha, 0.0f);
}

// Un efecto con modelo vivo, como el dragón, en (1.000; 1.000; 3.400), y su paso anterior 70 más atrás
// en y.
Client::World::ModelEffect LiveEffect()
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::World::DragonEffect;
    effect.transform.position = {1000.0f, 1000.0f, 3400.0f};
    effect.previousPosition = {1000.0f, 1070.0f, 3400.0f};
    effect.light = glm::vec3(1.0f);
    return effect;
}

// 0x52d620 → 0x5ffce0 con la luz por vértice: el color es la luz del terreno en su lugar más la del
// efecto, y cada esquina va por la luz de su normal.
TEST(ModelEffectsDrawingTest, TheDragonTakesTheTerrainLightPlusItsOwn)
{
    const TwoMeshModels models;
    auto lighting = std::make_unique<Client::World::TerrainLighting>();
    lighting->cells.fill({0.25f, 0.5f, 0.75f});
    const std::array<Client::World::ModelEffect, 1> effects{LiveEffect()};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::DrawModelEffects(batch, models, effects, lighting.get(), {}, 0.0f);
    ASSERT_EQ(batch.Vertices().size(), 6u);
    const float light = Client::Render::Models::NormalLight(glm::vec3(0.0f, 0.0f, 1.0f));
    const auto& color = batch.Vertices()[0].color;
    EXPECT_FLOAT_EQ(color.red, 1.25f * light);
    EXPECT_FLOAT_EQ(color.green, 1.5f * light);
    EXPECT_FLOAT_EQ(color.blue, 1.75f * light);
    EXPECT_NE(light, 1.0f) << "la normal tiene que cambiar el color para que el test mire la luz por vértice";
}

// El paso solo corre los efectos vivos.
TEST(ModelEffectsDrawingTest, OnlyLiveEffectsStep)
{
    const TwoMeshModels models;
    std::array<Client::World::ModelEffect, 2> effects{LiveEffect(), LiveEffect()};
    effects[1].alive = false;
    effects[0].life = 100;
    effects[1].life = 100;
    Client::Scenes::StepModelEffects(effects, models);
    EXPECT_EQ(effects[0].life, 99);
    EXPECT_EQ(effects[1].life, 100);
}

// Fuera del modo fiel, el dibujo va entre dos pasos (D-C4): la posición, `alpha` de un paso más, con lo
// que se movió en el último; el frame no, que es entero.
TEST(ModelEffectsDrawingTest, TheDragonIsDrawnBetweenTwoSteps)
{
    const TwoMeshModels models;
    const std::array<Client::World::ModelEffect, 1> effects{LiveEffect()};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::DrawModelEffects(batch, models, effects, nullptr, {}, 0.5f);
    ASSERT_FALSE(batch.Vertices().empty());
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.y, 1000.0f - 35.0f);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 1000.0f);
}

// Una biblioteca con el tamaño de la textura de los sprites (0x5ac de 64 × 32) y la del aliento (0x5f2): las
// dos son JPG, de 3 componentes, y ligan el slot de su selector, como las de EffectTextureSet en GameModels.
class SpriteTextures final : public Client::Scenes::IModelLibrary
{
public:
    [[nodiscard]] const Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    [[nodiscard]] std::optional<TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override
    {
        if (selector != 0x5ac && selector != 0x5f2)
        {
            return std::nullopt;
        }
        TextureMetadata metadata(Client::Data::Textures::Texture{});
        metadata.size = glm::vec2(64.0f, 32.0f);
        metadata.components = 3;
        return metadata;
    }
    [[nodiscard]] std::optional<TextureSlot> TextureSlotOf(std::uint32_t selector) const override
    {
        return TextureMetadataOf(selector) ? std::optional(TextureSlot{static_cast<std::uint16_t>(selector)})
                                           : std::nullopt;
    }
    [[nodiscard]] std::optional<glm::vec2> TextureSize(TextureSlot slot) const override
    {
        const auto metadata = TextureMetadataOf(slot.value);
        return metadata ? std::optional(metadata->size) : std::nullopt;
    }
};

// 0x549440(0): cada sprite vivo se dibuja una vez, aditivo, como el cuadrado de 0x621400 con la textura
// por la escala (0x549290), y se apaga.
TEST(SpritesDrawingTest, ASpriteIsDrawnOnceAndPutOut)
{
    const SpriteTextures textures;
    Client::World::FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x5ac, {10.0f, 20.0f, -300.0f}, 2.0f, {1.0f, 0.3f, 0.0f}, 0.0f, 0));
    sprites.MarkSeen();
    const auto identity = Client::Render::ViewRows(glm::mat4(1.0f));
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::DrawFrameSprites(batch, sprites, textures, identity);
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
    EXPECT_EQ(batch.Commands()[0].state.texture, TextureSlot{0x5ac});
    ASSERT_EQ(batch.Vertices().size(), 6u);
    // 64 × 2 de ancho y 32 × 2 de alto, centrado en (10, 20).
    float minX = 1e9f;
    float maxX = -1e9f;
    float minY = 1e9f;
    float maxY = -1e9f;
    for (const auto& vertex : batch.Vertices())
    {
        minX = std::min(minX, vertex.position.x);
        maxX = std::max(maxX, vertex.position.x);
        minY = std::min(minY, vertex.position.y);
        maxY = std::max(maxY, vertex.position.y);
        EXPECT_EQ(vertex.position.z, -300.0f);
        EXPECT_FLOAT_EQ(vertex.color.green, 0.3f);
    }
    EXPECT_FLOAT_EQ(maxX - minX, 128.0f);
    EXPECT_FLOAT_EQ(maxY - minY, 64.0f);
    EXPECT_FLOAT_EQ((minX + maxX) / 2.0f, 10.0f);
    EXPECT_FALSE(sprites.Sprites()[0].alive);
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::DrawFrameSprites(batch, sprites, textures, identity);
    EXPECT_TRUE(batch.Vertices().empty());
}

// La mezcla de cada sprite sale de su +4 (0x549440): 1 0x620b10, 2 la normal; la textura 0x637 siempre
// la normal.
TEST(SpritesDrawingTest, TheBlendComesFromTheSprite)
{
    const SpriteTextures textures;
    Client::World::FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x5ac, glm::vec3(0.0f, 0.0f, -10.0f), 1.0f, glm::vec3(1.0f), 0.0f, 1));
    ASSERT_TRUE(sprites.Create(0x5ac, glm::vec3(0.0f, 0.0f, -10.0f), 1.0f, glm::vec3(1.0f), 0.0f, 2));
    sprites.MarkSeen();
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::DrawFrameSprites(batch, sprites, textures, Client::Render::ViewRows(glm::mat4(1.0f)));
    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Subtractive);
    EXPECT_EQ(batch.Commands()[1].state.blend, ModelBlend::Alpha);
}

// Sin 0x549510 (+0x16c en 0), 0x549290 le resta 0,1 al alpha antes de medirlo: el cuadrado sale a 0,9
// de la textura por la escala, y el color del billboard no lleva el alpha.
TEST(SpritesDrawingTest, AnUnseenSpriteShrinksWithItsAlpha)
{
    const SpriteTextures textures;
    Client::World::FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x5ac, {0.0f, 0.0f, -300.0f}, 2.0f, glm::vec3(1.0f), 0.0f, 0));
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::DrawFrameSprites(batch, sprites, textures, Client::Render::ViewRows(glm::mat4(1.0f)));
    ASSERT_EQ(batch.Vertices().size(), 6u);
    float minX = 1e9f;
    float maxX = -1e9f;
    for (const auto& vertex : batch.Vertices())
    {
        minX = std::min(minX, vertex.position.x);
        maxX = std::max(maxX, vertex.position.x);
        EXPECT_FLOAT_EQ(vertex.color.red, 1.0f);
    }
    EXPECT_FLOAT_EQ(maxX - minX, 64.0f * 2.0f * 0.9f);
}

// 0x548550(0): cada aliento vivo, aditivo (smoke01 es de 3 componentes: 0x620a90), del tamaño de su textura
// por la escala, sin el alpha de los sprites y sin apagarlo; el paso lo apaga cuando se le acaba la vida. El
// material y el binder (0x620780) van al lote como efectos sin geometría antes del quad, en coordenadas de ojo.
TEST(ParticlesDrawingTest, ABreathIsDrawnWithItsTextureTimesItsScale)
{
    const SpriteTextures textures;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    const auto index = particles.Create(Client::World::BreathParticle, {0.0f, 0.0f, -300.0f}, glm::vec3(0.0f),
                                        glm::vec3(3.0f), 0, 1.0f, 0.0f, random);
    ASSERT_TRUE(index.has_value());
    const float scale = particles.All()[*index].scale;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Client::Scenes::DrawParticles(batch, particles, textures, Client::Render::ViewRows(glm::mat4(1.0f)))
                    .HasValue());
    ASSERT_FALSE(batch.Commands().empty());
    const auto& drawn = batch.Commands().back();
    EXPECT_EQ(drawn.operation, Client::Render::ModelStateOperation::Draw);
    EXPECT_EQ(drawn.state.blend, ModelBlend::Additive);
    EXPECT_EQ(drawn.state.texture, TextureSlot{0x5f2});
    EXPECT_EQ(drawn.state.space, Client::Render::ModelCoordinateSpace::Eye);
    EXPECT_EQ(drawn.state.depthWrite, std::optional(false));
    EXPECT_EQ(drawn.state.cullEnabled, std::optional(false));
    EXPECT_EQ(drawn.state.alphaTest, std::optional(false));
    EXPECT_EQ(drawn.state.texture2d, std::optional(true));
    EXPECT_EQ(drawn.vertexCount, 6u);
    for (std::size_t k = 0; k + 1 < batch.Commands().size(); ++k)
    {
        EXPECT_EQ(batch.Commands()[k].vertexCount, 0u) << "antes del quad, solo los efectos sin geometría";
    }
    ASSERT_EQ(batch.Vertices().size(), 6u);
    float minY = 1e9f;
    float maxY = -1e9f;
    for (const auto& vertex : batch.Vertices())
    {
        minY = std::min(minY, vertex.position.y);
        maxY = std::max(maxY, vertex.position.y);
    }
    // Con la rotación del reloj en 0, el alto es el de la textura por la escala.
    EXPECT_FLOAT_EQ(maxY - minY, 32.0f * scale);
    EXPECT_TRUE(particles.All()[*index].alive);
}

// Una biblioteca que no subió ninguna textura de efectos.
class NoEffectTextures final : public Client::Scenes::IModelLibrary
{
public:
    [[nodiscard]] const Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
};

// Sin el tamaño de su textura (una biblioteca que no la subió), el pase se corta en ese lugar con el error, en
// la primera frontera (Recurso+20/+24), antes de cualquier servicio: no se saltea en silencio.
TEST(ParticlesDrawingTest, ABreathWithoutItsTextureCutsThePassWithItsError)
{
    const NoEffectTextures textures;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    const auto index = particles.Create(Client::World::BreathParticle, {0.0f, 0.0f, -300.0f}, glm::vec3(0.0f),
                                        glm::vec3(3.0f), 0, 1.0f, 0.0f, random);
    ASSERT_TRUE(index.has_value());
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto drawn =
        Client::Scenes::DrawParticles(batch, particles, textures, Client::Render::ViewRows(glm::mat4(1.0f)));
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_EQ(drawn.Error().slot, *index);
    EXPECT_EQ(drawn.Error().error.reason, Client::World::ParticleError::MissingTextureSize);
    EXPECT_EQ(drawn.Error().error.phase, Client::World::ParticleDrawingPhase::Dimensions);
    EXPECT_TRUE(batch.Commands().empty());
}
} // namespace
