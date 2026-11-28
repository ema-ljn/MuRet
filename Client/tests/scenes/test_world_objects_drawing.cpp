#include "Models/IModelLibrary.h"
#include "support/CheckedWorldObjectProjection.h"

#include "WorldObjects/WorldObjectsDrawing.h"
#include "MapScripts/LandOfTrials/LandOfTrialsObjectServices.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Render/ModelBatch.h"
#include "Render/TextureEnvMode.h"
#include "Particles/BloodCastleParticleKernels.h"
#include "Math/HalfLifeMath.h"
#include "Models/ObjectState.h"
#include "Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/Drawing/PosePublishing.h"
#include "Kernel/PoseBuffer.h"
#include "Models/TextureMetadata.h"
#include "Data/Textures/Texture.h"
#include "Terrain/TerrainCulling.h"
#include "Render/Billboard.h"
#include "Particles/ParticleDrawing.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Models/Skinning.h"
#include "Models/ModelId.h"
#include "Terrain/TerrainLighting.h"
#include "Effects/ModelEffects.h"
#include "Effects/GroundEffectDrawing.h"
#include "MapScripts/Kalima/KalimaWorld.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Terrain/SphereFrustum.h"
#include "WorldObjects/WorldObjects.h"

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

// Client::WorldObjects::StepWorldObjects and DrawWorldObjects (06 § El mundo del login): which objects advance, in
// what order they are drawn and how map 55's whirlpools go, with fake models; and the game's, DrawGameWorldObjects
// and StepGameWorldObjects (06 § Los objetos del juego).
namespace
{
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Models::Skeleton;
using Client::Models::TextureMetadata;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::TextureSlot;
using Client::WorldObjects::DrawWorldObjects;
using Client::WorldObjects::StepWorldObjects;
using Client::WorldObjects::WorldObject;

constexpr std::uint8_t Login = 55;
constexpr std::uint8_t Lorencia = 0;
constexpr std::int16_t Bridge = 0x50;
constexpr std::int16_t RedWhirlpool = 0x56;
constexpr std::int16_t DarkWhirlpool = 0x5a;
constexpr std::int16_t Rock = 1;

// Any model: two one-triangle meshes of bone 0 (textures 0x41c and 0x41d) and a one-bone skeleton, without
// animation.
class TwoMeshModels final : public Client::Models::IModelLibrary
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
        Client::Models::SkeletonBone bone;
        bone.parent = -1;
        Client::Models::BoneKey key;
        key.rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
        bone.actions = {{key}};
        m_skeleton.bones = {bone};
        m_skeleton.actions = {
            Client::Models::ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
    }

    [[nodiscard]] const Model* Find(Client::Models::ModelId) const override
    {
        return &m_model;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return &m_skeleton;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return m_textures;
    }

private:
    Model m_model;
    Skeleton m_skeleton;
    std::array<TextureSlot, 2> m_textures{TextureSlot{0x41c}, TextureSlot{0x41d}};
};

// A model like Waterspout01: four one-triangle meshes of bone 0, each with its index's texture (0 to 3, slots 0x41c
// to 0x41f) and the UV (0.125; 0.25), without animation.
class FourMeshModels final : public Client::Models::IModelLibrary
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
        Client::Models::SkeletonBone bone;
        bone.parent = -1;
        Client::Models::BoneKey key;
        key.rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
        bone.actions = {{key}};
        m_skeleton.bones = {bone};
        m_skeleton.actions = {
            Client::Models::ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
    }

    [[nodiscard]] const Model* Find(Client::Models::ModelId) const override
    {
        return &m_model;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return &m_skeleton;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId) const override
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
                     Client::WorldObjects::ObjectScene scene = Client::WorldObjects::ObjectScene::Login)
{
    Client::Data::Terrain::TerrainObject record;
    record.type = type;
    record.position = {x, y, 0.0f};
    record.scale = 1.0f;
    const auto objects = Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), map, scene);
    return objects.front();
}

const Client::Terrain::TerrainPolygon Everything{.everything = true};

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
          const Client::Terrain::TerrainPolygon& polygon = Everything, std::uint8_t map = Login)
{
    batch.Begin(Client::Render::Pass3D{});
    DrawWorldObjects(batch, models, objects, polygon, nullptr, map, {}, 0.0f);
}

// 0x606560 only advances those seen in the last draw (+0x16c): freshly created, the alpha does not move; after one
// draw, a tenth toward its target (Q-C135).
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

// 0x6046a0: the sectors by x and then by y, and in each one the file's order. The object of sector (0, 3) goes
// before that of (5, 2), and the two of (0, 3) in their order.
TEST(WorldObjectsDrawingTest, ObjectsGoSectorBySector)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 8000.0f, 3500.0f), ObjectAt(Rock, 100.0f, 5000.0f),
                                     ObjectAt(Rock, 200.0f, 5100.0f)};
    ModelBatch batch;
    Draw(batch, models, objects);
    ASSERT_EQ(batch.Vertices().size(), 3u * 6u);
    // The first corner of each one's mesh 0, in batch order.
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 100.0f);
    EXPECT_FLOAT_EQ(batch.Vertices()[6].position.x, 200.0f);
    EXPECT_FLOAT_EQ(batch.Vertices()[12].position.x, 8000.0f);
}

// An object outside the game's trapezoid is neither seen nor advances: the one in a far sector, and the one in the
// hero's sector (9, 7) that is outside at its place.
TEST(WorldObjectsDrawingTest, AnObjectOutsideThePolygonIsNotSeen)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 14400.0f, 12100.0f), ObjectAt(Rock, 100.0f, 100.0f),
                                     ObjectAt(Rock, 15900.0f, 11300.0f)};
    const auto polygon =
        Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    ModelBatch batch;
    Draw(batch, models, objects, polygon);
    EXPECT_TRUE(objects[0].seen);
    EXPECT_FALSE(objects[1].seen);
    EXPECT_EQ(Client::WorldObjects::SectorX(objects[2]), 9);
    EXPECT_EQ(Client::WorldObjects::SectorY(objects[2]), 7);
    EXPECT_FALSE(objects[2].seen);
    EXPECT_EQ(batch.Vertices().size(), 6u);
}

// 0x6069bd: map 0's bridge uses −50 at +0xdc; 0x604744..0x60476b passes that margin to 0x5fba90. Sector (0, 0),
// center (8, 8), passes; (14, 5) gives −40 on the right side.
TEST(WorldObjectsDrawingTest, TheLorenciaBridgeExtendsItsOwnCullingMargin)
{
    const TwoMeshModels models;
    const Client::Terrain::TerrainPolygon polygon{.x = {0.0f, 0.0f, 10.0f, 10.0f}, .y = {0.0f, 10.0f, 10.0f, 0.0f}};
    for (const auto scene : {Client::WorldObjects::ObjectScene::Other, Client::WorldObjects::ObjectScene::Login,
                             Client::WorldObjects::ObjectScene::CharacterSelect})
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

// The edge of 0x5fba90 is strict: (15, 5) gives −50. On leaving, seen and the frame's batch are cleared.
TEST(WorldObjectsDrawingTest, TheBridgeIsCulledExactlyAtItsMargin)
{
    const TwoMeshModels models;
    const Client::Terrain::TerrainPolygon polygon{.x = {0.0f, 0.0f, 10.0f, 10.0f}, .y = {0.0f, 10.0f, 10.0f, 0.0f}};
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

// The consumer uses the object's field, without deducing it again from type, map or scene. The helper returns a copy
// and the array copies it again, like the scenes' containers.
TEST(WorldObjectsDrawingTest, CullingUsesTheStoredMarginAfterCopyingAnObject)
{
    const TwoMeshModels models;
    const Client::Terrain::TerrainPolygon polygon{.x = {0.0f, 0.0f, 10.0f, 10.0f}, .y = {0.0f, 10.0f, 10.0f, 0.0f}};
    WorldObject extended = ObjectAt(Rock, 1400.0f, 500.0f);
    extended.cullingMargin = -50.0f;
    std::array objects{extended, ObjectAt(Rock, 1400.0f, 500.0f)};
    ModelBatch batch;
    Draw(batch, models, objects, polygon);
    EXPECT_TRUE(objects[0].seen);
    EXPECT_FALSE(objects[1].seen);
    EXPECT_EQ(batch.Vertices().size(), 6u);
}

// 0x602ed0 takes a CRT rand() for each object that reaches it (0x602ef4, before looking at the scene or the type):
// those seen, even if not drawn (the one with alpha 0 and the one with +0x58 at −2). The one in the far sector does
// not reach it.
TEST(WorldObjectsDrawingTest, EachSeenObjectTakesARand)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 14400.0f, 12100.0f), ObjectAt(Rock, 100.0f, 100.0f),
                                     ObjectAt(Rock, 14450.0f, 12150.0f), ObjectAt(Rock, 14420.0f, 12120.0f)};
    objects[2].alpha = 0.0f;
    objects[3].state.hiddenMesh = Client::WorldObjects::HiddenObject;
    const auto polygon =
        Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    Client::Crt::CrtRandom random(7);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawWorldObjects(batch, models, objects, polygon, nullptr, Login, {}, 0.0f,
                     Client::WorldObjects::ObjectFrameEffects{.random = &random});
    Client::Crt::CrtRandom expected(7);
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
    Client::Crt::CrtRandom random(4);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawWorldObjects(batch, models, objects, Everything, nullptr, Login, {}, 0.0f,
                     Client::WorldObjects::ObjectFrameEffects{.random = &random, .frames = 0});
    EXPECT_TRUE(objects[0].seen);
    EXPECT_EQ(batch.Vertices().size(), 6u);
    EXPECT_EQ(random.State(), 4u);
}

