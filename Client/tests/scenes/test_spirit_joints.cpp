#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Math/X87.h"
#include "Render/ModelBatch.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Scenes/GameDrawTail.h"
#include "Joints/JointDrawing.h"
#include "Joints/JointKernels.h"
#include "Actors/SkillHits.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainLighting.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <functional>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The 0x613 and the 0x6ee (SpiritJoints.cpp) and the drop 0x60f/2 (DropJoints.cpp): the creation in 0x53265e and
// 0x5340dc, the step in 0x538d77 and 0x53b156, and the common drawing in 0x53c6a0. 06, "Los joints de las familias".
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointOwnerKind;
using Client::Kernel::JointState;

constexpr std::uint32_t Seed = 0x613beefu;
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);

using Client::Math::Bits;
using Client::Math::D;
using Client::Math::F32;

struct Pool
{
    Client::Core::Expected<std::optional<std::size_t>, std::string>
    Create(std::int32_t type, std::int32_t subtype, glm::vec3 at, float width, void* owner = nullptr,
           JointOwnerKind kind = JointOwnerKind::ByType, std::int32_t skill = 0)
    {
        glm::vec3 from = at;
        glm::vec3 angles = startAngles;
        const Client::Kernel::JointRequest request{.type = type,
                                                   .firstPosition = from,
                                                   .secondPosition = from,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = owner,
                                                   .scale = width,
                                                   .key = -1,
                                                   .skill = skill,
                                                   .skillSerial = 0,
                                                   .target = -1};
        const Client::Joints::JointKernels::CreationServices services{
            .playerBonePoint = {},
            .soundRequest = [this](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); },
            .ownRecordDepth = withOwnRecord ? &ownRecordDepth : nullptr,
            .ownVolley = withOwnRecord ? &ownVolley : nullptr};
        return Client::Joints::CreateFaunaJoint(joints, request, random, kind, &services);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(joints, random, &cloth, &particles, 0.0f, &sprites, lighting.get(), {},
                                          terrainHeight, {}, {}, effects.get(), hero, nullptr, volley);
    }

    std::vector<JointState> joints = std::vector<JointState>(4);
    std::function<double(float, float)> terrainHeight{};
    CrtRandom random{Seed};
    glm::vec3 startAngles{};
    std::vector<std::int32_t> sounds;
    bool withOwnRecord = true;
    std::uint8_t ownRecordDepth = 0;
    std::uint8_t ownVolley = 0;
    Client::Models::ClothWorld cloth{.random = CrtRandom{1}};
    Client::Particles::Particles particles;
    Client::Particles::FrameSprites sprites;
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    std::unique_ptr<Client::Effects::ModelEffects> effects = std::make_unique<Client::Effects::ModelEffects>();
    const ActorState* hero = nullptr;
    const Client::Joints::JointKernels::VolleyServices* volley = nullptr;
};

ActorState Owner(glm::vec3 position)
{
    ActorState owner;
    owner.alive = true;
    owner.position = position;
    return owner;
}

// A seed whose rand() number `index` (from 0) satisfies `wanted`.
template <typename Wanted> std::uint32_t SeedWhere(int index, Wanted wanted)
{
    for (std::uint32_t seed = 1; seed < 100000; ++seed)
    {
        CrtRandom probe(seed);
        probe.Discard(static_cast<std::uint32_t>(index));
        if (wanted(probe.Next()))
            return seed;
    }
    return 0;
}

std::vector<Client::Particles::Sprite> AliveSprites(Client::Particles::FrameSprites& sprites)
{
    std::vector<Client::Particles::Sprite> found;
    for (const Client::Particles::Sprite& sprite : sprites.Sprites())
        if (sprite.alive)
            found.push_back(sprite);
    return found;
}

// The particle a step asks for, repeated in a twin pool with `replay`, which advances the rand() like the original.
Client::Particles::Particle Twin(CrtRandom& replay, std::int32_t type, std::int32_t subtype, const glm::vec3& position,
                                 const glm::vec3& angles, const glm::vec3& light, float scale)
{
    auto twin = std::make_unique<Client::Particles::Particles>();
    const auto made = twin->CreateChecked(type, position, angles, light, subtype, scale, 0.0f, replay);
    EXPECT_TRUE(made.HasValue() && made.Value().has_value());
    return twin->All()[0];
}

std::array<std::uint32_t, 3> Bits(const glm::vec3& value)
{
    return {Bits(value.x), Bits(value.y), Bits(value.z)};
}

// Every field, bit for bit.
void ExpectSameParticle(const Client::Particles::Particle& made, const Client::Particles::Particle& twin)
{
    EXPECT_EQ(made.alive, twin.alive);
    EXPECT_EQ(made.type, twin.type);
    EXPECT_EQ(made.texture, twin.texture);
    EXPECT_EQ(made.subtype, twin.subtype);
    EXPECT_EQ(Bits(made.scale), Bits(twin.scale));
    EXPECT_EQ(Bits(made.position), Bits(twin.position));
    EXPECT_EQ(Bits(made.angles), Bits(twin.angles));
    EXPECT_EQ(Bits(made.light), Bits(twin.light));
    EXPECT_EQ(made.field38, twin.field38);
    EXPECT_EQ(made.life, twin.life);
    EXPECT_EQ(made.owner, twin.owner);
    EXPECT_EQ(Bits(made.rotation), Bits(twin.rotation));
    EXPECT_EQ(Bits(made.field48), Bits(twin.field48));
    EXPECT_EQ(made.moves, twin.moves);
    EXPECT_EQ(Bits(made.verticalSpeed), Bits(twin.verticalSpeed));
    EXPECT_EQ(Bits(made.velocity), Bits(twin.velocity));
    EXPECT_EQ(Bits(made.rotationFactor), Bits(twin.rotationFactor));
    EXPECT_EQ(made.field64, twin.field64);
    EXPECT_EQ(Bits(made.start), Bits(twin.start));
}

std::size_t AliveParticles(Client::Particles::Particles& particles)
{
    std::size_t count = 0;
    for (const Client::Particles::Particle& particle : particles.All())
        count += particle.alive ? 1 : 0;
    return count;
}

