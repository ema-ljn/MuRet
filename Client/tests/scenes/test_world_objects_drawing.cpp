#include "support/CheckedWorldObjectProjection.h"

#include "Scenes/WorldObjectsDrawing.h"
#include "Scenes/LandOfTrialsObjectServices.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Render/ModelBatch.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
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
#include "World/GroundEffectDrawing.h"
#include "World/KalimaWorld.h"
#include "Data/Terrain/TerrainHeights.h"
#include "World/SphereFrustum.h"
#include "World/WorldObjects.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <array>
#include <string>
#include <memory>
#include <limits>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Client::Scenes::StepWorldObjects y DrawWorldObjects (06 § El mundo del login): qué objetos avanzan,
// en qué orden se dibujan y cómo van los remolinos del mapa 55, con modelos de mentira; y los del juego,
// DrawGameWorldObjects y StepGameWorldObjects (06 § Los objetos del juego).
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

// Un modelo como Waterspout01: cuatro mallas de un triángulo del hueso 0, cada una con la textura de su
// índice (de 0 a 3, slots 0x41c a 0x41f) y la UV (0,125; 0,25), sin animación.
class FourMeshModels final : public Client::Scenes::IModelLibrary
{
public:
    FourMeshModels()
    {
        for (std::int16_t index = 0; index < 4; ++index)
        {
            Mesh mesh;
            mesh.texture = index;
            mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
            mesh.normals = {{0, {0, 0, 1}, 0}};
            mesh.texCoords = {{0.125f, 0.25f}};
            mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
            mesh.textureName = "a.jpg";
            m_model.meshes.push_back(mesh);
        }
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
    std::array<TextureSlot, 4> m_textures{TextureSlot{0x41c}, TextureSlot{0x41d}, TextureSlot{0x41e},
                                          TextureSlot{0x41f}};
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

// El pase de los objetos del juego (0x6046a0 en 0x6374ce) con los modelos de mentira, todo visto y la
// semilla 7.
Client::Core::Expected<void, std::string> DrawGame(ModelBatch& batch, const Client::Scenes::IModelLibrary& models,
                                                   std::span<WorldObject> objects,
                                                   const Client::Scenes::GameObjectPass& pass)
{
    batch.Begin(Client::Render::Pass3D{});
    return Client::Scenes::DrawGameWorldObjects(batch, models, objects, Everything, nullptr, {}, pass);
}

Client::Render::Models::CrtRandom SevenAfter(int draws)
{
    Client::Render::Models::CrtRandom random(7);
    for (int k = 0; k < draws; ++k)
    {
        static_cast<void>(random.Next());
    }
    return random;
}

// 0x6046a0 en el juego: cada objeto vivo que pasa el trapecio se dibuja (0x602e90) y saca el rand() de
// 0x602ed0 (0x602ef4), aunque no se dibuje (el de +0x58 en −2). El que no está vivo no se mira (0x60473b): no
// queda visto ni saca rand().
TEST(WorldObjectsDrawingTest, TheGamePassDrawsTheLiveObjectsWithARandEach)
{
    const TwoMeshModels models;
    for (const std::uint8_t map : {Lorencia, std::uint8_t{2}})
    {
        SCOPED_TRACE(static_cast<int>(map));
        std::vector<WorldObject> objects{ObjectAt(Rock, 8000.0f, 3500.0f, map, Client::World::ObjectScene::Other),
                                         ObjectAt(Rock, 100.0f, 5000.0f, map, Client::World::ObjectScene::Other),
                                         ObjectAt(Rock, 200.0f, 5100.0f, map, Client::World::ObjectScene::Other)};
        objects[1].alive = false;
        objects[2].state.hiddenMesh = Client::World::HiddenObject;
        Client::Render::Models::CrtRandom random(7);
        ModelBatch batch;
        const auto drawn = DrawGame(batch, models, objects, {.map = map, .random = &random});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(objects[0].seen);
        EXPECT_FALSE(objects[1].seen);
        EXPECT_TRUE(objects[2].seen);
        ASSERT_EQ(batch.Vertices().size(), 6u);
        EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 8000.0f);
        EXPECT_EQ(random.State(), SevenAfter(2).State());
    }
}

// El 0x38 de Lorencia (0x603149): después de su rand(), dos sprites 0x5ac en los huesos 48 y 57 con el
// desplazamiento de la pila que dejó 0x6002e0 (la y es su nivel del try, −1): en NaN, de escala L · 5 y luz
// (L · 0,6; L · 0,3; L · 0,1). Los de después siguen. Sin posar (el alpha por debajo de 0,01), la pila es
// la de 0x5ffdf0, que no está leída, y corta con los de antes dibujados y los de después sin mirar.
TEST(WorldObjectsDrawingTest, TheLorenciaMerchantAnimalLeavesTwoGlowsAtNaN)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 100.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other),
                                     ObjectAt(0x38, 200.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other),
                                     ObjectAt(Rock, 300.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other)};
    Client::Render::Models::CrtRandom random(7);
    Client::World::FrameSprites sprites;
    ModelBatch batch;
    const auto drawn = DrawGame(batch, models, objects, {.map = Lorencia, .random = &random, .sprites = &sprites});
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_TRUE(objects[2].seen);
    EXPECT_EQ(batch.Vertices().size(), 18u);
    EXPECT_EQ(random.State(), SevenAfter(3).State());
    auto expected = SevenAfter(1);
    const auto light = static_cast<float>(static_cast<double>(expected.Next() % 30 + 70) * static_cast<double>(0.01f));
    int glows = 0;
    for (const auto& sprite : sprites.Sprites())
    {
        if (!sprite.alive)
            continue;
        ++glows;
        EXPECT_EQ(sprite.texture, 0x5ac);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(sprite.position.x), 0xffffffffu);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(sprite.position.y), 0xffffffffu);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(sprite.position.z), 0xffffffffu);
        EXPECT_EQ(sprite.scale, static_cast<float>(static_cast<double>(light) * 5.0));
        EXPECT_EQ(sprite.light.x, static_cast<float>(static_cast<double>(light) * static_cast<double>(0.6f)));
        EXPECT_EQ(sprite.light.y, static_cast<float>(static_cast<double>(light) * static_cast<double>(0.3f)));
        EXPECT_EQ(sprite.light.z, static_cast<float>(static_cast<double>(light) * static_cast<double>(0.1f)));
        EXPECT_EQ(sprite.rotation, 0.0f);
    }
    EXPECT_EQ(glows, 2);

    for (auto& object : objects)
        object.seen = false;
    objects[1].alpha = 0.005f;
    Client::Render::Models::CrtRandom unposed(7);
    ModelBatch second;
    const auto cut = DrawGame(second, models, objects, {.map = Lorencia, .random = &unposed, .sprites = &sprites});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x60318a"), std::string::npos) << cut.Error();
    EXPECT_FALSE(objects[2].seen) << "el de después no se mira";
    EXPECT_EQ(second.Vertices().size(), 6u);
    EXPECT_EQ(unposed.State(), SevenAfter(2).State());
}