// The game objects pass (0x6046a0 at 0x6374ce) with the fake models, everything seen and seed 7.
Client::Core::Expected<void, std::string> DrawGame(ModelBatch& batch, const Client::Models::IModelLibrary& models,
                                                   std::span<WorldObject> objects,
                                                   const Client::WorldObjects::GameObjectPass& pass)
{
    batch.Begin(Client::Render::Pass3D{});
    return Client::WorldObjects::DrawGameWorldObjects(batch, models, objects, Everything, nullptr, {}, pass);
}

Client::Crt::CrtRandom SevenAfter(int draws)
{
    Client::Crt::CrtRandom random(7);
    for (int k = 0; k < draws; ++k)
    {
        static_cast<void>(random.Next());
    }
    return random;
}

// 0x6046a0 in the game: each live object that passes the trapezoid is drawn (0x602e90) and takes the rand() of
// 0x602ed0 (0x602ef4), even if it is not drawn (the one with +0x58 at −2). The one that is not alive is not looked at
// (0x60473b): it is not seen and takes no rand().
TEST(WorldObjectsDrawingTest, TheGamePassDrawsTheLiveObjectsWithARandEach)
{
    const TwoMeshModels models;
    for (const std::uint8_t map : {Lorencia, std::uint8_t{2}})
    {
        SCOPED_TRACE(static_cast<int>(map));
        std::vector<WorldObject> objects{
            ObjectAt(Rock, 8000.0f, 3500.0f, map, Client::WorldObjects::ObjectScene::Other),
            ObjectAt(Rock, 100.0f, 5000.0f, map, Client::WorldObjects::ObjectScene::Other),
            ObjectAt(Rock, 200.0f, 5100.0f, map, Client::WorldObjects::ObjectScene::Other)};
        objects[1].alive = false;
        objects[2].state.hiddenMesh = Client::WorldObjects::HiddenObject;
        Client::Crt::CrtRandom random(7);
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

// Lorencia's 0x38 (0x603149): after its rand(), two 0x5ac sprites at bones 48 and 57 with the stack offset that
// 0x6002e0 left (the y is its try level, −1): at NaN, scale L · 5 and light (L · 0.6; L · 0.3; L · 0.1). The later
// ones go on. Unposed (alpha below 0.01), the stack is 0x5ffdf0's, which is not read, and it cuts with the earlier
// ones drawn and the later ones not looked at.
TEST(WorldObjectsDrawingTest, TheLorenciaMerchantAnimalLeavesTwoGlowsAtNaN)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{
        ObjectAt(Rock, 100.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other),
        ObjectAt(0x38, 200.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other),
        ObjectAt(Rock, 300.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other)};
    Client::Crt::CrtRandom random(7);
    Client::Particles::FrameSprites sprites;
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
    Client::Crt::CrtRandom unposed(7);
    ModelBatch second;
    const auto cut = DrawGame(second, models, objects, {.map = Lorencia, .random = &unposed, .sprites = &sprites});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x60318a"), std::string::npos) << cut.Error();
    EXPECT_FALSE(objects[2].seen) << "el de después no se mira";
    EXPECT_EQ(second.Vertices().size(), 6u);
    EXPECT_EQ(unposed.State(), SevenAfter(2).State());
}

// 0x6002e0 with map 4: the 3 takes a rand() (in the original's frame) and, before its body, a RenderBody with
// Chrome01 (0x5c0) forced, opaque, with the light (1; 0.2; 0.1) and mesh 1 (its model+0x88) unlit and with the
// object's U and V; the 0x17 only takes the rand(). The body after, as always. An intermediate MuRet draw has no
// rand().
TEST(WorldObjectsDrawingTest, TheLostTowerChromeGoesBeforeTheBody)
{
    constexpr std::uint8_t LostTower = 4;
    const TwoMeshModels models;
    std::vector<WorldObject> objects{
        ObjectAt(3, 100.0f, 100.0f, LostTower, Client::WorldObjects::ObjectScene::Other),
        ObjectAt(0x17, 300.0f, 100.0f, LostTower, Client::WorldObjects::ObjectScene::Other)};
    for (auto& object : objects)
    {
        object.vertexLight = false;
        object.light = glm::vec3(0.5f);
        object.state.uv = glm::vec2(0.25f, 0.5f);
    }
    Client::Crt::CrtRandom random(7);
    ModelBatch batch;
    ASSERT_TRUE(DrawGame(batch, models, objects, {.map = LostTower, .random = &random}).HasValue());
    EXPECT_EQ(random.State(), SevenAfter(4).State()) << "dos del 6002e0 del 4 y dos de 0x602ed0";
    // The two chromed meshes share the state and end up in a single command.
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

    Client::Crt::CrtRandom intermediate(7);
    ModelBatch between;
    ASSERT_TRUE(DrawGame(between, models, objects, {.map = LostTower, .originalFrame = false, .random = &intermediate})
                    .HasValue());
    EXPECT_EQ(intermediate.State(), 7u);
    EXPECT_EQ(between.Commands().size(), 5u);
}

// 0x602ed0 with map 4: the 0x28 leaves two 0x5d5 of 2.5 over the object, 260 higher, with (L; L; L) and the turn r
// and −r (r = ftol(clock · 0.1f) % 360); the 9 falls into the 6's case and leaves a 0x5ac at NaN. The 0x13 reads the
// global pose's bones: without it, it cuts.
TEST(WorldObjectsDrawingTest, TheLostTowerFrameLeavesItsSprites)
{
    constexpr std::uint8_t LostTower = 4;
    const TwoMeshModels models;
    std::vector<WorldObject> objects{
        ObjectAt(0x28, 100.0f, 100.0f, LostTower, Client::WorldObjects::ObjectScene::Other),
        ObjectAt(9, 300.0f, 100.0f, LostTower, Client::WorldObjects::ObjectScene::Other)};
    objects[0].transform.position.z = 40.0f;
    Client::Crt::CrtRandom random(7);
    Client::Particles::FrameSprites sprites;
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

    std::vector<WorldObject> boned{ObjectAt(0x13, 100.0f, 100.0f, LostTower, Client::WorldObjects::ObjectScene::Other)};
    Client::Crt::CrtRandom other(7);
    ModelBatch third;
    const auto cut = DrawGame(third, models, boned, {.map = LostTower, .random = &other, .sprites = &sprites});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4ec110"), std::string::npos) << cut.Error();
}

// 0x600c72..0x600cdc: Tarkan's 0x51 goes without RenderBody, with two 0x4ecc30 of mesh 0: the common pass (2) and the
// chrome one (0x44) with Chrome01 (0x5c0), additive, in the model color without each normal's light; with the alpha
// below 0.99, the color times the alpha. Mesh 1 is not drawn.
TEST(WorldObjectsDrawingTest, TheTarkanChromeGoesAfterItsFirstMesh)
{
    constexpr std::uint8_t Tarkan = 8;
    const TwoMeshModels models;
    for (const float alpha : {1.0f, 0.5f})
    {
        SCOPED_TRACE(alpha);
        std::vector<WorldObject> objects{
            ObjectAt(0x51, 100.0f, 100.0f, Tarkan, Client::WorldObjects::ObjectScene::Other)};
        objects[0].vertexLight = false;
        objects[0].light = glm::vec3(0.5f);
        objects[0].alpha = alpha;
        objects[0].targetAlpha = alpha;
        Client::Crt::CrtRandom random(7);
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

// 0x600a4a..0x600ae6: Lorencia's 0x69 goes with four loose 0x4ecc30, without RenderBody. Meshes 0 to 2, opaque, with
// each normal's light and without the U or the V; mesh 3, its blendMesh one, additive, by the blendMesh light and with
// the object's U and V. The hidden mesh (+0x58) does not count: 0x4ecc30 does not look at it.
TEST(WorldObjectsDrawingTest, TheLorenciaWaterspoutDrawsFourLooseMeshes)
{
    const FourMeshModels models;
    std::vector<WorldObject> objects{
        ObjectAt(Client::WorldObjects::Waterspout, 100.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other)};
    ASSERT_EQ(objects[0].state.blendMesh, 3) << "el alta del 0x69 en el 0";
    objects[0].light = glm::vec3(1.0f);
    objects[0].state.light = 0.5f;
    objects[0].state.uv = glm::vec2(0.25f, 0.5f);
    objects[0].state.hiddenMesh = 3;
    Client::Crt::CrtRandom random(7);
    ModelBatch batch;
    ASSERT_TRUE(
        DrawGame(batch, models, objects, {.map = Lorencia, .originalFrame = false, .random = &random}).HasValue());
    ASSERT_EQ(batch.Commands().size(), 4u);
    ASSERT_EQ(batch.Vertices().size(), 12u);
    const float light = Client::Models::NormalLight(glm::vec3(0.0f, 0.0f, 1.0f));
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

// The first seed from 1 whose third rand() (that of rand() % 2 of 0x603011, after the common ones of an object and of
// the fountain) is odd (`odd`) or even.
std::uint32_t SeedWithThirdRand(bool odd)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        Client::Crt::CrtRandom random(seed);
        static_cast<void>(random.Next());
        static_cast<void>(random.Next());
        if (((random.Next() % 2) != 0) == odd)
        {
            return seed;
        }
    }
}

// 0x602ed0 with Lorencia's 0x69 (0x603011): after the common rand(), the blendMesh light at 1, the V with the pass
// clock and rand() % 2. Odd, it goes on with the later ones; even, it cuts at the first drop (0x6030c7) after its two
// rand(), with the later ones not looked at. Hidden, it is not drawn and runs the same.
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
            ObjectAt(Rock, 100.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other),
            ObjectAt(Client::WorldObjects::Waterspout, 200.0f, 100.0f, Lorencia,
                     Client::WorldObjects::ObjectScene::Other),
            ObjectAt(Rock, 300.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other)};
        if (probe.hidden)
        {
            objects[1].state.hiddenMesh = Client::WorldObjects::HiddenObject;
        }
        objects[1].state.light = 0.25f;
        const std::uint32_t seed = SeedWithThirdRand(probe.odd);
        Client::Crt::CrtRandom random(seed);
        ModelBatch batch;
        const auto drawn =
            DrawGame(batch, models, objects, {.map = Lorencia, .random = &random, .worldClock = 1234.0f});
        EXPECT_EQ(objects[1].state.light, 1.0f);
        EXPECT_EQ(objects[1].state.uv.y, static_cast<float>(-234.0 * static_cast<double>(0.001f)));
        const std::size_t fountain = probe.hidden ? 0u : 12u;
        Client::Crt::CrtRandom expected(seed);
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

// 0x52dc2d..0x52dc50: the retained +0x16c decides the common rand and the blend change, even in a type that has no
// ground case (the dragon). The sphere is not recomputed in this pass.
TEST(WorldObjectsDrawingTest, GroundEffectsUseOnlyTheRetainedVisibilityOfLiveSlots)
{
    std::array<Client::Effects::ModelEffect, 3> effects{};
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Crt::CrtRandom random(1234);
    Client::Crt::CrtRandom expected(1234);
    const Client::Effects::GroundEffectResources resources{.heights = *heights, .random = random};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto initial = batch.State();
    EXPECT_TRUE(Client::WorldObjects::DrawGroundEffects(batch, effects, resources).HasValue());
    effects[1].alive = true;
    effects[1].type = Client::Effects::ArrivalEffect;
    EXPECT_TRUE(Client::WorldObjects::DrawGroundEffects(batch, effects, resources).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(batch.State(), initial);
    effects[0].seen = true; // muerto: tampoco participa.
    effects[2].alive = true;
    effects[2].seen = true;
    effects[2].type = Client::Effects::DragonEffect;
    EXPECT_TRUE(Client::WorldObjects::DrawGroundEffects(batch, effects, resources).HasValue());
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Additive);
    EXPECT_EQ(batch.State().depthWrite, false);
    EXPECT_EQ(batch.State().cullEnabled, false);
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(WorldObjectsDrawingTest, AVisibleArrivalConsumesTheCommonRandomBeforeDrawing)
{
    std::array<Client::Effects::ModelEffect, 1> effects{};
    effects[0].alive = true;
    effects[0].seen = true;
    effects[0].type = Client::Effects::ArrivalEffect;
    effects[0].life = 20;
    effects[0].transform.position = {1000.0f, 1000.0f, 0.0f};
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Crt::CrtRandom random(1234);
    Client::Crt::CrtRandom expected(1234);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto drawn = Client::WorldObjects::DrawGroundEffects(batch, effects, {.heights = *heights, .random = random});
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_FALSE(batch.Vertices().empty());
}

TEST(WorldObjectsDrawingTest, ANoopGroundCasePublishesItsStateWithoutBindingATexture)
{
    std::array<Client::Effects::ModelEffect, 1> effects{};
    effects[0].alive = true;
    effects[0].seen = true;
    effects[0].type = Client::Effects::DragonEffect;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Crt::CrtRandom random;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(
        Client::WorldObjects::DrawGroundEffects(batch, effects, {.heights = *heights, .random = random}).HasValue());
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
    std::array<Client::Effects::ModelEffect, 1> effects{};
    effects[0].alive = true;
    effects[0].seen = true;
    effects[0].type = 0x6eb;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Crt::CrtRandom random;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto drawn = Client::WorldObjects::DrawGroundEffects(
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
    std::array<Client::Effects::ModelEffect, 1> effects{};
    effects[0].alive = true;
    effects[0].seen = true;
    effects[0].type = 0x6eb;
    effects[0].transform.scale = 1;
    effects[0].transform.position.x = std::numeric_limits<float>::infinity();
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::MapScripts::Kalima::KalimaWorld water(24);
    Client::Crt::CrtRandom random;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto drawn = Client::WorldObjects::DrawGroundEffects(
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

// 0x60479c..0x604a36: Devias' 0x64 only goes on with the own record's level from 0x50, or from 0x35 with the hero's
// class at 3 or 4 (+0x1d4 & 7), with the record opened and closed: with the count at 0, the close takes the 3848
// rand() of 0x40b4d0; with another access open, none. If it goes on, its case of 0x602ed0 (0x603251) leaves its two
// sprites after the rand(); if not, it does not run.
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
        std::vector<WorldObject> objects{ObjectAt(0x64, 200.0f, 100.0f, 2, Client::WorldObjects::ObjectScene::Other)};
        Client::Crt::CrtRandom random(7);
        std::uint8_t depth = probe.depth;
        Client::Particles::FrameSprites sprites;
        const Client::Kernel::PoseBuffer pose;
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

// 0x603251: Devias' 0x64 leaves two 0x5d5 of 2.5 at (0; 0; 150) by bone 0 of the global pose (the last posed one's,
// untranslated: 0x4ec110 with 0), with (L; L; L) and the turn r and −r (r = ftol(clock · 0.1f) % 360). Without the
// global pose it cuts.
TEST(WorldObjectsDrawingTest, TheDeviasBeamObjectLeavesTwoSpritesOverItsFirstBone)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(0x64, 200.0f, 100.0f, 2, Client::WorldObjects::ObjectScene::Other)};
    Client::Kernel::PoseBuffer pose;
    Client::Math::BoneTransform bone(1.0f);
    bone[3] = glm::vec3(10.0f, 20.0f, 30.0f);
    pose.bones.push_back(bone);
    Client::Crt::CrtRandom random(7);
    Client::Particles::FrameSprites sprites;
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

    Client::Crt::CrtRandom other(7);
    ModelBatch second;
    const auto cut =
        DrawGame(second, models, objects, {.map = 2, .random = &other, .ownLevel = 0x50, .sprites = &sprites});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4ec110"), std::string::npos) << cut.Error();
}

// Like Devias' 0x67 (Object104.bmd): two meshes of bone 0 and two actions of six keys.
class SwitchingModels final : public Client::Models::IModelLibrary
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
        Client::Models::SkeletonBone bone;
        bone.parent = -1;
        Client::Models::BoneKey key;
        key.rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
        const std::vector<Client::Models::BoneKey> keys(6, key);
        bone.actions = {keys, keys};
        m_skeleton.bones = {bone};
        const Client::Models::ActionInfo action{.repeats = true, .speed = 0.25f, .keyCount = 6, .lockPositions = false};
        m_skeleton.actions = {action, action};
    }

    [[nodiscard]] const Model* Find(Client::Models::ModelId) const override
    {
        return &m_model;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return &m_skeleton;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return m_textures;
    }

private:
    Model m_model;
    Skeleton m_skeleton;
    std::array<TextureSlot, 2> m_textures{TextureSlot{0x41c}, TextureSlot{0x41d}};
};

// 0x6031f7: Devias' 0x67 looks at the frame that 0x4eb860 left in its model when posing it (+0xa8, the short of the
// frame's ftol, 0 if it is not a key of the action) and, on the last key of its action, takes a rand(): & 31 at 0
// moves it to action 1 with 0x4ea030 (the old one and its key stay as previous, the frame at 0), and otherwise to
// action 0 (with 0 already set, nothing). With the frame on another key it takes nothing. Unposed (alpha below 0.01)
// or with the action outside the table, it cuts.
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
    // With seed 2, the second rand() is 29216 (& 31 at 0); with 7, 17422 (14).
    for (const Case& probe :
         {Case{2, 0, 5.5f, 2, 1, 0.0f}, Case{7, 0, 5.5f, 2, 0, 5.5f}, Case{2, 0, 4.99f, 1, 0, 4.99f},
          Case{2, 0, 6.0f, 1, 0, 6.0f}, Case{7, 1, 5.0f, 2, 0, 0.0f}, Case{2, 1, 5.0f, 2, 1, 5.0f}})
    {
        SCOPED_TRACE(std::to_string(probe.seed) + " " + std::to_string(probe.action) + " " +
                     std::to_string(probe.frame));
        std::vector<WorldObject> objects{ObjectAt(0x67, 200.0f, 100.0f, 2, Client::WorldObjects::ObjectScene::Other)};
        objects[0].animation.action = probe.action;
        objects[0].animation.frame = probe.frame;
        Client::Crt::CrtRandom random(probe.seed);
        ModelBatch batch;
        const auto drawn = DrawGame(batch, models, objects, {.map = 2, .random = &random});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        Client::Crt::CrtRandom expected(probe.seed);
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

    std::vector<WorldObject> unposed{ObjectAt(0x67, 200.0f, 100.0f, 2, Client::WorldObjects::ObjectScene::Other)};
    unposed[0].alpha = 0.005f;
    unposed[0].animation.frame = 5.0f;
    Client::Crt::CrtRandom random(7);
    ModelBatch batch;
    const auto cut = DrawGame(batch, models, unposed, {.map = 2, .random = &random});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x60320a"), std::string::npos) << cut.Error();
    EXPECT_EQ(random.State(), SevenAfter(1).State());

    std::vector<WorldObject> outside{ObjectAt(0x67, 200.0f, 100.0f, 2, Client::WorldObjects::ObjectScene::Other)};
    outside[0].animation.action = 2;
    Client::Crt::CrtRandom other(7);
    ModelBatch second;
    const auto beyond = DrawGame(second, models, outside, {.map = 2, .random = &other});
    ASSERT_FALSE(beyond.HasValue());
    EXPECT_NE(beyond.Error().find("0x603205"), std::string::npos) << beyond.Error();
}

// An intermediate MuRet draw (no original frame): draws and marks the seen ones, without the rand() of 0x602ed0 or
// its cases, and Devias' 0x64 reads the level without opening the record.
TEST(WorldObjectsDrawingTest, AnIntermediateGameDrawTakesNoRandomness)
{
    const TwoMeshModels models;
    std::vector<WorldObject> lorencia{
        ObjectAt(0x38, 100.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other)};
    Client::Crt::CrtRandom random(7);
    ModelBatch batch;
    ASSERT_TRUE(
        DrawGame(batch, models, lorencia, {.map = Lorencia, .originalFrame = false, .random = &random}).HasValue());
    EXPECT_TRUE(lorencia[0].seen);
    EXPECT_EQ(batch.Vertices().size(), 6u);

    std::vector<WorldObject> devias{ObjectAt(0x64, 100.0f, 100.0f, 2, Client::WorldObjects::ObjectScene::Other)};
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
    std::vector<WorldObject> objects{
        ObjectAt(Rock, 100.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other)};
    Client::Crt::CrtRandom random(7);
    ModelBatch batch;
    int publications = 0;
    Client::WorldObjects::GameObjectPass pass;
    pass.map = Lorencia;
    pass.originalFrame = false;
    pass.random = &random;
    pass.onPose = [&](const Client::Actors::Drawing::DrawnObject&) -> Client::Core::Expected<void, std::string>
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

// 0x5ffdf0 poses before 0x600328 discards hiddenMesh=-2: the scratch changes without geometry.
TEST(WorldObjectsDrawingTest, HiddenGameObjectsPublishNativeOrProjectedPoseWithoutGeometry)
{
    const TwoMeshModels models;
    for (const bool original : {false, true})
    {
        std::vector<WorldObject> objects{
            ObjectAt(Rock, 100.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other)};
        objects.front().state.hiddenMesh = Client::WorldObjects::HiddenObject;
        Client::Kernel::PoseBuffer native;
        native.bones.resize(1, Client::Math::BoneTransform(1.0f));
        native.bones.front()[3].x = 900.0f;
        auto projected = native;
        auto& target = original ? native : projected;
        Client::Crt::CrtRandom random(7);
        ModelBatch batch;
        Client::WorldObjects::GameObjectPass pass;
        pass.map = Lorencia;
        pass.originalFrame = original;
        pass.publishProjectedPose = !original;
        pass.random = &random;
        pass.retainedPose = &target;
        int publications = 0;
        pass.onPose = [&](const Client::Actors::Drawing::DrawnObject& pose) -> Client::Core::Expected<void, std::string>
        {
            EXPECT_EQ(pose.state.hiddenMesh, Client::WorldObjects::HiddenObject);
            ++publications;
            return Client::Actors::Drawing::PublishPose(target, models, pose, true);
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
        std::vector<WorldObject> objects{
            ObjectAt(Rock, 100.0f, 100.0f, Login, Client::WorldObjects::ObjectScene::Login)};
        objects.front().state.hiddenMesh = Client::WorldObjects::HiddenObject;
        Client::Kernel::PoseBuffer native;
        native.bones.resize(1, Client::Math::BoneTransform(1.0f));
        native.bones.front()[3].x = 900.0f;
        auto projected = native;
        auto& target = original ? native : projected;
        Client::Crt::CrtRandom random(7);
        ModelBatch batch;
        Client::WorldObjects::ObjectFrameEffects effects;
        effects.random = &random;
        effects.frames = original ? 1 : 0;
        effects.publishProjectedPose = !original;
        effects.retainedPose = &target;
        int publications = 0;
        effects.onPose =
            [&](const Client::Actors::Drawing::DrawnObject& pose) -> Client::Core::Expected<void, std::string>
        {
            ++publications;
            return Client::Actors::Drawing::PublishPose(target, models, pose, true);
        };
        DrawWorldObjects(batch, models, objects, Client::Terrain::TerrainPolygon{.everything = true}, nullptr, Login,
                         {}, 0.0f, effects);
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
    std::vector<WorldObject> objects{
        ObjectAt(Rock, 100.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other)};
    objects.front().alpha = 0.005f;
    objects.front().state.hiddenMesh = Client::WorldObjects::HiddenObject;
    int publications = 0;
    const auto observe = [&](const Client::Actors::Drawing::DrawnObject&) -> Client::Core::Expected<void, std::string>
    {
        ++publications;
        return {};
    };
    ModelBatch batch;
    Client::WorldObjects::GameObjectPass pass;
    pass.map = Lorencia;
    pass.originalFrame = false;
    pass.publishProjectedPose = true;
    pass.onPose = observe;
    ASSERT_TRUE(DrawGame(batch, models, objects, pass).HasValue());
    Client::WorldObjects::ObjectFrameEffects effects;
    effects.frames = 0;
    effects.publishProjectedPose = true;
    effects.onPose = observe;
    DrawWorldObjects(batch, models, objects, Client::Terrain::TerrainPolygon{.everything = true}, nullptr, Login, {},
                     0.0f, effects);
    EXPECT_EQ(publications, 0);
    EXPECT_TRUE(batch.Vertices().empty());
}

// The other maps cut before looking at an object: their cases of 0x6002e0 and 0x602ed0 are not read.
TEST(WorldObjectsDrawingTest, TheGamePassRejectsUnreadMaps)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 100.0f, 100.0f, 3, Client::WorldObjects::ObjectScene::Other)};
    Client::Crt::CrtRandom random(7);
    ModelBatch batch;
    const auto drawn = DrawGame(batch, models, objects, {.map = 35, .random = &random});
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x6046a0"), std::string::npos) << drawn.Error();
    EXPECT_FALSE(objects[0].seen);
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_EQ(random.State(), 7u);
}

// 0x606560 in the game: only the live ones that were seen, in draw order, up to the first one that cuts (map 0's
// lamp, after its light's rand()); the later one does not advance.
TEST(WorldObjectsDrawingTest, TheGameStepStopsAtTheFirstCaseMuRetDoesNotHave)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{
        ObjectAt(0x7a, 300.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other),
        ObjectAt(0x5a, 200.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other),
        ObjectAt(0x7a, 100.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other),
        ObjectAt(0x7a, 150.0f, 100.0f, Lorencia, Client::WorldObjects::ObjectScene::Other)};
    for (WorldObject& object : objects)
    {
        object.seen = true;
        object.alpha = 0.5f;
    }
    objects[2].alive = false;
    Client::Crt::CrtRandom random(7);
    const auto stepped = Client::WorldObjects::StepGameWorldObjects(objects, Lorencia, models, random,
                                                                    Client::WorldObjects::ObjectStepGlobals{});
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x605718"), std::string::npos) << stepped.Error();
    EXPECT_GT(objects[0].alpha, 0.5f);
    EXPECT_GT(objects[1].alpha, 0.5f);
    EXPECT_EQ(objects[2].alpha, 0.5f) << "no está vivo";
    EXPECT_EQ(objects[3].alpha, 0.5f) << "después del corte";
    EXPECT_EQ(random.State(), SevenAfter(2).State());
}