// 0x53265e and the table 0x534f18: the subtypes without rand() write their fields; the blend is 3 except where it is
// overwritten (in the /3, the /0xd and the /0x13 only the 0x613; in the /0x12 and the /0x14 only the 0x6ee).
TEST(SpiritJointsTest, TheFixedSubtypesWriteTheirFields)
{
    struct Case
    {
        std::int32_t type;
        std::int32_t subtype;
        std::uint8_t blend;
        std::uint8_t faces;
        float speed;
        std::int32_t life;
        std::int32_t points;
        glm::vec3 light;
        bool keepsTheStart;
    };
    const glm::vec3 white{1.0f};
    const std::array cases{
        Case{0x613, 1, 3, 3, 70.0f, 49, 6, white, false},
        Case{0x613, 3, 1, 3, 140.0f, 49, 10, {1.0f, 0.5f, Tenth}, false},
        Case{0x6ee, 3, 3, 3, 140.0f, 49, 10, white, false},
        Case{0x613, 0xd, 1, 3, 40.0f, 20, 10, {1.0f, std::bit_cast<float>(0x3f19999au), 0.4f}, false},
        Case{0x6ee, 0xd, 3, 3, 40.0f, 20, 10, {1.0f, std::bit_cast<float>(0x3f19999au), 0.4f}, false},
        Case{0x613, 2, 1, 2, 50.0f, 20, 3, glm::vec3(0.5f), true},
        Case{0x613, 0x15, 1, 2, 50.0f, 20, 3, glm::vec3(0.5f), true},
        Case{0x613, 7, 3, 0, 10.0f, 20, 5, {0.3f, 0.3f, 1.0f}, true},
        Case{0x613, 8, 3, 3, 0.0f, 49, 15, white, false},
        Case{0x613, 0xa, 3, 3, 0.0f, 20, 6, white, false},
        Case{0x613, 0xb, 3, 3, 0.0f, 50, 1, white, false},
        Case{0x613, 0xc, 3, 3, 0.0f, 30, 1, white, false},
        Case{0x613, 0xf, 3, 3, 0.0f, 20, 1, white, false},
        Case{0x613, 0x10, 3, 3, 0.0f, 50, 1, white, false},
        Case{0x613, 0x11, 3, 3, 0.0f, 100, 1, white, false},
        Case{0x613, 0x12, 3, 3, 50.0f, 39, 15, glm::vec3(std::bit_cast<float>(0x3f333333u)), false},
        Case{0x6ee, 0x12, 1, 3, 50.0f, 39, 15, glm::vec3(std::bit_cast<float>(0x3f333333u)), false},
        Case{0x613, 0x13, 1, 3, 70.0f, 49, 9, {Tenth, 0.5f, 0.2f}, false},
        Case{0x6ee, 0x14, 1, 3, 0.0f, 49, 15, white, false},
        Case{0x613, 0x16, 3, 3, 0.0f, 0, 0, white, false},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << std::hex << probe.type << "/" << probe.subtype);
        Pool pool;
        const auto created = pool.Create(probe.type, probe.subtype, {10.0f, 20.0f, 30.0f}, 25.0f);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.texture, probe.type);
        EXPECT_EQ(joint.blend, probe.blend);
        EXPECT_EQ(joint.drawEnabled, probe.faces);
        EXPECT_EQ(joint.speed, probe.speed);
        EXPECT_EQ(joint.life, probe.life);
        EXPECT_EQ(joint.maximumCount, probe.points);
        EXPECT_EQ(joint.light, probe.light);
        EXPECT_EQ(joint.width, probe.subtype == 0x16 ? 0.0f : 25.0f);
        if (probe.keepsTheStart)
        {
            EXPECT_EQ(joint.pathIncrement, glm::vec3(10.0f, 20.0f, 30.0f));
        }
        EXPECT_EQ(pool.random.State(), CrtRandom(Seed).State());
        EXPECT_EQ(pool.sounds, probe.subtype == 1 ? std::vector<std::int32_t>{0x3e} : std::vector<std::int32_t>{});
    }
    // 0x5334bb: the /0xe is born with the angles' x at 20.
    Pool pool;
    ASSERT_TRUE(pool.Create(0x613, 0xe, {}, 25.0f).HasValue());
    EXPECT_EQ(pool.joints[0].angles.x, 20.0f);
    EXPECT_EQ(pool.joints[0].life, 10);
}

// 0x5331e8..0x533201: the /6 draws its key with rand() % 5 and without 0 loses its tail; skill 1 leaves it with speed
// 10 and light 0.5 (0x533218).
TEST(SpiritJointsTest, TheSixthDrawsItsKeyAndLosesItsTailWithoutZero)
{
    for (const bool zero : {true, false})
    {
        SCOPED_TRACE(zero);
        Pool pool;
        pool.random = CrtRandom(SeedWhere(0, [zero](int value) { return (value % 5 == 0) == zero; }));
        CrtRandom expected = pool.random;
        ASSERT_TRUE(pool.Create(0x613, 6, {}, 25.0f, nullptr, JointOwnerKind::ByType, 1).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.key, expected.Next() % 5);
        EXPECT_EQ(joint.tailEnabled, zero);
        EXPECT_EQ(joint.speed, 10.0f);
        EXPECT_EQ(joint.light, glm::vec3(0.5f));
        EXPECT_EQ(joint.blend, 1);
        EXPECT_EQ(joint.drawEnabled, 2);
        EXPECT_EQ(pool.random.State(), expected.State());
    }
}

// 0x532675 and 0x532bab: the /0 and the /5 copy the own record's +0xf04 to +0x9eb with one access; the close that
// leaves it at 0 draws 3848 rand(). Without the record, they stop. With the pool full, nothing.
TEST(SpiritJointsTest, TheVolleySubtypesReadTheOwnRecord)
{
    for (const std::int32_t subtype : {0, 5})
    {
        SCOPED_TRACE(subtype);
        for (const std::uint8_t depth : {std::uint8_t{0}, std::uint8_t{1}})
        {
            Pool pool;
            pool.ownRecordDepth = depth;
            pool.ownVolley = 0x2a;
            ASSERT_TRUE(pool.Create(0x613, subtype, {}, 80.0f).HasValue());
            const JointState& joint = pool.joints[0];
            EXPECT_EQ(joint.volley, 0x2a);
            EXPECT_EQ(pool.ownRecordDepth, depth);
            CrtRandom expected(Seed);
            if (depth == 0)
                expected.Discard(Client::Models::OwnRecordCloseRandoms);
            EXPECT_EQ(pool.random.State(), expected.State());
            EXPECT_EQ(joint.blend, subtype == 0 ? 3 : 1);
            EXPECT_EQ(joint.speed, subtype == 0 ? 70.0f : 30.0f);
            EXPECT_EQ(joint.life, 49);
            EXPECT_EQ(joint.maximumCount, subtype == 0 ? 6 : 12);
            EXPECT_EQ(joint.width, 80.0f);
        }
        Pool without;
        without.withOwnRecord = false;
        const auto cut = without.Create(0x613, subtype, {}, 80.0f);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_TRUE(cut.Error().starts_with(subtype == 0 ? "0x532675" : "0x532bab")) << cut.Error();
        Pool full;
        for (JointState& joint : full.joints)
            joint.alive = true;
        const auto none = full.Create(0x613, subtype, {}, 80.0f);
        ASSERT_TRUE(none.HasValue());
        EXPECT_FALSE(none.Value().has_value());
        EXPECT_EQ(full.random.State(), CrtRandom(Seed).State());
    }
}

// 0x5332b8 and 0x5333f6 with the shared table 0x534f18: the 0x6ee/4 is born like the 0x613/4 (the helper's beam,
// without speed) and the 0x613/9 like the meteor 0x6ee/9; the steps are the same (the /4 without a case, the /9 with
// two rand()).
TEST(SpiritJointsTest, TheFourthAndTheNinthShareTheirTable)
{
    Pool beam;
    ASSERT_TRUE(beam.Create(0x6ee, 4, {}, 20.0f).HasValue());
    EXPECT_EQ(beam.joints[0].speed, 0.0f);
    EXPECT_EQ(beam.joints[0].maximumCount, 10);
    EXPECT_EQ(beam.joints[0].light, glm::vec3(std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3e99999au), 1));
    ASSERT_TRUE(beam.Step().HasValue());
    EXPECT_EQ(beam.random.State(), CrtRandom(Seed).State());

    Pool meteor;
    ASSERT_TRUE(meteor.Create(0x613, 9, {}, 20.0f).HasValue());
    EXPECT_EQ(meteor.joints[0].blend, 3);
    EXPECT_EQ(meteor.joints[0].drawEnabled, 2);
    EXPECT_EQ(meteor.joints[0].angles, glm::vec3(-90.0f, 0.0f, 0.0f));
    EXPECT_EQ(meteor.joints[0].life, 20);
    EXPECT_EQ(meteor.joints[0].maximumCount, 15);
    ASSERT_TRUE(meteor.Step().HasValue());
    CrtRandom expected(Seed);
    expected.Discard(2);
    EXPECT_EQ(meteor.random.State(), expected.State());
    EXPECT_EQ(meteor.joints[0].speed, 11.0f);
}

