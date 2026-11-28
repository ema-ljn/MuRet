#include "Math/X87.h"
#include "Models/IModelLibrary.h"
#include "Actors/Drawing/GameHanging.h"
#include "Guild/GuildState.h"

#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Kernel/PoseBuffer.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Session/CharacterDrawHooks.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// DrawGameHanging (0x50db57..0x50e9fa and 0x509340) on a synthetic body: bone k of the body and of each hanging model
// is at (0, 0, k) without a turn, so each point shows which bone it comes from. The expected values come from the ASM
// of c32bbd5f, without running the original.
namespace
{
using Client::Actors::Drawing::GameHangingPass;
using Client::Kernel::ActorState;

const Client::Session::CharacterDrawHooks JointHooks;

constexpr std::uint16_t Player = 0x20a;
constexpr std::uint16_t Sword = 0x212;
constexpr std::uint16_t BladeSword = 0x21d; // the flash at −150 of 0x50e921
constexpr std::uint16_t Sword224 = 0x224;
constexpr std::uint16_t Sword223 = 0x223;
constexpr std::uint16_t Sword21 = 0x226;
constexpr std::uint16_t Mace10 = 0x61b;
constexpr std::uint16_t Bolts = 0xa19;
constexpr std::uint16_t Staff08 = 0xc19;
constexpr std::uint16_t Helper02 = 0x1c13;
constexpr std::uint16_t MuSign = 0x12a;
constexpr std::uint16_t Flare = 0x5ac;
constexpr std::uint16_t Shiny = 0x5fd;
constexpr std::size_t BoneCount = 48;
constexpr float PlayerSecondActionSpeed = 0.75f;

// The CRT's rand() with seed 1 after `count` calls.
std::uint32_t AfterRandoms(std::size_t count)
{
    Client::Crt::CrtRandom random{1};
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random.State();
}

using Client::Math::D;
using Client::Math::F32;

// The first seed with which `condition` accepts the draws of a new CrtRandom.
std::uint32_t SeedWhere(const std::function<bool(Client::Crt::CrtRandom&)>& condition)
{
    for (std::uint32_t seed = 1; seed < 100000; ++seed)
    {
        Client::Crt::CrtRandom random(seed);
        if (condition(random))
            return seed;
    }
    ADD_FAILURE() << "ninguna semilla cumple la condición";
    return 1;
}

// A CrtRandom with the seed `seed` after `count` calls.
Client::Crt::CrtRandom Skipped(std::uint32_t seed, std::size_t count)
{
    Client::Crt::CrtRandom random(seed);
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random;
}

void ExpectSameJoints(std::span<const Client::Kernel::JointState> actual,
                      std::span<const Client::Kernel::JointState> expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t k = 0; k < actual.size(); ++k)
    {
        SCOPED_TRACE(k);
        const auto& a = actual[k];
        const auto& e = expected[k];
        ASSERT_EQ(a.alive, e.alive);
        if (!a.alive)
            continue;
        EXPECT_EQ(a.type, e.type);
        EXPECT_EQ(a.subtype, e.subtype);
        EXPECT_EQ(a.texture, e.texture);
        EXPECT_EQ(a.blend, e.blend);
        EXPECT_EQ(a.width, e.width);
        EXPECT_EQ(a.speed, e.speed);
        EXPECT_EQ(a.life, e.life);
        EXPECT_EQ(a.light, e.light);
        EXPECT_EQ(a.position, e.position);
        EXPECT_EQ(a.angles, e.angles);
        EXPECT_EQ(a.targetPosition, e.targetPosition);
        EXPECT_EQ(a.ownerContext, e.ownerContext);
        EXPECT_EQ(a.count, e.count);
        EXPECT_EQ(a.maximumCount, e.maximumCount);
        EXPECT_EQ(a.repeatedTexture, e.repeatedTexture);
        EXPECT_EQ(a.points, e.points);
    }
}

void ExpectSameParticles(const Client::Particles::Particles& actual, const Client::Particles::Particles& expected)
{
    for (std::size_t k = 0; k < Client::Particles::ParticleCount; ++k)
    {
        const auto& a = actual.All()[k];
        const auto& e = expected.All()[k];
        ASSERT_EQ(a.alive, e.alive) << k;
        if (!a.alive)
            continue;
        SCOPED_TRACE(k);
        EXPECT_EQ(a.type, e.type);
        EXPECT_EQ(a.subtype, e.subtype);
        EXPECT_EQ(a.position, e.position);
        EXPECT_EQ(a.angles, e.angles);
        EXPECT_EQ(a.light, e.light);
        EXPECT_EQ(a.scale, e.scale);
        EXPECT_EQ(a.life, e.life);
        EXPECT_EQ(a.velocity, e.velocity);
        EXPECT_EQ(a.verticalSpeed, e.verticalSpeed);
        EXPECT_EQ(a.field38, e.field38);
    }
}

// [ebp+0xc] of 0x50ba30 with the first rand() of seed 1 (41): (41 % 30 + 70) · 0.01 in the x87.
float FirstLum()
{
    return static_cast<float>(static_cast<double>(81.0f) * static_cast<double>(0.01f));
}

class HangingModels final : public Client::Models::IModelLibrary
{
public:
    explicit HangingModels(std::set<std::uint16_t> items) : m_items(std::move(items))
    {
        using namespace Client::Models;
        SkeletonBone bone;
        bone.parent = -1;
        // Action 1 has two keys: what hangs in it can advance.
        bone.actions = {{BoneKey{}}, {BoneKey{}, BoneKey{}}};
        m_skeleton.bones.assign(BoneCount, bone);
        for (std::size_t index = 0; index < BoneCount; ++index)
        {
            for (auto& keys : m_skeleton.bones[index].actions)
            {
                for (auto& key : keys)
                    key.position = {0.0f, 0.0f, static_cast<float>(index)};
            }
        }
        m_skeleton.actions = {
            ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false},
            ActionInfo{.repeats = true, .speed = PlayerSecondActionSpeed, .keyCount = 2, .lockPositions = false}};
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "item.jpg";
        m_model.meshes = {mesh};
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId model) const override
    {
        return m_items.contains(model.Value()) ? &m_model : nullptr;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return model.Value() == Player || m_items.contains(model.Value()) ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return m_texture;
    }

private:
    std::set<std::uint16_t> m_items;
    Client::Models::Skeleton m_skeleton;
    Client::Data::Models::Model m_model;
    std::array<Client::Render::TextureSlot, 1> m_texture{Client::Render::TextureSlot{0x300}};
};

class GameHangingTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_batch.Begin(Client::Render::Pass3D{});
        for (std::size_t index = 0; index < BoneCount; ++index)
        {
            Client::Math::BoneTransform bone(1.0f);
            bone[3] = {0.0f, 0.0f, static_cast<float>(index)};
            m_body.push_back(bone);
        }
        m_actor.type = Player;
        m_actor.word376 = 0xffff;
        m_actor.scale = 1.0f;
        m_actor.alpha = 1.0f;
        m_actor.position = {150.0f, 150.0f, 10.0f};
        m_actor.drawnLight = {0.5f, 0.5f, 0.5f};
        m_actor.wings.model = 0xffff;
        m_actor.pet.model = 0xffff;
        for (auto& hand : m_actor.hands)
            hand.model = 0xffff;
        m_actor.hands[0].bone = 0x21;
        m_actor.hands[1].bone = 0x2a;
    }

    GameHangingPass Pass(const Client::Models::IModelLibrary& models)
    {
        return {.batch = m_batch,
                .models = models,
                .body = m_body,
                .random = m_random,
                .carry = &m_carry,
                .sprites = &m_sprites,
                .joints = m_joints,
                .jointHooks = &JointHooks,
                .particles = &m_particles,
                .logicSteps = 1};
    }

    std::vector<Client::Particles::Sprite> LiveSprites()
    {
        std::vector<Client::Particles::Sprite> live;
        for (const auto& sprite : m_sprites.Sprites())
        {
            if (sprite.alive)
                live.push_back(sprite);
        }
        return live;
    }

    ActorState m_actor;
    std::vector<Client::Math::BoneTransform> m_body;
    Client::Render::ModelBatch m_batch;
    Client::Crt::CrtRandom m_random{1};
    Client::Kernel::PoseBuffer m_carry;
    Client::Particles::FrameSprites m_sprites;
    std::vector<Client::Kernel::JointState> m_joints = std::vector<Client::Kernel::JointState>(8);
    Client::Particles::Particles m_particles;
};

// With nothing hanging, the rand() of 0x50db57 remains and, outside the back, the close of +0x38e before each hand.
// Without a buffer or sprites, nothing is needed.
TEST_F(GameHangingTest, WithNothingHangingOnlyTheLightAndTheTwoHandBytesSpendRandoms)
{
    const HangingModels models({});
    GameHangingPass pass = Pass(models);
    pass.carry = nullptr;
    pass.sprites = nullptr;

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
}

// 0x50ddde..0x50de60 with '4' in +0x38e (0x509926): the back weapon uses the wings' part, with the speed of the
// player's action 1, and 0x509340 leaves its previous key at 2 and the frame at 3.2. On returning, 0x50de5a restores
// only the wings' frame: the previous key and action stay (Q-C296).
TEST_F(GameHangingTest, TheBackWeaponLeavesTheWingsWithTheirFrameButItsPreviousKey)
{
    const HangingModels models({Sword});
    m_actor.safeZone = 1;
    m_actor.hands[0].model = Sword;
    m_actor.byte38e = 0x34;
    m_actor.animation.action = 0x30;
    m_actor.animation.frame = 6.0f;
    m_actor.wings.animation = {.action = 0, .frame = 7.5f, .previousAction = 9, .previousKey = 5};

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_actor.wings.bone, 0x2f);
    EXPECT_EQ(m_actor.wings.speed, PlayerSecondActionSpeed);
    EXPECT_EQ(m_actor.wings.animation.frame, 7.5f);
    EXPECT_EQ(m_actor.wings.animation.previousKey, 2);
    EXPECT_EQ(m_actor.wings.animation.previousAction, 9);
    // On the back the hands' loop does not run: that of 0x50db57, +0x38e in 0x509340 and the light of 0x509a92.
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
    ASSERT_EQ(m_batch.Vertices().size(), 3u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(130.0f, 155.0f, 97.0f));
}

// 0x50dcda..0x50dd70 outside the back: the bolts of the left go to the wings' bone with the bows' place (70, 0, 90 and
// −10, 5, 10), and the hands' loop does not draw them again.
TEST_F(GameHangingTest, BoltsInTheLeftHandHangOnTheBackOutsideTheSafeZone)
{
    const HangingModels models({Bolts});
    m_actor.hands[1].model = Bolts;

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 3u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(140.0f, 155.0f, 67.0f));
    // 0x50db57, +0x38e and the light in 0x509340, and +0x38e before each hand.
    EXPECT_EQ(m_random.State(), AfterRandoms(5));
}

// Q-C102: a hand with a model the original does not load (0x1202, type 0xff0 of the lab VM's list) draws nothing and
// does not stop the frame: 0x509340 runs the same on the table's zeroed entry, without actions or meshes. 0x50db57,
// +0x38e before each hand, and the +0x38e and the light (0x509a92) of 0x509340, which does not depend on the model,
// run.
TEST_F(GameHangingTest, AHandWhoseModelNobodyLoadsDrawsNothing)
{
    constexpr std::uint16_t UnloadedHand = 0x1202;
    const HangingModels models({});
    m_actor.hands[0].model = UnloadedHand;

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_TRUE(LiveSprites().empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(5));
}

// 0x50e896..0x50e9ea: the flash of 0x21d at −150 from the hand's bone, with the light of level 7 (0.5, 0.4 and 0.3 of
// [ebp+0xc]). The reds (+0x375 from 6) and +0x1d6 at 4 do not carry it.
TEST_F(GameHangingTest, TheHandSwordFlareUsesTheLevelLight)
{
    const HangingModels models({BladeSword});
    m_actor.hands[0].model = BladeSword;
    m_actor.hands[0].level = 7;

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // With level 7, 0x60b3d0 draws the mesh and its glow pass.
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(150.0f, 150.0f, 43.0f));
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 1u);
    const float lum = FirstLum();
    EXPECT_EQ(sprites[0].texture, Flare);
    EXPECT_EQ(sprites[0].position, glm::vec3(150.0f, 0.0f, 43.0f));
    EXPECT_EQ(sprites[0].scale, 1.4f);
    EXPECT_EQ(sprites[0].light, glm::vec3(lum * 0.5f, lum * 0.4f, lum * 0.3f));
    // 0x50db57, +0x38e of the hand and of 0x509340, the light of 0x509a92 and +0x38e of the left one.
    EXPECT_EQ(m_random.State(), AfterRandoms(5));

    for (const bool red : {true, false})
    {
        SCOPED_TRACE(red);
        m_sprites.Clear();
        m_actor.pk = static_cast<std::uint8_t>(red ? 6 : 0);
        m_actor.level = static_cast<std::uint16_t>(red ? 0 : 4);
        ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models)).HasValue());
        EXPECT_TRUE(LiveSprites().empty());
    }
}