// Models of ten root bones, without animation, bone k at (100 · k, 10 · k, 0) by its type's base; type 2's with bone
// 8 dummy. The lantern (0x3c), one bone, like Object61.bmd.
class TenBoneModels final : public Client::Models::IModelLibrary
{
public:
    TenBoneModels()
    {
        const auto skeletonOf = [](std::size_t bones, float base, std::optional<std::size_t> dummy)
        {
            Skeleton skeleton;
            for (std::size_t k = 0; k < bones; ++k)
            {
                Client::Models::SkeletonBone bone;
                bone.parent = -1;
                if (dummy == k)
                {
                    bone.dummy = true;
                    skeleton.bones.push_back(bone);
                    continue;
                }
                Client::Models::BoneKey key;
                key.position = {base + (100.0f * static_cast<float>(k)), 10.0f * static_cast<float>(k), 0.0f};
                key.rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
                bone.actions = {{key}};
                skeleton.bones.push_back(bone);
            }
            skeleton.actions = {
                Client::Models::ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
            return skeleton;
        };
        m_horse = skeletonOf(10, 0.0f, std::nullopt);
        m_rock = skeletonOf(10, 5000.0f, 8);
        m_lantern = skeletonOf(1, 0.0f, std::nullopt);
    }

    [[nodiscard]] const Model* Find(Client::Models::ModelId) const override
    {
        return &m_model;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        if (model.Value() == Lantern)
        {
            return &m_lantern;
        }
        return model.Value() == SecondRock ? &m_rock : &m_horse;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId) const override
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
    std::vector<Client::Particles::Sprite> sprites;
    std::optional<glm::vec3> horse8;
    std::optional<glm::vec3> horse9;
    std::optional<glm::vec3> rock9;
};

// The horse posed before the objects (0x4453b0) at (1000; 2000; 300) and `objects` drawn in order, with the lantern
// last.
LanternGlow GlowOf(std::vector<WorldObject>& objects, Client::WorldObjects::ObjectScene scene, std::uint32_t seed)
{
    const TenBoneModels models;
    Client::Actors::Drawing::DrawnObject horse;
    horse.model = 1;
    horse.transform.position = {1000.0f, 2000.0f, 300.0f};
    horse.transform.scale = 1.5f;
    const std::array<Client::Actors::Drawing::PosedInTheBuffer, 1> before{{{&models, horse}}};
    Client::Crt::CrtRandom random(seed);
    Client::Particles::FrameSprites sprites;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawWorldObjects(batch, models, objects, Everything, nullptr, Login, {}, 0.0f,
                     Client::WorldObjects::ObjectFrameEffects{
                         .random = &random, .sprites = &sprites, .posedBefore = before, .scene = scene});
    LanternGlow glow;
    for (const Client::Particles::Sprite& sprite : sprites.Sprites())
    {
        if (sprite.alive)
        {
            glow.sprites.push_back(sprite);
        }
    }
    glow.horse8 = Client::Actors::Drawing::PosedBonePoint(models, horse, 8, {});
    glow.horse9 = Client::Actors::Drawing::PosedBonePoint(models, horse, 9, {});
    Client::Actors::Drawing::DrawnObject rock;
    rock.model = TenBoneModels::SecondRock;
    rock.transform = objects.front().transform;
    glow.rock9 = Client::Actors::Drawing::PosedBonePoint(models, rock, 9, {});
    return glow;
}

TEST(WorldObjectsDrawingTest, AZeroFrameDrawReplaysTheLanternWithoutConsumingRandomness)
{
    const TenBoneModels models;
    std::array objects{ObjectAt(TenBoneModels::Lantern, 300.0f, 300.0f)};
    Client::Particles::FrameSprites sprites;
    Client::Crt::CrtRandom random(4);
    std::array<std::optional<int>, 1> lastRandoms;
    Client::Actors::Drawing::DrawnObject horse;
    horse.model = 1;
    horse.transform.position = {1000.0f, 2000.0f, 300.0f};
    std::array<Client::Actors::Drawing::PosedInTheBuffer, 1> before{{{&models, horse}}};
    ModelBatch batch;
    const auto draw = [&](int frames)
    {
        batch.Begin(Client::Render::Pass3D{});
        DrawWorldObjects(batch, models, objects, Everything, nullptr, Login, {}, 0.0f,
                         Client::WorldObjects::ObjectFrameEffects{.random = &random,
                                                                  .sprites = &sprites,
                                                                  .posedBefore = before,
                                                                  .scene = Client::WorldObjects::ObjectScene::Login,
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
    Client::Particles::FrameSprites sprites;
    Client::Crt::CrtRandom random(4);
    std::array<std::optional<int>, 1> lastRandoms;
    const auto outside =
        Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    ModelBatch batch;
    const auto draw = [&](int frames, const Client::Terrain::TerrainPolygon& polygon)
    {
        sprites.Clear();
        batch.Begin(Client::Render::Pass3D{});
        DrawWorldObjects(batch, models, objects, polygon, nullptr, Login, {}, 0.0f,
                         Client::WorldObjects::ObjectFrameEffects{.random = &random,
                                                                  .sprites = &sprites,
                                                                  .scene = Client::WorldObjects::ObjectScene::Login,
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

// 0x602ed0 in scene 2 with the lantern (0x3c, 0x602f21): two flare01 sprites (0x5ac), scale 1, additive and
// unrotated, at the translations of bones 8 and 9 of the global pose buffer (0x68b199c and 0x68b19cc), with the light
// of its object's rand(). The lantern has one bone: both are those of the last posed model that has them non-dummy.
// With no other object before, the horse's.
TEST(WorldObjectsDrawingTest, TheLanternGlowsAtTheBonesThatTheHorseLeft)
{
    std::vector<WorldObject> objects{ObjectAt(TenBoneModels::Lantern, 300.0f, 300.0f)};
    const LanternGlow glow = GlowOf(objects, Client::WorldObjects::ObjectScene::Login, 11);
    ASSERT_EQ(glow.sprites.size(), 2u);
    Client::Crt::CrtRandom random(11);
    const glm::vec3 light = Client::WorldObjects::StreetLightGlowLight(random.Next());
    ASSERT_TRUE(glow.horse8.has_value());
    ASSERT_TRUE(glow.horse9.has_value());
    EXPECT_EQ(glow.sprites[0].position, *glow.horse8);
    EXPECT_EQ(glow.sprites[1].position, *glow.horse9);
    for (const Client::Particles::Sprite& sprite : glow.sprites)
    {
        EXPECT_EQ(sprite.texture, 0x5ac);
        EXPECT_EQ(sprite.scale, 1.0f);
        EXPECT_EQ(sprite.blend, 0);
        EXPECT_EQ(sprite.rotation, 0.0f);
        EXPECT_EQ(sprite.light, light);
    }
}

// A ten-bone object drawn before the lantern overwrites bone 9; its 8 is dummy (0x4eb973 does not write it) and the
// horse's stays. With the alpha below 0.01, 0x5ffdf0 exits before posing it and both of the horse's remain; exactly at
// 0.01, it poses it. With +0x58 at −2 it poses it (0x6002e0 is the one that does not draw it).
TEST(WorldObjectsDrawingTest, TheLanternGlowsAtTheBonesOfTheLastPosedModel)
{
    std::vector<WorldObject> objects{ObjectAt(static_cast<std::int16_t>(TenBoneModels::SecondRock), 100.0f, 100.0f),
                                     ObjectAt(TenBoneModels::Lantern, 300.0f, 300.0f)};
    const LanternGlow posed = GlowOf(objects, Client::WorldObjects::ObjectScene::Login, 3);
    ASSERT_EQ(posed.sprites.size(), 2u);
    EXPECT_EQ(posed.sprites[0].position, *posed.horse8);
    ASSERT_TRUE(posed.rock9.has_value());
    EXPECT_EQ(posed.sprites[1].position, *posed.rock9);

    objects[0].state.hiddenMesh = Client::WorldObjects::HiddenObject;
    const LanternGlow hidden = GlowOf(objects, Client::WorldObjects::ObjectScene::Login, 3);
    ASSERT_EQ(hidden.sprites.size(), 2u);
    EXPECT_EQ(hidden.sprites[1].position, *hidden.rock9) << "posado aunque no se dibuje";

    objects[0].state.hiddenMesh = Client::Models::NoHiddenMesh;
    objects[0].alpha = 0.0f;
    const LanternGlow faded = GlowOf(objects, Client::WorldObjects::ObjectScene::Login, 3);
    ASSERT_EQ(faded.sprites.size(), 2u);
    EXPECT_EQ(faded.sprites[1].position, *faded.horse9);

    // `fcomp` with 0.01f and `test ah, 1`: with the alpha exactly at 0.01 it does not exit.
    objects[0].alpha = std::bit_cast<float>(0x3C23D70Au);
    const LanternGlow edge = GlowOf(objects, Client::WorldObjects::ObjectScene::Login, 3);
    ASSERT_EQ(edge.sprites.size(), 2u);
    EXPECT_EQ(edge.sprites[1].position, *edge.rock9) << "posado con el alpha en 0,01";
}

// The lantern case belongs to scene 2 (0x602f09): in another one, the rand() is still taken but there are no sprites.
TEST(WorldObjectsDrawingTest, TheLanternGlowsOnlyInTheLogin)
{
    std::vector<WorldObject> objects{ObjectAt(TenBoneModels::Lantern, 300.0f, 300.0f)};
    EXPECT_TRUE(GlowOf(objects, Client::WorldObjects::ObjectScene::CharacterSelect, 11).sprites.empty());
}

// The two red whirlpools (86) of the .obj are at the same place, with the same turn and additive from their first
// step: they add up twice, both meshes of each one.
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

// Whirlpool 90: before its first step it is not drawn (without the blendMesh, Q-C135); afterwards, only its mesh 0,
// subtractive and white.
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

// 0x6046a0 stores each object's test result in +0x16c, and 0x606560 clears those of a sector that did not pass: one
// that leaves the view stops advancing.
TEST(WorldObjectsDrawingTest, AnObjectThatLeavesTheViewStopsStepping)
{
    const TwoMeshModels models;
    std::vector<WorldObject> objects{ObjectAt(Rock, 100.0f, 100.0f)};
    objects[0].alpha = 0.0f;
    ModelBatch batch;
    Draw(batch, models, objects);
    ASSERT_TRUE(objects[0].seen);
    const auto away =
        Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    Draw(batch, models, objects, away);
    EXPECT_FALSE(objects[0].seen);
    StepWorldObjects(objects, Login, models);
    EXPECT_EQ(objects[0].alpha, 0.0f) << "no avanzó";
}

// 0x606560 walks the sectors like the draw (x outside, y inside) and, in each one, the order in which they were
// created: the order matters when the steps take rand().
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

// A live model effect like the dragon, at (1000; 1000; 3400), with its previous step 70 further back in y.
Client::Effects::ModelEffect LiveEffect()
{
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::Effects::DragonEffect;
    effect.transform.position = {1000.0f, 1000.0f, 3400.0f};
    effect.previousPosition = {1000.0f, 1070.0f, 3400.0f};
    effect.light = glm::vec3(1.0f);
    return effect;
}

// 0x52d620 → 0x5ffce0 with per-vertex light: the color is the terrain light at its place plus the effect's, and each
// corner goes by its normal's light.
TEST(ModelEffectsDrawingTest, TheDragonTakesTheTerrainLightPlusItsOwn)
{
    const TwoMeshModels models;
    auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    lighting->cells.fill({0.25f, 0.5f, 0.75f});
    std::array<Client::Effects::ModelEffect, 1> effects{LiveEffect()};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::WorldObjects::DrawModelEffects(batch, models, effects, Client::Terrain::SphereFrustum{}, lighting.get(), {},
                                           0.0f);
    ASSERT_EQ(batch.Vertices().size(), 6u);
    const float light = Client::Models::NormalLight(glm::vec3(0.0f, 0.0f, 1.0f));
    const auto& color = batch.Vertices()[0].color;
    EXPECT_FLOAT_EQ(color.red, 1.25f * light);
    EXPECT_FLOAT_EQ(color.green, 1.5f * light);
    EXPECT_FLOAT_EQ(color.blue, 1.75f * light);
    EXPECT_NE(light, 1.0f) << "la normal tiene que cambiar el color para que el test mire la luz por vértice";
}

TEST(ModelEffectsDrawingTest, CullingUsesTheLogicalPositionAndRetainsDeadSlotHistory)
{
    const TwoMeshModels models;
    std::array<Client::Effects::ModelEffect, 3> effects{LiveEffect(), LiveEffect(), LiveEffect()};
    Client::Terrain::SphereFrustum frustum;
    frustum.normals[0] = {1.0f, 0.0f, 0.0f};
    effects[0].seen = true;
    effects[0].transform.position.x = -401.0f;
    effects[0].previousPosition.x = -1000.0f;
    // The extrapolation would come on screen; the culling uses +0x10, not that drawn position.
    effects[1].seen = false;
    effects[1].transform.position.x = 0.0f;
    effects[1].previousPosition.x = 1000.0f;
    effects[2].alive = false;
    effects[2].seen = true;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::WorldObjects::DrawModelEffects(batch, models, effects, frustum, nullptr, {}, 1.0f);
    EXPECT_FALSE(effects[0].seen);
    EXPECT_TRUE(effects[1].seen);
    EXPECT_TRUE(effects[2].seen);
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_EQ(batch.Vertices()[0].position.x, -1000.0f);
}

struct MissingEffectModels final : Client::Models::IModelLibrary
{
    mutable int lookups = 0;
    const Model* Find(Client::Models::ModelId) const override
    {
        ++lookups;
        return nullptr;
    }
    const Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        ++lookups;
        return nullptr;
    }
    std::span<const TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        ++lookups;
        return {};
    }
};

TEST(ModelEffectsDrawingTest, VisibilityIsPublishedBeforeLookingForTheModel)
{
    const MissingEffectModels models;
    std::array<Client::Effects::ModelEffect, 1> effects{LiveEffect()};
    Client::Terrain::SphereFrustum frustum;
    frustum.normals[0] = {1.0f, 0.0f, 0.0f};
    effects[0].seen = true;
    effects[0].transform.position.x = -401.0f;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::WorldObjects::DrawModelEffects(batch, models, effects, frustum, nullptr, {}, 0.0f);
    EXPECT_FALSE(effects[0].seen);
    EXPECT_EQ(models.lookups, 0);
    effects[0].transform.position.x = 0.0f;
    Client::WorldObjects::DrawModelEffects(batch, models, effects, frustum, nullptr, {}, 0.0f);
    EXPECT_TRUE(effects[0].seen);
    EXPECT_GT(models.lookups, 0);
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(ModelEffectsDrawingTest, OnlyLiveEffectsStep)
{
    const TwoMeshModels models;
    std::array<Client::Effects::ModelEffect, 2> effects{LiveEffect(), LiveEffect()};
    effects[1].alive = false;
    effects[0].life = 100;
    effects[1].life = 100;
    Client::WorldObjects::StepModelEffects(effects, models);
    EXPECT_EQ(effects[0].life, 99);
    EXPECT_EQ(effects[1].life, 100);
}

// Outside the faithful mode, the draw goes between two steps (D-C4): the position, `alpha` one step further, with what
// it moved in the last one; not the frame, which is whole.
TEST(ModelEffectsDrawingTest, TheDragonIsDrawnBetweenTwoSteps)
{
    const TwoMeshModels models;
    std::array<Client::Effects::ModelEffect, 1> effects{LiveEffect()};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::WorldObjects::DrawModelEffects(batch, models, effects, Client::Terrain::SphereFrustum{}, nullptr, {}, 0.5f);
    ASSERT_FALSE(batch.Vertices().empty());
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.y, 1000.0f - 35.0f);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 1000.0f);
}

// A library with the size of the sprites' texture (0x5ac, 64 × 32) and the breath's (0x5f2): both are JPG, 3
// components, and bind their selector's slot, like those of EffectTextureSet in GameModels.
class SpriteTextures : public Client::Models::IModelLibrary
{
public:
    [[nodiscard]] const Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId) const override
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

// Synthetic metadata: forces the color boundary after Begin, without assets or GL.
class InterruptedEffectTextures final : public SpriteTextures
{
public:
    explicit InterruptedEffectTextures(const ModelBatch& batch) : m_batch(batch) {}
    bool interrupt = true;
    std::optional<TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override
    {
        if (interrupt && m_batch.HasOpenPrimitive() && ++m_colorQueries == 2)
            return std::nullopt;
        if (selector == Client::Particles::TorchFlameParticle)
            selector = Client::Particles::BreathParticle;
        return SpriteTextures::TextureMetadataOf(selector);
    }

private:
    const ModelBatch& m_batch;
    mutable std::size_t m_colorQueries = 0;
};

class RgbaSpriteTextures final : public SpriteTextures
{
public:
    std::optional<TextureMetadata> TextureMetadataOf(std::uint32_t) const override
    {
        TextureMetadata metadata(Client::Data::Textures::Texture{});
        metadata.size = glm::vec2(64.0f, 32.0f);
        metadata.components = 4;
        return metadata;
    }
};

TEST(SpritesDrawingTest, RgbaSpritesUseLightRedAsBillboardAlpha)
{
    const RgbaSpriteTextures textures;
    for (const std::uint16_t selector :
         {std::uint16_t{0x5ac}, Client::Particles::AtlasSprite, Client::Render::OpaqueAlphaBillboardSlot})
    {
        Client::Particles::FrameSprites sprites;
        ASSERT_TRUE(sprites.Create(selector, {0, 0, -10}, 1, {0.25f, 0.5f, 0.75f}, 0, 0));
        sprites.MarkSeen();
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        Client::WorldObjects::DrawFrameSprites(batch, sprites, textures, Client::Render::ViewRows(glm::mat4(1)));
        ASSERT_EQ(batch.Vertices().size(), 6u);
        for (const auto& vertex : batch.Vertices())
        {
            EXPECT_FLOAT_EQ(vertex.color.red, 0.25f);
            EXPECT_FLOAT_EQ(vertex.color.green, 0.5f);
            EXPECT_FLOAT_EQ(vertex.color.blue, 0.75f);
            EXPECT_FLOAT_EQ(vertex.color.alpha, selector == Client::Render::OpaqueAlphaBillboardSlot ? 1.0f : 0.25f);
        }
    }
}

TEST(ParticlesDrawingTest, InterruptedQuadPreservesCompletedGeometryAndAllowsNextPass)
{
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    InterruptedEffectTextures textures(batch);
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    for (int i = 0; i < 2; ++i)
        ASSERT_TRUE(
            particles
                .Create(Client::Particles::BreathParticle, {0, 0, -300}, glm::vec3(0), glm::vec3(1), 0, 1, 0, random)
                .has_value());
    const auto view = Client::Render::ViewRows(glm::mat4(1));
    const auto failed = Client::WorldObjects::DrawParticles(batch, particles, textures, view);
    ASSERT_FALSE(failed.HasValue());
    EXPECT_EQ(failed.Error().slot, 1u);
    EXPECT_EQ(failed.Error().error.reason, Client::Particles::ParticleError::MissingColorFlag);
    EXPECT_EQ(failed.Error().error.phase, Client::Particles::ParticleDrawingPhase::ColorFlag);
    EXPECT_FALSE(failed.Error().error.backendError);
    EXPECT_FALSE(batch.HasOpenPrimitive());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 1.0f);
    std::size_t completed = 0;
    for (const auto& command : batch.Commands())
        completed += command.vertexCount;
    EXPECT_EQ(completed, 6u);
    textures.interrupt = false;
    ASSERT_TRUE(Client::WorldObjects::DrawParticles(batch, particles, textures, view).HasValue());
    EXPECT_EQ(batch.Vertices().size(), 18u);
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Client::WorldObjects::DrawParticles(batch, particles, textures, view).HasValue());
    EXPECT_EQ(batch.Vertices().size(), 12u);
}

TEST(ParticlesDrawingTest, AnInterruptedTextureOverrideDoesNotAffectTheNextProducer)
{
    using Client::Render::TextureEnvMode;
    for (const bool temporaryOverride : {false, true})
    {
        SCOPED_TRACE(temporaryOverride);
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        const auto expected = temporaryOverride ? TextureEnvMode::Modulate : TextureEnvMode::Add;
        batch.SetTextureEnv(expected);
        InterruptedEffectTextures textures(batch);
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(1);
        for (int i = 0; i < 2; ++i)
            ASSERT_TRUE(particles
                            .Create(Client::Particles::BreathParticle, {0, 0, -300}, glm::vec3(0), glm::vec3(1), 0, 1,
                                    0, random)
                            .has_value());
        if (temporaryOverride)
        {
            auto& second = particles.All()[1];
            second.type = Client::Particles::TorchFlameParticle;
            second.texture = Client::Particles::TorchFlameParticle;
            second.subtype = Client::Particles::BrightFlameSubtype;
        }
        const auto result =
            Client::WorldObjects::DrawParticles(batch, particles, textures, Client::Render::ViewRows(glm::mat4(1)));
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error().slot, 1u);
        EXPECT_EQ(result.Error().error.phase, Client::Particles::ParticleDrawingPhase::ColorFlag);
        EXPECT_FALSE(batch.HasOpenPrimitive());
        ASSERT_EQ(batch.Vertices().size(), 6u);
        EXPECT_EQ(batch.TextureEnv(), expected);
        batch.Triangle({batch.Vertices()[0], batch.Vertices()[1], batch.Vertices()[2]});
        ASSERT_EQ(batch.Vertices().size(), 9u);
        EXPECT_EQ(batch.Commands().back().textureEnv, expected);
    }
}

// 0x549440(0): each live sprite is drawn once, additive, as the square of 0x621400 with the texture times the scale
// (0x549290), and is turned off.
TEST(SpritesDrawingTest, ASpriteIsDrawnOnceAndPutOut)
{
    const SpriteTextures textures;
    Client::Particles::FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x5ac, {10.0f, 20.0f, -300.0f}, 2.0f, {1.0f, 0.3f, 0.0f}, 0.0f, 0));
    sprites.MarkSeen();
    const auto identity = Client::Render::ViewRows(glm::mat4(1.0f));
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::WorldObjects::DrawFrameSprites(batch, sprites, textures, identity);
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
    EXPECT_EQ(batch.Commands()[0].state.texture, TextureSlot{0x5ac});
    ASSERT_EQ(batch.Vertices().size(), 6u);
    // 64 × 2 wide and 32 × 2 high, centered at (10, 20).
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
    Client::WorldObjects::DrawFrameSprites(batch, sprites, textures, identity);
    EXPECT_TRUE(batch.Vertices().empty());
}