// 0x538d99..0x538dda: the /1 advances 70 with its angles, drops the sprite 0x5ac of scale 4 with the light (0.8; 0.4;
// 1) and the turn rand() % 360, and loses one life.
TEST(SpiritJointsTest, TheSparkStepsWithItsSprite)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x613, 1, {100.0f, 200.0f, 300.0f}, 60.0f).HasValue());
    CrtRandom expected = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    const JointState& joint = pool.joints[0];
    EXPECT_EQ(joint.position, glm::vec3(100.0f, 130.0f, 300.0f));
    EXPECT_EQ(joint.life, 48);
    const auto sprites = AliveSprites(pool.sprites);
    ASSERT_EQ(sprites.size(), 1u);
    EXPECT_EQ(sprites[0].texture, 0x5ac);
    EXPECT_EQ(sprites[0].scale, 4.0f);
    EXPECT_EQ(sprites[0].light, glm::vec3(std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3ecccccdu), 1.0f));
    EXPECT_EQ(sprites[0].rotation, static_cast<float>(expected.Next() % 360));
    EXPECT_EQ(sprites[0].position, joint.position);
    EXPECT_EQ(pool.random.State(), expected.State());
}

// 0x539586..0x539775: on its first step the /2 drops the scale 4 sprite with the light (1; 0.5; 0.1) and raises the
// speed by 5; with life 19 it draws the angles and a point around the start and drops there the drop 0x60f/2 of width
// 40, which is born with vz at rand() % 20 + 35 and life rand() % 50 + 25 and, in a later slot, steps in the same
// round.
TEST(SpiritJointsTest, TheCrossDropsItsDropOnItsSecondStep)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x613, 2, {100.0f, 200.0f, 300.0f}, 60.0f).HasValue());
    CrtRandom expected = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    const JointState& cross = pool.joints[0];
    EXPECT_EQ(cross.speed, 55.0f);
    EXPECT_EQ(cross.life, 19);
    auto sprites = AliveSprites(pool.sprites);
    ASSERT_EQ(sprites.size(), 1u);
    EXPECT_EQ(sprites[0].scale, 4.0f);
    EXPECT_EQ(sprites[0].light, glm::vec3(1.0f, 0.5f, Tenth));
    EXPECT_EQ(sprites[0].rotation, static_cast<float>(expected.Next() % 360));
    EXPECT_FALSE(pool.joints[1].alive);

    pool.sprites.Clear();
    ASSERT_TRUE(pool.Step().HasValue());
    const float turn = static_cast<float>(expected.Next() % 360);
    const float x = F32((static_cast<double>(expected.Next() % 200) + 100.0) - 100.0);
    const float y = F32((static_cast<double>(expected.Next() % 200) + 200.0) - 100.0);
    const float fall = static_cast<float>(expected.Next() % 20 + 35);
    const std::int32_t life = expected.Next() % 50 + 25;
    const float rotation = static_cast<float>(expected.Next() % 360);
    EXPECT_EQ(pool.random.State(), expected.State());
    const JointState& drop = pool.joints[1];
    ASSERT_TRUE(drop.alive);
    EXPECT_EQ(drop.type, 0x60f);
    EXPECT_EQ(drop.subtype, 2);
    EXPECT_EQ(drop.angles, glm::vec3(0.0f, 0.0f, turn));
    EXPECT_EQ(drop.targetPosition, glm::vec3(x, y, 100.0f));
    EXPECT_EQ(drop.width, 40.0f);
    EXPECT_EQ(drop.light, glm::vec3(1.0f));
    EXPECT_EQ(drop.life, life - 1);
    if (life > 25)
    {
        EXPECT_EQ(drop.velocity.z, fall);
        EXPECT_EQ(drop.position, glm::vec3(x, y, 100.0f));
    }
    sprites = AliveSprites(pool.sprites);
    ASSERT_EQ(sprites.size(), 1u);
    EXPECT_EQ(sprites[0].rotation, rotation);
    EXPECT_EQ(sprites[0].scale, 4.0f);
    EXPECT_EQ(cross.speed, 60.0f);
    EXPECT_EQ(cross.life, 18);
}

// 0x53b156..0x53b185 and 0x53b339: with life 25 or less, the drop falls onto its target with vz + 5; under 10, the
// light times 0.7692308f.
TEST(SpiritJointsTest, TheDropFallsOnItsTarget)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x60f, 2, {50.0f, 60.0f, 70.0f}, 40.0f).HasValue());
    JointState& drop = pool.joints[0];
    drop.life = 9;
    drop.position = {1.0f, 2.0f, 70.0f};
    drop.velocity.z = 36.0f;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(drop.position, glm::vec3(50.0f, 60.0f, 111.0f));
    EXPECT_EQ(drop.velocity.z, 41.0f);
    EXPECT_EQ(drop.light, glm::vec3(F32(1.0 * D(std::bit_cast<float>(0x3f44ec4fu)))));
    EXPECT_EQ(drop.life, 8);
}

// 0x539600..0x539641: with life under 10, its own light times 0.8333333f is also the sprite's, of scale (20 − life) / 5
// + 4. The /6 with a skill other than 0 or 1 leaves the local light unwritten: it stops.
TEST(SpiritJointsTest, TheCrossFadesBelowTenAndCutsWithoutLight)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x613, 0x15, {}, 60.0f).HasValue());
    pool.joints[0].life = 9;
    ASSERT_TRUE(pool.Step().HasValue());
    const float faded = F32(0.5 * D(std::bit_cast<float>(0x3f555555u)));
    EXPECT_EQ(pool.joints[0].light, glm::vec3(faded));
    const auto sprites = AliveSprites(pool.sprites);
    ASSERT_EQ(sprites.size(), 1u);
    EXPECT_EQ(sprites[0].light, glm::vec3(faded));
    EXPECT_EQ(sprites[0].scale, 6.0f);

    Pool cut;
    ASSERT_TRUE(cut.Create(0x613, 6, {}, 60.0f, nullptr, JointOwnerKind::ByType, 2).HasValue());
    const auto stepped = cut.Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_TRUE(stepped.Error().starts_with("0x53975b")) << stepped.Error();
}

// 0x539941..0x5399a2 and 0x5399a2..0x539a09: the /3 advances 140 and veers with 0x53d1a0(slot, dir, 1): the counter
// with its access (4 rand() when closing it at 0) and the direction of phase slot + counter, times 50; then the sprites
// 0x5ac of scale 3 and 0x5fd of scale 1.5 with the light (1; 0.5; 0.1).
TEST(SpiritJointsTest, TheBoltWandersWithTheSharedCounter)
{
    Pool pool;
    pool.cloth.logicSteps = 1234;
    ASSERT_TRUE(pool.Create(0x613, 3, {1000.0f, 2000.0f, 300.0f}, 50.0f).HasValue());
    CrtRandom expected = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    expected.Discard(Client::Models::SmallLogicalCloseRandoms);
    const auto direction = Client::Math::Detail::FaunaDirectionPC53(1234);
    ASSERT_TRUE(direction.has_value());
    const JointState& bolt = pool.joints[0];
    const glm::vec3 wandered{F32(D((*direction)[0]) * 50.0 + 1000.0), F32(D(F32(D((*direction)[1]) * 50.0)) + 1860.0),
                             F32(D(F32(D((*direction)[2]) * 50.0)) + 300.0)};
    EXPECT_EQ(bolt.position, wandered);
    const auto sprites = AliveSprites(pool.sprites);
    ASSERT_EQ(sprites.size(), 2u);
    EXPECT_EQ(sprites[0].texture, 0x5ac);
    EXPECT_EQ(sprites[0].scale, 3.0f);
    EXPECT_EQ(sprites[0].rotation, static_cast<float>(expected.Next() % 360));
    EXPECT_EQ(sprites[1].texture, 0x5fd);
    EXPECT_EQ(sprites[1].scale, 1.5f);
    EXPECT_EQ(sprites[1].rotation, static_cast<float>(expected.Next() % 360));
    EXPECT_EQ(sprites[1].light, glm::vec3(1.0f, 0.5f, Tenth));
    EXPECT_EQ(pool.random.State(), expected.State());

    // With the counter open, without the 4 rand().
    Pool open;
    open.cloth.openCounterAccesses = 1;
    ASSERT_TRUE(open.Create(0x613, 3, {}, 50.0f).HasValue());
    CrtRandom twoDraws = open.random;
    twoDraws.Discard(2);
    ASSERT_TRUE(open.Step().HasValue());
    EXPECT_EQ(open.random.State(), twoDraws.State());
}

