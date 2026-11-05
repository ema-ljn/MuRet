#include "Scenes/WorldObjectsDrawing.h"

#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Render/ModelBatch.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "World/TerrainCulling.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
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
using Client::World::WorldObject;

constexpr std::uint8_t Login = 55;
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

WorldObject ObjectAt(std::int16_t type, float x, float y)
{
    Client::Data::Terrain::TerrainObject record;
    record.type = type;
    record.position = {x, y, 0.0f};
    record.scale = 1.0f;
    const auto objects =
        Client::World::CreateWorldObjects(std::span(&record, 1), Login, Client::World::ObjectScene::Login);
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
          const Client::World::TerrainPolygon& polygon = Everything)
{
    batch.Begin(Client::Render::Pass3D{});
    DrawWorldObjects(batch, models, objects, polygon, nullptr, Login, {}, 0.0f);
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
} // namespace