// 0x6002e0 con el mapa 4: el 3 saca un rand() (en el frame del original) y, antes de su cuerpo, un
// RenderBody con Chrome01 (0x5c0) forzado, opaco, con la luz (1; 0,2; 0,1) y la malla 1 (su model+0x88) sin
// luz y con la U y la V del objeto; el 0x17 sólo saca el rand(). El cuerpo después, como siempre. En un
// dibujo intermedio de MuRet no hay rand().
TEST(WorldObjectsDrawingTest, TheLostTowerChromeGoesBeforeTheBody)
{
    constexpr std::uint8_t LostTower = 4;
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(3, 100.0f, 100.0f, LostTower, Client::World::ObjectScene::Other),
                                     ObjectAt(0x17, 300.0f, 100.0f, LostTower, Client::World::ObjectScene::Other)};
    for (auto& object : objects)
    {
        object.vertexLight = false;
        object.light = glm::vec3(0.5f);
        object.state.uv = glm::vec2(0.25f, 0.5f);
    }
    Client::Render::Models::CrtRandom random(7);
    ModelBatch batch;
    ASSERT_TRUE(DrawGame(batch, models, objects, {.map = LostTower, .random = &random}).HasValue());
    EXPECT_EQ(random.State(), SevenAfter(4).State()) << "dos del 6002e0 del 4 y dos de 0x602ed0";
    // Las dos mallas cromadas comparten el estado y quedan en un solo comando.
    ASSERT_EQ(batch.Commands().size(), 5u);
    const auto& chrome = batch.Commands()[0];
    EXPECT_EQ(chrome.state.texture, TextureSlot{0x5c0});
    EXPECT_EQ(chrome.state.blend, ModelBlend::Opaque);
    ASSERT_EQ(chrome.vertexCount, 6u);
    for (std::size_t mesh = 0; mesh < 2; ++mesh)
    {
        SCOPED_TRACE(mesh);
        const auto& vertex = batch.Vertices()[chrome.firstVertex + mesh * 3];
        EXPECT_EQ(vertex.color.red, 1.0f);
        EXPECT_EQ(vertex.color.green, std::bit_cast<float>(0x3e4ccccdu));
        EXPECT_EQ(vertex.color.blue, std::bit_cast<float>(0x3dcccccdu));
        EXPECT_EQ(vertex.uv, mesh == 1 ? glm::vec2(0.25f, 0.5f) : glm::vec2(0.0f));
    }
    for (std::size_t body = 1; body < 5; ++body)
    {
        SCOPED_TRACE(body);
        const auto& command = batch.Commands()[body];
        EXPECT_EQ(command.state.texture, TextureSlot{static_cast<std::uint16_t>(0x41c + (body + 1) % 2)});
        EXPECT_EQ(batch.Vertices()[command.firstVertex].uv, glm::vec2(0.0f));
    }

    Client::Render::Models::CrtRandom intermediate(7);
    ModelBatch between;
    ASSERT_TRUE(DrawGame(between, models, objects, {.map = LostTower, .originalFrame = false, .random = &intermediate})
                    .HasValue());
    EXPECT_EQ(intermediate.State(), 7u);
    EXPECT_EQ(between.Commands().size(), 5u);
}

// 0x602ed0 con el mapa 4: el 0x28 deja dos 0x5d5 de 2,5 sobre el objeto, 260 más arriba, con (L; L; L) y el
// giro r y −r (r = ftol(reloj · 0,1f) % 360); el 9 cae al caso del 6 y deja un 0x5ac en NaN. El 0x13 lee
// los huesos de la pose global: sin ella corta.
TEST(WorldObjectsDrawingTest, TheLostTowerFrameLeavesItsSprites)
{
    constexpr std::uint8_t LostTower = 4;
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(0x28, 100.0f, 100.0f, LostTower, Client::World::ObjectScene::Other),
                                     ObjectAt(9, 300.0f, 100.0f, LostTower, Client::World::ObjectScene::Other)};
    objects[0].transform.position.z = 40.0f;
    Client::Render::Models::CrtRandom random(7);
    Client::World::FrameSprites sprites;
    ModelBatch batch;
    const auto drawn = DrawGame(batch, models, objects,
                                {.map = LostTower, .random = &random, .worldClock = 12345.0f, .sprites = &sprites});
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), SevenAfter(2).State());
    auto expected = SevenAfter(0);
    const auto first = static_cast<float>(static_cast<double>(expected.Next() % 30 + 70) * static_cast<double>(0.01f));
    const auto second = static_cast<float>(static_cast<double>(expected.Next() % 30 + 70) * static_cast<double>(0.01f));
    const auto turn = static_cast<float>(static_cast<std::int64_t>(12345.0 * static_cast<double>(0.1f)) % 360);
    const auto live = sprites.Sprites();
    ASSERT_TRUE(live[0].alive && live[1].alive && live[2].alive);
    EXPECT_FALSE(live[3].alive);
    for (std::size_t k = 0; k < 2; ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_EQ(live[k].texture, 0x5d5);
        EXPECT_EQ(live[k].position, glm::vec3(100.0f, 100.0f, 300.0f));
        EXPECT_EQ(live[k].scale, 2.5f);
        EXPECT_EQ(live[k].light, glm::vec3(first));
        EXPECT_EQ(live[k].rotation, k == 0 ? turn : -turn);
    }
    EXPECT_EQ(live[2].texture, 0x5ac);
    EXPECT_TRUE(std::isnan(live[2].position.x));
    EXPECT_EQ(live[2].scale, static_cast<float>(static_cast<double>(second) * 5.0));

    std::vector<WorldObject> boned{ObjectAt(0x13, 100.0f, 100.0f, LostTower, Client::World::ObjectScene::Other)};
    Client::Render::Models::CrtRandom other(7);
    ModelBatch third;
    const auto cut = DrawGame(third, models, boned, {.map = LostTower, .random = &other, .sprites = &sprites});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4ec110"), std::string::npos) << cut.Error();
}