// 0x539789..0x53993f: the /0xd with life 28 or less and a character owner turns 20 in x with rand() & 1 at 0, seeks a
// point ±70 around the owner and 50 higher with 0x4e9810 by (speed − 20), raises the speed by 1 and uses its own light
// with scale (life / 10 + 1) · 2. Without an owner, the local light stays unwritten: it stops.
TEST(SpiritJointsTest, TheThirteenthSeeksItsOwner)
{
    ActorState owner = Owner({1500.0f, 1500.0f, 100.0f});
    Pool pool;
    pool.random = CrtRandom(SeedWhere(0, [](int value) { return (value & 1) == 0; }));
    ASSERT_TRUE(pool.Create(0x613, 0xd, {1000.0f, 1000.0f, 100.0f}, 40.0f, &owner, JointOwnerKind::Actor).HasValue());
    JointState& seeker = pool.joints[0];
    seeker.life = 28;
    seeker.speed = 0.0f;
    CrtRandom expected = pool.random;
    glm::vec3 angles = seeker.angles;
    const glm::vec3 position = seeker.position;
    ASSERT_TRUE(pool.Step().HasValue());
    static_cast<void>(expected.Next());
    angles.x = F32(D(angles.x) - 20.0);
    const glm::vec3 target{F32((static_cast<double>(expected.Next() % 150) - 70.0) + 1500.0),
                           F32((static_cast<double>(expected.Next() % 150) - 70.0) + 1500.0), 150.0f};
    const double distance = Client::Math::MoveToward(position, angles, target, -20.0f);
    EXPECT_EQ(seeker.angles, angles);
    EXPECT_EQ(seeker.speed, 1.0f);
    const std::int32_t life = distance < 50.0 ? 26 : 27;
    EXPECT_EQ(seeker.life, life);
    const auto sprites = AliveSprites(pool.sprites);
    ASSERT_EQ(sprites.size(), 2u);
    EXPECT_EQ(sprites[0].scale, 6.0f);
    EXPECT_EQ(sprites[0].light, seeker.light);
    EXPECT_EQ(sprites[0].rotation, static_cast<float>(expected.Next() % 360));
    EXPECT_EQ(sprites[1].rotation, static_cast<float>(expected.Next() % 360));
    EXPECT_EQ(pool.random.State(), expected.State());

    Pool ownerless;
    ASSERT_TRUE(ownerless.Create(0x613, 0xd, {}, 40.0f).HasValue());
    ownerless.joints[0].life = 20;
    const auto stepped = ownerless.Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_TRUE(stepped.Error().starts_with("0x5399cd")) << stepped.Error();
}

// 0x53909f..0x539125: the 0x6ee/0xe (Kundun's 24, 0x43d19f) lowers the angles' x by 2 and with rand() % 5 creates the
// 0x6f2/2 (0), the 0x6f3/2 (1 or 2) or the 0x6f4/4, of scale 1 at its position, with its angles and its light (+0x48);
// the tail follows. Each particle is repeated in a twin pool with the same CrtRandom.
TEST(SpiritJointsTest, TheKundunSparkTiltsAndPicksItsParticle)
{
    struct Case
    {
        std::int32_t pick;
        std::int32_t type;
        std::int32_t subtype;
    };
    for (const Case& each :
         {Case{0, 0x6f2, 2}, Case{1, 0x6f3, 2}, Case{2, 0x6f3, 2}, Case{3, 0x6f4, 4}, Case{4, 0x6f4, 4}})
    {
        SCOPED_TRACE(each.pick);
        Pool pool;
        ASSERT_TRUE(pool.Create(0x6ee, 0xe, {10.0f, 20.0f, 30.0f}, 30.0f).HasValue());
        pool.joints[0].angles.x = 20.0f;
        const std::uint32_t seed = SeedWhere(0, [&each](std::int32_t r) { return r % 5 == each.pick; });
        pool.random = CrtRandom(seed);
        const JointState before = pool.joints[0];

        ASSERT_TRUE(pool.Step().HasValue());

        EXPECT_EQ(pool.joints[0].angles.x, 18.0f);
        CrtRandom replay(seed);
        static_cast<void>(replay.Next());
        const auto twin = Twin(replay, each.type, each.subtype, before.position,
                               {18.0f, before.angles.y, before.angles.z}, before.light, 1.0f);
        ExpectSameParticle(pool.particles.All()[0], twin);
        EXPECT_EQ(AliveParticles(pool.particles), 1U);
        EXPECT_EQ(pool.random.State(), replay.State());
        EXPECT_EQ(pool.joints[0].life, before.life - 1);
    }
}

// 0x538e1e..0x538e7f: the /8 rises with x at 110 and speed 10 until life 42; then it goes with speed 50 from 180 by
// rand() % 3 + 2. 0x538e89: the /0xa turns 50 in z per step with speed 50.
TEST(SpiritJointsTest, TheEighthArcsAndTheTenthTurns)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x613, 8, {}, 40.0f).HasValue());
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(pool.joints[0].angles.x, 110.0f);
    EXPECT_EQ(pool.joints[0].speed, 10.0f);
    pool.joints[0].life = 41;
    CrtRandom expected = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(pool.joints[0].speed, 50.0f);
    EXPECT_EQ(pool.joints[0].angles.x, F32((static_cast<double>(expected.Next() % 3) + 180.0) + 2.0));
    EXPECT_EQ(pool.random.State(), expected.State());

    Pool turning;
    ASSERT_TRUE(turning.Create(0x613, 0xa, {}, 40.0f).HasValue());
    ASSERT_TRUE(turning.Step().HasValue());
    EXPECT_EQ(turning.joints[0].angles.z, -50.0f);
    EXPECT_EQ(turning.joints[0].speed, 50.0f);
    EXPECT_EQ(turning.random.State(), CrtRandom(Seed).State());
}

