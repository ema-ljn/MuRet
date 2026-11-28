#include "Models/IModelLibrary.h"
#include "Effects/HighEffectDrawing.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Math/HalfLifeMath.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/Drawing/PosePublishing.h"
#include "Kernel/PoseBuffer.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Scenes/GameDrawTail.h"
#include "Effects/HighModelEffects.h"
#include "Kernel/WorldMap.h"
#include "Kernel/ObjectPass.h"
#include "Kernel/WorldState.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Terrain/SphereFrustum.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainLighting.h"

#include "support/LentWorld.h"

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

// The high types' own cases of 0x6002e0 (and of 0x438fe0): the 0x11b with its bones, its sprites and its two rand()
// per pass; the 0x114/3..5 with texture 0x6ef; the chromed 0x140; the 0x125 with its sprites. 06, "Los efectos con
// modelo de los tipos altos".
namespace
{
using Client::Actors::Drawing::DrawnObject;
using Client::Crt::CrtRandom;
using Client::Effects::ModelEffect;
using Client::Math::BoneTransform;
using Client::Render::ModelBatch;

constexpr std::uint32_t Seed = 0x2468ace1u;
constexpr glm::vec3 SecondBonePoint{100.0f, 200.0f, 300.0f};
constexpr glm::vec3 ThirdBonePoint{400.0f, 500.0f, 600.0f};
constexpr glm::vec3 FourthBonePoint{-70.0f, 80.0f, 90.0f};
constexpr Client::Render::TextureSlot OwnTexture{0x400};

// One triangle with its texture and one bone.
class TriangleModels final : public Client::Models::IModelLibrary
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
        Client::Models::SkeletonBone bone;
        bone.actions = {{Client::Models::BoneKey{}}};
        skeleton.bones = {bone};
        skeleton.actions = {Client::Models::ActionInfo{.keyCount = 1}};
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return &model;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return textures;
    }
    Client::Data::Models::Model model;
    Client::Models::Skeleton skeleton;
    std::array<Client::Render::TextureSlot, 1> textures{OwnTexture};
};

BoneTransform Translation(const glm::vec3& point)
{
    BoneTransform bone(1.0f);
    bone[3] = point;
    return bone;
}

// How many rand() `random` drew since the seed.
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

// What 0x5ffdf0's pose leaves in the global buffer: bones 2, 3 and 4 at their world points.
struct Drawer
{
    Client::Core::Expected<void, std::string> Draw(std::span<ModelEffect> effects, std::uint8_t mode)
    {
        Client::Scenes::ModelEffectDrawResources resources{
            .heights = *heights,
            .worldClock = clock,
            .onPose = [this](const DrawnObject& object) -> Client::Core::Expected<void, std::string>
            {
                ++poses;
                // As in production (GameSceneTail): 0x52da99 → 0x602e90(effect, 0, …) leaves the world scratch.
                auto published = Client::Actors::Drawing::PublishPose(carry, models, object, true);
                bones = {BoneTransform(0.0f), BoneTransform(0.0f), Translation(SecondBonePoint),
                         Translation(ThirdBonePoint), Translation(FourthBonePoint)};
                return published;
            },
            .posedMeshes = [this]() -> std::span<const Client::Models::SkinnedMesh> { return carry.meshes; },
            .sprites = &sprites,
            .random = withRandom ? &random : nullptr,
            .kalimaPass = pass.get(),
            .heroByte1a1 = heroByte1a1,
            .originalFrame = originalFrame,
            .posedBones = [this]() -> std::span<const BoneTransform> { return bones; }};
        return Client::Scenes::DrawGameModelEffects(batch, models, effects, Client::Terrain::SphereFrustum{}, nullptr,
                                                    {}, 0, resources, mode);
    }

    [[nodiscard]] std::vector<Client::Particles::Sprite> Alive()
    {
        std::vector<Client::Particles::Sprite> alive;
        for (const Client::Particles::Sprite& sprite : sprites.Sprites())
            if (sprite.alive)
                alive.push_back(sprite);
        return alive;
    }

