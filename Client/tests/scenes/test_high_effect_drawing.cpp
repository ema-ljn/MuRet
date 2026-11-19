#include "Scenes/HighEffectDrawing.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HighModelEffects.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/SphereFrustum.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Los casos propios de 0x6002e0 (y de 0x438fe0) de los tipos altos: el 0x11b con sus huesos, sus sprites y sus dos
// rand() por pase; el 0x114/3..5 con la textura 0x6ef; el 0x140 cromado; el 0x125 con sus sprites. 06, "Los efectos
// con modelo de los tipos altos".
namespace
{
using Client::Render::ModelBatch;
using Client::Render::Models::BoneTransform;
using Client::Render::Models::CrtRandom;
using Client::Scenes::DrawnObject;
using Client::World::ModelEffect;

constexpr std::uint32_t Seed = 0x2468ace1u;
constexpr glm::vec3 SecondBonePoint{100.0f, 200.0f, 300.0f};
constexpr glm::vec3 ThirdBonePoint{400.0f, 500.0f, 600.0f};
constexpr glm::vec3 FourthBonePoint{-70.0f, 80.0f, 90.0f};
constexpr Client::Render::TextureSlot OwnTexture{0x400};

// Un triángulo con su textura y un hueso.
class TriangleModels final : public Client::Scenes::IModelLibrary
{
public:
    TriangleModels()
    {
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "plain.jpg";
        model.meshes = {mesh};
        Client::Render::Models::SkeletonBone bone;
        bone.actions = {{Client::Render::Models::BoneKey{}}};
        skeleton.bones = {bone};
        skeleton.actions = {Client::Render::Models::ActionInfo{.keyCount = 1}};
    }
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return &model;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return textures;
    }
    Client::Data::Models::Model model;
    Client::Render::Models::Skeleton skeleton;
    std::array<Client::Render::TextureSlot, 1> textures{OwnTexture};
};

BoneTransform Translation(const glm::vec3& point)
{
    BoneTransform bone(1.0f);
    bone[3] = point;
    return bone;
}

// Cuántos rand() sacó `random` desde la semilla.
int DrawsSince(const CrtRandom& random)
{
    CrtRandom probe(Seed);
    for (int draws = 0; draws < 1000; ++draws)
    {
        if (probe.State() == random.State())
            return draws;
        static_cast<void>(probe.Next());
    }
    return -1;
}

// Lo que la pose de 0x5ffdf0 deja en el buffer global: el hueso 2, el 3 y el 4 en sus puntos del mundo.
struct Drawer
{
    Client::Core::Expected<void, std::string> Draw(std::span<ModelEffect> effects, std::uint8_t mode)
    {
        Client::Scenes::ModelEffectDrawResources resources{
            .heights = *heights,
            .worldClock = clock,
            .onPose = [this](const DrawnObject& object) -> Client::Core::Expected<void, std::string>
            {
                bones = {BoneTransform(0.0f), BoneTransform(0.0f), Translation(SecondBonePoint),
                         Translation(ThirdBonePoint), Translation(FourthBonePoint)};
                meshes = Client::Scenes::SkinObjectMeshes(models, object, std::vector(1, BoneTransform(1.0f)));
                ++poses;
                return {};
            },
            .posedMeshes = [this]() -> std::span<const Client::Render::Models::SkinnedMesh> { return meshes; },
            .sprites = &sprites,
            .random = withRandom ? &random : nullptr,
            .kalimaPass = pass.get(),
            .heroByte1a1 = heroByte1a1,
            .originalFrame = originalFrame,
            .posedBones = [this]() -> std::span<const BoneTransform> { return bones; }};
        return Client::Scenes::DrawGameModelEffects(batch, models, effects, Client::World::SphereFrustum{}, nullptr, {},
                                                    0, resources, mode);
    }

