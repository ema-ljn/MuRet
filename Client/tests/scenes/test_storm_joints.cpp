#include "Math/X87.h"
#include "Models/IModelLibrary.h"
#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Actors/ActorPickingBox.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/WorldState.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Effects/SkinShellEffects.h"
#include "Particles/Sprites.h"
#include "Audio/SoundId.h"

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The 0x632 (StormJoints.cpp): the /7 and its three /6 (0x534b27, 0x534a16 and the step 0x53bec2), the subtypes above
// 7 and the siege pulse's /5 (0x534bcb and the step 0x53c0a5).
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::JointState;

constexpr std::uint32_t Seed = 0x632u;

struct Storm
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype)
    {
        glm::vec3 from{1000.0f, 1000.0f, 0.0f};
        glm::vec3 to = from;
        glm::vec3 angles{0.0f, 0.0f, 0.0f};
        const Client::Kernel::JointRequest request{.type = 0x632,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = nullptr,
                                                   .scale = 40.0f,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(
            joints, random, nullptr, &particles, 0.0f, nullptr, nullptr, [this](Client::Audio::SoundId sound)
            { sounds.push_back(sound.Value()); }, [this](float, float) { return ground; });
    }

    std::vector<JointState> joints = std::vector<JointState>(8);
    CrtRandom random{Seed};
    Client::Particles::Particles particles;
    double ground = -1000.0;
    std::vector<std::int32_t> sounds;
};

// 0x53bf7f..0x53c0a0: from the tenth step, with z above the ground (0x5f93c0), width 40, the crossed tail (0x535020
// with the step's matrix) and, with life under half the volley, the light · 0.7692308f.
TEST(StormJointsTest, TheBoltFallsUntilItTouchesTheGround)
{
    Storm storm;
    ASSERT_TRUE(storm.Create(7).HasValue());
    for (std::size_t slot : {0u, 2u, 3u})
        storm.joints[slot].alive = false;
    JointState& bolt = storm.joints[1];
    ASSERT_EQ(bolt.subtype, 6);
    ASSERT_EQ(bolt.volley, 0x19);
    bolt.life = 12;
    const glm::vec3 light = bolt.light;
    CrtRandom expected = storm.random;

    ASSERT_TRUE(storm.Step().HasValue());

    static_cast<void>(expected.Next()); // the y shake
    EXPECT_EQ(storm.random.State(), expected.State());
    EXPECT_EQ(bolt.width, 40.0f);
    EXPECT_EQ(bolt.count, 1);
    EXPECT_EQ(bolt.light, light); // 12 is not less than 0x19 / 2.
    EXPECT_EQ(bolt.life, 11);

    ASSERT_TRUE(storm.Step().HasValue());

    const auto faded = [](float value)
    { return static_cast<float>(static_cast<double>(value) * static_cast<double>(std::bit_cast<float>(0x3f44ec4fu))); };
    EXPECT_EQ(bolt.light, glm::vec3(faded(light.x), faded(light.y), faded(light.z)));
    EXPECT_EQ(bolt.count, 2);
}

// 0x53bfa9..0x53c04f: on the ground (the terrain's z, 1234.567 as a float, not below the joint's), before the tail, the
// joint 0x614/4 from its position with its angles, of scale 60 and without an owner; with the velocity's z at 0 or
// less, FSIN(velocity) · 20; z on the ground + 5 and the particle 0x6ac/2 of scale 3 at a copy 30 higher, with its
// light. The bits come from a separate computation (the sine exact to 64 bits); the joint and the particle are
// repeated with twins that draw the rand() from the same CrtRandom. With the velocity's z positive, it does not touch
// it.
TEST(StormJointsTest, TheBoltHitsTheGround)
{
    for (const float rise : {-3.0f, 50.0f})
    {
        SCOPED_TRACE(rise);
        auto storm = std::make_unique<Storm>();
        ASSERT_TRUE(storm->Create(7).HasValue());
        for (std::size_t slot : {0u, 2u, 3u})
            storm->joints[slot].alive = false;
        JointState& bolt = storm->joints[1];
        bolt.life = 12;
        bolt.speed = 2.5f;
        bolt.velocity.z = rise;
        auto above = std::make_unique<Storm>(*storm);
        storm->ground = static_cast<double>(1234.567f);
        CrtRandom replay = storm->random;

        ASSERT_TRUE(storm->Step().HasValue());
        ASSERT_TRUE(above->Step().HasValue());

        const glm::vec3 moved = above->joints[1].position;
        EXPECT_EQ(bolt.position.x, moved.x);
        EXPECT_EQ(bolt.position.y, moved.y);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(bolt.position.z), 0x449af225u);
        if (rise < 0.0f)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(bolt.velocity.z), 0x413f82d7u);
        }
        else
        {
            EXPECT_EQ(bolt.velocity.z, 40.0f);
        }
        EXPECT_EQ(bolt.count, above->joints[1].count);
        EXPECT_EQ(bolt.life, 11);

        static_cast<void>(replay.Next()); // the y shake
        std::vector<JointState> twinJoints(1);
        glm::vec3 from = moved;
        glm::vec3 angles = bolt.angles;
        const Client::Kernel::JointRequest request{.type = 0x614,
                                                   .firstPosition = from,
                                                   .secondPosition = from,
                                                   .angles = angles,
                                                   .subtype = 4,
                                                   .ownerContext = nullptr,
                                                   .scale = 60.0f,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
        ASSERT_TRUE(Client::Joints::CreateFaunaJoint(twinJoints, request, replay).HasValue());
        const JointState& spark = storm->joints[0];
        ASSERT_TRUE(spark.alive);
        EXPECT_EQ(spark.type, 0x614);
        EXPECT_EQ(spark.subtype, 4);
        EXPECT_EQ(spark.ownerContext, nullptr);
        EXPECT_EQ(spark.position, twinJoints[0].position);
        EXPECT_EQ(spark.angles, twinJoints[0].angles);
        EXPECT_EQ(spark.width, twinJoints[0].width);
        EXPECT_EQ(spark.speed, twinJoints[0].speed);
        EXPECT_EQ(spark.life, twinJoints[0].life);

        auto twin = std::make_unique<Client::Particles::Particles>();
        const glm::vec3 flame{moved.x, moved.y, std::bit_cast<float>(0x449eb225u)};
        ASSERT_TRUE(twin->CreateChecked(0x6ac, flame, bolt.angles, bolt.light, 2, 3.0f, 0.0f, replay).HasValue());
        const auto& made = storm->particles.All()[0];
        const auto& expected = twin->All()[0];
        ASSERT_TRUE(made.alive);
        EXPECT_EQ(made.type, 0x6ac);
        EXPECT_EQ(made.subtype, 2);
        EXPECT_EQ(made.position, expected.position);
        EXPECT_EQ(made.scale, expected.scale);
        EXPECT_EQ(made.light, expected.light);
        EXPECT_EQ(made.life, expected.life);
        EXPECT_EQ(made.velocity, expected.velocity);
        EXPECT_EQ(storm->random.State(), replay.State());
    }
}