// Each sprite's blend comes from its +4 (0x549440): 1 0x620b10, 2 the normal one; texture 0x637 always the normal one.
TEST(SpritesDrawingTest, TheBlendComesFromTheSprite)
{
    const SpriteTextures textures;
    Client::Particles::FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x5ac, glm::vec3(0.0f, 0.0f, -10.0f), 1.0f, glm::vec3(1.0f), 0.0f, 1));
    ASSERT_TRUE(sprites.Create(0x5ac, glm::vec3(0.0f, 0.0f, -10.0f), 1.0f, glm::vec3(1.0f), 0.0f, 2));
    sprites.MarkSeen();
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::WorldObjects::DrawFrameSprites(batch, sprites, textures, Client::Render::ViewRows(glm::mat4(1.0f)));
    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Subtractive);
    EXPECT_EQ(batch.Commands()[1].state.blend, ModelBlend::Alpha);
}

// Without 0x549510 (+0x16c at 0), 0x549290 subtracts 0.1 from the alpha before measuring it: the square comes out at
// 0.9 of the texture times the scale, and the billboard color does not carry the alpha.
TEST(SpritesDrawingTest, AnUnseenSpriteShrinksWithItsAlpha)
{
    const SpriteTextures textures;
    Client::Particles::FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x5ac, {0.0f, 0.0f, -300.0f}, 2.0f, glm::vec3(1.0f), 0.0f, 0));
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::WorldObjects::DrawFrameSprites(batch, sprites, textures, Client::Render::ViewRows(glm::mat4(1.0f)));
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