    [[nodiscard]] std::vector<Client::World::Sprite> Alive()
    {
        std::vector<Client::World::Sprite> alive;
        for (const Client::World::Sprite& sprite : sprites.Sprites())
            if (sprite.alive)
                alive.push_back(sprite);
        return alive;
    }

    TriangleModels models;
    std::unique_ptr<Client::Data::Terrain::TerrainHeights> heights =
        std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::World::FrameSprites sprites;
    CrtRandom random{Seed};
    bool withRandom = true;
    float clock = 1000.0f;
    // El pase de los objetos (su mapa, 0x6c9dd0), el +0x1a1 del héroe y si el dibujo es de un frame del original: lo
    // que 0x5ffdf0 mira para la resta de Chaos Castle.
    std::unique_ptr<Client::Scenes::GameObjectPass> pass;
    std::uint8_t heroByte1a1 = 0;
    bool originalFrame = false;
    std::vector<BoneTransform> bones;
    std::vector<Client::Render::Models::SkinnedMesh> meshes;
    int poses = 0;
    ModelBatch batch;
};

ModelEffect EffectOf(std::int16_t type, std::int32_t subtype, std::int16_t blendMesh = -1)
{
    ModelEffect effect;
    effect.alive = true;
    effect.type = type;
    effect.subtype = subtype;
    effect.life = 10;
    effect.light = {0.25f, 0.25f, 0.25f};
    effect.vertexLight = false;
    effect.objectState.blendMesh = blendMesh;
    effect.transform.position = {1000.0f, 2000.0f, 300.0f};
    return effect;
}

struct Drawn
{
    Client::Render::ModelBlend blend;
    std::optional<Client::Render::TextureSlot> texture;
};

std::vector<Drawn> DrawCommands(const ModelBatch& batch)
{
    std::vector<Drawn> drawn;
    for (const Client::Render::ModelDrawCommand& command : batch.Commands())
        if (command.operation == Client::Render::ModelStateOperation::Draw && command.vertexCount != 0)
            drawn.push_back({command.state.blend, command.state.texture});
    return drawn;
}

void ExpectSprite(const Client::World::Sprite& sprite, std::uint16_t texture, const glm::vec3& position, float scale,
                  const glm::vec3& light, float rotation)
{
    EXPECT_EQ(sprite.texture, texture);
    EXPECT_EQ(sprite.position, position);
    EXPECT_EQ(sprite.scale, scale);
    EXPECT_EQ(sprite.light, light);
    EXPECT_EQ(sprite.rotation, rotation);
    EXPECT_EQ(sprite.blend, 0);
}
} // namespace

// 0x601e68..0x601faf: el 0x11b/1 (blendMesh −2) pasa el filtro del modo 1 de 0x52d620 (0x52d673): en cada pase, +0x40 y
// +0x4c en los huesos 2 y 3 (0x4ec110 sin trasladar), los dos 0x5ac de luz (1, 0, 0) y escala 1 y los dos 0x5fd de luz
// 0,5 y escala 0,5f, cada uno con su rand() % 360 de giro: dos rand() por pase y cuatro por frame. Con la semilla, los
// giros son 211 y 205 en el modo 0 y 2 y 89 en el 1 (calculados aparte con el LCG del CRT).
TEST(HighEffectDrawingTest, TheRingTakesTwoRandomsInEachPassAndLeavesItsPointsOnItsBones)
{
    Drawer drawer;
    ModelEffect ring = EffectOf(0x11b, 1, -2);
    ring.point40 = {1.0f, 2.0f, 3.0f};
    ring.point4c = {4.0f, 5.0f, 6.0f};
    for (const std::uint8_t mode : {std::uint8_t{0}, std::uint8_t{1}})
    {
        SCOPED_TRACE(mode);
        const auto drawn = drawer.Draw(std::span(&ring, 1), mode);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(DrawsSince(drawer.random), mode == 0 ? 2 : 4);
        EXPECT_EQ(ring.point40, SecondBonePoint);
        EXPECT_EQ(ring.point4c, ThirdBonePoint);
        const auto alive = drawer.Alive();
        ASSERT_EQ(alive.size(), mode == 0 ? 4u : 8u);
        const std::size_t first = mode == 0 ? 0 : 4;
        ExpectSprite(alive[first], 0x5ac, SecondBonePoint, 1.0f, {1.0f, 0.0f, 0.0f}, 0.0f);
        ExpectSprite(alive[first + 1], 0x5ac, ThirdBonePoint, 1.0f, {1.0f, 0.0f, 0.0f}, 0.0f);
        ExpectSprite(alive[first + 2], 0x5fd, SecondBonePoint, 0.5f, glm::vec3(0.5f), mode == 0 ? 211.0f : 2.0f);
        ExpectSprite(alive[first + 3], 0x5fd, ThirdBonePoint, 0.5f, glm::vec3(0.5f), mode == 0 ? 205.0f : 89.0f);
    }
    EXPECT_EQ(drawer.poses, 2);
}