// 0x600c72..0x600cdc: el 0x51 de Tarkan va sin RenderBody, con dos 0x4ecc30 de la malla 0: el pase común (2) y
// el del cromo (0x44) con Chrome01 (0x5c0), aditivo, en el color del modelo sin la luz de cada normal; con el
// alpha por debajo de 0,99, el color por el alpha. La malla 1 no se dibuja.
TEST(WorldObjectsDrawingTest, TheTarkanChromeGoesAfterItsFirstMesh)
{
    constexpr std::uint8_t Tarkan = 8;
    const TwoMeshModels models;
    for (const float alpha : {1.0f, 0.5f})
    {
        SCOPED_TRACE(alpha);
        std::vector<WorldObject> objects{ObjectAt(0x51, 100.0f, 100.0f, Tarkan, Client::World::ObjectScene::Other)};
        objects[0].vertexLight = false;
        objects[0].light = glm::vec3(0.5f);
        objects[0].alpha = alpha;
        objects[0].targetAlpha = alpha;
        Client::Render::Models::CrtRandom random(7);
        ModelBatch batch;
        ASSERT_TRUE(DrawGame(batch, models, objects, {.map = Tarkan, .random = &random}).HasValue());
        EXPECT_EQ(random.State(), SevenAfter(1).State());
        ASSERT_EQ(batch.Commands().size(), 2u);
        const auto& base = batch.Commands()[0];
        EXPECT_EQ(base.state.texture, TextureSlot{0x41c});
        EXPECT_EQ(base.state.blend, alpha < 0.99f ? ModelBlend::Alpha : ModelBlend::Opaque);
        ASSERT_EQ(base.vertexCount, 3u);
        const auto& chrome = batch.Commands()[1];
        EXPECT_EQ(chrome.state.texture, TextureSlot{0x5c0});
        EXPECT_EQ(chrome.state.blend, ModelBlend::Additive);
        ASSERT_EQ(chrome.vertexCount, 3u);
        const float shade = alpha < 0.99f ? 0.5f * alpha : 0.5f;
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const auto& vertex = batch.Vertices()[chrome.firstVertex + corner];
            EXPECT_EQ(vertex.color.red, shade);
            EXPECT_EQ(vertex.color.green, shade);
            EXPECT_EQ(vertex.color.blue, shade);
        }
    }
}

// 0x600a4a..0x600ae6: el 0x69 de Lorencia va con cuatro 0x4ecc30 sueltos, sin RenderBody. Las mallas 0 a 2,
// opacas, con la luz de cada normal y sin la U ni la V; la 3, la de su blendMesh, aditiva, por la luz del
// blendMesh y con la U y la V del objeto. La malla oculta (+0x58) no cuenta: 0x4ecc30 no la mira.
TEST(WorldObjectsDrawingTest, TheLorenciaWaterspoutDrawsFourLooseMeshes)
{
    const FourMeshModels models;
    std::vector<WorldObject> objects{
        ObjectAt(Client::World::Waterspout, 100.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other)};
    ASSERT_EQ(objects[0].state.blendMesh, 3) << "el alta del 0x69 en el 0";
    objects[0].light = glm::vec3(1.0f);
    objects[0].state.light = 0.5f;
    objects[0].state.uv = glm::vec2(0.25f, 0.5f);
    objects[0].state.hiddenMesh = 3;
    Client::Render::Models::CrtRandom random(7);
    ModelBatch batch;
    ASSERT_TRUE(
        DrawGame(batch, models, objects, {.map = Lorencia, .originalFrame = false, .random = &random}).HasValue());
    ASSERT_EQ(batch.Commands().size(), 4u);
    ASSERT_EQ(batch.Vertices().size(), 12u);
    const float light = Client::Render::Models::NormalLight(glm::vec3(0.0f, 0.0f, 1.0f));
    for (std::size_t mesh = 0; mesh < 4; ++mesh)
    {
        SCOPED_TRACE(mesh);
        const auto& command = batch.Commands()[mesh];
        EXPECT_EQ(command.state.texture, TextureSlot{static_cast<std::uint16_t>(0x41c + mesh)});
        const auto& vertex = batch.Vertices()[command.firstVertex];
        if (mesh < 3)
        {
            EXPECT_EQ(command.state.blend, ModelBlend::Opaque);
            EXPECT_EQ(vertex.uv, glm::vec2(0.125f, 0.25f));
            EXPECT_FLOAT_EQ(vertex.color.red, light);
            continue;
        }
        EXPECT_EQ(command.state.blend, ModelBlend::Additive);
        EXPECT_EQ(vertex.uv, glm::vec2(0.375f, 0.75f));
        EXPECT_FLOAT_EQ(vertex.color.red, 0.5f);
    }
    EXPECT_EQ(random.State(), 7u);
}

// La primera semilla desde 1 cuyo tercer rand() (el de rand() % 2 de 0x603011, después de los comunes de un
// objeto y de la fuente) es impar (`odd`) o par.
std::uint32_t SeedWithThirdRand(bool odd)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        Client::Render::Models::CrtRandom random(seed);
        static_cast<void>(random.Next());
        static_cast<void>(random.Next());
        if (((random.Next() % 2) != 0) == odd)
        {
            return seed;
        }
    }
}

// 0x602ed0 con el 0x69 de Lorencia (0x603011): después del rand() común, la luz del blendMesh en 1, la V
// con el reloj del pase y rand() % 2. Impar, sigue con los de después; par, corta en la primera gota
// (0x6030c7) después de sus dos rand(), con los de después sin mirar. Oculto no se dibuja, y corre igual.
TEST(WorldObjectsDrawingTest, TheLorenciaWaterspoutRunsItsFrameCase)
{
    const FourMeshModels models;
    struct Case
    {
        bool odd;
        bool hidden;
    };
    for (const Case& probe : {Case{true, false}, Case{false, false}, Case{true, true}, Case{false, true}})
    {
        SCOPED_TRACE(std::to_string(probe.odd) + " " + std::to_string(probe.hidden));
        std::vector<WorldObject> objects{
            ObjectAt(Rock, 100.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other),
            ObjectAt(Client::World::Waterspout, 200.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other),
            ObjectAt(Rock, 300.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other)};
        if (probe.hidden)
        {
            objects[1].state.hiddenMesh = Client::World::HiddenObject;
        }
        objects[1].state.light = 0.25f;
        const std::uint32_t seed = SeedWithThirdRand(probe.odd);
        Client::Render::Models::CrtRandom random(seed);
        ModelBatch batch;
        const auto drawn =
            DrawGame(batch, models, objects, {.map = Lorencia, .random = &random, .worldClock = 1234.0f});
        EXPECT_EQ(objects[1].state.light, 1.0f);
        EXPECT_EQ(objects[1].state.uv.y, static_cast<float>(-234.0 * static_cast<double>(0.001f)));
        const std::size_t fountain = probe.hidden ? 0u : 12u;
        Client::Render::Models::CrtRandom expected(seed);
        const int draws = probe.odd ? 4 : 5;
        for (int k = 0; k < draws; ++k)
        {
            static_cast<void>(expected.Next());
        }
        if (probe.odd)
        {
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            EXPECT_TRUE(objects[2].seen);
            EXPECT_EQ(batch.Vertices().size(), 12u + fountain + 12u);
        }
        else
        {
            ASSERT_FALSE(drawn.HasValue());
            EXPECT_NE(drawn.Error().find("0x6030c7"), std::string::npos) << drawn.Error();
            EXPECT_FALSE(objects[2].seen);
            EXPECT_EQ(batch.Vertices().size(), 12u + fountain);
        }
        EXPECT_EQ(random.State(), expected.State());
    }
}

