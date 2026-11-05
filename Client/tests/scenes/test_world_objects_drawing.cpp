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
#include "Render/Billboard.h"
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

// Una biblioteca con el tamaño de la textura de los sprites (0x5ac de 64 × 32).
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
    [[nodiscard]] std::optional<glm::vec2> TextureSize(TextureSlot slot) const override
    {
        return slot == TextureSlot{0x5ac} ? std::optional<glm::vec2>(glm::vec2(64.0f, 32.0f)) : std::nullopt;
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
} // namespace