// 0x602e90 → 0x5ffdf0 (0x5ffe2a..0x5ffe3d): con el alpha por debajo de 0,01f (0x6b88d4), o sin orden, devuelve 0 sin
// posar y 0x602eb0 salta 0x6002e0: el 0x11b no saca sus rand(), ni deja sprites, ni mueve +0x40 y +0x4c, en ningún
// pase.
TEST(HighEffectDrawingTest, TheRingBelowTheVisibleAlphaTakesNoRandom)
{
    const float justBelow = std::bit_cast<float>(0x3c23d709u); // el float anterior a 0,01f (0x3c23d70a)
    for (const float alpha : {0.0f, justBelow, std::numeric_limits<float>::quiet_NaN()})
    {
        SCOPED_TRACE(alpha);
        Drawer drawer;
        ModelEffect ring = EffectOf(0x11b, 1, -2);
        ring.alpha = alpha;
        ring.point40 = {1.0f, 2.0f, 3.0f};
        ring.point4c = {4.0f, 5.0f, 6.0f};
        for (const std::uint8_t mode : {std::uint8_t{0}, std::uint8_t{1}})
        {
            SCOPED_TRACE(mode);
            const auto drawn = drawer.Draw(std::span(&ring, 1), mode);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        }
        EXPECT_EQ(DrawsSince(drawer.random), 0);
        EXPECT_TRUE(drawer.Alive().empty());
        EXPECT_TRUE(drawer.batch.Vertices().empty());
        EXPECT_EQ(drawer.poses, 0);
        EXPECT_EQ(ring.point40, (glm::vec3{1.0f, 2.0f, 3.0f}));
        EXPECT_EQ(ring.point4c, (glm::vec3{4.0f, 5.0f, 6.0f}));
    }
}