// 0x60b573: with the options' glow detail (0x6c9dcc) at 1, a level 13 sword in the hand draws as a level 7 one: the
// mesh and the 0x44 pass.
TEST_F(GameHangingTest, TheGlowDetailOfTheOptionsTrimsTheLevelOfWhatHangs)
{
    const HangingModels models({BladeSword});
    m_actor.hands[0].model = BladeSword;
    m_actor.hands[0].level = 13;
    for (const std::uint8_t detail : {std::uint8_t{4}, std::uint8_t{1}})
    {
        SCOPED_TRACE(static_cast<int>(detail));
        m_batch.Begin(Client::Render::Pass3D{});
        Client::Actors::Drawing::ItemDrawContext items;
        items.glowDetail = detail;
        GameHangingPass pass = Pass(models);
        pass.itemContext = &items;

        const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_batch.Vertices().size(), detail == 4 ? 12u : 6u);
    }
}

// 0x50f6d6..0x50f74b: Staff08 carries Shiny02 on bone 27 and flashes from 28 to 37 of its own pose, which 0x509340 left
// in the global buffer (0x5092d0): its root is bone 33 of the body. 0x607b60 draws one more rand() for it.
TEST_F(GameHangingTest, TheStaffSpritesComeFromItsPoseInTheGlobalBuffer)
{
    const HangingModels models({Staff08});
    m_actor.hands[0].model = Staff08;

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 11u);
    const float lum = FirstLum();
    const glm::vec3 light{lum * 0.4f, lum * 0.6f, lum};
    for (std::size_t index = 0; index < sprites.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(sprites[index].texture, index == 0 ? Shiny : Flare);
        EXPECT_EQ(sprites[index].scale, index == 0 ? 2.0f : 1.5f);
        EXPECT_EQ(sprites[index].position, glm::vec3(150.0f, 150.0f, static_cast<float>(10 + 33 + 27 + index)));
        EXPECT_EQ(sprites[index].light, light);
    }
    EXPECT_EQ(m_random.State(), AfterRandoms(6));
}

// 0x509f17..0x509ff8: Sword21 walks bones 1 and 2 of its pose (EDI from 0x68b184c to 0x68b18ac) and on each it puts the
// two flare01, the pulsing one and the gray one, at the same point. With the clock at 0, the pulse is 0.65f.
TEST_F(GameHangingTest, Sword21PutsBothFlaresOnEachOfItsTwoBones)
{
    const HangingModels models({Sword21});
    m_actor.hands[0].model = Sword21;

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 4u);
    const auto x = static_cast<double>(0.65f);
    const auto f32 = [](double value) { return static_cast<float>(value); };
    const glm::vec3 beat{f32(x * static_cast<double>(0.43f)), f32(x * static_cast<double>(0.14f)),
                         f32(x * static_cast<double>(0.6f))};
    for (std::size_t index = 0; index < sprites.size(); ++index)
    {
        SCOPED_TRACE(index);
        const bool grey = index % 2 == 1;
        EXPECT_EQ(sprites[index].texture, Flare);
        EXPECT_EQ(sprites[index].position, glm::vec3(150.0f, 150.0f, static_cast<float>(10 + 33 + 1 + index / 2)));
        EXPECT_EQ(sprites[index].scale, grey ? 0.4f : f32(x * static_cast<double>(0.9f)));
        EXPECT_EQ(sprites[index].light, grey ? glm::vec3(0.3f) : beat);
    }
    // 0x50db57, +0x38e of the hand and of 0x509340, the light of 0x509a92 and +0x38e of the left one.
    EXPECT_EQ(m_random.State(), AfterRandoms(5));
}

// 0x50a683..0x50a75f: Mace10 puts a flare01 at (−10, 0, 0) of bones 2 to 5 of its pose (0x68b184c + ESI · 0x30, with
// ESI from 1 to 4) and, with ESI at 3, the Shiny02 on bone 4 with the turn of rand() % 360.
TEST_F(GameHangingTest, Mace10FlaresBones2To5WithTheShinyOnBone4)
{
    const HangingModels models({Mace10});
    m_actor.hands[0].model = Mace10;

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 5u);
    const auto x = static_cast<double>(0.65f);
    const glm::vec3 flareLight{static_cast<float>(x * static_cast<double>(0.6f)),
                               static_cast<float>(x * static_cast<double>(0.8f)), static_cast<float>(x)};
    constexpr std::array<int, 5> Bones{2, 3, 4, 4, 5};
    for (std::size_t index = 0; index < sprites.size(); ++index)
    {
        SCOPED_TRACE(index);
        const bool shiny = index == 3;
        EXPECT_EQ(sprites[index].texture, shiny ? Shiny : Flare);
        EXPECT_EQ(sprites[index].position, glm::vec3(140.0f, 150.0f, static_cast<float>(10 + 33 + Bones[index])));
        EXPECT_EQ(sprites[index].scale, shiny ? 0.6f : 1.0f);
        EXPECT_EQ(sprites[index].light, shiny ? glm::vec3(0.5f) : flareLight);
    }
    // The turn is the fifth rand(): after that of 0x50db57, the two +0x38e and the light of 0x509a92.
    Client::Crt::CrtRandom replay{1};
    for (int call = 0; call < 4; ++call)
        static_cast<void>(replay.Next());
    EXPECT_EQ(sprites[3].rotation, static_cast<float>(replay.Next() % 360));
    EXPECT_EQ(m_random.State(), AfterRandoms(6));
}

