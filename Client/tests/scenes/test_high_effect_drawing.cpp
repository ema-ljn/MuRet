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
#include <cmath>
#include <cstddef>
#include <cstdint>
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
// 0,5 y escala 0,5f, cada uno con su rand() % 360 de giro: dos rand() por pase y cuatro por frame.
TEST(HighEffectDrawingTest, TheRingTakesTwoRandomsInEachPassAndLeavesItsPointsOnItsBones)
{
    Drawer drawer;
    ModelEffect ring = EffectOf(0x11b, 1, -2);
    ring.point40 = {1.0f, 2.0f, 3.0f};
    ring.point4c = {4.0f, 5.0f, 6.0f};
    CrtRandom replay(Seed);
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
        const float firstTurn = static_cast<float>(replay.Next() % 360);
        const float secondTurn = static_cast<float>(replay.Next() % 360);
        ExpectSprite(alive[first + 2], 0x5fd, SecondBonePoint, 0.5f, glm::vec3(0.5f), firstTurn);
        ExpectSprite(alive[first + 3], 0x5fd, ThirdBonePoint, 0.5f, glm::vec3(0.5f), secondTurn);
    }
    EXPECT_EQ(drawer.poses, 2);
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
// 0x5fd de escala 0,5f con la luz (l; l · 0,2f; l · 0,1f), l = FSIN(T · 0,003f) · 0,2f + 0,8f. Sin rand(); sin el
// reloj del mundo, corta.
TEST(HighEffectDrawingTest, TheKundunSpiritGlowsOnItsThirdAndFourthBones)
{
    Drawer drawer;
    ModelEffect spirit = EffectOf(0x125, 0, -2);
    const auto drawn = drawer.Draw(std::span(&spirit, 1), 1);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(drawer.batch.Vertices().size(), 3u);
    EXPECT_EQ(DrawsSince(drawer.random), 0);
    const double angle = static_cast<double>(drawer.clock) * static_cast<double>(0.003f);
    const auto l = static_cast<float>(std::sin(angle) * static_cast<double>(0.2f) + static_cast<double>(0.8f));
    const glm::vec3 light{l, static_cast<float>(static_cast<double>(l) * static_cast<double>(0.2f)),
                          static_cast<float>(static_cast<double>(l) * static_cast<double>(0.1f))};
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