// 0x539a0e..0x539a48: with width 80, the /0, the /5 and the /0x13 drop the 0xd0 (the ray, subtype 3 for the /5 and 0
// for the others) with their position, angles and light, without rand(); without the siege, without the hero as owner
// or off the multiples of 15 of life, they go on as with another width (the same step, the same rand()), and the /0x13
// does not drop the 0x11b/0. With another width, the /0x13 drops the flash 0x11b/0 (0x539b08: life 1, scale 2.3f,
// without rand()) and goes on like the /0; without the effect table, it stops there.
TEST(SpiritJointsTest, TheWideVolleyDropsItsRayAndGoesOn)
{
    const ActorState someoneElse = Owner({});
    const Client::Joints::JointKernels::VolleyServices calm{.siegeActive = false};
    for (const std::int32_t subtype : {0, 5, 0x13})
    {
        SCOPED_TRACE(subtype);
        ActorState owner = Owner({50.0f, 60.0f, 70.0f});
        Pool wide;
        Pool thin;
        for (Pool* pool : {&wide, &thin})
        {
            pool->terrainHeight = [](float, float) { return 150.0; };
            pool->hero = &someoneElse;
            pool->volley = &calm;
        }
        ASSERT_TRUE(
            wide.Create(0x613, subtype, {100.0f, 200.0f, 300.0f}, 80.0f, &owner, JointOwnerKind::Actor).HasValue());
        ASSERT_TRUE(
            thin.Create(0x613, subtype, {100.0f, 200.0f, 300.0f}, 20.0f, &owner, JointOwnerKind::Actor).HasValue());
        const glm::vec3 light = wide.joints[0].light;
        const auto stepped = wide.Step();
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_TRUE(thin.Step().HasValue());
        EXPECT_EQ(wide.random.State(), thin.random.State());
        const JointState& joint = wide.joints[0];
        EXPECT_EQ(joint.position, thin.joints[0].position);
        EXPECT_EQ(joint.angles, thin.joints[0].angles);
        EXPECT_EQ(joint.velocity, thin.joints[0].velocity);
        EXPECT_EQ(joint.life, thin.joints[0].life);
        EXPECT_EQ(joint.light, thin.joints[0].light);
        const auto& ray = wide.effects->Effects()[0];
        ASSERT_TRUE(ray.alive);
        EXPECT_EQ(ray.type, 0xd0);
        EXPECT_EQ(ray.subtype, subtype == 5 ? 3 : 0);
        EXPECT_EQ(ray.life, 1);
        EXPECT_EQ(ray.transform.position, joint.position);
        EXPECT_EQ(ray.light, light);
        EXPECT_EQ(ray.ownerContext, nullptr);
        EXPECT_FALSE(wide.effects->Effects()[1].alive);
        if (subtype != 0x13)
        {
            EXPECT_FALSE(thin.effects->Effects()[0].alive);
            continue;
        }
        // The width 20's 0x11b/0 is born at the same step point as the 0xd0: the same angles.
        const auto& spark = thin.effects->Effects()[0];
        ASSERT_TRUE(spark.alive);
        EXPECT_EQ(spark.type, 0x11b);
        EXPECT_EQ(spark.subtype, 0);
        EXPECT_EQ(spark.life, 1);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(spark.transform.scale), 0x40133333u);
        EXPECT_EQ(spark.light, light);
        EXPECT_EQ(ray.transform.angles, spark.transform.angles);
        EXPECT_FALSE(thin.effects->Effects()[1].alive);
    }

    ActorState owner = Owner({});
    Pool tableless;
    tableless.effects.reset();
    ASSERT_TRUE(tableless.Create(0x613, 0x13, {}, 20.0f, &owner, JointOwnerKind::Actor).HasValue());
    const auto cut = tableless.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x539b08")) << cut.Error();
    Pool wideTableless;
    wideTableless.effects.reset();
    ASSERT_TRUE(wideTableless.Create(0x613, 0, {}, 80.0f, &owner, JointOwnerKind::Actor).HasValue());
    const auto wideCut = wideTableless.Step();
    ASSERT_FALSE(wideCut.HasValue());
    EXPECT_TRUE(wideCut.Error().starts_with("0x539a48")) << wideCut.Error();
    // Without the siege, the attributes and the targets, it stops after dropping the 0xd0.
    Pool bare;
    ASSERT_TRUE(bare.Create(0x613, 0, {}, 80.0f, &owner, JointOwnerKind::Actor).HasValue());
    const auto bareCut = bare.Step();
    ASSERT_FALSE(bareCut.HasValue());
    EXPECT_TRUE(bareCut.Error().starts_with("0x539a50")) << bareCut.Error();
    EXPECT_TRUE(bare.effects->Effects()[0].alive);
}

// 0x539a50..0x539a95: with the siege on and the bit 0x100 of 0x5f82c0 at the joint's point, speed at 0, life at
// _ftol(49 · 0.2f) = 9 and the tail 0x53c393, without the rest of the step or its rand(). With the siege and the cell
// without the bit, as without the siege.
TEST(SpiritJointsTest, TheWideVolleyStopsOverTheCastleWall)
{
    auto walled = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    walled->cells.fill(0x100);
    auto open = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    const ActorState someoneElse = Owner({});
    ActorState owner = Owner({50.0f, 60.0f, 70.0f});
    const Client::Joints::JointKernels::VolleyServices wall{.siegeActive = true, .attributes = walled.get()};
    const Client::Joints::JointKernels::VolleyServices field{.siegeActive = true, .attributes = open.get()};
    Pool stopped;
    Pool going;
    Pool thin;
    for (Pool* pool : {&stopped, &going, &thin})
    {
        pool->terrainHeight = [](float, float) { return 150.0; };
        pool->hero = &someoneElse;
        pool->volley = &field;
    }
    stopped.volley = &wall;
    ASSERT_TRUE(stopped.Create(0x613, 0, {100.0f, 200.0f, 300.0f}, 80.0f, &owner, JointOwnerKind::Actor).HasValue());
    ASSERT_TRUE(going.Create(0x613, 0, {100.0f, 200.0f, 300.0f}, 80.0f, &owner, JointOwnerKind::Actor).HasValue());
    ASSERT_TRUE(thin.Create(0x613, 0, {100.0f, 200.0f, 300.0f}, 20.0f, &owner, JointOwnerKind::Actor).HasValue());
    ASSERT_EQ(stopped.joints[0].life, 49);
    const std::uint32_t created = stopped.random.State();
    const auto stepped = stopped.Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_TRUE(going.Step().HasValue());
    ASSERT_TRUE(thin.Step().HasValue());
    // The tail 0x53c393 subtracts the life the same as with the whole step.
    const std::int32_t tail = thin.joints[0].life - 49;
    EXPECT_EQ(stopped.joints[0].life, 9 + tail);
    EXPECT_EQ(stopped.joints[0].speed, 0.0f);
    EXPECT_EQ(stopped.random.State(), created);
    EXPECT_TRUE(stopped.effects->Effects()[0].alive);
    EXPECT_EQ(going.joints[0].life, thin.joints[0].life);
    EXPECT_EQ(going.joints[0].angles, thin.joints[0].angles);
    EXPECT_EQ(going.random.State(), thin.random.State());
}