// 0x50a005..0x50a138: 0x227 in action 0x8c, outside map 10 and with an even rand(), shifts bone 1 of its pose (rand() %
// 30 − 15 in x and y, +20 in z) and releases four times the 0x617/0 joint (scale 10, no owner) and the white 0x5c5/0
// particle, each pair with the angles (rand() % 60 + 150, 0, the character's z). In map 10 it does not draw the rand().
TEST_F(GameHangingTest, TheAction8cSwordThrowsFourSparksFromItsFirstBone)
{
    constexpr std::uint16_t Sword227 = 0x227;
    // 0x50db57, +0x38e of the hand and of 0x509340 and the light of 0x509a92, before the rand() & 1.
    constexpr std::size_t Before = 4;
    const std::uint32_t seed = SeedWhere(
        [](Client::Crt::CrtRandom& random)
        {
            for (std::size_t call = 0; call < Before; ++call)
                static_cast<void>(random.Next());
            return (random.Next() & 1) == 0;
        });
    const HangingModels models({Sword227});
    m_actor.hands[0].model = Sword227;
    m_actor.animation.action = 0x8c;
    m_actor.angles = {0.0f, 0.0f, 135.0f};
    m_random = Client::Crt::CrtRandom{seed};

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    Client::Crt::CrtRandom draws = Skipped(seed, Before + 1);
    // Bone 1 of the sword, hanging from the hand's 0x21: (150, 150, 10 + 33 + 1).
    glm::vec3 point{150.0f, 150.0f, 44.0f};
    point.x = F32(static_cast<double>(draws.Next() % 30) - 15.0 + D(point.x));
    point.y = F32(static_cast<double>(draws.Next() % 30) - 15.0 + D(point.y));
    point.z = 64.0f;
    std::vector<Client::Kernel::JointState> joints(m_joints.size());
    Client::Particles::Particles particles;
    for (int spark = 0; spark < 4; ++spark)
    {
        glm::vec3 angles{static_cast<float>(draws.Next() % 60 + 150), 0.0f, 135.0f};
        const Client::Kernel::JointRequest request{0x617, point, point, angles, 0, nullptr, 10.0f, -1, 0, 0, -1};
        ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, draws).HasValue());
        ASSERT_TRUE(particles.CreateChecked(0x5c5, point, angles, glm::vec3(1.0f), 0, 1.0f, 0.0f, draws).HasValue());
    }
    static_cast<void>(draws.Next()); // +0x38e of the left one.
    EXPECT_EQ(m_random.State(), draws.State());
    ExpectSameJoints(m_joints, joints);
    ExpectSameParticles(m_particles, particles);
    for (std::size_t k = 0; k < 4; ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_EQ(m_joints[k].type, 0x617);
        EXPECT_EQ(m_joints[k].position, point);
        EXPECT_EQ(m_joints[k].angles.z, 135.0f);
        EXPECT_EQ(m_particles.All()[k].type, 0x5c5);
        EXPECT_EQ(m_particles.All()[k].position, point);
    }
    EXPECT_FALSE(m_joints[4].alive);

    m_random = Client::Crt::CrtRandom{seed};
    GameHangingPass pass = Pass(models);
    pass.map = 10;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_EQ(m_random.State(), Skipped(seed, Before + 1).State());
}

// 0x50af03..0x50b0a0: the bow 0xa27, with an odd rand(), releases the 0x5c6/0xb spark of 0.8 on bones 4 and 12 of its
// pose, with the light (0.8, 0.8, 0.2) and the character's angles; with an even one, it does not. The five flashes go
// the same.
TEST_F(GameHangingTest, TheBowSparksFromBones4And12WithHalfTheRandoms)
{
    constexpr std::uint16_t Bow = 0xa27;
    constexpr std::size_t Before = 4;
    for (const bool sparks : {true, false})
    {
        SCOPED_TRACE(sparks);
        const std::uint32_t seed = SeedWhere(
            [sparks](Client::Crt::CrtRandom& random)
            {
                for (std::size_t call = 0; call < Before; ++call)
                    static_cast<void>(random.Next());
                return ((random.Next() & 1) == 1) == sparks;
            });
        const HangingModels models({Bow});
        m_actor.hands[0].model = Bow;
        m_actor.angles = {0.0f, 0.0f, 60.0f};
        m_random = Client::Crt::CrtRandom{seed};
        m_particles = Client::Particles::Particles{};
        m_sprites.Clear();

        const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));

        ASSERT_TRUE(result.HasValue()) << result.Error();
        Client::Crt::CrtRandom draws = Skipped(seed, Before + 1);
        Client::Particles::Particles particles;
        if (sparks)
        {
            for (const float z : {47.0f, 55.0f})
                ASSERT_TRUE(particles
                                .CreateChecked(0x5c6, {150.0f, 150.0f, z}, m_actor.angles, {0.8f, 0.8f, 0.2f}, 11, 0.8f,
                                               0.0f, draws)
                                .HasValue());
        }
        static_cast<void>(draws.Next()); // +0x38e of the left one.
        EXPECT_EQ(m_random.State(), draws.State());
        ExpectSameParticles(m_particles, particles);
        EXPECT_EQ(LiveSprites().size(), 5u);
    }
}

// 0x50f454..0x50f676: the staff 0xc18 in the hand carries Spark02 and Shiny03 at −145 and then four 0x5c5/1 sparks
// shifted (rand() % 20 − 10, rand() % 20 − 100, rand() % 20 − 10) with the light (l, l · 0.6, l · 0.4) and the
// character's angles, and from 0 to −180 a flash of 1 at (0, y + 60, 0) with three of every four rand() calls, with (l,
// l · 0.2, l · 0.1).
TEST_F(GameHangingTest, TheStaffC18SparksFourTimesAndFlaresAlongTheHand)
{
    constexpr std::uint16_t Staff = 0xc18;
    const HangingModels models({Staff});
    m_actor.hands[0].model = Staff;
    m_actor.angles = {0.0f, 0.0f, 30.0f};

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const float lum = FirstLum();
    const glm::vec3 light{lum, lum * 0.6f, lum * 0.4f};
    // 0x50db57, +0x38e of the hand and of 0x509340 and the light of 0x509a92.
    Client::Crt::CrtRandom draws = Skipped(1, 4);
    Client::Particles::Particles particles;
    for (int spark = 0; spark < 4; ++spark)
    {
        const auto x = static_cast<float>(draws.Next() % 20 - 10);
        const auto y = static_cast<float>(draws.Next() % 20 - 10 - 90);
        const auto z = static_cast<float>(draws.Next() % 20 - 10);
        const glm::vec3 point{150.0f + x, 150.0f + y, 43.0f + z};
        ASSERT_TRUE(particles.CreateChecked(0x5c5, point, m_actor.angles, light, 1, 1.0f, 0.0f, draws).HasValue());
    }
    std::vector<float> flares;
    for (int y = 0; y > -200; y -= 20)
    {
        if ((draws.Next() & 3) < 3)
            flares.push_back(static_cast<float>(y + 60));
    }
    static_cast<void>(draws.Next()); // +0x38e of the left one.
    EXPECT_EQ(m_random.State(), draws.State());
    ExpectSameParticles(m_particles, particles);
    for (std::size_t k = 0; k < 4; ++k)
    {
        EXPECT_EQ(m_particles.All()[k].subtype, 1);
        EXPECT_GE(m_particles.All()[k].life, 8);
        EXPECT_LE(m_particles.All()[k].life, 15);
    }
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 2 + flares.size());
    EXPECT_EQ(sprites[0].texture, 0x5c5);
    EXPECT_EQ(sprites[0].position, glm::vec3(150.0f, 5.0f, 43.0f));
    EXPECT_EQ(sprites[0].scale, 3.0f);
    EXPECT_EQ(sprites[0].light, light);
    EXPECT_EQ(sprites[1].texture, 0x5fe);
    for (std::size_t k = 0; k < flares.size(); ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_EQ(sprites[2 + k].texture, Flare);
        EXPECT_EQ(sprites[2 + k].position, glm::vec3(150.0f, 150.0f + flares[k], 43.0f));
        EXPECT_EQ(sprites[2 + k].scale, 1.0f);
        EXPECT_EQ(sprites[2 + k].light, glm::vec3(lum, lum * 0.2f, lum * 0.1f));
    }
}