    TriangleModels models;
    std::unique_ptr<Client::Data::Terrain::TerrainHeights> heights =
        std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Particles::FrameSprites sprites;
    CrtRandom random{Seed};
    bool withRandom = true;
    float clock = 1000.0f;
    // The objects' pass (its map, 0x6c9dd0), the hero's +0x1a1 and whether the drawing is of an original frame: what
    // 0x5ffdf0 looks at for the Chaos Castle subtraction.
    std::unique_ptr<Client::Kernel::GameObjectPass> pass;
    std::uint8_t heroByte1a1 = 0;
    bool originalFrame = false;
    std::vector<BoneTransform> bones;
    Client::Kernel::PoseBuffer carry;
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

void ExpectSprite(const Client::Particles::Sprite& sprite, std::uint16_t texture, const glm::vec3& position,
                  float scale, const glm::vec3& light, float rotation)
{
    EXPECT_EQ(sprite.texture, texture);
    EXPECT_EQ(sprite.position, position);
    EXPECT_EQ(sprite.scale, scale);
    EXPECT_EQ(sprite.light, light);
    EXPECT_EQ(sprite.rotation, rotation);
    EXPECT_EQ(sprite.blend, 0);
}
} // namespace

// 0x601e68..0x601faf: the 0x11b/1 (blendMesh −2) passes the filter of 0x52d620's mode 1 (0x52d673): in each pass, +0x40
// and +0x4c on bones 2 and 3 (0x4ec110 without translating), the two 0x5ac with light (1, 0, 0) and scale 1 and the
// two 0x5fd with light 0.5 and scale 0.5f, each with its rand() % 360 turn: two rand() per pass and four per frame.
// With the seed, the turns are 211 and 205 in mode 0, and 2 and 89 in mode 1 (computed separately with the CRT's LCG).
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