// 0x53bf3c..0x53bf76: in the volley's first ten steps, the blaze 0x5ee/0 (0x53bf6e) at +0x24, with the angles (+0x30),
// the light (+0x48) and scale 3, without rand(), and the sound 0x40 of its creation (0x542aa7); width 100. With the
// table full, neither the blaze nor the sound.
TEST(StormJointsTest, TheBoltBlazesInItsFirstTenSteps)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        Storm storm;
        ASSERT_TRUE(storm.Create(7).HasValue());
        for (std::size_t slot : {0u, 2u, 3u})
            storm.joints[slot].alive = false;
        JointState& bolt = storm.joints[1];
        ASSERT_EQ(bolt.subtype, 6);
        bolt.life = 20;
        if (full)
        {
            for (auto& particle : storm.particles.All())
                particle.alive = true;
        }
        const glm::vec3 point = bolt.pathIncrement;
        const glm::vec3 angles = bolt.angles;
        const glm::vec3 light = bolt.light;
        CrtRandom expected = storm.random;

        ASSERT_TRUE(storm.Step().HasValue());

        static_cast<void>(expected.Next()); // the y shake
        EXPECT_EQ(storm.random.State(), expected.State());
        EXPECT_EQ(bolt.width, 100.0f);
        if (full)
        {
            EXPECT_TRUE(storm.sounds.empty());
            continue;
        }
        EXPECT_EQ(storm.sounds, std::vector<std::int32_t>{0x40});
        const auto& all = storm.particles.All();
        const auto blaze = std::ranges::find_if(all, [](const Client::Particles::Particle& particle)
                                                { return particle.alive && particle.type == 0x5ee; });
        ASSERT_NE(blaze, all.end());
        EXPECT_EQ(blaze->subtype, 0);
        EXPECT_EQ(blaze->position, point);
        EXPECT_EQ(blaze->angles, angles);
        EXPECT_EQ(blaze->light, light);
        EXPECT_EQ(blaze->scale, 3.0f);
        EXPECT_EQ(blaze->life, 12);
    }
}

// 0x534b1e and 0x53bec5: those above 7 only do the first segment and the cap of 50 points, and their step goes to the
// tail.
TEST(StormJointsTest, TheSubtypesAboveSevenOnlyAge)
{
    Storm storm;
    storm.joints[0].maximumCount = 70;
    const auto created = storm.Create(8);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    JointState& joint = storm.joints[0];
    EXPECT_EQ(joint.maximumCount, 50);
    const std::int32_t life = joint.life;

    ASSERT_TRUE(storm.Step().HasValue());

    EXPECT_EQ(joint.life, life - 1);
    EXPECT_EQ(storm.random.State(), CrtRandom(Seed).State());
}
} // namespace

namespace
{
using Client::Math::D;
using Client::Math::F32;

// The /5 with an owner effect (which it does not read), the pulse's angles (90, 0, 0) and the ground height from
// `ground`.
struct FallingStar
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create()
    {
        glm::vec3 from = start;
        glm::vec3 turn = angles;
        const Client::Kernel::JointRequest request{.type = 0x632,
                                                   .firstPosition = from,
                                                   .secondPosition = from,
                                                   .angles = turn,
                                                   .subtype = 5,
                                                   .ownerContext = &owner,
                                                   .scale = 110.0f,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random, Client::Kernel::JointOwnerKind::ModelEffect);
    }