// 0x50ed00..0x50ee71: the sword 0x224 in the hand carries Shiny02 at (0, −20, 15) and releases from there three
// 0x614/10 joints without an owner up to (0, −133, 7), with the character's angles and the width v · 20 + 20, with v =
// (sin(clock · 0.004f) + 1) · 0.3f.
TEST_F(GameHangingTest, TheSword224ThrowsThreeBeamsAlongTheBlade)
{
    const HangingModels models({Sword224});
    m_actor.hands[0].model = Sword224;
    m_actor.angles = {0.0f, 0.0f, 90.0f};
    GameHangingPass pass = Pass(models);
    pass.worldClock = 0.0f;

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // With the clock at 0: v = 0.3f and the width 0.3f · 20 + 20.
    const float v = F32(1.0 * D(0.3f));
    const float width = F32(D(v) * 20.0 + 20.0);
    glm::vec3 first{150.0f, 130.0f, 58.0f};
    glm::vec3 second{150.0f, 17.0f, 50.0f};
    glm::vec3 angles = m_actor.angles;
    Client::Crt::CrtRandom draws = Skipped(1, 4);
    std::vector<Client::Kernel::JointState> joints(m_joints.size());
    for (int beam = 0; beam < 3; ++beam)
    {
        const Client::Kernel::JointRequest request{0x614, first, second, angles, 10, nullptr, width, -1, 0, 0, -1};
        ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, draws).HasValue());
    }
    static_cast<void>(draws.Next()); // +0x38e of the left one.
    EXPECT_EQ(m_random.State(), draws.State());
    ExpectSameJoints(m_joints, joints);
    for (std::size_t k = 0; k < 3; ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_EQ(m_joints[k].type, 0x614);
        EXPECT_EQ(m_joints[k].subtype, 10);
        EXPECT_EQ(m_joints[k].width, width);
        EXPECT_EQ(m_joints[k].position, second);
    }
    EXPECT_FALSE(m_joints[3].alive);
    ASSERT_EQ(LiveSprites().size(), 1u);
    EXPECT_EQ(LiveSprites()[0].texture, Shiny);
    EXPECT_EQ(LiveSprites()[0].position, first);
}

// 0x50eb43..0x50ecf0: the sword 0x223. Two 0x610/4 joints from the hand, with the character as owner and its angles,
// without rand() or sprites. With the clock at 0, the width is 20.
TEST_F(GameHangingTest, TheSword223LeavesTwoTrailsOwnedByTheCharacter)
{
    const HangingModels models({Sword223});
    m_actor.hands[0].model = Sword223;
    m_actor.angles = {0.0f, 0.0f, 90.0f};
    GameHangingPass pass = Pass(models);
    pass.worldClock = 0.0f;

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // (0, −20, −40) to (0, −160, −10) and (0, −10, 28) to (0, −145, 18) of bone 0x21: (150 + x, 150 + y, 43 + z).
    const std::array<std::pair<glm::vec3, glm::vec3>, 2> edges{{
        {{150.0f, 130.0f, 3.0f}, {150.0f, -10.0f, 33.0f}},
        {{150.0f, 140.0f, 71.0f}, {150.0f, 5.0f, 61.0f}},
    }};
    glm::vec3 angles = m_actor.angles;
    std::vector<Client::Kernel::JointState> joints(m_joints.size());
    Client::Crt::CrtRandom unused(1);
    for (auto [first, second] : edges)
    {
        const Client::Kernel::JointRequest request{0x610, first, second, angles, 4, &m_actor, 20.0f, -1, 0, 0, -1};
        ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, unused).HasValue());
    }
    EXPECT_EQ(unused.State(), Client::Crt::CrtRandom(1).State());
    EXPECT_EQ(m_random.State(), AfterRandoms(5));
    ExpectSameJoints(m_joints, joints);
    for (std::size_t k = 0; k < edges.size(); ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_EQ(m_joints[k].type, 0x610);
        EXPECT_EQ(m_joints[k].subtype, 4);
        EXPECT_EQ(m_joints[k].width, 20.0f);
        EXPECT_EQ(m_joints[k].ownerContext, &m_actor);
        EXPECT_EQ(m_joints[k].targetPosition, edges[k].second);
    }
    EXPECT_FALSE(m_joints[2].alive);
    EXPECT_TRUE(LiveSprites().empty());
}