// 0x539a9a..0x539add: with life a multiple of 15 and the hero as owner, 0x517a20's targets with the skill (+0x9ea), the
// position, 150, the volley (+0x9eb), the key (+0x9e8) and the byte at +0xa08; before the rest of the step. With
// another owner or another life, none.
TEST(SpiritJointsTest, TheWideVolleyOfTheHeroHitsEveryFifteenSteps)
{
    std::vector<Client::Actors::SkillHitsRequest> hits;
    const Client::Joints::JointKernels::VolleyServices volley{
        .siegeActive = false,
        .skillHits =
            [&hits](const Client::Actors::SkillHitsRequest& request) -> Client::Core::Expected<void, std::string>
        {
            hits.push_back(request);
            return {};
        }};
    ActorState hero = Owner({50.0f, 60.0f, 70.0f});
    const ActorState someoneElse = Owner({});
    struct Case
    {
        std::int32_t life;
        bool heroOwned;
        bool hits;
    };
    for (const Case tested :
         {Case{45, true, true}, Case{30, true, true}, Case{44, true, false}, Case{45, false, false}})
    {
        SCOPED_TRACE(tested.life);
        hits.clear();
        Pool pool;
        pool.terrainHeight = [](float, float) { return 150.0; };
        pool.hero = tested.heroOwned ? &hero : &someoneElse;
        pool.volley = &volley;
        pool.ownVolley = 7;
        ASSERT_TRUE(pool.Create(0x613, 5, {100.0f, 200.0f, 300.0f}, 80.0f, &hero, JointOwnerKind::Actor, 3).HasValue());
        pool.joints[0].life = tested.life;
        pool.joints[0].key = 0x1234;
        const auto stepped = pool.Step();
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        if (!tested.hits)
        {
            EXPECT_TRUE(hits.empty());
            continue;
        }
        ASSERT_EQ(hits.size(), 1U);
        EXPECT_EQ(hits[0].skillIndex, 3);
        EXPECT_EQ(hits[0].point, pool.joints[0].position);
        EXPECT_EQ(hits[0].radius, 150.0f);
        EXPECT_EQ(hits[0].volley, 7);
        EXPECT_EQ(hits[0].key, 0x1234);
        EXPECT_EQ(hits[0].extra, 0);
    }
}

// 0x539b10..0x539cc7: the /0 and the /5 of another width go toward the owner 80 higher, the /0 shaken with two rand();
// the ground raises them below 100 and lowers them above 400; the light is life · 0.1f and the dynamic light, one more
// rand().
TEST(SpiritJointsTest, TheThinVolleyFollowsItsOwnerOverTheGround)
{
    for (const std::int32_t subtype : {0, 5})
        for (const double ground : {20.0, 150.0, -300.0})
        {
            SCOPED_TRACE(subtype);
            SCOPED_TRACE(ground);
            Pool pool;
            pool.terrainHeight = [ground](float, float) { return ground; };
            pool.ownRecordDepth = 1; // Without the own record's close (3848 rand()) in the creation.
            ActorState owner = Owner({900.0f, 700.0f, 120.0f});
            ASSERT_TRUE(pool.Create(0x613, subtype, {1000.0f, 1100.0f, 200.0f}, 20.0f, &owner, JointOwnerKind::Actor)
                            .HasValue());
            JointState joint = pool.joints[0];
            const float dx = F32(D(joint.position.x) - D(joint.targetPosition.x));
            const float dy = F32(D(joint.position.y) - D(joint.targetPosition.y));
            const auto matrix = Client::Math::OriginalAngleMatrix(joint.angles);
            const auto moved = Client::Math::OriginalVectorRotate({0.0f, -joint.speed, 0.0f}, matrix);
            for (int axis = 0; axis < 3; ++axis)
                joint.position[axis] = F32(D(joint.position[axis]) + moved[axis]);
            const glm::vec3 target{900.0f, 700.0f, 200.0f};
            static_cast<void>(
                Client::Math::MoveToward(joint.position, joint.angles, target, subtype == 5 ? 2.0f : 10.0f));
            CrtRandom expected(Seed);
            float velocityX = 0.0f;
            if (subtype == 0)
            {
                const int first = (expected.Next() & 31) - 16;
                const int second = (expected.Next() & 31) - 16;
                velocityX = F32(first * D(0.2f));
                const double shake = second * D(0.8f);
                joint.angles.x = F32(D(joint.angles.x) + D(velocityX));
                joint.angles.z = F32(shake + D(joint.angles.z));
                velocityX = F32(D(velocityX) * D(0.6f));
                joint.velocity.z = F32(shake * D(0.8f));
            }
            if (ground + 100.0 > D(joint.position.z))
            {
                velocityX = 0.0f;
                joint.angles.x = -5.0f;
            }
            if (!(ground + 400.0 >= D(joint.position.z)))
            {
                velocityX = 0.0f;
                joint.angles.x = 5.0f;
            }
            expected.Discard(1);
            ASSERT_TRUE(pool.Step().HasValue());
            const JointState& stepped = pool.joints[0];
            EXPECT_EQ(stepped.targetPosition, target);
            EXPECT_EQ(stepped.position, joint.position);
            EXPECT_EQ(stepped.angles, joint.angles);
            EXPECT_EQ(stepped.velocity.x, velocityX);
            EXPECT_EQ(stepped.velocity.z, joint.velocity.z);
            const float distance = F32(std::sqrt(D(dy) * D(dy) + D(dx) * D(dx)));
            EXPECT_EQ(stepped.field9d4, D(joint.speed) * 2 >= D(distance) ? 1 : 0);
            EXPECT_EQ(stepped.light, glm::vec3(F32(49 * D(Tenth))));
            EXPECT_EQ(stepped.life, 48);
            EXPECT_EQ(pool.random.State(), expected.State());
        }
    // Without the ground height, it stops at 0x5f93c0.
    Pool blind;
    ActorState owner = Owner({});
    ASSERT_TRUE(blind.Create(0x613, 5, {}, 20.0f, &owner, JointOwnerKind::Actor).HasValue());
    const auto cut = blind.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x539c19")) << cut.Error();
}

// 0x538f45..0x538feb: the /0xb (life 50) puts the life in the angles' x, leaves the 0x5da/5 of 0.9f with light 1 at its
// position, draws rand() % 200 (with 0, the joint 0x613/0xc there) and advances 20 by FCOS and FSIN of z (30, not
// converted to radians). 0x539328..0x539384: the /0x10 the same with the 0x6f4/4 of scale 1 and its light, without its
// own rand(), and 30. The position's bits come from a separate computation (sine and cosine exact to 64 bits, products
// and sums at 53, fstp to float).
TEST(SpiritJointsTest, TheStillOnesLeaveTheirParticlesAndDrift)
{
    struct Case
    {
        std::int32_t subtype;
        std::int32_t type;
        std::int32_t particleSubtype;
        std::uint32_t scale;
        std::array<std::uint32_t, 2> drifted;
    };
    for (const Case& each : {Case{0xb, 0x5da, 5, 0x3f666666u, {0x42ce2b89u, 0x43343d47u}},
                             Case{0x10, 0x6f4, 4, 0x3f800000u, {0x42d1414du, 0x432a5bebu}}})
    {
        SCOPED_TRACE(each.subtype);
        Pool pool;
        pool.startAngles = {0.0f, 0.0f, 30.0f};
        ASSERT_TRUE(pool.Create(0x613, each.subtype, {100.0f, 200.0f, 0.0f}, 30.0f).HasValue());
        ASSERT_EQ(pool.joints[0].life, 50);
        const JointState before = pool.joints[0];
        const CrtRandom start = pool.random;

        ASSERT_TRUE(pool.Step().HasValue());

        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.angles.x, 50.0f);
        CrtRandom replay = start;
        const glm::vec3 light = each.subtype == 0xb ? glm::vec3(1.0f) : before.light;
        const auto twin = Twin(replay, each.type, each.particleSubtype, before.position, {50.0f, 0.0f, 30.0f}, light,
                               std::bit_cast<float>(each.scale));
        ExpectSameParticle(pool.particles.All()[0], twin);
        if (each.subtype == 0xb)
        {
            EXPECT_NE(replay.Next() % 200, 0);
        }
        EXPECT_EQ(pool.random.State(), replay.State());
        EXPECT_FALSE(pool.joints[1].alive);
        EXPECT_EQ(Bits(joint.position.x), each.drifted[0]);
        EXPECT_EQ(Bits(joint.position.y), each.drifted[1]);
        EXPECT_EQ(joint.position.z, 0.0f);
    }
}