    Client::Core::Expected<void, std::string> Step(const Client::Models::IModelLibrary* models = nullptr,
                                                   Client::Kernel::WorldState* world = nullptr)
    {
        return Client::Joints::StepJoints(
            joints, random, nullptr, nullptr, 0.0f, sprites.get(), nullptr, {}, [this](float, float) { return ground; },
            {}, {}, effects.get(), nullptr, world, nullptr, models);
    }

    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
    std::unique_ptr<Client::Particles::FrameSprites> sprites = std::make_unique<Client::Particles::FrameSprites>();
    std::unique_ptr<Client::Effects::ModelEffects> effects = std::make_unique<Client::Effects::ModelEffects>();
    Client::Effects::ModelEffect owner{};
    glm::vec3 start{1000.0f, 2000.0f, 1500.0f};
    glm::vec3 angles{90.0f, 0.0f, 0.0f};
    double ground = 200.0;
};
} // namespace

// 0x534bcb..0x534cb6 with the /5: 15 points, life 40, the width, +0x9f1 at 2 (after storing rand() & 1), the key at 0,
// +0x9ec at 150, texture 0x60f, speed rand() % 20 + 10 and +0x3c with the angles. Two rand(); the owner effect stays in
// +0x54. With the pool full, nothing, not even rand().
TEST(StormJointsTest, TheFallingStarIsBornWithItsSpeed)
{
    FallingStar star;
    ASSERT_TRUE(star.Create().HasValue());
    CrtRandom expected{Seed};
    static_cast<void>(expected.Next());
    const int speed = expected.Next() % 20;
    EXPECT_EQ(star.random.State(), expected.State());
    const JointState& joint = star.joints[0];
    EXPECT_EQ(joint.texture, 0x60f);
    EXPECT_EQ(joint.width, 110.0f);
    EXPECT_EQ(joint.maximumCount, 15);
    EXPECT_EQ(joint.life, 40);
    EXPECT_EQ(joint.speed, static_cast<float>(speed + 10));
    EXPECT_EQ(joint.orientation, 2);
    EXPECT_EQ(joint.key, 0);
    EXPECT_EQ(joint.field9ec, 150);
    EXPECT_EQ(joint.bendAngles, star.angles);
    EXPECT_EQ(joint.position, star.start);
    EXPECT_EQ(joint.ownerContext, &star.owner);
    EXPECT_TRUE(joint.tailEnabled);

    for (auto& other : star.joints)
        other.alive = true;
    const CrtRandom before = star.random;
    const auto full = star.Create();
    ASSERT_TRUE(full.HasValue());
    EXPECT_FALSE(full.Value().has_value());
    EXPECT_EQ(star.random.State(), before.State());
}

// 0x53590e and 0x53c0c9..0x53c38b: in the air it advances with the velocity turned by the angles (the common
// movement), speeds up 10, adds the tail, speeds up another 10 and advances again turned by +0x3c, which are not the
// angles; the sprite 0x60f of scale 2.5 with the light and rand() % 360. The common tail adds another segment.
TEST(StormJointsTest, TheFallingStarSpeedsUpInTheAir)
{
    FallingStar star;
    ASSERT_TRUE(star.Create().HasValue());
    JointState& joint = star.joints[0];
    const glm::vec3 heading{30.0f, 0.0f, 45.0f};
    joint.angles = heading;
    const float speed = joint.speed;
    CrtRandom expected = star.random;

    ASSERT_TRUE(star.Step().HasValue());

    const auto first = Client::Math::OriginalVectorRotate({0, -speed, 0}, Client::Math::OriginalAngleMatrix(heading));
    const auto matrix = Client::Math::OriginalAngleMatrix(star.angles);
    const glm::vec3 moved{F32(D(star.start.x) + D(first.x)), F32(D(star.start.y) + D(first.y)),
                          F32(D(star.start.z) + D(first.z))};
    const float faster = F32(D(F32(D(speed) + 10.0)) + 10.0);
    const auto second = Client::Math::OriginalVectorRotate({0, -faster, 0}, matrix);
    EXPECT_EQ(joint.position,
              glm::vec3(F32(D(second.x) + D(moved.x)), F32(D(second.y) + D(moved.y)), F32(D(second.z) + D(moved.z))));
    EXPECT_EQ(joint.speed, faster);
    EXPECT_EQ(joint.count, 2);
    EXPECT_EQ(joint.life, 39);
    const auto rotation = static_cast<float>(expected.Next() % 360);
    EXPECT_EQ(star.random.State(), expected.State());
    const auto sprites = star.sprites->Sprites();
    const auto live = std::ranges::find_if(sprites, [](const auto& sprite) { return sprite.alive; });
    ASSERT_NE(live, sprites.end());
    EXPECT_EQ(live->texture, 0x60f);
    EXPECT_EQ(live->scale, 2.5f);
    EXPECT_EQ(live->rotation, rotation);
    EXPECT_EQ(live->light, glm::vec3(1.0f));
    EXPECT_EQ(live->position, joint.position);
}