// 0x602e90 → 0x5ffdf0 (0x5ffe2a..0x5ffe3d): with alpha below 0.01f (0x6b88d4), or unordered, it returns 0 without
// posing and 0x602eb0 skips 0x6002e0: the 0x11b draws no rand(), leaves no sprites and does not move +0x40 and +0x4c,
// in any pass.
TEST(HighEffectDrawingTest, TheRingBelowTheVisibleAlphaTakesNoRandom)
{
    const float justBelow = std::bit_cast<float>(0x3c23d709u); // the float before 0.01f (0x3c23d70a)
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

// 0x5ffdff..0x5ffe24: on maps 18 to 23 (Chaos Castle), with the hero's +0x1a1, each call to 0x5ffdf0 subtracts 0.15f
// (0x6b8ae8) from the effect's alpha and stores it, before the check. The 0x11b/1 with 0.2f: in the first frame it ends
// at 0x3d4ccccc (passes, two rand()); in the second, at 0xbdccccce (does not pass: no pose, sprites or rand()). Outside
// 18 to 23, without the byte or in an intermediate MuRet drawing (the subtraction goes once per call of the original's
// frame), the 0x134 (common path) stays as it is.
TEST(HighEffectDrawingTest, ChaosCastleFadesTheEffectBeforeItsVisibleCheck)
{
    {
        Drawer drawer;
        drawer.pass = std::make_unique<Client::Kernel::GameObjectPass>();
        drawer.pass->map = 18;
        drawer.heroByte1a1 = 1;
        drawer.originalFrame = true;
        ModelEffect ring = EffectOf(0x11b, 1, -2);
        ring.alpha = std::bit_cast<float>(0x3e4ccccdu); // 0.2f
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
        drawer.pass = std::make_unique<Client::Kernel::GameObjectPass>();
        drawer.pass->map = outside.map;
        drawer.heroByte1a1 = outside.heroByte1a1;
        drawer.originalFrame = outside.originalFrame;
        ModelEffect aurora = EffectOf(0x134, 0);
        ASSERT_TRUE(drawer.Draw(std::span(&aurora, 1), 0).HasValue());
        EXPECT_EQ(aurora.alpha, 1.0f);
        EXPECT_EQ(drawer.poses, 1);
        EXPECT_EQ(drawer.batch.Vertices().size(), 3u);
    }
    // On map 23 with the byte, the 0x134 with alpha 1 ends at 0x3f59999a (1 − 0.15f) and is drawn with that alpha.
    Drawer drawer;
    drawer.pass = std::make_unique<Client::Kernel::GameObjectPass>();
    drawer.pass->map = 23;
    drawer.heroByte1a1 = 1;
    drawer.originalFrame = true;
    ModelEffect aurora = EffectOf(0x134, 0);
    ASSERT_TRUE(drawer.Draw(std::span(&aurora, 1), 0).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(aurora.alpha), 0x3f59999au);
    ASSERT_EQ(drawer.batch.Vertices().size(), 3u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(drawer.batch.Vertices()[0].color.alpha), 0x3f59999au);
}

// 0x52d86a..0x52d89f: the 0x135 calls 0x602e90 twice (0x52d871 and 0x52d897) and each goes through 0x5ffdf0. On maps
// 18 to 23, with the hero's +0x1a1, each call subtracts 0.15f (0x6b8ae8) from the stored alpha (0x5ffdff..0x5ffe24):
// with alpha 1, the first leaves it at 0x3f59999a and the second at 0x3f333334, and it is drawn both times with that
// alpha (computed separately with RN at 53 bits in the fsub and at 24 in the fstp; at 64 or 24 it gives the same).
TEST(HighEffectDrawingTest, ChaosCastleFadesTheMirroredEffectOnEachOfItsTwoBodies)
{
    for (std::uint8_t map = 18; map <= 23; ++map)
    {
        SCOPED_TRACE(static_cast<int>(map));
        Drawer drawer;
        drawer.pass = std::make_unique<Client::Kernel::GameObjectPass>();
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

// 0x5ffe2a..0x5ffe3d: fcomp with 0.01f (0x6b88d4) and test ah,1 only exits with C0 (less or unordered); with alpha
// equal to 0.01f (0x3c23d70a) it returns 1 and draws: the 0x134 poses and leaves its body with that alpha and the
// 0x11b/1 draws its two rand() and leaves its four sprites.
TEST(HighEffectDrawingTest, AnEffectWithExactlyTheVisibleAlphaIsDrawn)
{
    const float visible = std::bit_cast<float>(0x3c23d70au); // 0.01f
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

// Regression of 0x5ffdf0 before 0x6002e0: map 30's gate 0x3d..0x40 (0x42cc10) and Kalima's tail type 9 (maps 24 to 29
// and 36), with alpha 0 or NaN, neither pose nor draw anything. With alpha 1 both pose once and draw their body (the
// gate's RenderBody(2) with +0x4 at 0), and type 9 reaches Kalima's tail.
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
            drawer.pass = std::make_unique<Client::Kernel::GameObjectPass>();
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
        drawer.pass = std::make_unique<Client::Kernel::GameObjectPass>();
        drawer.pass->map = tested.map;
        ModelEffect effect = EffectOf(tested.type, 0);
        const auto drawn = drawer.Draw(std::span(&effect, 1), 0);
        EXPECT_EQ(drawer.poses, 1);
        EXPECT_EQ(drawer.batch.Vertices().size(), 3u);
        if (tested.type == 9)
        {
            // After the body, Kalima's tail, which here stops at its water layer without the producer of DAT_07d2d360.
            ASSERT_FALSE(drawn.HasValue());
            EXPECT_TRUE(drawn.Error().starts_with("0x439593")) << drawn.Error();
        }
        else
        {
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        }
    }
}

// 0x52d66a: the 0x11b/0 (blendMesh −1) only draws in mode 0, with the model color at (1; 0.6f; 0.3f) and
// RenderBody(2) with its texture; in mode 1, no sprites and no rand().
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

// 0x601f49: without the CRT it stops after the two 0x5ac, with +0x40 and +0x4c already written.
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

// 0x601ea9 and 0x601ed4 → 0x538256: the drawing leaves +0x40 and +0x4c on the bones and the next step of the 0x618/0xa
// and the /0xb (BeamJoints) follows them; without that drawing they would stay at the birth point (0x51c84b).
TEST(HighEffectDrawingTest, TheRingBeamsFollowTheBonesAfterTheDraw)
{
    auto world = Client::Tests::LentWorld();
    CrtRandom random(Seed);
    Client::Kernel::ActorState owner;
    const auto made = Client::Effects::CreateHighModelEffect(
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
    auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    const auto stepped =
        Client::Joints::StepJoints(joints, random, nullptr, &world->Particles(), 0.0f, &drawer.sprites, lighting.get());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(joints[0].subtype, 0xa);
    EXPECT_EQ(joints[0].position, SecondBonePoint);
    EXPECT_EQ(joints[1].subtype, 0xb);
    EXPECT_EQ(joints[1].position, ThirdBonePoint);
}

// 0x601fb4..0x602000: the 0x114/3, /4 and /5, RenderBody(2) with the hidden mesh at −1 (it draws the one +0x58 would
// hide) and texture 0x6ef; the /0 goes through the common pass (the hidden mesh is not drawn) and the /2 through
// 0x601fd5, with its texture.
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

// 0x602005..0x602044: the 0x140 with the model color at (0.4f; 0.6f; 1) and two RenderBody(0x44) with the texture at
// −1: 0x4ecc30's chrome, additive and with Chrome01 (0x5c0). No sprites or rand().
TEST(HighEffectDrawingTest, TheGuildMarkTakesTwoChromePasses)
{
    Drawer drawer;
    ModelEffect mark = EffectOf(0x140, 0);
    const auto drawn = drawer.Draw(std::span(&mark, 1), 0);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // Both passes have the same state: the batch joins them in one six-vertex command.
    const auto commands = DrawCommands(drawer.batch);
    ASSERT_FALSE(commands.empty());
    for (const Drawn& command : commands)
    {
        EXPECT_EQ(command.blend, Client::Render::ModelBlend::Additive);
        EXPECT_EQ(command.texture, std::optional(Client::Actors::Drawing::ChromeTextureSlot));
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

// 0x52da99 → 0x602e90(effect, 0, …): 0x5ffdf0 poses the 0x140 with 0x4eb860's flag at 1 and 0x4ebc80 with Translate at
// 0 (0x6001a1), the normal rotated by R · s. The two RenderBody(0x44) of 0x602005..0x602044 take the chrome U from
// 0x4ecc30, 0.5 · nz: with scale 1.5 and normal (0, 0, 1), 0.75, with the published pose (production's) and through
// the fallback path, without it.
TEST(HighEffectDrawingTest, TheGuildMarkChromeKeepsTheScaleOfItsNormal)
{
    constexpr float MarkScale = 1.5f;
    constexpr float ScaledChromeU = 0.75f;
    {
        Drawer drawer;
        ModelEffect mark = EffectOf(0x140, 0);
        mark.transform.scale = MarkScale;
        const auto drawn = drawer.Draw(std::span(&mark, 1), 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(drawer.poses, 1);
        ASSERT_EQ(drawer.batch.Vertices().size(), 6u);
        for (const Client::Render::ModelVertex& vertex : drawer.batch.Vertices())
            EXPECT_EQ(vertex.uv.x, ScaledChromeU);
    }
    Drawer drawer;
    ModelEffect mark = EffectOf(0x140, 0);
    DrawnObject object;
    object.model = 0x140;
    object.transform.position = mark.transform.position;
    object.transform.scale = MarkScale;
    object.alpha = 1.0f;
    ASSERT_TRUE(object.retainedMeshes.empty());
    ModelBatch batch;
    const auto drawn = Client::Effects::DrawTheHighEffectBody(batch, drawer.models, mark, object, {}, {});
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_TRUE(drawn.Value());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    for (const Client::Render::ModelVertex& vertex : batch.Vertices())
        EXPECT_EQ(vertex.uv.x, ScaledChromeU);
}

// 0x43909e..0x439214: the 0x125 with the color at 0.3 and RenderBody(2); on bones 3 and 4, the 0x5ca with scale 0.2f
// and the 0x5fd with scale 0.5f with light (l; l · 0.2f; l · 0.1f), l = FSIN(T · 0.003f) · 0.2f + 0.8f: with T = 1000,
// l at 0x3f54067d, l · 0.2f at 0x3e299ecb and l · 0.1f at 0x3da99ecb (computed separately). No rand(); without the
// world clock, it stops. The freshly created 0x125 (0x51cdc9, alpha at 0 at 0x51cdf5) does not pass 0x5ffdf0
// (0x5ffe2a..0x5ffe3d) in any pass: no pose, body, sprites on the buffer's old bones or rand(); its joint 0x6ee/0x12
// turns it on (0x53949a).
TEST(HighEffectDrawingTest, TheKundunSpiritGlowsOnItsThirdAndFourthBones)
{
    {
        auto world = Client::Tests::LentWorld();
        for (Client::Kernel::JointState& joint : world->DrawTail().joints)
            joint.alive = true;
        CrtRandom creation(Seed);
        const auto made = Client::Effects::CreateHighModelEffect(
            {.type = 0x125, .position = {1000.0f, 2000.0f, 300.0f}}, {.world = *world, .random = creation});
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const auto effects = world->Map().effects.Effects();
        ASSERT_TRUE(effects[0].alive);
        ASSERT_EQ(effects[0].type, 0x125);
        ASSERT_EQ(effects[0].alpha, 0.0f);
        ASSERT_EQ(effects[0].objectState.blendMesh, -2);
        Drawer drawer;
        // What the last one posed left in the pose buffer.
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
    const auto cut = Client::Effects::DrawTheHighEffectBody(batch, drawer.models, clockless, object, {},
                                                            {.sprites = &drawer.sprites});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x4390df")) << cut.Error();
}