// 0x52dc2d..0x52dc50: el +0x16c retenido decide el rand común y el cambio de mezcla, aun en un tipo
// que no tiene caso de suelo (el dragón). No se recalcula la esfera en este pase.
TEST(WorldObjectsDrawingTest, GroundEffectsUseOnlyTheRetainedVisibilityOfLiveSlots)
{
    std::array<Client::World::ModelEffect, 3> effects{};
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Render::Models::CrtRandom random(1234);
    Client::Render::Models::CrtRandom expected(1234);
    const Client::World::GroundEffectResources resources{.heights = *heights, .random = random};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto initial = batch.State();
    EXPECT_TRUE(Client::Scenes::DrawGroundEffects(batch, effects, resources).HasValue());
    effects[1].alive = true;
    effects[1].type = Client::World::ArrivalEffect;
    EXPECT_TRUE(Client::Scenes::DrawGroundEffects(batch, effects, resources).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(batch.State(), initial);
    effects[0].seen = true; // muerto: tampoco participa.
    effects[2].alive = true;
    effects[2].seen = true;
    effects[2].type = Client::World::DragonEffect;
    EXPECT_TRUE(Client::Scenes::DrawGroundEffects(batch, effects, resources).HasValue());
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Additive);
    EXPECT_EQ(batch.State().depthWrite, false);
    EXPECT_EQ(batch.State().cullEnabled, false);
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(WorldObjectsDrawingTest, AVisibleArrivalConsumesTheCommonRandomBeforeDrawing)
{
    std::array<Client::World::ModelEffect, 1> effects{};
    effects[0].alive = true;
    effects[0].seen = true;
    effects[0].type = Client::World::ArrivalEffect;
    effects[0].life = 20;
    effects[0].transform.position = {1000.0f, 1000.0f, 0.0f};
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Render::Models::CrtRandom random(1234);
    Client::Render::Models::CrtRandom expected(1234);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto drawn = Client::Scenes::DrawGroundEffects(batch, effects, {.heights = *heights, .random = random});
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_FALSE(batch.Vertices().empty());
}

TEST(WorldObjectsDrawingTest, ANoopGroundCasePublishesItsStateWithoutBindingATexture)
{
    std::array<Client::World::ModelEffect, 1> effects{};
    effects[0].alive = true;
    effects[0].seen = true;
    effects[0].type = Client::World::DragonEffect;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Render::Models::CrtRandom random;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Client::Scenes::DrawGroundEffects(batch, effects, {.heights = *heights, .random = random}).HasValue());
    using Operation = Client::Render::ModelStateOperation;
    const std::array wanted{Operation::Blend, Operation::Cull, Operation::DepthWrite, Operation::AlphaTest,
                            Operation::Texture2d};
    ASSERT_EQ(batch.Commands().size(), wanted.size());
    for (std::size_t index = 0; index < wanted.size(); ++index)
    {
        EXPECT_EQ(batch.Commands()[index].operation, wanted[index]);
        EXPECT_EQ(batch.Commands()[index].vertexCount, 0u);
    }
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(WorldObjectsDrawingTest, AbsentKalimaWaterKeepsTheCommonStateAndRestoresDepthWrite)
{
    std::array<Client::World::ModelEffect, 1> effects{};
    effects[0].alive = true;
    effects[0].seen = true;
    effects[0].type = 0x6eb;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Render::Models::CrtRandom random;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto drawn = Client::Scenes::DrawGroundEffects(
        batch, effects, {.heights = *heights, .random = random, .worldClock = 0.0f, .map = 24});
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    using Operation = Client::Render::ModelStateOperation;
    const std::array wanted{Operation::Blend,     Operation::Cull,       Operation::DepthWrite, Operation::AlphaTest,
                            Operation::Texture2d, Operation::DepthWrite, Operation::DepthWrite};
    ASSERT_EQ(batch.Commands().size(), wanted.size());
    for (std::size_t index = 0; index < wanted.size(); ++index)
    {
        EXPECT_EQ(batch.Commands()[index].operation, wanted[index]);
    }
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_EQ(batch.State().depthWrite, true);
}

TEST(WorldObjectsDrawingTest, RejectedKalimaDecalKeepsTheCommonPrefixAndDisabledDepthWrite)
{
    std::array<Client::World::ModelEffect, 1> effects{};
    effects[0].alive = true;
    effects[0].seen = true;
    effects[0].type = 0x6eb;
    effects[0].transform.scale = 1;
    effects[0].transform.position.x = std::numeric_limits<float>::infinity();
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::World::KalimaWorld water(24);
    Client::Render::Models::CrtRandom random;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto drawn = Client::Scenes::DrawGroundEffects(
        batch, effects, {.heights = *heights, .random = random, .worldClock = 0.0f, .map = 24, .kalima = &water});
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x41dd90"), std::string::npos);
    using Operation = Client::Render::ModelStateOperation;
    const std::array wanted{Operation::Blend,     Operation::Cull,       Operation::DepthWrite, Operation::AlphaTest,
                            Operation::Texture2d, Operation::DepthWrite, Operation::Binding};
    ASSERT_EQ(batch.Commands().size(), wanted.size());
    for (std::size_t index = 0; index < wanted.size(); ++index)
        EXPECT_EQ(batch.Commands()[index].operation, wanted[index]);
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_EQ(batch.State().depthWrite, false);
}

// 0x60479c..0x604a36: el 0x64 de Devias solo sigue con el nivel del registro propio desde 0x50, o desde 0x35
// con la clase del héroe en 3 o 4 (+0x1d4 & 7), con el registro abierto y cerrado: con la cuenta en 0, el
// cierre saca los 3.848 rand() de 0x40b4d0; con otro acceso abierto, ninguno. Si sigue, su caso de 0x602ed0
// (0x603251) deja sus dos sprites después del rand(); si no, no corre.
TEST(WorldObjectsDrawingTest, TheLeveledObjectOfDeviasReadsTheOwnRecord)
{
    const TwoMeshModels models;
    constexpr int OwnRecordClose = 0xf08;
    struct Case
    {
        std::uint8_t heroClass;
        std::uint16_t level;
        std::uint8_t depth;
        bool drawn;
        int draws;
    };
    for (const Case& probe : {Case{0, 0x4f, 0, false, OwnRecordClose}, Case{0, 0x50, 0, true, OwnRecordClose + 1},
                              Case{0x13, 0x35, 0, true, OwnRecordClose + 1}, Case{4, 0x34, 0, false, OwnRecordClose},
                              Case{2, 0x35, 0, false, OwnRecordClose}, Case{0, 0x50, 1, true, 1}})
    {
        SCOPED_TRACE(std::to_string(probe.heroClass) + " " + std::to_string(probe.level));
        std::vector<WorldObject> objects{ObjectAt(0x64, 200.0f, 100.0f, 2, Client::World::ObjectScene::Other)};
        Client::Render::Models::CrtRandom random(7);
        std::uint8_t depth = probe.depth;
        Client::World::FrameSprites sprites;
        const Client::Scenes::FaunaDrawCarry pose;
        ModelBatch batch;
        const auto drawn = DrawGame(batch, models, objects,
                                    {.map = 2,
                                     .random = &random,
                                     .heroClass = probe.heroClass,
                                     .ownLevel = probe.level,
                                     .ownRecordDepth = &depth,
                                     .sprites = &sprites,
                                     .retainedPose = &pose});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(objects[0].seen);
        EXPECT_EQ(sprites.Sprites()[0].alive, probe.drawn);
        EXPECT_EQ(depth, probe.depth);
        EXPECT_EQ(random.State(), SevenAfter(probe.draws).State());
    }
}

// 0x603251: el 0x64 de Devias deja dos 0x5d5 de 2,5 en (0; 0; 150) por el hueso 0 de la pose global (la del
// último posado, sin trasladar: 0x4ec110 con 0), con (L; L; L) y el giro r y −r (r = ftol(reloj · 0,1f) %
// 360). Sin la pose global corta.
TEST(WorldObjectsDrawingTest, TheDeviasBeamObjectLeavesTwoSpritesOverItsFirstBone)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(0x64, 200.0f, 100.0f, 2, Client::World::ObjectScene::Other)};
    Client::Scenes::FaunaDrawCarry pose;
    Client::Render::Models::BoneTransform bone(1.0f);
    bone[3] = glm::vec3(10.0f, 20.0f, 30.0f);
    pose.bones.push_back(bone);
    Client::Render::Models::CrtRandom random(7);
    Client::World::FrameSprites sprites;
    ModelBatch batch;
    const auto drawn = DrawGame(batch, models, objects,
                                {.map = 2,
                                 .random = &random,
                                 .ownLevel = 0x50,
                                 .worldClock = 12345.0f,
                                 .sprites = &sprites,
                                 .retainedPose = &pose});
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), SevenAfter(1).State());
    auto expected = SevenAfter(0);
    const auto light = static_cast<float>(static_cast<double>(expected.Next() % 30 + 70) * static_cast<double>(0.01f));
    const auto turn = static_cast<float>(static_cast<std::int64_t>(12345.0 * static_cast<double>(0.1f)) % 360);
    const auto live = sprites.Sprites();
    ASSERT_TRUE(live[0].alive && live[1].alive);
    EXPECT_FALSE(live[2].alive);
    for (std::size_t k = 0; k < 2; ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_EQ(live[k].texture, 0x5d5);
        EXPECT_EQ(live[k].position, glm::vec3(10.0f, 20.0f, 180.0f));
        EXPECT_EQ(live[k].scale, 2.5f);
        EXPECT_EQ(live[k].light, glm::vec3(light));
        EXPECT_EQ(live[k].rotation, k == 0 ? turn : -turn);
    }

    Client::Render::Models::CrtRandom other(7);
    ModelBatch second;
    const auto cut =
        DrawGame(second, models, objects, {.map = 2, .random = &other, .ownLevel = 0x50, .sprites = &sprites});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4ec110"), std::string::npos) << cut.Error();
}