// 0x53c16d..0x53c2e0: below the ground height minus 150, z stays there; the first time (the key at 0) it drops the wave
// 0x6eb/4 at the position, with the angles and the light (1; 0.8f; 0.3f) (0x53c295, with its scale's rand() if there
// is room); the key always becomes 1, the speed −20 and the light · 0.8333333f, and the sprite comes out the same.
TEST(StormJointsTest, TheFallingStarBouncesOnTheGround)
{
    for (const std::int16_t key : {std::int16_t{0}, std::int16_t{1}})
    {
        SCOPED_TRACE(key);
        FallingStar star;
        star.ground = 5000.0;
        ASSERT_TRUE(star.Create().HasValue());
        JointState& joint = star.joints[0];
        joint.key = key;
        const CrtRandom before = star.random;

        const auto stepped = star.Step();

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(joint.position.z, 4850.0f);
        EXPECT_EQ(joint.key, 1);
        EXPECT_EQ(joint.speed, -20.0f);
        const float faded = F32(1.0 * D(std::bit_cast<float>(0x3f555555u)));
        EXPECT_EQ(joint.light, glm::vec3(faded));
        CrtRandom expected = before;
        const auto waves =
            std::ranges::count_if(star.effects->Effects(), [](const auto& effect) { return effect.alive; });
        if (key == 0)
        {
            ASSERT_EQ(waves, 1);
            const Client::Effects::ModelEffect& wave = star.effects->Effects()[0];
            EXPECT_EQ(wave.type, Client::Effects::ShockWaveEffect);
            EXPECT_EQ(wave.subtype, 4);
            EXPECT_EQ(wave.transform.position, joint.position);
            EXPECT_EQ(wave.transform.angles, joint.angles);
            EXPECT_EQ(wave.life, 10);
            EXPECT_EQ(wave.light,
                      glm::vec3(1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3e99999au)));
            EXPECT_EQ(wave.transform.scale,
                      F32((static_cast<double>(expected.Next() % 6) + 6.0) * D(std::bit_cast<float>(0x3dcccccdu))));
        }
        else
            EXPECT_EQ(waves, 0);
        static_cast<void>(expected.Next());
        EXPECT_EQ(star.random.State(), expected.State());
    }
}

// 0x53c190..0x53c19b: with the tail off (+0x9f2), the key at 0 does not drop the wave (nor its rand()); the bounce
// stays the same.
TEST(StormJointsTest, TheFallingStarWithoutItsTailBouncesWithoutAWave)
{
    FallingStar star;
    star.ground = 5000.0;
    ASSERT_TRUE(star.Create().HasValue());
    JointState& joint = star.joints[0];
    joint.tailEnabled = false;
    joint.key = 0;
    CrtRandom expected = star.random;

    const auto stepped = star.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(joint.position.z, 4850.0f);
    EXPECT_EQ(joint.key, 1);
    EXPECT_EQ(joint.speed, -20.0f);
    EXPECT_EQ(std::ranges::count_if(star.effects->Effects(), [](const auto& effect) { return effect.alive; }), 0);
    static_cast<void>(expected.Next());
    EXPECT_EQ(star.random.State(), expected.State());
}

// 0x53c295: without the effect table, the wave stops; with the table full, there is no wave nor its rand().
TEST(StormJointsTest, TheFallingStarWaveNeedsTheEffectTable)
{
    FallingStar tableless;
    tableless.ground = 5000.0;
    tableless.effects.reset();
    ASSERT_TRUE(tableless.Create().HasValue());
    const auto cut = tableless.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x53c295"), std::string::npos) << cut.Error();

    FallingStar full;
    full.ground = 5000.0;
    for (Client::Effects::ModelEffect& effect : full.effects->Effects())
        effect.alive = true;
    ASSERT_TRUE(full.Create().HasValue());
    const CrtRandom before = full.random;
    ASSERT_TRUE(full.Step().HasValue());
    CrtRandom expected = before;
    static_cast<void>(expected.Next());
    EXPECT_EQ(full.random.State(), expected.State());
    EXPECT_EQ(full.joints[0].key, 1);
}

namespace
{
// The /0..4 with the ground height from `ground`. The /0 and the /1 have the target character as owner (0x4fe235,
// 0x4fe4e7) and the /4 the effect 0x13a (0x51e01a).
struct Falling
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype, void* owner,
                                                                           Client::Kernel::JointOwnerKind kind,
                                                                           float width = 120.0f, std::int32_t key = -1)
    {
        glm::vec3 from = start;
        glm::vec3 turn = angles;
        const Client::Kernel::JointRequest request{.type = 0x632,
                                                   .firstPosition = from,
                                                   .secondPosition = from,
                                                   .angles = turn,
                                                   .subtype = subtype,
                                                   .ownerContext = owner,
                                                   .scale = width,
                                                   .key = key,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random, kind);
    }

    Client::Core::Expected<void, std::string> Step(const Client::Models::IModelLibrary* models = nullptr,
                                                   Client::Kernel::WorldState* world = nullptr)
    {
        return Client::Joints::StepJoints(
            joints, random, nullptr, nullptr, 0.0f, sprites.get(), nullptr, {}, [this](float, float) { return ground; },
            {}, {}, effects.get(), nullptr, world, nullptr, models);
    }

    const Client::Particles::Sprite* LiveSprite() const
    {
        const auto all = sprites->Sprites();
        const auto live = std::ranges::find_if(all, [](const auto& sprite) { return sprite.alive; });
        return live == all.end() ? nullptr : &*live;
    }

    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
    std::unique_ptr<Client::Particles::FrameSprites> sprites = std::make_unique<Client::Particles::FrameSprites>();
    std::unique_ptr<Client::Effects::ModelEffects> effects = std::make_unique<Client::Effects::ModelEffects>();
    Client::Kernel::ActorState target{};
    glm::vec3 start{1500.0f, 1600.0f, 1220.0f};
    glm::vec3 angles{0.0f, 0.0f, 135.0f};
    double ground = 20.0;
};