// 0x5ffdff..0x5ffe24: en los mapas 18 a 23 (Chaos Castle), con +0x1a1 del héroe, cada llamada a 0x5ffdf0 le resta 0,15f
// (0x6b8ae8) al alpha del efecto y lo guarda, antes del chequeo. El 0x11b/1 con 0,2f: en el primer frame queda en
// 0x3d4ccccc (pasa, dos rand()); en el segundo, en 0xbdccccce (no pasa: ni pose, ni sprites, ni rand()). Fuera de 18 a
// 23, sin el byte o en un dibujo intermedio de MuRet (la resta va una vez por llamada del frame del original), el 0x134
// (camino común) queda como está.
TEST(HighEffectDrawingTest, ChaosCastleFadesTheEffectBeforeItsVisibleCheck)
{
    {
        Drawer drawer;
        drawer.pass = std::make_unique<Client::Scenes::GameObjectPass>();
        drawer.pass->map = 18;
        drawer.heroByte1a1 = 1;
        drawer.originalFrame = true;
        ModelEffect ring = EffectOf(0x11b, 1, -2);
        ring.alpha = std::bit_cast<float>(0x3e4ccccdu); // 0,2f
        ASSERT_TRUE(drawer.Draw(std::span(&ring, 1), 0).HasValue());
        EXPECT_EQ(std::bit_cast<std::uint32_t>(ring.alpha), 0x3d4cccccu);
        EXPECT_EQ(DrawsSince(drawer.random), 2);
        EXPECT_EQ(drawer.poses, 1);
        EXPECT_EQ(drawer.Alive().size(), 4u);
        ASSERT_TRUE(drawer.Draw(std::span(&ring, 1), 0).HasValue());
        EXPECT_EQ(std::bit_cast<std::uint32_t>(ring.alpha), 0xbdccccceu);
        EXPECT_EQ(DrawsSince(drawer.random), 2);
        EXPECT_EQ(drawer.poses, 1);
        EXPECT_EQ(drawer.Alive().size(), 4u);
    }
    struct Case
    {
        std::uint8_t map;
        std::uint8_t heroByte1a1;
        bool originalFrame;
    };
    for (const Case& outside : {Case{17, 1, true}, Case{24, 1, true}, Case{23, 0, true}, Case{23, 1, false}})
    {
        SCOPED_TRACE(outside.map);
        SCOPED_TRACE(outside.heroByte1a1);
        SCOPED_TRACE(outside.originalFrame);
        Drawer drawer;
        drawer.pass = std::make_unique<Client::Scenes::GameObjectPass>();
        drawer.pass->map = outside.map;
        drawer.heroByte1a1 = outside.heroByte1a1;
        drawer.originalFrame = outside.originalFrame;
        ModelEffect aurora = EffectOf(0x134, 0);
        ASSERT_TRUE(drawer.Draw(std::span(&aurora, 1), 0).HasValue());
        EXPECT_EQ(aurora.alpha, 1.0f);
        EXPECT_EQ(drawer.poses, 1);
        EXPECT_EQ(drawer.batch.Vertices().size(), 3u);
    }
    // En el 23 con el byte, el 0x134 con alpha 1 queda en 0x3f59999a (1 − 0,15f) y se dibuja con ese alpha.
    Drawer drawer;
    drawer.pass = std::make_unique<Client::Scenes::GameObjectPass>();
    drawer.pass->map = 23;
    drawer.heroByte1a1 = 1;
    drawer.originalFrame = true;
    ModelEffect aurora = EffectOf(0x134, 0);
    ASSERT_TRUE(drawer.Draw(std::span(&aurora, 1), 0).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(aurora.alpha), 0x3f59999au);
    ASSERT_EQ(drawer.batch.Vertices().size(), 3u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(drawer.batch.Vertices()[0].color.alpha), 0x3f59999au);
}

// 0x52d86a..0x52d89f: el 0x135 llama dos veces a 0x602e90 (0x52d871 y 0x52d897) y cada una pasa por 0x5ffdf0. En los
// mapas 18 a 23, con +0x1a1 del héroe, cada llamada le resta 0,15f (0x6b8ae8) al alpha guardado (0x5ffdff..0x5ffe24):
// con alpha 1, la primera lo deja en 0x3f59999a y la segunda en 0x3f333334, y se dibuja las dos veces con ese alpha
// (calculado aparte con RN a 53 bits en el fsub y a 24 en el fstp; a 64 o a 24 da lo mismo).
TEST(HighEffectDrawingTest, ChaosCastleFadesTheMirroredEffectOnEachOfItsTwoBodies)
{
    for (std::uint8_t map = 18; map <= 23; ++map)
    {
        SCOPED_TRACE(static_cast<int>(map));
        Drawer drawer;
        drawer.pass = std::make_unique<Client::Scenes::GameObjectPass>();
        drawer.pass->map = map;
        drawer.heroByte1a1 = 1;
        drawer.originalFrame = true;
        ModelEffect mirrored = EffectOf(0x135, 0);
        ASSERT_EQ(mirrored.alpha, 1.0f);
        const auto drawn = drawer.Draw(std::span(&mirrored, 1), 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(std::bit_cast<std::uint32_t>(mirrored.alpha), 0x3f333334u);
        EXPECT_EQ(drawer.poses, 2);
        const auto vertices = drawer.batch.Vertices();
        ASSERT_EQ(vertices.size(), 6u);
        for (std::size_t vertex = 0; vertex < 3; ++vertex)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(vertices[vertex].color.alpha), 0x3f59999au);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(vertices[vertex + 3].color.alpha), 0x3f333334u);
        }
    }
}