// Como el 0x67 de Devias (Object104.bmd): dos mallas del hueso 0 y dos acciones de seis keys.
class SwitchingModels final : public Client::Scenes::IModelLibrary
{
public:
    SwitchingModels()
    {
        Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "a.jpg";
        m_model.meshes = {mesh, mesh};
        Client::Render::Models::SkeletonBone bone;
        bone.parent = -1;
        Client::Render::Models::BoneKey key;
        key.rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
        const std::vector<Client::Render::Models::BoneKey> keys(6, key);
        bone.actions = {keys, keys};
        m_skeleton.bones = {bone};
        const Client::Render::Models::ActionInfo action{
            .repeats = true, .speed = 0.25f, .keyCount = 6, .lockPositions = false};
        m_skeleton.actions = {action, action};
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

// 0x6031f7: el 0x67 de Devias mira el cuadro que 0x4eb860 dejó en su modelo al posarlo (+0xa8, el short de
// ftol del frame, en 0 si no es una key de la acción) y, en la última key de su acción, saca un rand(): & 31
// en 0 lo pasa a la acción 1 con 0x4ea030 (la vieja y su key quedan como anteriores, el frame en 0), y si no,
// a la 0 (con la 0 puesta, nada). Con el cuadro en otra key no saca nada. Sin posar (alpha por debajo de 0,01)
// o con la acción fuera de la tabla, corta.
TEST(WorldObjectsDrawingTest, TheDeviasSwitchingObjectChangesItsActionOnTheLastKey)
{
    const SwitchingModels models;
    struct Case
    {
        std::uint32_t seed;
        std::uint8_t action;
        float frame;
        int draws;
        std::uint8_t wantedAction;
        float wantedFrame;
    };
    // Con la semilla 2, el segundo rand() es 29.216 (& 31 en 0); con la 7, 17.422 (14).
    for (const Case& probe :
         {Case{2, 0, 5.5f, 2, 1, 0.0f}, Case{7, 0, 5.5f, 2, 0, 5.5f}, Case{2, 0, 4.99f, 1, 0, 4.99f},
          Case{2, 0, 6.0f, 1, 0, 6.0f}, Case{7, 1, 5.0f, 2, 0, 0.0f}, Case{2, 1, 5.0f, 2, 1, 5.0f}})
    {
        SCOPED_TRACE(std::to_string(probe.seed) + " " + std::to_string(probe.action) + " " +
                     std::to_string(probe.frame));
        std::vector<WorldObject> objects{ObjectAt(0x67, 200.0f, 100.0f, 2, Client::World::ObjectScene::Other)};
        objects[0].animation.action = probe.action;
        objects[0].animation.frame = probe.frame;
        Client::Render::Models::CrtRandom random(probe.seed);
        ModelBatch batch;
        const auto drawn = DrawGame(batch, models, objects, {.map = 2, .random = &random});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        Client::Render::Models::CrtRandom expected(probe.seed);
        for (int k = 0; k < probe.draws; ++k)
            static_cast<void>(expected.Next());
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(objects[0].animation.action, probe.wantedAction);
        EXPECT_EQ(objects[0].animation.frame, probe.wantedFrame);
        if (probe.wantedAction != probe.action)
        {
            EXPECT_EQ(objects[0].animation.previousAction, probe.action);
            EXPECT_EQ(objects[0].animation.previousKey, 5);
        }
    }

    std::vector<WorldObject> unposed{ObjectAt(0x67, 200.0f, 100.0f, 2, Client::World::ObjectScene::Other)};
    unposed[0].alpha = 0.005f;
    unposed[0].animation.frame = 5.0f;
    Client::Render::Models::CrtRandom random(7);
    ModelBatch batch;
    const auto cut = DrawGame(batch, models, unposed, {.map = 2, .random = &random});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x60320a"), std::string::npos) << cut.Error();
    EXPECT_EQ(random.State(), SevenAfter(1).State());

    std::vector<WorldObject> outside{ObjectAt(0x67, 200.0f, 100.0f, 2, Client::World::ObjectScene::Other)};
    outside[0].animation.action = 2;
    Client::Render::Models::CrtRandom other(7);
    ModelBatch second;
    const auto beyond = DrawGame(second, models, outside, {.map = 2, .random = &other});
    ASSERT_FALSE(beyond.HasValue());
    EXPECT_NE(beyond.Error().find("0x603205"), std::string::npos) << beyond.Error();
}

// Un dibujo intermedio de MuRet (sin frame del original): dibuja y marca los vistos, sin el rand() de
// 0x602ed0 ni sus casos, y el 0x64 de Devias lee el nivel sin abrir el registro.
TEST(WorldObjectsDrawingTest, AnIntermediateGameDrawTakesNoRandomness)
{
    const TwoMeshModels models;
    std::vector<WorldObject> lorencia{ObjectAt(0x38, 100.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other)};
    Client::Render::Models::CrtRandom random(7);
    ModelBatch batch;
    ASSERT_TRUE(
        DrawGame(batch, models, lorencia, {.map = Lorencia, .originalFrame = false, .random = &random}).HasValue());
    EXPECT_TRUE(lorencia[0].seen);
    EXPECT_EQ(batch.Vertices().size(), 6u);

    std::vector<WorldObject> devias{ObjectAt(0x64, 100.0f, 100.0f, 2, Client::World::ObjectScene::Other)};
    std::uint8_t depth = 0;
    ASSERT_TRUE(
        DrawGame(batch, models, devias,
                 {.map = 2, .originalFrame = false, .random = &random, .ownLevel = 0x50, .ownRecordDepth = &depth})
            .HasValue());
    EXPECT_EQ(random.State(), 7u);
}

TEST(WorldObjectsDrawingTest, ProjectedPosePublicationDoesNotRunOriginalFrameProducers)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 100.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other)};
    Client::Render::Models::CrtRandom random(7);
    ModelBatch batch;
    int publications = 0;
    Client::Scenes::GameObjectPass pass;
    pass.map = Lorencia;
    pass.originalFrame = false;
    pass.random = &random;
    pass.onPose = [&](const Client::Scenes::DrawnObject&) -> Client::Core::Expected<void, std::string>
    {
        ++publications;
        return {};
    };
    ASSERT_TRUE(DrawGame(batch, models, objects, pass).HasValue());
    EXPECT_EQ(publications, 0);
    pass.publishProjectedPose = true;
    ASSERT_TRUE(DrawGame(batch, models, objects, pass).HasValue());
    EXPECT_EQ(publications, 1);
    EXPECT_EQ(random.State(), 7u);
    EXPECT_FALSE(batch.Vertices().empty());
}