// The test 0x20b (Player\Shadow01): a mesh of three vertices of bone 0 and 30 triangles (90 corners), a parentless bone
// with its key at the origin and an action of one key. The other models are absent.
class ShadowModels final : public Client::Models::IModelLibrary
{
public:
    ShadowModels()
    {
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {1.0f, 2.0f, 3.0f}}, {0, {-4.0f, 5.0f, 6.0f}}, {0, {7.0f, -8.0f, 9.0f}}};
        mesh.triangles.assign(30, {3, {0, 1, 2, 0}});
        model.meshes.push_back(mesh);
        Client::Models::SkeletonBone bone;
        bone.actions = {{Client::Models::BoneKey{}}};
        skeleton.bones = {bone};
        skeleton.actions = {Client::Models::ActionInfo{.keyCount = 1}};
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId type) const override
    {
        return type.Value() == Client::Tables::PlayerShadowModel ? &model : nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId type) const override
    {
        return type.Value() == Client::Tables::PlayerShadowModel ? &skeleton : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    Client::Data::Models::Model model;
    Client::Models::Skeleton skeleton;
};

// Without the 0x20b: neither model nor skeleton.
class NoModels final : public Client::Models::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
};
} // namespace

// 0x534bcb..0x534cb6 with the /0..3: 10 points, life 40, the width, +0x9f1 at rand() & 1, the key at 0, speed 70 and
// +0x9ec at 150 (the /2 and the /3, 30 and 300 with texture 0x62c and 0x62b; the /0 and the /1 keep the 0x632) and the
// angles and +0x3c at (90, 0, 0). One rand(); the owner stays in +0x54.
TEST(StormJointsTest, TheFallingSubtypesAreBornUpright)
{
    struct Case
    {
        std::int32_t subtype;
        std::uint16_t texture;
        float speed;
        std::int32_t below;
    };
    for (const Case& probe : {Case{0, 0x632, 70.0f, 150}, Case{1, 0x632, 70.0f, 150}, Case{2, 0x62c, 30.0f, 300},
                              Case{3, 0x62b, 30.0f, 300}})
    {
        SCOPED_TRACE(probe.subtype);
        Falling falling;
        ASSERT_TRUE(falling.Create(probe.subtype, &falling.target, Client::Kernel::JointOwnerKind::Actor).HasValue());
        const JointState& joint = falling.joints[0];
        CrtRandom expected{Seed};
        EXPECT_EQ(joint.orientation, expected.Next() & 1);
        EXPECT_EQ(falling.random.State(), expected.State());
        EXPECT_EQ(joint.texture, probe.texture);
        EXPECT_EQ(joint.width, 120.0f);
        EXPECT_EQ(joint.maximumCount, 10);
        EXPECT_EQ(joint.life, 40);
        EXPECT_EQ(joint.speed, probe.speed);
        EXPECT_EQ(joint.field9ec, probe.below);
        EXPECT_EQ(joint.key, 0);
        EXPECT_EQ(joint.angles, glm::vec3(90.0f, 0.0f, 0.0f));
        EXPECT_EQ(joint.bendAngles, glm::vec3(90.0f, 0.0f, 0.0f));
        EXPECT_EQ(joint.position, falling.start);
        EXPECT_EQ(joint.light, glm::vec3(1.0f));
        EXPECT_EQ(joint.ownerContext, &falling.target);
        EXPECT_TRUE(joint.tailEnabled);
    }
}

// 0x53c0a5..0x53c38b in the air: the /2 turns z by rand() % 10 + 10; all speed up 10, add the tail, speed up another 10
// and go down with +0x3c (90, 0, 0); the sprite with rand() % 360: the 0x60f of scale 1 with the light for the /0 and
// the /1 and the 0x5fe of 1.6f with (0; 1; 1) for the /2 and the /3. The common tail adds another segment.
TEST(StormJointsTest, TheFallingSubtypesSpeedUpInTheAir)
{
    for (const std::int32_t subtype : {0, 1, 2, 3})
    {
        SCOPED_TRACE(subtype);
        Falling falling;
        ASSERT_TRUE(falling.Create(subtype, &falling.target, Client::Kernel::JointOwnerKind::Actor).HasValue());
        JointState& joint = falling.joints[0];
        const float speed = joint.speed;
        CrtRandom expected = falling.random;

        ASSERT_TRUE(falling.Step().HasValue());

        const auto matrix = Client::Math::OriginalAngleMatrix({90.0f, 0.0f, 0.0f});
        const auto first = Client::Math::OriginalVectorRotate({0, -speed, 0}, matrix);
        const glm::vec3 moved{F32(D(falling.start.x) + D(first.x)), F32(D(falling.start.y) + D(first.y)),
                              F32(D(falling.start.z) + D(first.z))};
        float z = 0.0f;
        if (subtype == 2)
            z = F32((static_cast<double>(expected.Next() % 10) + 0.0) + 10.0);
        const float faster = F32(D(F32(D(speed) + 10.0)) + 10.0);
        const auto second = Client::Math::OriginalVectorRotate({0, -faster, 0}, matrix);
        EXPECT_EQ(joint.position, glm::vec3(F32(D(second.x) + D(moved.x)), F32(D(second.y) + D(moved.y)),
                                            F32(D(second.z) + D(moved.z))));
        EXPECT_EQ(joint.angles.z, z);
        EXPECT_EQ(joint.speed, faster);
        EXPECT_EQ(joint.count, 2);
        EXPECT_EQ(joint.life, 39);
        EXPECT_TRUE(joint.tailEnabled);
        const auto rotation = static_cast<float>(expected.Next() % 360);
        EXPECT_EQ(falling.random.State(), expected.State());
        const Client::Particles::Sprite* sprite = falling.LiveSprite();
        ASSERT_NE(sprite, nullptr);
        EXPECT_EQ(sprite->texture, subtype < 2 ? 0x60f : 0x5fe);
        EXPECT_EQ(sprite->scale, subtype < 2 ? 1.0f : std::bit_cast<float>(0x3fcccccdu));
        EXPECT_EQ(sprite->light, subtype < 2 ? glm::vec3(1.0f) : glm::vec3(0.0f, 1.0f, 1.0f));
        EXPECT_EQ(sprite->rotation, rotation);
        EXPECT_EQ(sprite->position, joint.position);
    }
}