// 0x5ffe2a..0x5ffe3d: fcomp con 0,01f (0x6b88d4) y test ah,1 sólo sale con C0 (menor o sin orden); con el alpha igual a
// 0,01f (0x3c23d70a) devuelve 1 y se dibuja: el 0x134 posa y deja su cuerpo con ese alpha y el 0x11b/1 saca sus dos
// rand() y deja sus cuatro sprites.
TEST(HighEffectDrawingTest, AnEffectWithExactlyTheVisibleAlphaIsDrawn)
{
    const float visible = std::bit_cast<float>(0x3c23d70au); // 0,01f
    {
        Drawer drawer;
        ModelEffect aurora = EffectOf(0x134, 0);
        aurora.alpha = visible;
        const auto drawn = drawer.Draw(std::span(&aurora, 1), 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(drawer.poses, 1);
        ASSERT_EQ(drawer.batch.Vertices().size(), 3u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(drawer.batch.Vertices()[0].color.alpha), 0x3c23d70au);
    }
    Drawer drawer;
    ModelEffect ring = EffectOf(0x11b, 1, -2);
    ring.alpha = visible;
    const auto drawn = drawer.Draw(std::span(&ring, 1), 0);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(drawer.poses, 1);
    EXPECT_EQ(DrawsSince(drawer.random), 2);
    EXPECT_EQ(drawer.Alive().size(), 4u);
    EXPECT_EQ(ring.point40, SecondBonePoint);
}

// Regresión de 0x5ffdf0 antes de 0x6002e0: la puerta 0x3d..0x40 del mapa 30 (0x42cc10) y el tipo 9 de la cola de
// Kalima (mapas 24 a 29 y 36), con alpha 0 o NaN, no posan ni dibujan nada (antes, con 0, caían a DrawObject sin pose;
// con NaN, se posaban). Con alpha 1 los dos posan una vez y dibujan su cuerpo (RenderBody(2) de la puerta con +0x4 en
// 0), y el tipo 9 llega a la cola de Kalima.
TEST(HighEffectDrawingTest, TheGateAndTheKalimaTailBelowTheVisibleAlphaNeitherPoseNorDraw)
{
    struct Case
    {
        std::int16_t type;
        std::uint8_t map;
    };
    for (const Case tested :
         {Case{0x3d, 30}, Case{0x3e, 30}, Case{0x3f, 30}, Case{0x40, 30}, Case{9, 24}, Case{9, 29}, Case{9, 36}})
    {
        SCOPED_TRACE(tested.type);
        SCOPED_TRACE(static_cast<int>(tested.map));
        for (const float alpha : {0.0f, std::numeric_limits<float>::quiet_NaN()})
        {
            SCOPED_TRACE(alpha);
            Drawer drawer;
            drawer.pass = std::make_unique<Client::Scenes::GameObjectPass>();
            drawer.pass->map = tested.map;
            ModelEffect effect = EffectOf(tested.type, 0);
            effect.alpha = alpha;
            for (const std::uint8_t mode : {std::uint8_t{0}, std::uint8_t{1}})
            {
                SCOPED_TRACE(mode);
                const auto drawn = drawer.Draw(std::span(&effect, 1), mode);
                ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            }
            EXPECT_EQ(drawer.poses, 0);
            EXPECT_TRUE(drawer.batch.Vertices().empty());
            EXPECT_EQ(DrawsSince(drawer.random), 0);
        }
        Drawer drawer;
        drawer.pass = std::make_unique<Client::Scenes::GameObjectPass>();
        drawer.pass->map = tested.map;
        ModelEffect effect = EffectOf(tested.type, 0);
        const auto drawn = drawer.Draw(std::span(&effect, 1), 0);
        EXPECT_EQ(drawer.poses, 1);
        EXPECT_EQ(drawer.batch.Vertices().size(), 3u);
        if (tested.type == 9)
        {
            // Después del cuerpo, la cola de Kalima, que acá corta en su capa de agua sin el productor de DAT_07d2d360.
            ASSERT_FALSE(drawn.HasValue());
            EXPECT_TRUE(drawn.Error().starts_with("0x439593")) << drawn.Error();
        }
        else
        {
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        }
    }
}

// 0x52d66a: el 0x11b/0 (blendMesh −1) sólo se dibuja en el modo 0, con el color del modelo en (1; 0,6f; 0,3f) y
// RenderBody(2) con su textura; en el modo 1, ni sprites ni rand().
TEST(HighEffectDrawingTest, TheRingSparkDrawsOnlyInTheFirstPassWithItsWarmLight)
{
    Drawer drawer;
    ModelEffect spark = EffectOf(0x11b, 0);
    const auto skipped = drawer.Draw(std::span(&spark, 1), 1);
    ASSERT_TRUE(skipped.HasValue()) << skipped.Error();
    EXPECT_EQ(DrawsSince(drawer.random), 0);
    EXPECT_TRUE(drawer.Alive().empty());
    EXPECT_TRUE(drawer.batch.Vertices().empty());

    const auto drawn = drawer.Draw(std::span(&spark, 1), 0);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(DrawsSince(drawer.random), 2);
    EXPECT_EQ(drawer.Alive().size(), 4u);
    const auto commands = DrawCommands(drawer.batch);
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands[0].texture, std::optional(OwnTexture));
    ASSERT_EQ(drawer.batch.Vertices().size(), 3u);
    for (const Client::Render::ModelVertex& vertex : drawer.batch.Vertices())
    {
        EXPECT_EQ(vertex.color.red, 1.0f);
        EXPECT_EQ(vertex.color.green, std::bit_cast<float>(0x3f19999au));
        EXPECT_EQ(vertex.color.blue, std::bit_cast<float>(0x3e99999au));
    }
}