// 5ffdf0 posa antes de que600328 descarte hiddenMesh=-2: el scratch cambia sin geometría.
TEST(WorldObjectsDrawingTest, HiddenGameObjectsPublishNativeOrProjectedPoseWithoutGeometry)
{
    const TwoMeshModels models;
    for (const bool original : {false, true})
    {
        std::vector<WorldObject> objects{ObjectAt(Rock, 100.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other)};
        objects.front().state.hiddenMesh = Client::World::HiddenObject;
        Client::Scenes::FaunaDrawCarry native;
        native.bones.resize(1, Client::Render::Models::BoneTransform(1.0f));
        native.bones.front()[3].x = 900.0f;
        auto projected = native;
        auto& target = original ? native : projected;
        Client::Render::Models::CrtRandom random(7);
        ModelBatch batch;
        Client::Scenes::GameObjectPass pass;
        pass.map = Lorencia;
        pass.originalFrame = original;
        pass.publishProjectedPose = !original;
        pass.random = &random;
        pass.retainedPose = &target;
        int publications = 0;
        pass.onPose = [&](const Client::Scenes::DrawnObject& pose) -> Client::Core::Expected<void, std::string>
        {
            EXPECT_EQ(pose.state.hiddenMesh, Client::World::HiddenObject);
            ++publications;
            return Client::Scenes::PublishFaunaPose(target, models, pose, true);
        };
        ASSERT_TRUE(DrawGame(batch, models, objects, pass).HasValue());
        EXPECT_EQ(publications, 1);
        EXPECT_TRUE(objects.front().seen);
        EXPECT_TRUE(batch.Vertices().empty());
        EXPECT_TRUE(batch.Commands().empty());
        ASSERT_FALSE(target.meshes.empty());
        EXPECT_EQ(target.meshes.front().positions.front(), glm::vec3(100.0f, 100.0f, 0.0f));
        EXPECT_EQ(target.bones.front()[3].x, 100.0f);
        if (!original)
        {
            EXPECT_EQ(native.bones.front()[3].x, 900.0f);
        }
        EXPECT_EQ(random.State(), SevenAfter(original ? 1 : 0).State());
    }
}

TEST(WorldObjectsDrawingTest, HiddenLoginObjectsPublishNativeOrProjectedPoseWithoutGeometry)
{
    const TwoMeshModels models;
    for (const bool original : {false, true})
    {
        std::vector<WorldObject> objects{ObjectAt(Rock, 100.0f, 100.0f, Login, Client::World::ObjectScene::Login)};
        objects.front().state.hiddenMesh = Client::World::HiddenObject;
        Client::Scenes::FaunaDrawCarry native;
        native.bones.resize(1, Client::Render::Models::BoneTransform(1.0f));
        native.bones.front()[3].x = 900.0f;
        auto projected = native;
        auto& target = original ? native : projected;
        Client::Render::Models::CrtRandom random(7);
        ModelBatch batch;
        Client::Scenes::ObjectFrameEffects effects;
        effects.random = &random;
        effects.frames = original ? 1 : 0;
        effects.publishProjectedPose = !original;
        effects.retainedPose = &target;
        int publications = 0;
        effects.onPose = [&](const Client::Scenes::DrawnObject& pose) -> Client::Core::Expected<void, std::string>
        {
            ++publications;
            return Client::Scenes::PublishFaunaPose(target, models, pose, true);
        };
        DrawWorldObjects(batch, models, objects, Client::World::TerrainPolygon{.everything = true}, nullptr, Login, {},
                         0.0f, effects);
        EXPECT_EQ(publications, 1);
        EXPECT_TRUE(batch.Vertices().empty());
        EXPECT_TRUE(batch.Commands().empty());
        ASSERT_FALSE(target.meshes.empty());
        EXPECT_EQ(target.meshes.front().positions.front(), glm::vec3(100.0f, 100.0f, 0.0f));
        EXPECT_EQ(target.bones.front()[3].x, 100.0f);
        if (!original)
        {
            EXPECT_EQ(native.bones.front()[3].x, 900.0f);
        }
        EXPECT_EQ(random.State(), SevenAfter(original ? 1 : 0).State());
    }
}