// 0x50b411..0x50b763: the staff 0xc1e. Spark03 at (0, p − 170, p) of bone 0 of its pose, which stays in the character's
// +0x40 and where the 0x618/0x11 joint is born with the character as owner; another p with no effect and the scale t; a
// flash, two Shiny02 and the 0x5c6/0xb spark of 2 with the light (1, 0.4, 1); and six Shiny02 on bones 2 and 1 with (a,
// 0.1, a), turned g and g + 90. With the clock at 0, a = 0.5 − 0.1f and g = 0.
TEST_F(GameHangingTest, TheStaffC1eLeavesItsPointOnTheCharacterAndAJointThatFollowsIt)
{
    constexpr std::uint16_t Staff = 0xc1e;
    const HangingModels models({Staff});
    m_actor.hands[0].model = Staff;
    m_actor.angles = {0.0f, 0.0f, 45.0f};

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    Client::Crt::CrtRandom draws = Skipped(1, 4);
    const float p = F32(static_cast<double>(draws.Next() % 60) * D(0.05f) - D(1.5f));
    const float s = F32(static_cast<double>(draws.Next() % 15) * D(0.05f) + D(1.8f));
    // Bone 0 of the staff, hanging from the hand's 0x21: (150, 150, 43).
    glm::vec3 point{150.0f, F32(D(F32(D(p) - 170.0)) + 150.0), F32(D(p) + 43.0)};
    static_cast<void>(draws.Next() % 60);
    const float t = F32(static_cast<double>(draws.Next() % 15) * D(0.05f) + 1.0);
    glm::vec3 angles = m_actor.angles;
    std::vector<Client::Kernel::JointState> joints(m_joints.size());
    const Client::Kernel::JointRequest request{0x618, point, point, angles, 0x11, &m_actor, 30.0f, -1, 0, 0, -1};
    Client::Crt::CrtRandom unused(1);
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, unused).HasValue());
    const glm::vec3 violet{1.0f, 0.4f, 1.0f};
    Client::Particles::Particles particles;
    ASSERT_TRUE(particles.CreateChecked(0x5c6, point, angles, violet, 11, 2.0f, 0.0f, draws).HasValue());
    static_cast<void>(draws.Next()); // +0x38e of the left one.
    EXPECT_EQ(m_random.State(), draws.State());
    EXPECT_EQ(m_actor.effectOrigin, point);
    ExpectSameJoints(m_joints, joints);
    EXPECT_EQ(m_joints[0].type, 0x618);
    EXPECT_EQ(m_joints[0].subtype, 0x11);
    EXPECT_EQ(m_joints[0].position, point);
    EXPECT_EQ(m_joints[0].ownerContext, &m_actor);
    EXPECT_EQ(m_joints[0].life, 12);
    ExpectSameParticles(m_particles, particles);
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 10u);
    EXPECT_EQ(sprites[0].texture, 0x5c6);
    EXPECT_EQ(sprites[0].position, point);
    EXPECT_EQ(sprites[0].scale, s);
    EXPECT_EQ(sprites[0].light, glm::vec3(0.4f));
    EXPECT_EQ(sprites[1].texture, Flare);
    EXPECT_EQ(sprites[1].scale, t);
    EXPECT_EQ(sprites[1].light, violet);
    EXPECT_EQ(sprites[2].texture, Shiny);
    EXPECT_EQ(sprites[2].scale, t);
    EXPECT_EQ(sprites[2].rotation, 0.0f);
    EXPECT_EQ(sprites[3].scale, F32(D(t) - D(0.3f)));
    EXPECT_EQ(sprites[3].rotation, 90.0f);
    const float a = F32(0.5 - D(0.1f));
    const std::array<glm::vec3, 3> shines{glm::vec3{150.0f, 160.0f, 45.0f}, glm::vec3{110.0f, 140.0f, 44.0f},
                                          glm::vec3{-10.0f, 145.0f, 44.0f}};
    const std::array<float, 6> scales{1.5f, 1.2f, 1.0f, 0.7f, 1.2f, 1.0f};
    for (std::size_t k = 0; k < 6; ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_EQ(sprites[4 + k].texture, Shiny);
        EXPECT_EQ(sprites[4 + k].position, shines[k / 2]);
        EXPECT_EQ(sprites[4 + k].scale, scales[k]);
        EXPECT_EQ(sprites[4 + k].light, glm::vec3(a, 0.1f, a));
        EXPECT_EQ(sprites[4 + k].rotation, k % 2 == 0 ? 0.0f : 90.0f);
    }
}

// 0x50df80..0x50e05d: Helper02 hangs from bone 34 at (20, 0, 0) at half a key per frame, and then 0x4ec110 with (20, 0,
// 15) on the same bone carries a red flash of 1.5.
TEST_F(GameHangingTest, Helper02HangsFromBone34WithItsRedFlare)
{
    const HangingModels models({Helper02});
    m_actor.pet.model = Helper02;

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_actor.pet.bone, 0x22);
    EXPECT_EQ(m_actor.pet.speed, 0.5f);
    ASSERT_EQ(m_batch.Vertices().size(), 3u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(170.0f, 150.0f, 44.0f));
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 1u);
    EXPECT_EQ(sprites[0].texture, Flare);
    EXPECT_EQ(sprites[0].position, glm::vec3(170.0f, 150.0f, 59.0f));
    EXPECT_EQ(sprites[0].scale, 1.5f);
    EXPECT_EQ(sprites[0].light, glm::vec3(FirstLum() * 0.5f, 0.0f, 0.0f));
    EXPECT_EQ(m_random.State(), AfterRandoms(5));
}

// 0x50df22: in maps 18 to 23 neither the wings nor the pet hang.
TEST_F(GameHangingTest, TheEventMapsSkipTheWingsAndThePet)
{
    const HangingModels models({Helper02});
    m_actor.pet.model = Helper02;
    GameHangingPass pass = Pass(models);
    pass.map = 20;

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_TRUE(LiveSprites().empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
}

// 0x413fe0, 0x4142f0 and 0x414470: with the CtlCode at 0x20, +0x360 goes to 5 and the new layer draws MuSign on bone 20
// of the body, shifted (70, −5, 0) and turned 45° in z, without rand(): the base pass (per-vertex light) and the
// additive chrome in (1, 0.5, 0). Its pose stays in the global buffer, and the wings are posed afterwards.
TEST_F(GameHangingTest, AGameMasterCarriesTheSignOnBone20)
{
    const HangingModels models({MuSign});
    m_actor.controlCode = 0x20;

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_actor.byte360, 5);
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.action, 0);
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
    // (70, −5, 0) from bone 20 at (0, 0, 20), plus the place (150, 150, 10).
    const glm::vec3 origin{220.0f, 145.0f, 30.0f};
    ASSERT_TRUE(m_carry.models.contains(MuSign));
    EXPECT_EQ(m_carry.models.at(MuSign).position, origin);
    EXPECT_EQ(m_carry.models.at(MuSign).angles, glm::vec3(0.0f, 0.0f, 45.0f));
    const auto vertices = m_batch.Vertices();
    ASSERT_EQ(vertices.size(), 6u);
    for (const std::size_t first : {std::size_t{0}, std::size_t{3}})
    {
        EXPECT_NEAR(vertices[first].position.x, origin.x, 1e-3f);
        EXPECT_NEAR(vertices[first].position.y, origin.y, 1e-3f);
        EXPECT_NEAR(vertices[first].position.z, origin.z, 1e-3f);
        // The mesh's (1, 0, 0), turned 45° in z.
        EXPECT_NEAR(vertices[first + 1].position.x, origin.x + 0.70710677f, 1e-3f);
        EXPECT_NEAR(vertices[first + 1].position.y, origin.y + 0.70710677f, 1e-3f);
    }
    EXPECT_NEAR(vertices[3].color.red, 1.0f, 1e-6f);
    EXPECT_NEAR(vertices[3].color.green, 0.5f, 1e-6f);
    EXPECT_NEAR(vertices[3].color.blue, 0.0f, 1e-6f);
}