// 0x53c16d..0x53c2e0 on the ground (the ground height minus +0x9ec above z): z stays there; the /2 and the /3, with the
// tail and the key at 0, drop the wave 0x6eb/4 with the light (0.3f; 0.8f; 1) (with its scale's rand()); all turn off
// the tail (the key stays) and the light becomes · 0.8333333f; the sprite comes out the same. The /0 and the /1 without
// an owner drop nothing; with an owner, without the models, the world or the effect table, MuRet stops at 0x53c1c6.
TEST(StormJointsTest, TheFallingSubtypesLandWithTheirWaveOrTheirVertices)
{
    for (const std::int32_t subtype : {0, 1, 2, 3})
    {
        SCOPED_TRACE(subtype);
        Falling falling;
        falling.ground = 5000.0;
        ASSERT_TRUE(falling.Create(subtype, nullptr, Client::Kernel::JointOwnerKind::ByType).HasValue());
        JointState& joint = falling.joints[0];
        CrtRandom expected = falling.random;

        const auto stepped = falling.Step();

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(joint.position.z, F32(5000.0 - (subtype < 2 ? 150.0 : 300.0)));
        EXPECT_FALSE(joint.tailEnabled);
        EXPECT_EQ(joint.key, 0);
        EXPECT_EQ(joint.light, glm::vec3(F32(1.0 * D(std::bit_cast<float>(0x3f555555u)))));
        if (subtype == 2)
            static_cast<void>(expected.Next());
        const auto waves =
            std::ranges::count_if(falling.effects->Effects(), [](const auto& effect) { return effect.alive; });
        if (subtype >= 2)
        {
            ASSERT_EQ(waves, 1);
            const Client::Effects::ModelEffect& wave = falling.effects->Effects()[0];
            EXPECT_EQ(wave.type, Client::Effects::ShockWaveEffect);
            EXPECT_EQ(wave.subtype, 4);
            EXPECT_EQ(wave.transform.position, joint.position);
            EXPECT_EQ(wave.light,
                      glm::vec3(std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3f4ccccdu), 1.0f));
            static_cast<void>(expected.Next());
        }
        else
            EXPECT_EQ(waves, 0);
        static_cast<void>(expected.Next());
        EXPECT_EQ(falling.random.State(), expected.State());
        EXPECT_NE(falling.LiveSprite(), nullptr);
    }
    for (const std::int32_t subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        Falling owned;
        owned.ground = 5000.0;
        ASSERT_TRUE(owned.Create(subtype, &owned.target, Client::Kernel::JointOwnerKind::Actor).HasValue());
        const auto cut = owned.Step();
        ASSERT_FALSE(cut.HasValue());
        EXPECT_TRUE(cut.Error().starts_with("0x53c1c6")) << cut.Error();
        // Without the tail, the ground does not read the owner.
        Falling tailless;
        tailless.ground = 5000.0;
        ASSERT_TRUE(tailless.Create(subtype, &tailless.target, Client::Kernel::JointOwnerKind::Actor).HasValue());
        tailless.joints[0].tailEnabled = false;
        ASSERT_TRUE(tailless.Step().HasValue());
    }
}