TEST(WorldObjectsDrawingTest, AlphaRejectedObjectsDoNotPublishPoseEvenWithProjectedPublication)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 100.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other)};
    objects.front().alpha = 0.005f;
    objects.front().state.hiddenMesh = Client::World::HiddenObject;
    int publications = 0;
    const auto observe = [&](const Client::Scenes::DrawnObject&) -> Client::Core::Expected<void, std::string>
    {
        ++publications;
        return {};
    };
    ModelBatch batch;
    Client::Scenes::GameObjectPass pass;
    pass.map = Lorencia;
    pass.originalFrame = false;
    pass.publishProjectedPose = true;
    pass.onPose = observe;
    ASSERT_TRUE(DrawGame(batch, models, objects, pass).HasValue());
    Client::Scenes::ObjectFrameEffects effects;
    effects.frames = 0;
    effects.publishProjectedPose = true;
    effects.onPose = observe;
    DrawWorldObjects(batch, models, objects, Client::World::TerrainPolygon{.everything = true}, nullptr, Login, {},
                     0.0f, effects);
    EXPECT_EQ(publications, 0);
    EXPECT_TRUE(batch.Vertices().empty());
}

// Los demás mapas cortan antes de mirar un objeto: sus casos de 0x6002e0 y 0x602ed0 no están leídos.
TEST(WorldObjectsDrawingTest, TheGamePassRejectsUnreadMaps)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 100.0f, 100.0f, 3, Client::World::ObjectScene::Other)};
    Client::Render::Models::CrtRandom random(7);
    ModelBatch batch;
    const auto drawn = DrawGame(batch, models, objects, {.map = 9, .random = &random});
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x6046a0"), std::string::npos) << drawn.Error();
    EXPECT_FALSE(objects[0].seen);
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_EQ(random.State(), 7u);
}

// 0x606560 en el juego: solo los vivos que se vieron, en el orden del dibujo, hasta el primero que corta (la
// lámpara del 0, después del rand() de su luz); el de después no avanza.
TEST(WorldObjectsDrawingTest, TheGameStepStopsAtTheFirstCaseMuRetDoesNotHave)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(0x7a, 300.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other),
                                     ObjectAt(0x5a, 200.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other),
                                     ObjectAt(0x7a, 100.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other),
                                     ObjectAt(0x7a, 150.0f, 100.0f, Lorencia, Client::World::ObjectScene::Other)};
    for (WorldObject& object : objects)
    {
        object.seen = true;
        object.alpha = 0.5f;
    }
    objects[2].alive = false;
    Client::Render::Models::CrtRandom random(7);
    const auto stepped =
        Client::Scenes::StepGameWorldObjects(objects, Lorencia, models, random, Client::World::ObjectStepGlobals{});
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x605718"), std::string::npos) << stepped.Error();
    EXPECT_GT(objects[0].alpha, 0.5f);
    EXPECT_GT(objects[1].alpha, 0.5f);
    EXPECT_EQ(objects[2].alpha, 0.5f) << "no está vivo";
    EXPECT_EQ(objects[3].alpha, 0.5f) << "después del corte";
    EXPECT_EQ(random.State(), SevenAfter(2).State());
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
    std::array<Client::World::ModelEffect, 1> effects{LiveEffect()};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::DrawModelEffects(batch, models, effects, Client::World::SphereFrustum{}, lighting.get(), {}, 0.0f);
    ASSERT_EQ(batch.Vertices().size(), 6u);
    const float light = Client::Render::Models::NormalLight(glm::vec3(0.0f, 0.0f, 1.0f));
    const auto& color = batch.Vertices()[0].color;
    EXPECT_FLOAT_EQ(color.red, 1.25f * light);
    EXPECT_FLOAT_EQ(color.green, 1.5f * light);
    EXPECT_FLOAT_EQ(color.blue, 1.75f * light);
    EXPECT_NE(light, 1.0f) << "la normal tiene que cambiar el color para que el test mire la luz por vértice";
}

TEST(ModelEffectsDrawingTest, CullingUsesTheLogicalPositionAndRetainsDeadSlotHistory)
{
    const TwoMeshModels models;
    std::array<Client::World::ModelEffect, 3> effects{LiveEffect(), LiveEffect(), LiveEffect()};
    Client::World::SphereFrustum frustum;
    frustum.normals[0] = {1.0f, 0.0f, 0.0f};
    effects[0].seen = true;
    effects[0].transform.position.x = -401.0f;
    effects[0].previousPosition.x = -1000.0f;
    // La extrapolación entraría en pantalla; el recorte usa +0x10, no esa posición dibujada.
    effects[1].seen = false;
    effects[1].transform.position.x = 0.0f;
    effects[1].previousPosition.x = 1000.0f;
    effects[2].alive = false;
    effects[2].seen = true;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::DrawModelEffects(batch, models, effects, frustum, nullptr, {}, 1.0f);
    EXPECT_FALSE(effects[0].seen);
    EXPECT_TRUE(effects[1].seen);
    EXPECT_TRUE(effects[2].seen);
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_EQ(batch.Vertices()[0].position.x, -1000.0f);
}

struct MissingEffectModels final : Client::Scenes::IModelLibrary
{
    mutable int lookups = 0;
    const Model* Find(std::uint16_t) const override
    {
        ++lookups;
        return nullptr;
    }
    const Skeleton* SkeletonOf(std::uint16_t) const override
    {
        ++lookups;
        return nullptr;
    }
    std::span<const TextureSlot> MeshTextures(std::uint16_t) const override
    {
        ++lookups;
        return {};
    }
};