// The layer stays in the record: with +0x360 at 5 and without being a GM it is still drawn, and 0x4ec200 advances it
// 0.5 per original frame (between frames, as it was left). Without a layer and with the 5 of an earlier GM, it creates
// it again (Q-C301).
TEST_F(GameHangingTest, TheSignStaysInTheRecordAndAdvancesOnlyOnOriginalFrames)
{
    const HangingModels models({MuSign});
    m_actor.byte360 = 5;
    m_actor.layer = Client::Kernel::GameMasterLayer{.animation = {.action = 1}};
    GameHangingPass pass = Pass(models);
    pass.originalFrame = false;

    ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.frame, 0.0f);

    pass.originalFrame = true;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.action, 1);
    EXPECT_EQ(m_actor.layer->animation.frame, 0.5f);

    m_actor.layer.reset();
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.action, 0);
    EXPECT_EQ(m_actor.byte360, 5);
}

// A GM with a layer of another kind drops it (0x414340 sets +0x360 back to 0) and creates layer 5.
TEST_F(GameHangingTest, AGameMasterReplacesALayerOfAnotherKind)
{
    const HangingModels models({MuSign});
    m_actor.controlCode = 8;
    m_actor.byte360 = 2;
    m_actor.layer = Client::Kernel::GameMasterLayer{.animation = {.action = 1, .frame = 0.75f}};

    ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models)).HasValue());

    EXPECT_EQ(m_actor.byte360, 5);
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.action, 0);
    EXPECT_EQ(m_actor.layer->animation.frame, 0.0f);
}

// 0x414310..0x414330: with +0x360 from 6 to 13, 0x4294b0 (the siege byte, 0x7239ec) decides; with the 0 of the start it
// does not draw the layer, and with the siege it calls the method of +0x364 and emits the sprite. 5 and 14 do not look
// at it.
TEST_F(GameHangingTest, TheSiegeLayersAreLeftAloneWithoutTheSiege)
{
    const HangingModels models({MuSign});
    for (const std::uint8_t kind : {std::uint8_t{6}, std::uint8_t{13}})
    {
        SCOPED_TRACE(static_cast<int>(kind));
        m_sprites.Clear();
        m_actor.byte360 = kind;
        m_actor.layer = Client::Kernel::GameMasterLayer{};
        GameHangingPass pass = Pass(models);
        ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
        EXPECT_TRUE(m_batch.Vertices().empty());

        pass.siegeActive = true;
        const auto drawn = Client::Actors::Drawing::DrawGameHanging(m_actor, pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(LiveSprites().size(), 1u);
        EXPECT_TRUE(m_batch.Vertices().empty());
    }
}

// 0x414470 and 0x5ffdf0 in Chaos Castle: without the hero's +0x1a1 the layer is drawn as in another map; with it, each
// original frame lowers the stored alpha by 0.15f (an intermediate draw does not), draws it with that alpha and, when
// it drops below 0.01, neither poses nor draws it.
TEST_F(GameHangingTest, TheSignFadesInChaosCastleWithTheHerosFlag)
{
    const HangingModels models({MuSign});
    m_actor.byte360 = 5;
    m_actor.layer = Client::Kernel::GameMasterLayer{};
    GameHangingPass pass = Pass(models);
    pass.map = 20;

    ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->alpha, 1.0f);
    EXPECT_EQ(m_batch.Vertices().size(), 6u);

    pass.heroByte1a1 = 1;
    pass.originalFrame = false;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_EQ(m_actor.layer->alpha, 1.0f);
    pass.originalFrame = true;
    float expected = 1.0f;
    for (int frame = 0; frame < 6; ++frame)
    {
        SCOPED_TRACE(frame);
        const std::size_t before = m_batch.Vertices().size();
        ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
        expected = static_cast<float>(static_cast<double>(expected) - static_cast<double>(0.15f));
        EXPECT_EQ(m_actor.layer->alpha, expected);
        EXPECT_GT(m_batch.Vertices().size(), before);
    }
    const std::size_t drawn = m_batch.Vertices().size();
    m_carry.models.clear();
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_LT(m_actor.layer->alpha, 0.01f);
    EXPECT_EQ(m_batch.Vertices().size(), drawn);
    EXPECT_FALSE(m_carry.models.contains(MuSign));
    const float faded = m_actor.layer->alpha;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_EQ(m_actor.layer->alpha, faded);
}

// 0x50de63..0x50defd: in Blood Castle (11 to 17) with +0x360 from 1 to 3, the event's weapon (0xc1c, 0x225 and 0xa24)
// hangs on the back from the wings' slot, with the items' place (0x509340 with 1) but with the wings' speed and without
// bringing the frame back; outside Blood Castle (Lorencia, Devias) nothing hangs.
TEST_F(GameHangingTest, TheBloodCastleWeaponHangsFromTheWingsPlaceOnlyInBloodCastle)
{
    constexpr std::array<std::uint16_t, 3> Weapons{0xc1c, 0x225, 0xa24};
    const ActorState pristine = m_actor;
    for (const std::int32_t map : {0, 2, 11, 14, 17})
    {
        for (std::uint8_t weapon = 1; weapon <= 3; ++weapon)
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", arma " << static_cast<int>(weapon));
            m_actor = pristine;
            m_batch.Begin(Client::Render::Pass3D{});
            m_random = Client::Crt::CrtRandom{1};
            const HangingModels models({Weapons[weapon - 1U]});
            m_actor.safeZone = 1;
            m_actor.byte360 = weapon;
            m_actor.wings.animation = {.action = 0, .frame = 7.5f, .previousAction = 9, .previousKey = 5};
            GameHangingPass pass = Pass(models);
            pass.map = map;

            const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, pass);

            ASSERT_TRUE(result.HasValue()) << result.Error();
            if (map < 11)
            {
                EXPECT_TRUE(m_batch.Vertices().empty());
                EXPECT_EQ(m_actor.wings.animation.frame, 7.5f);
                continue;
            }
            EXPECT_EQ(m_actor.wings.bone, 0x2f);
            EXPECT_EQ(m_actor.wings.speed, 0.25f);
            // The three carry the weapons' chrome (ChromeWeapon): the body and the chrome of mesh 0, the only one of
            // the test's model. 0xc1c and 0x225 with the swords' place from bone 0x2f (like the sword on the back);
            // 0xa24, with the crossbows'.
            ASSERT_EQ(m_batch.Vertices().size(), 6u);
            if (weapon == 3)
                EXPECT_NE(m_batch.Vertices()[0].position, glm::vec3(130.0f, 155.0f, 97.0f));
            else
                EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(130.0f, 155.0f, 97.0f));
        }
    }
}