// 0x601f49: sin el CRT corta después de los dos 0x5ac, con +0x40 y +0x4c ya escritos.
TEST(HighEffectDrawingTest, TheRingWithoutTheRandomStopsAfterItsGlows)
{
    Drawer drawer;
    drawer.withRandom = false;
    ModelEffect ring = EffectOf(0x11b, 1, -2);
    const auto drawn = drawer.Draw(std::span(&ring, 1), 0);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_TRUE(drawn.Error().starts_with("0x601f49")) << drawn.Error();
    EXPECT_EQ(ring.point40, SecondBonePoint);
    EXPECT_EQ(ring.point4c, ThirdBonePoint);
    EXPECT_EQ(drawer.Alive().size(), 2u);
}

// 0x601ea9 y 0x601ed4 → 0x538256: el dibujo deja +0x40 y +0x4c en los huesos y el paso siguiente del 0x618/0xa y del
// /0xb (BeamJoints) los sigue; sin ese dibujo seguirían en el punto de nacimiento (0x51c84b).
TEST(HighEffectDrawingTest, TheRingBeamsFollowTheBonesAfterTheDraw)
{
    auto world = std::make_unique<Client::Scenes::WorldState>();
    CrtRandom random(Seed);
    Client::Scenes::ActorState owner;
    const auto made = Client::Scenes::CreateHighModelEffect(
        {.type = 0x11b, .position = {1000.0f, 2000.0f, 300.0f}, .subtype = 1, .owner = &owner},
        {.world = *world, .random = random, .worldClock = 0.0f});
    ASSERT_TRUE(made.HasValue()) << made.Error();
    auto& joints = world->DrawTail().joints;
    ASSERT_TRUE(joints[0].alive);
    ASSERT_TRUE(joints[1].alive);
    EXPECT_EQ(joints[0].position, (glm::vec3{1000.0f, 2000.0f, 300.0f}));

    Drawer drawer;
    const auto drawn = drawer.Draw(world->Map().effects.Effects(), 0);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    auto lighting = std::make_unique<Client::World::TerrainLighting>();
    const auto stepped =
        Client::Scenes::StepJoints(joints, random, nullptr, &world->Particles(), 0.0f, &drawer.sprites, lighting.get());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(joints[0].subtype, 0xa);
    EXPECT_EQ(joints[0].position, SecondBonePoint);
    EXPECT_EQ(joints[1].subtype, 0xb);
    EXPECT_EQ(joints[1].position, ThirdBonePoint);
}