TEST(ModelEffectsDrawingTest, VisibilityIsPublishedBeforeLookingForTheModel)
{
    const MissingEffectModels models;
    std::array<Client::World::ModelEffect, 1> effects{LiveEffect()};
    Client::World::SphereFrustum frustum;
    frustum.normals[0] = {1.0f, 0.0f, 0.0f};
    effects[0].seen = true;
    effects[0].transform.position.x = -401.0f;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::DrawModelEffects(batch, models, effects, frustum, nullptr, {}, 0.0f);
    EXPECT_FALSE(effects[0].seen);
    EXPECT_EQ(models.lookups, 0);
    effects[0].transform.position.x = 0.0f;
    Client::Scenes::DrawModelEffects(batch, models, effects, frustum, nullptr, {}, 0.0f);
    EXPECT_TRUE(effects[0].seen);
    EXPECT_GT(models.lookups, 0);
    EXPECT_TRUE(batch.Vertices().empty());
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
    std::array<Client::World::ModelEffect, 1> effects{LiveEffect()};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::DrawModelEffects(batch, models, effects, Client::World::SphereFrustum{}, nullptr, {}, 0.5f);
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

// 0x4255f3 y 0x42588d (0x425260 desde 0x602ed0): el 0x29 y el 0x41/0x42 de Aida repiten su malla 0 con un
// 0x4ecc30 después del cuerpo, sin luz por vértice y con la U del reloj (−ftol(T) % 100.000 · paso) y la V del
// objeto. El 0x29 suma (banderas 0x42) en 0,52 y paso 0,00001f; el 0x41/0x42 va con el pase base (2), el color
// que le dejó el cuerpo y paso 0,00005f. Dibujan también en un dibujo intermedio de MuRet, sin rand().
TEST(WorldObjectsDrawingTest, TheAidaBodiesRepeatTheirFirstMeshWithTheClockScroll)
{
    constexpr std::uint8_t Aida = 33;
    const TwoMeshModels models;
    struct Case
    {
        std::int16_t type;
        ModelBlend blend;
        float step;
    };
    for (const Case& probe : {Case{0x29, ModelBlend::Additive, std::bit_cast<float>(0x3727c5acu)},
                              Case{0x41, ModelBlend::Opaque, std::bit_cast<float>(0x3851b717u)},
                              Case{0x42, ModelBlend::Opaque, std::bit_cast<float>(0x3851b717u)}})
    {
        SCOPED_TRACE(probe.type);
        std::vector<WorldObject> objects{ObjectAt(probe.type, 100.0f, 100.0f, Aida, Client::World::ObjectScene::Other)};
        objects[0].light = glm::vec3(0.25f, 0.5f, 0.75f);
        objects[0].vertexLight = false;
        objects[0].state.uv = glm::vec2(0.5f, 0.125f);
        for (const bool original : {false, true})
        {
            Client::Render::Models::CrtRandom random(7);
            ModelBatch batch;
            ASSERT_TRUE(DrawGame(batch, models, objects,
                                 {.map = Aida, .originalFrame = original, .random = &random, .worldClock = 123456.5f})
                            .HasValue());
            EXPECT_EQ(random.State(), SevenAfter(original ? 1 : 0).State());
            ASSERT_EQ(batch.Commands().size(), 3u);
            const auto& pass = batch.Commands()[2];
            EXPECT_EQ(pass.state.texture, TextureSlot{0x41c});
            EXPECT_EQ(pass.state.blend, probe.blend);
            ASSERT_EQ(pass.vertexCount, 3u);
            const auto& vertex = batch.Vertices()[pass.firstVertex];
            const auto u = static_cast<float>(-23456.0 * static_cast<double>(probe.step));
            EXPECT_EQ(vertex.uv, glm::vec2(u, 0.125f));
            const glm::vec3 color =
                probe.type == 0x29 ? glm::vec3(std::bit_cast<float>(0x3f051eb8u)) : glm::vec3(0.25f, 0.5f, 0.75f);
            EXPECT_EQ(vertex.color.red, color.x);
            EXPECT_EQ(vertex.color.green, color.y);
            EXPECT_EQ(vertex.color.blue, color.z);
        }
    }
    // Otros tipos del 33 no repiten nada.
    std::vector<WorldObject> objects{ObjectAt(0x10, 100.0f, 100.0f, Aida, Client::World::ObjectScene::Other)};
    Client::Render::Models::CrtRandom random(7);
    ModelBatch batch;
    ASSERT_TRUE(DrawGame(batch, models, objects, {.map = Aida, .originalFrame = false, .random = &random}).HasValue());
    EXPECT_EQ(batch.Commands().size(), 2u);
}

// 0x4403b0 (la cadena de 0x6002e0 en el 31): el 0x1b, el 0x36 y el 0x34 multiplican el color del modelo por
// el pulso de su fase; el 0x34 se dibuja dos veces con ese color (Q-C317); el 0xa va en (0,56; 0,8; 0,81).
// Sin rand() ni estado: igual en un dibujo intermedio y en uno del original.
TEST(WorldObjectsDrawingTest, TheLandOfTrialsBodiesPulseAndTheBrazierBaseIsDrawnTwice)
{
    constexpr std::uint8_t Trials = 31;
    const TwoMeshModels models;
    const glm::vec3 light(0.25f, 0.5f, 0.75f);
    const float clock = 123456.5f;
    struct Case
    {
        std::int16_t type;
        std::size_t commands;
    };
    for (const Case& probe : {Case{0x1b, 2}, Case{0x36, 2}, Case{0x34, 4}, Case{0xa, 2}, Case{0x10, 2}})
    {
        SCOPED_TRACE(probe.type);
        std::vector<WorldObject> objects{
            ObjectAt(probe.type, 100.0f, 100.0f, Trials, Client::World::ObjectScene::Other)};
        objects[0].light = light;
        objects[0].vertexLight = false;
        objects[0].field84 = 2.5f;
        const auto body = Client::Scenes::LandOfTrialsBody(objects[0], light, clock);
        ASSERT_TRUE(body.HasValue());
        const glm::vec3 color = body.Value() ? body.Value()->color : light;
        for (const bool original : {false, true})
        {
            Client::Render::Models::CrtRandom random(7);
            ModelBatch batch;
            ASSERT_TRUE(DrawGame(batch, models, objects,
                                 {.map = Trials, .originalFrame = original, .random = &random, .worldClock = clock})
                            .HasValue());
            EXPECT_EQ(random.State(), SevenAfter(original ? 1 : 0).State());
            ASSERT_EQ(batch.Commands().size(), probe.commands);
            for (const auto& command : batch.Commands())
            {
                ASSERT_GT(command.vertexCount, 0u);
                const auto& vertex = batch.Vertices()[command.firstVertex];
                EXPECT_EQ(vertex.color.red, color.x);
                EXPECT_EQ(vertex.color.green, color.y);
                EXPECT_EQ(vertex.color.blue, color.z);
            }
        }
    }
}
} // namespace