// 0x538f96..0x538fc3: with rand() % 200 at 0, the /0xb drops the joint 0x613/0xc (life 30) from the copy of its
// position, with its angles (x already the life), without an owner and of width 0.9f; from a 0x6ee, also a 0x613.
TEST(SpiritJointsTest, TheStillOneSometimesReleasesASmoke)
{
    std::uint32_t seed = 0;
    for (std::uint32_t candidate = 1; candidate < 100000 && seed == 0; ++candidate)
    {
        CrtRandom probe(candidate);
        static_cast<void>(Twin(probe, 0x5da, 5, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 0.9f));
        if (probe.Next() % 200 == 0)
            seed = candidate;
    }
    ASSERT_NE(seed, 0U);
    Pool pool;
    pool.startAngles = {0.0f, 0.0f, 30.0f};
    ASSERT_TRUE(pool.Create(0x6ee, 0xb, {100.0f, 200.0f, 0.0f}, 30.0f).HasValue());
    pool.joints[3] = pool.joints[0];
    pool.joints[0].alive = false;
    pool.random = CrtRandom(seed);

    ASSERT_TRUE(pool.Step().HasValue());

    const JointState& smoke = pool.joints[0];
    ASSERT_TRUE(smoke.alive);
    EXPECT_EQ(smoke.type, 0x613);
    EXPECT_EQ(smoke.subtype, 0xc);
    EXPECT_EQ(smoke.ownerContext, nullptr);
    EXPECT_EQ(smoke.position, (glm::vec3{100.0f, 200.0f, 0.0f}));
    EXPECT_EQ(smoke.angles, (glm::vec3{50.0f, 0.0f, 30.0f}));
    EXPECT_EQ(Bits(smoke.width), 0x3f666666u);
    EXPECT_EQ(smoke.life, 30);
    EXPECT_FALSE(pool.joints[1].alive);
    EXPECT_EQ(Bits(pool.joints[3].position.x), 0x42ce2b89u);
}

// 0x538ff9..0x539091: the /0xc draws rand() & 31 and at its position leaves the 0x5f5/0 of scale (r + 48) · 0.01f (bits
// from a separate computation) with the light (1; 0.2f; 0.1f) and then the 0x5da/6 of 0.6f with light 1, with its
// angles.
TEST(SpiritJointsTest, TheSmokeLeavesTwoParticles)
{
    constexpr std::array<std::uint32_t, 32> Scales{
        0x3ef5c28fu, 0x3efae147u, 0x3f000000u, 0x3f028f5cu, 0x3f051eb8u, 0x3f07ae14u, 0x3f0a3d70u, 0x3f0ccccdu,
        0x3f0f5c29u, 0x3f11eb85u, 0x3f147ae1u, 0x3f170a3du, 0x3f199999u, 0x3f1c28f6u, 0x3f1eb852u, 0x3f2147aeu,
        0x3f23d70au, 0x3f266666u, 0x3f28f5c2u, 0x3f2b851eu, 0x3f2e147bu, 0x3f30a3d7u, 0x3f333333u, 0x3f35c28fu,
        0x3f3851ebu, 0x3f3ae147u, 0x3f3d70a4u, 0x3f400000u, 0x3f428f5cu, 0x3f451eb8u, 0x3f47ae14u, 0x3f4a3d70u};
    Pool pool;
    pool.startAngles = {0.0f, 0.0f, 30.0f};
    ASSERT_TRUE(pool.Create(0x613, 0xc, {100.0f, 200.0f, 0.0f}, 30.0f).HasValue());
    const JointState before = pool.joints[0];
    CrtRandom replay = pool.random;

    ASSERT_TRUE(pool.Step().HasValue());

    const std::int32_t size = replay.Next() & 31;
    const float scale = std::bit_cast<float>(Scales[static_cast<std::size_t>(size)]);
    const auto smoke = Twin(replay, 0x5f5, 0, before.position, before.angles,
                            {1.0f, std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3dcccccdu)}, scale);
    const auto glow =
        Twin(replay, 0x5da, 6, before.position, before.angles, glm::vec3(1.0f), std::bit_cast<float>(0x3f19999au));
    ExpectSameParticle(pool.particles.All()[0], smoke);
    ExpectSameParticle(pool.particles.All()[1], glow);
    EXPECT_EQ(AliveParticles(pool.particles), 2U);
    EXPECT_EQ(pool.random.State(), replay.State());
    EXPECT_EQ(pool.joints[0].position, before.position);
}

// 0x539133..0x53931e: the /0xf adds 30 to the key. With life under 35 (the creation's, 20) it leaves the 0x5d9/7 of
// scale 1 with light 1 at its position and draws rand() & 1: with 1, nothing more. It always advances k = key · 0.1f +
// 30 with the sine and cosine of z (30). With 35 or more, it only advances (without rand()). Bits from a separate
// computation.
TEST(SpiritJointsTest, TheSpiralTurnsWithItsKey)
{
    std::uint32_t seed = 0;
    for (std::uint32_t candidate = 1; candidate < 100000 && seed == 0; ++candidate)
    {
        CrtRandom probe(candidate);
        static_cast<void>(Twin(probe, 0x5d9, 7, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 1.0f));
        if ((probe.Next() & 1) != 0)
            seed = candidate;
    }
    ASSERT_NE(seed, 0U);
    Pool pool;
    pool.startAngles = {0.0f, 0.0f, 30.0f};
    ASSERT_TRUE(pool.Create(0x613, 0xf, {100.0f, 200.0f, 0.0f}, 30.0f).HasValue());
    EXPECT_EQ(pool.joints[0].life, 20);
    pool.random = CrtRandom(seed);
    const JointState before = pool.joints[0];

    ASSERT_TRUE(pool.Step().HasValue());

    CrtRandom replay(seed);
    const auto twin = Twin(replay, 0x5d9, 7, before.position, before.angles, glm::vec3(1.0f), 1.0f);
    ExpectSameParticle(pool.particles.All()[0], twin);
    EXPECT_EQ(replay.Next() & 1, 1);
    EXPECT_EQ(pool.random.State(), replay.State());
    EXPECT_FALSE(pool.joints[1].alive);
    EXPECT_EQ(pool.joints[0].key, 29);
    EXPECT_EQ(Bits(pool.joints[0].position.x), 0x4286fcceu);
    EXPECT_EQ(Bits(pool.joints[0].position.y), 0x4342ecd5u);

    pool.joints[0].life = 40;
    pool.joints[0].position = {100.0f, 200.0f, 0.0f};
    const CrtRandom still = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    // k = 59 · 0.1f + 30 retained; bits from a separate computation (sine and cosine of 30 exact to 64 bits).
    EXPECT_EQ(pool.joints[0].key, 59);
    EXPECT_EQ(Bits(pool.joints[0].position.x), 0x42810f30u);
    EXPECT_EQ(Bits(pool.joints[0].position.y), 0x4342765eu);
    EXPECT_EQ(pool.random.State(), still.State());
}