// 0x53c1c6..0x53c246: the /0 and the /1 with an owner, on the ground with the tail, pose the 0x20b with the action, the
// scale (2) and the origin (100, 200, 300) the last shadow left (not the owner's position) and the owner's angles (at
// 0): the vertices end up at the origin plus twice their own. 0x4ebc80 writes the owner's box at +0x13c (the origin
// plus its floor; the axes, its box). Then 0x4ee770(0, 0x137, subtype): per corner, rand() % 8 and with 0 the
// subtype's shell 0x137 (nine rand()); at the end, the sprite with its rand() % 360.
TEST(StormJointsTest, TheOwnedFallingSubtypesShedTheShellsOfTheShadow)
{
    const ShadowModels models;
    const glm::vec3 origin{100.0f, 200.0f, 300.0f};
    const std::array<glm::vec3, 3> posed{glm::vec3{102.0f, 204.0f, 306.0f}, glm::vec3{92.0f, 210.0f, 312.0f},
                                         glm::vec3{114.0f, 184.0f, 318.0f}};
    for (const std::int32_t subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        auto world = std::make_unique<Client::Kernel::WorldState>();
        world->PlaceModel(Client::Tables::PlayerShadowModel, {.scale = 2.0f, .origin = origin, .action = 0});
        Falling owned;
        owned.ground = 5000.0;
        owned.target.position = {9000.0f, 9000.0f, 9000.0f};
        owned.target.boundsMin = {-60.0f, -50.0f, 0.0f};
        owned.target.boundsMaxX = 60.0f;
        owned.target.boundsMaxY = 70.0f;
        owned.target.boundsTop = 150.0f;
        ASSERT_TRUE(owned.Create(subtype, &owned.target, Client::Kernel::JointOwnerKind::Actor).HasValue());
        CrtRandom expected = owned.random;

        const auto stepped = owned.Step(&models, world.get());

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        auto twin = std::make_unique<Client::Effects::ModelEffects>();
        std::size_t shells = 0;
        for (std::size_t corner = 0; corner < 90; ++corner)
        {
            if (expected.Next() % 8 != 0)
                continue;
            ASSERT_TRUE(Client::Effects::CreateSkinShell({.type = Client::Effects::SkinShellEffect,
                                                          .position = posed[corner % 3],
                                                          .angles = glm::vec3(0.0f),
                                                          .light = glm::vec3(1.0f),
                                                          .subtype = subtype},
                                                         *twin, expected)
                            .has_value());
            ++shells;
        }
        static_cast<void>(expected.Next());
        EXPECT_EQ(owned.random.State(), expected.State());
        ASSERT_GT(shells, 0U);
        for (std::size_t slot = 0; slot < shells; ++slot)
        {
            const Client::Effects::ModelEffect& shell = owned.effects->Effects()[slot];
            const Client::Effects::ModelEffect& replica = twin->Effects()[slot];
            EXPECT_TRUE(shell.alive);
            EXPECT_EQ(shell.type, Client::Effects::SkinShellEffect);
            EXPECT_EQ(shell.subtype, subtype);
            EXPECT_EQ(shell.transform.position, replica.transform.position);
            EXPECT_EQ(shell.velocity, replica.velocity);
            EXPECT_EQ(shell.light, replica.light);
        }
        EXPECT_FALSE(owned.effects->Effects()[shells].alive);
        const Client::Actors::ActorPickingBox box = Client::Actors::PickingBoxOf(owned.target);
        EXPECT_EQ(box.origin, (glm::vec3{40.0f, 150.0f, 300.0f}));
        EXPECT_EQ(box.axisX, (glm::vec3{120.0f, 0.0f, 0.0f}));
        EXPECT_EQ(box.axisY, (glm::vec3{0.0f, 120.0f, 0.0f}));
        EXPECT_EQ(box.axisZ, (glm::vec3{0.0f, 0.0f, 150.0f}));
        EXPECT_FALSE(owned.joints[0].tailEnabled);
        EXPECT_NE(owned.LiveSprite(), nullptr);

        // Without the 0x20b in the models, it stops at 0x53c1eb.
        const NoModels none;
        Falling bare;
        bare.ground = 5000.0;
        ASSERT_TRUE(bare.Create(subtype, &bare.target, Client::Kernel::JointOwnerKind::Actor).HasValue());
        const auto missing = bare.Step(&none, world.get());
        ASSERT_FALSE(missing.HasValue());
        EXPECT_TRUE(missing.Error().starts_with("0x53c1eb")) << missing.Error();
    }
}

// 0x53c1c6..0x53c210 with the owner's angles and a root bone that is not the identity: 0x4eb860 builds the root with
// 0x5fc8f0(owner's angles) times the scale (2) and the origin (100, 200, 300) of the 0x20b, concatenates (0x5fca90) the
// bone's key, the quaternion (0.5; 0.5; 0.5; 0.5) (0x5fcdb0: (x, y, z) → (z, x, y), exact) at (1, 2, 3), and 0x4ebc80
// passes each vertex through that matrix (0x5fccc0). With (0, 0, 90) the cosine of 90 · 0x3c8efa35 stays on the x87
// stack (0x32510b46 as a float) and is lost when rounding: (94, 208, 310), (104, 214, 316) and (82, 220, 290). With
// (30, 0, 45), the bits below. The vertices come from repeating 0x5fc8f0, 0x5fcdb0, 0x5fca90 and 0x5fccc0 on the x87
// separately (exact Fraction with rounding to 53 and 24 bits, the sine and cosine to 64).
TEST(StormJointsTest, TheShellsOfTheShadowTurnWithTheOwnerAndTheRootBone)
{
    struct Case
    {
        glm::vec3 angles;
        std::array<std::array<std::uint32_t, 3>, 3> posed;
    };
    const std::array cases{Case{{0.0f, 0.0f, 90.0f},
                                {{{0x42bc0000u, 0x43500000u, 0x439b0000u},
                                  {0x42d00000u, 0x43560000u, 0x439e0000u},
                                  {0x42a40000u, 0x435c0000u, 0x43910000u}}}},
                           Case{{30.0f, 0.0f, 45.0f},
                                {{{0x42d3094bu, 0x434dcbaau, 0x439bd484u},
                                  {0x42ec02fdu, 0x4349cb0cu, 0x439bed9fu},
                                  {0x42c72af5u, 0x4364b34cu, 0x43962b7du}}}}};
    ShadowModels models;
    Client::Models::BoneKey& key = models.skeleton.bones[0].actions[0][0];
    key.position = {1.0f, 2.0f, 3.0f};
    key.rotation = glm::quat::wxyz(0.5f, 0.5f, 0.5f, 0.5f);
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(::testing::Message() << probe.angles.x << ", " << probe.angles.y << ", " << probe.angles.z);
        auto world = std::make_unique<Client::Kernel::WorldState>();
        world->PlaceModel(Client::Tables::PlayerShadowModel,
                          {.scale = 2.0f, .origin = {100.0f, 200.0f, 300.0f}, .action = 0});
        Falling owned;
        owned.ground = 5000.0;
        owned.target.position = {9000.0f, 9000.0f, 9000.0f};
        owned.target.angles = probe.angles;
        ASSERT_TRUE(owned.Create(0, &owned.target, Client::Kernel::JointOwnerKind::Actor).HasValue());
        CrtRandom expected = owned.random;

        const auto stepped = owned.Step(&models, world.get());

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        auto twin = std::make_unique<Client::Effects::ModelEffects>();
        std::size_t shells = 0;
        for (std::size_t corner = 0; corner < 90; ++corner)
        {
            if (expected.Next() % 8 != 0)
                continue;
            const std::array<std::uint32_t, 3>& bits = probe.posed[corner % 3];
            const glm::vec3 vertex{std::bit_cast<float>(bits[0]), std::bit_cast<float>(bits[1]),
                                   std::bit_cast<float>(bits[2])};
            ASSERT_TRUE(Client::Effects::CreateSkinShell({.type = Client::Effects::SkinShellEffect,
                                                          .position = vertex,
                                                          .angles = glm::vec3(0.0f),
                                                          .light = glm::vec3(1.0f),
                                                          .subtype = 0},
                                                         *twin, expected)
                            .has_value());
            ++shells;
        }
        static_cast<void>(expected.Next());
        EXPECT_EQ(owned.random.State(), expected.State());
        ASSERT_GT(shells, 0U);
        for (std::size_t slot = 0; slot < shells; ++slot)
        {
            SCOPED_TRACE(slot);
            const Client::Effects::ModelEffect& shell = owned.effects->Effects()[slot];
            EXPECT_TRUE(shell.alive);
            EXPECT_EQ(shell.transform.position, twin->Effects()[slot].transform.position);
        }
        EXPECT_FALSE(owned.effects->Effects()[shells].alive);
    }
}