// 0x601fb4..0x602000: el 0x114/3, /4 y /5, RenderBody(2) con la malla oculta en −1 (dibuja la que +0x58 ocultaría) y la
// textura 0x6ef; el /0 va por el pase común (la malla oculta no se dibuja) y el /2, por 0x601fd5, con su textura.
TEST(HighEffectDrawingTest, ThePinkWavesDrawEveryMeshWithTheirTexture)
{
    for (const std::int32_t subtype : {0, 2, 3, 4, 5})
    {
        SCOPED_TRACE(subtype);
        Drawer drawer;
        ModelEffect wave = EffectOf(0x114, subtype);
        wave.objectState.hiddenMesh = subtype == 2 ? -1 : 0;
        const auto drawn = drawer.Draw(std::span(&wave, 1), 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        const auto commands = DrawCommands(drawer.batch);
        if (subtype == 0)
        {
            EXPECT_TRUE(commands.empty());
            continue;
        }
        ASSERT_EQ(commands.size(), 1u);
        const bool pink = subtype >= 3;
        EXPECT_EQ(commands[0].texture, std::optional(pink ? Client::Render::TextureSlot{0x6ef} : OwnTexture));
        EXPECT_EQ(wave.objectState.hiddenMesh, subtype == 2 ? -1 : 0);
        EXPECT_EQ(DrawsSince(drawer.random), 0);
        EXPECT_TRUE(drawer.Alive().empty());
    }
}

// 0x602005..0x602044: el 0x140 con el color del modelo en (0,4f; 0,6f; 1) y dos RenderBody(0x44) con la textura en
// −1: el cromado de 0x4ecc30, aditivo y con Chrome01 (0x5c0). Sin sprites ni rand().
TEST(HighEffectDrawingTest, TheGuildMarkTakesTwoChromePasses)
{
    Drawer drawer;
    ModelEffect mark = EffectOf(0x140, 0);
    const auto drawn = drawer.Draw(std::span(&mark, 1), 0);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // Los dos pases tienen el mismo estado: el lote los junta en un comando de seis vértices.
    const auto commands = DrawCommands(drawer.batch);
    ASSERT_FALSE(commands.empty());
    for (const Drawn& command : commands)
    {
        EXPECT_EQ(command.blend, Client::Render::ModelBlend::Additive);
        EXPECT_EQ(command.texture, std::optional(Client::Scenes::ChromeTextureSlot));
    }
    ASSERT_EQ(drawer.batch.Vertices().size(), 6u);
    for (const Client::Render::ModelVertex& vertex : drawer.batch.Vertices())
    {
        EXPECT_EQ(vertex.color.red, std::bit_cast<float>(0x3ecccccdu));
        EXPECT_EQ(vertex.color.green, std::bit_cast<float>(0x3f19999au));
        EXPECT_EQ(vertex.color.blue, 1.0f);
    }
    EXPECT_EQ(DrawsSince(drawer.random), 0);
    EXPECT_TRUE(drawer.Alive().empty());
}

// 0x43909e..0x439214: el 0x125 con el color en 0,3 y RenderBody(2); en los huesos 3 y 4, el 0x5ca de escala 0,2f y el
// 0x5fd de escala 0,5f con la luz (l; l · 0,2f; l · 0,1f), l = FSIN(T · 0,003f) · 0,2f + 0,8f: con T = 1000, l en
// 0x3f54067d, l · 0,2f en 0x3e299ecb y l · 0,1f en 0x3da99ecb (calculados aparte). Sin rand(); sin el reloj del mundo,
// corta. El 0x125 recién creado (0x51cdc9, el alpha en 0 en 0x51cdf5) no pasa 0x5ffdf0 (0x5ffe2a..0x5ffe3d) en ningún
// pase: ni pose, ni cuerpo, ni sprites sobre los huesos viejos del buffer, ni rand(); lo enciende su joint 0x6ee/0x12
// (0x53949a).
TEST(HighEffectDrawingTest, TheKundunSpiritGlowsOnItsThirdAndFourthBones)
{
    {
        auto world = std::make_unique<Client::Scenes::WorldState>();
        for (Client::Scenes::JointState& joint : world->DrawTail().joints)
            joint.alive = true;
        CrtRandom creation(Seed);
        const auto made = Client::Scenes::CreateHighModelEffect({.type = 0x125, .position = {1000.0f, 2000.0f, 300.0f}},
                                                                {.world = *world, .random = creation});
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const auto effects = world->Map().effects.Effects();
        ASSERT_TRUE(effects[0].alive);
        ASSERT_EQ(effects[0].type, 0x125);
        ASSERT_EQ(effects[0].alpha, 0.0f);
        ASSERT_EQ(effects[0].objectState.blendMesh, -2);
        Drawer drawer;
        // Lo que dejó en el buffer de la pose el último que se posó.
        drawer.bones = {BoneTransform(0.0f), BoneTransform(0.0f), Translation(SecondBonePoint),
                        Translation(ThirdBonePoint), Translation(FourthBonePoint)};
        for (const std::uint8_t mode : {std::uint8_t{0}, std::uint8_t{1}})
        {
            SCOPED_TRACE(mode);
            const auto drawn = drawer.Draw(effects, mode);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            EXPECT_TRUE(drawer.Alive().empty());
            EXPECT_TRUE(drawer.batch.Vertices().empty());
            EXPECT_EQ(drawer.poses, 0);
            EXPECT_EQ(DrawsSince(drawer.random), 0);
        }
    }

    Drawer drawer;
    ASSERT_EQ(drawer.clock, 1000.0f);
    ModelEffect spirit = EffectOf(0x125, 0, -2);
    const auto drawn = drawer.Draw(std::span(&spirit, 1), 1);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(drawer.batch.Vertices().size(), 3u);
    EXPECT_EQ(DrawsSince(drawer.random), 0);
    const glm::vec3 light{std::bit_cast<float>(0x3f54067du), std::bit_cast<float>(0x3e299ecbu),
                          std::bit_cast<float>(0x3da99ecbu)};
    const auto alive = drawer.Alive();
    ASSERT_EQ(alive.size(), 4u);
    ExpectSprite(alive[0], 0x5ca, ThirdBonePoint, 0.2f, light, 0.0f);
    ExpectSprite(alive[1], 0x5fd, ThirdBonePoint, 0.5f, light, 0.0f);
    ExpectSprite(alive[2], 0x5ca, FourthBonePoint, 0.2f, light, 0.0f);
    ExpectSprite(alive[3], 0x5fd, FourthBonePoint, 0.5f, light, 0.0f);

    ModelBatch batch;
    ModelEffect clockless = EffectOf(0x125, 0);
    DrawnObject object;
    object.model = 0x125;
    const auto cut = Client::Scenes::DrawTheHighEffectBody(batch, drawer.models, clockless, object, {},
                                                           {.sprites = &drawer.sprites});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x4390df")) << cut.Error();
}