// 0x5391a9..0x53923a: with rand() & 1 at 0, the position copy becomes (x − FSIN(z) · k, FCOS(z) · k + y, 350) with k =
// 40 − key · 0.1f retained and drops two joints 0x613/0xe (life 10, the angles' x at 20 from their creation) with the
// angles (0, 0, 0) and (0, 0, 3), of width 0.9f; they land in earlier slots and do not step in this round. At (0, 0)
// with z at 2, the retained k gives (0xc206f093, 0xc1770657) and stored as a float would give (0xc206f092,
// 0xc1770656). Bits from a separate computation.
TEST(SpiritJointsTest, TheSpiralSometimesReleasesTwoSplashes)
{
    std::uint32_t seed = 0;
    for (std::uint32_t candidate = 1; candidate < 100000 && seed == 0; ++candidate)
    {
        CrtRandom probe(candidate);
        static_cast<void>(Twin(probe, 0x5d9, 7, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 1.0f));
        if ((probe.Next() & 1) == 0)
            seed = candidate;
    }
    ASSERT_NE(seed, 0U);
    Pool pool;
    pool.startAngles = {0.0f, 0.0f, 2.0f};
    ASSERT_TRUE(pool.Create(0x613, 0xf, {0.0f, 0.0f, 0.0f}, 30.0f).HasValue());
    pool.joints[3] = pool.joints[0];
    pool.joints[0].alive = false;
    pool.random = CrtRandom(seed);

    ASSERT_TRUE(pool.Step().HasValue());

    for (std::size_t piece = 0; piece < 2; ++piece)
    {
        SCOPED_TRACE(piece);
        const JointState& splash = pool.joints[piece];
        ASSERT_TRUE(splash.alive);
        EXPECT_EQ(splash.type, 0x613);
        EXPECT_EQ(splash.subtype, 0xe);
        EXPECT_EQ(splash.ownerContext, nullptr);
        EXPECT_EQ(Bits(splash.position.x), 0xc206f093u);
        EXPECT_EQ(Bits(splash.position.y), 0xc1770657u);
        EXPECT_EQ(splash.position.z, 350.0f);
        EXPECT_EQ(splash.angles, (glm::vec3{20.0f, 0.0f, piece == 0 ? 0.0f : 3.0f}));
        EXPECT_EQ(Bits(splash.width), 0x3f666666u);
        EXPECT_EQ(splash.life, 10);
    }
    EXPECT_FALSE(pool.joints[2].alive);
    EXPECT_EQ(pool.joints[3].key, 29);
    EXPECT_EQ(Bits(pool.joints[3].position.x), 0x41ef53bcu);
    EXPECT_EQ(Bits(pool.joints[3].position.y), 0x415b0f48u);

    Pool other;
    int otherOwner = 0;
    ASSERT_TRUE(other.Create(0x613, 0xf, {}, 30.0f, &otherOwner, JointOwnerKind::ByType).HasValue());
    const auto cut = other.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x539249")) << cut.Error();
}

// 0x539392..0x53941e: the /0x11 with life 100 drops 120 joints 0x613/0x10 of width 0.9f with the angles (0, 0, 3 · i).
// Those that land in later slots step in the same round (and leave their 0x6f4/4); here all land before.
TEST(SpiritJointsTest, TheBurstReleasesItsPieces)
{
    Pool pool;
    pool.joints = std::vector<JointState>(130);
    ASSERT_TRUE(pool.Create(0x613, 0x11, {10.0f, 20.0f, 30.0f}, 30.0f).HasValue());
    EXPECT_EQ(pool.joints[0].life, 100);
    pool.joints[129] = pool.joints[0];
    pool.joints[0].alive = false;
    ASSERT_TRUE(pool.Step().HasValue());
    for (std::size_t i = 0; i < 120; ++i)
    {
        SCOPED_TRACE(i);
        const JointState& piece = pool.joints[i];
        ASSERT_TRUE(piece.alive);
        EXPECT_EQ(piece.subtype, 0x10);
        EXPECT_EQ(piece.angles, glm::vec3(0.0f, 0.0f, static_cast<float>(i) * 3.0f));
        EXPECT_EQ(piece.width, std::bit_cast<float>(0x3f666666u));
        EXPECT_EQ(piece.position, glm::vec3(10.0f, 20.0f, 30.0f));
        EXPECT_EQ(piece.life, 50);
    }
    EXPECT_FALSE(pool.joints[120].alive);
    EXPECT_EQ(pool.joints[129].life, 99);
    // On the next step, each piece (0x613/0x10) leaves its 0x6f4/4 and advances 30 by the cosine and sine of its z.
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(AliveParticles(pool.particles), 120U);
    EXPECT_EQ(pool.joints[0].position, glm::vec3(40.0f, 20.0f, 30.0f));
    EXPECT_EQ(pool.joints[0].angles.x, 50.0f);
}

// 0x53942c..0x539500: the /0x12 raises the speed by 3, moves the key by 6 and aims −90 − key in x and 10 more in z;
// it leaves the 0x5f2/0x13 of scale 7 with light 1 at its position and the tail follows. With a character owner it
// stops earlier, at 0x539494 (only the 0x125's model effect is ported, test_high_effect_families). The /0x14 rises like
// the /8.
TEST(SpiritJointsTest, TheSwingAndTheTwentiethStep)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x613, 0x12, {}, 30.0f).HasValue());
    const CrtRandom start = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    const JointState& joint = pool.joints[0];
    EXPECT_EQ(joint.speed, 53.0f);
    EXPECT_EQ(joint.key, 5); // 39 % 12 = 3: + 6.
    EXPECT_EQ(joint.angles.x, -95.0f);
    EXPECT_EQ(joint.angles.z, 10.0f);
    EXPECT_EQ(joint.life, 38);
    CrtRandom replay = start;
    const auto twin = Twin(replay, 0x5f2, 0x13, joint.position, joint.angles, glm::vec3(1.0f), 7.0f);
    ExpectSameParticle(pool.particles.All()[0], twin);
    EXPECT_EQ(pool.random.State(), replay.State());

    Pool owned;
    ActorState owner = Owner({});
    ASSERT_TRUE(owned.Create(0x613, 0x12, {}, 30.0f, &owner, JointOwnerKind::Actor).HasValue());
    const auto ownerCut = owned.Step();
    ASSERT_FALSE(ownerCut.HasValue());
    EXPECT_TRUE(ownerCut.Error().starts_with("0x539494")) << ownerCut.Error();

    Pool rising;
    ASSERT_TRUE(rising.Create(0x613, 0x14, {}, 30.0f).HasValue());
    ASSERT_TRUE(rising.Step().HasValue());
    EXPECT_EQ(rising.joints[0].angles.x, 110.0f);
    EXPECT_EQ(rising.joints[0].speed, 10.0f);
}

// 0x53c6a0: the common drawing, with the type's texture and blend 3 (subtract) or 1 (add); the /2 only the second face.
TEST(SpiritJointsTest, TheJointsDrawWithTheirBlend)
{
    std::array<std::size_t, 2> vertices{};
    for (const std::int32_t subtype : {1, 2})
    {
        SCOPED_TRACE(subtype);
        Pool pool;
        ASSERT_TRUE(pool.Create(0x613, subtype, {}, 50.0f).HasValue());
        ASSERT_TRUE(pool.Step().HasValue());
        ASSERT_EQ(pool.joints[0].count, 1);
        Client::Render::ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Client::Joints::DrawJoints(batch, pool.joints, 0).HasValue());
        ASSERT_TRUE(batch.State().texture.has_value());
        EXPECT_EQ(*batch.State().texture, Client::Render::TextureSlot{0x613});
        EXPECT_EQ(batch.State().blend,
                  subtype == 1 ? Client::Render::ModelBlend::Subtractive : Client::Render::ModelBlend::Additive);
        vertices[static_cast<std::size_t>(subtype - 1)] = batch.Vertices().size();
    }
    EXPECT_GT(vertices[1], 0u);
    EXPECT_EQ(vertices[0], 2 * vertices[1]);
}
} // namespace