// 0x534977..0x534a08: the /4 with 0x51e01a's arguments (the 0x13a's position and angles, width 40, key 50): speed 0,
// pass 1, texture 0x62b, light 1, 20 points, life 30, the volley at 0 and the owner's position and angles, without
// rand(). Without an owner, the original dereferences the null pointer: MuRet stops.
TEST(StormJointsTest, TheBlastJointIsBornOnItsEffect)
{
    Falling blast;
    Client::Effects::ModelEffect effect{};
    effect.alive = true;
    effect.transform.position = {700.0f, 800.0f, 260.0f};
    effect.transform.angles = {-30.0f, 0.0f, 45.0f};
    blast.start = effect.transform.position;
    blast.angles = effect.transform.angles;
    blast.joints[0].volley = 9;
    ASSERT_TRUE(blast.Create(4, &effect, Client::Kernel::JointOwnerKind::ModelEffect, 40.0f, 0x32).HasValue());
    const JointState& joint = blast.joints[0];
    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.width, 40.0f);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.category, 1);
    EXPECT_EQ(joint.texture, 0x62b);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.velocity, glm::vec3(0.0f));
    EXPECT_EQ(joint.maximumCount, 20);
    EXPECT_EQ(joint.life, 30);
    EXPECT_EQ(joint.volley, 0);
    EXPECT_EQ(joint.key, 0x32);
    EXPECT_EQ(joint.position, effect.transform.position);
    EXPECT_EQ(joint.angles, effect.transform.angles);
    EXPECT_EQ(joint.ownerContext, &effect);
    EXPECT_EQ(blast.random.State(), Seed);

    Falling ownerless;
    const auto cut = ownerless.Create(4, nullptr, Client::Kernel::JointOwnerKind::ByType);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x5349d3")) << cut.Error();
}

// 0x53be90..0x53bebd and 0x53c076..0x53c0a0: the /4 takes its effect's live byte and position; with life under 15 the
// light times 0.7692308f. Without an owner it goes out. Without rand().
TEST(StormJointsTest, TheBlastJointFollowsItsEffect)
{
    Falling blast;
    Client::Effects::ModelEffect effect{};
    effect.alive = true;
    blast.start = {700.0f, 800.0f, 260.0f};
    ASSERT_TRUE(blast.Create(4, &effect, Client::Kernel::JointOwnerKind::ModelEffect, 40.0f, 0x32).HasValue());
    JointState& joint = blast.joints[0];
    effect.transform.position = {710.0f, 790.0f, 250.0f};

    ASSERT_TRUE(blast.Step().HasValue());

    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.position, effect.transform.position);
    EXPECT_EQ(joint.life, 29);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(blast.random.State(), Seed);

    joint.life = 14;
    ASSERT_TRUE(blast.Step().HasValue());
    EXPECT_EQ(joint.light, glm::vec3(F32(1.0 * D(std::bit_cast<float>(0x3f44ec4fu)))));

    effect.alive = false;
    ASSERT_TRUE(blast.Step().HasValue());
    EXPECT_FALSE(joint.alive);

    Falling lost;
    ASSERT_TRUE(lost.Create(4, &effect, Client::Kernel::JointOwnerKind::ModelEffect, 40.0f, 0x32).HasValue());
    lost.joints[0].ownerContext = nullptr;
    ASSERT_TRUE(lost.Step().HasValue());
    EXPECT_FALSE(lost.joints[0].alive);
}