// 0x50de84 with +0x360 at 5 (a GM): the model is the one left in [ebp-0x18] (0x50dd97), that of the slot the back chose
// even if it did not hang. Outside the safe zone it is the right-hand sword, which hangs twice: on the back and in the
// hand. With the slot empty it stops (Q-C1332).
TEST_F(GameHangingTest, AGameMasterInBloodCastleHangsTheWeaponLeftInTheBackSlot)
{
    {
        const HangingModels models({Sword, MuSign});
        m_actor.hands[0].model = Sword;
        m_actor.byte360 = 5;
        GameHangingPass pass = Pass(models);
        pass.map = 11;

        const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, pass);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_actor.wings.bone, 0x2f);
        EXPECT_GE(m_batch.Vertices().size(), 6u);
    }
    {
        m_actor.hands[0].model = 0xffff;
        m_actor.layer.reset();
        const HangingModels models({MuSign});
        GameHangingPass pass = Pass(models);
        pass.map = 17;

        const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, pass);

        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x50def8"), std::string::npos) << result.Error();
        EXPECT_NE(result.Error().find("Q-C1332"), std::string::npos) << result.Error();
    }
}

// 0x4140d5..0x4142a5: eight variants of the 0x637 atlas, created without randomness and drawn on bone 20.
TEST_F(GameHangingTest, EverySiegeRoleCreatesItsAtlasLayerAndDrawsItsFrame)
{
    const HangingModels models({});
    m_actor.hands[0].model = 0xffff;
    for (std::uint8_t role = 6; role <= 13; ++role)
    {
        m_actor.byte360 = role;
        m_actor.layer.reset();
        m_sprites.Clear();
        GameHangingPass pass = Pass(models);
        pass.map = 30;
        pass.siegeActive = true;
        const auto drawn = Client::Actors::Drawing::DrawGameHanging(m_actor, pass);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        ASSERT_TRUE(m_actor.layer.has_value());
        const auto sprites = LiveSprites();
        ASSERT_EQ(sprites.size(), 1u);
        EXPECT_EQ(sprites.front().texture, 0x637);
        EXPECT_EQ(sprites.front().blend, role - 6);
        EXPECT_EQ(sprites.front().position.x, m_actor.position.x);
        EXPECT_EQ(sprites.front().position.y, m_actor.position.y);
        EXPECT_EQ(sprites.front().position.z, 30.0f);
        EXPECT_EQ(sprites.front().light, glm::vec3(1.0f));
    }
}

// 0x41489c..0x41494d: only a master (0x80), with an empty alliance or one equal to the guild, uses the leader's frame.
TEST_F(GameHangingTest, AUnionLeaderUsesTheAdditionalAtlasFrame)
{
    const HangingModels models({});
    auto guild = std::make_unique<Client::Guild::GuildState>();
    m_actor.guildStatus = 0x80;
    m_actor.word1f2 = 0;
    guild->table[0].guildName = {'P', 'i', 'n', 'o', 's'};
    for (const std::uint8_t role : std::array<std::uint8_t, 4>{6, 7, 8, 12})
    {
        for (const bool sameName : {false, true})
        {
            guild->table[0].unionName = sameName ? guild->table[0].guildName : std::array<char, 9>{};
            m_actor.byte360 = role;
            m_actor.layer.reset();
            m_sprites.Clear();
            GameHangingPass pass = Pass(models);
            pass.siegeActive = true;
            pass.guild = guild.get();
            ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
            const auto sprites = LiveSprites();
            ASSERT_EQ(sprites.size(), 1u);
            EXPECT_EQ(sprites.front().blend, role - 6 + (role == 12 ? 1 : 3));
        }
    }
    guild->table[0].unionName = {'R', 'o', 'b', 'l', 'e', 's'};
    m_actor.layer.reset();
    m_actor.byte360 = 6;
    m_sprites.Clear();
    GameHangingPass pass = Pass(models);
    pass.siegeActive = true;
    pass.guild = guild.get();
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
    ASSERT_EQ(LiveSprites().size(), 1u);
    EXPECT_EQ(LiveSprites().front().blend, 0);
}

// Without MuSign, the GM stops at 0x414470 after what is already drawn; the siege layers do not need that BMD.
TEST_F(GameHangingTest, AMissingGameMasterModelStopsWithItsAddress)
{
    {
        const HangingModels models({});
        m_actor.hands[0].model = 0xffff;
        constexpr std::string_view Name = "xwebzenx";
        for (std::size_t index = 0; index < Name.size(); ++index)
            m_actor.name[index] = static_cast<std::byte>(Name[index]);
        const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x414470"), std::string::npos) << result.Error();
        EXPECT_EQ(m_actor.byte360, 5);
        m_actor.name = {};
    }
    {
        const HangingModels models({MuSign});
        m_actor.layer.reset();
        m_actor.byte360 = 6;
        const auto other = Client::Actors::Drawing::DrawGameHanging(m_actor, Pass(models));
        ASSERT_TRUE(other.HasValue()) << other.Error();
        EXPECT_TRUE(m_actor.layer.has_value());
        m_actor.byte360 = 0;
    }
}

// 0x50dcc2: what is not a 0x20a jumps to 0x50e060, without a GM's layer or the event's weapon; the rand() of 0x50db57
// and the closes of +0x38e of the two hands remain.
TEST_F(GameHangingTest, AMonsterSkipsTheBackTheLayerAndTheEventWeapon)
{
    const HangingModels models({MuSign});
    m_actor.type = 0x20b;
    constexpr std::string_view Name = "xwebzenx";
    for (std::size_t index = 0; index < Name.size(); ++index)
        m_actor.name[index] = static_cast<std::byte>(Name[index]);
    GameHangingPass pass = Pass(models);
    pass.map = 11;

    const auto result = Client::Actors::Drawing::DrawGameHanging(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FALSE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.byte360, 0);
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
}

// Between original frames (0x509340 with the animation as it was left), what hangs does not advance.
TEST_F(GameHangingTest, BetweenOriginalFramesTheHangingAnimationDoesNotAdvance)
{
    const HangingModels models({Helper02});
    m_actor.pet.model = Helper02;
    m_actor.pet.animation = {.action = 1, .frame = 0.25f, .previousAction = 0, .previousKey = 0};
    GameHangingPass pass = Pass(models);
    pass.originalFrame = false;

    ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_EQ(m_actor.pet.animation.frame, 0.25f);

    pass.originalFrame = true;
    ASSERT_TRUE(Client::Actors::Drawing::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_EQ(m_actor.pet.animation.frame, 0.75f);
}
} // namespace