// 0x548550(0): each live breath, additive (smoke01 has 3 components: 0x620a90), the size of its texture times the
// scale, without the sprites' alpha and without turning it off; the step turns it off when its life runs out. The
// material and the binder (0x620780) go to the batch as effects without geometry before the quad, in eye coordinates.
TEST(ParticlesDrawingTest, ABreathIsDrawnWithItsTextureTimesItsScale)
{
    const SpriteTextures textures;
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    const auto index = particles.Create(Client::Particles::BreathParticle, {0.0f, 0.0f, -300.0f}, glm::vec3(0.0f),
                                        glm::vec3(3.0f), 0, 1.0f, 0.0f, random);
    ASSERT_TRUE(index.has_value());
    const float scale = particles.All()[*index].scale;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(
        Client::WorldObjects::DrawParticles(batch, particles, textures, Client::Render::ViewRows(glm::mat4(1.0f)))
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
    // With the clock rotation at 0, the height is the texture's times the scale.
    EXPECT_FLOAT_EQ(maxY - minY, 32.0f * scale);
    EXPECT_TRUE(particles.All()[*index].alive);
}

class NoEffectTextures final : public Client::Models::IModelLibrary
{
public:
    [[nodiscard]] const Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
};

// Without its texture's size (a library that did not upload it), the pass cuts at that place with the error, at the
// first boundary (the resource's +20/+24), before any service: it is not skipped silently.
TEST(ParticlesDrawingTest, ABreathWithoutItsTextureCutsThePassWithItsError)
{
    const NoEffectTextures textures;
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    const auto index = particles.Create(Client::Particles::BreathParticle, {0.0f, 0.0f, -300.0f}, glm::vec3(0.0f),
                                        glm::vec3(3.0f), 0, 1.0f, 0.0f, random);
    ASSERT_TRUE(index.has_value());
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto drawn =
        Client::WorldObjects::DrawParticles(batch, particles, textures, Client::Render::ViewRows(glm::mat4(1.0f)));
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_EQ(drawn.Error().slot, *index);
    EXPECT_EQ(drawn.Error().error.reason, Client::Particles::ParticleError::MissingTextureSize);
    EXPECT_EQ(drawn.Error().error.phase, Client::Particles::ParticleDrawingPhase::Dimensions);
    EXPECT_TRUE(batch.Commands().empty());
}

// 0x4255f3 and 0x42588d (0x425260 from 0x602ed0): Aida's 0x29 and 0x41/0x42 repeat their mesh 0 with one 0x4ecc30
// after the body, without per-vertex light and with the U from the clock (−ftol(T) % 100000 · step) and the object's
// V. The 0x29 adds (flags 0x42) at 0.52 and step 0.00001f; the 0x41/0x42 goes with the base pass (2), the color the
// body left it and step 0.00005f. They also draw in an intermediate MuRet draw, without rand().
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
        std::vector<WorldObject> objects{
            ObjectAt(probe.type, 100.0f, 100.0f, Aida, Client::WorldObjects::ObjectScene::Other)};
        objects[0].light = glm::vec3(0.25f, 0.5f, 0.75f);
        objects[0].vertexLight = false;
        objects[0].state.uv = glm::vec2(0.5f, 0.125f);
        for (const bool original : {false, true})
        {
            Client::Crt::CrtRandom random(7);
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
    // Other types of the 33 repeat nothing.
    std::vector<WorldObject> objects{ObjectAt(0x10, 100.0f, 100.0f, Aida, Client::WorldObjects::ObjectScene::Other)};
    Client::Crt::CrtRandom random(7);
    ModelBatch batch;
    ASSERT_TRUE(DrawGame(batch, models, objects, {.map = Aida, .originalFrame = false, .random = &random}).HasValue());
    EXPECT_EQ(batch.Commands().size(), 2u);
}

// 0x4403b0 (the chain of 0x6002e0 on the 31): the 0x1b, the 0x36 and the 0x34 multiply the model color by their
// phase's pulse; the 0x34 is drawn twice with that color (Q-C317); the 0xa goes at (0.56; 0.8; 0.81). No rand() or
// state: the same in an intermediate draw and in an original one.
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
            ObjectAt(probe.type, 100.0f, 100.0f, Trials, Client::WorldObjects::ObjectScene::Other)};
        objects[0].light = light;
        objects[0].vertexLight = false;
        objects[0].field84 = 2.5f;
        const auto body = Client::MapScripts::LandOfTrials::LandOfTrialsBody(objects[0], light, clock);
        ASSERT_TRUE(body.HasValue());
        const glm::vec3 color = body.Value() ? body.Value()->color : light;
        for (const bool original : {false, true})
        {
            Client::Crt::CrtRandom random(7);
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
