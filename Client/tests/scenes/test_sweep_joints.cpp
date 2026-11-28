#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Math/X87.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Skills/Skill.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointKernels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Actors/SkillHits.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "Terrain/WorldTerrain.h"

#include "support/SameBytes.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The 0x61c/0 (SweepJoints.cpp): creation 0x5342e4..0x534399 and step 0x53b3e1..0x53b623, with the request of effect
// 0x5be (0x52a72a: 0x530860(0x61c, position + 100 in z, the same, +0x28, 0, +0x108, 150, +0x8a, +0x89, 0, −1)).
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointState;

constexpr std::uint32_t Seed = 0x61cu;
constexpr std::size_t Last = 19;

using Client::Math::F32;

struct SweepWorld
{
    // The sweep in the last slot: the 0x614/3 bolts it drops land in the first ones, which this step already passed.
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(glm::vec3 angles, void* owner = nullptr)
    {
        for (std::size_t slot = 0; slot < Last; ++slot)
            joints[slot].alive = true;
        glm::vec3 from = start;
        const Client::Kernel::JointRequest request{.type = 0x61c,
                                                   .firstPosition = from,
                                                   .secondPosition = from,
                                                   .angles = angles,
                                                   .subtype = 0,
                                                   .ownerContext = owner,
                                                   .scale = 150.0f,
                                                   .key = 7,
                                                   .skill = 3,
                                                   .skillSerial = 0,
                                                   .target = -1};
        auto created = Client::Joints::CreateFaunaJoint(joints, request, random);
        for (std::size_t slot = 0; slot < Last; ++slot)
            joints[slot].alive = false;
        return created;
    }

    Client::Core::Expected<void, std::string> Step(const ActorState* hero = nullptr)
    {
        return Client::Joints::StepJoints(joints, random, nullptr, particles.get(), 0.0f, nullptr, nullptr, {}, {}, {},
                                          {}, nullptr, hero, nullptr, volley.get());
    }

    glm::vec3 start{1000.0f, 2000.0f, 400.0f};
    std::vector<JointState> joints = std::vector<JointState>(Last + 1);
    std::unique_ptr<Client::Particles::Particles> particles = std::make_unique<Client::Particles::Particles>();
    CrtRandom random{Seed};
    // The siege, the attributes and the hero's targets (0x53b569..0x53b5b1).
    std::unique_ptr<Client::Joints::JointKernels::VolleyServices> volley;
};

constexpr std::size_t HeroSlot = 42;
// The skill's slot in the own record (the joint's +0x9ea) and the skill there: 0x37, which creates the 0x5be
// (0x4f9fae).
constexpr std::uint8_t SkillPlace = 3;
constexpr std::uint8_t Skill = 0x37;
// +0x9eb: 0x530860 does not write it; the 0x61c/0 sends the one the slot had.
constexpr std::uint8_t Volley = 0x0c;
// 0x517a20 with targets: 0x518515's padding and the serial close (0x40b413) of the C3, after the C1 10.
constexpr std::uint32_t TargetRandoms = 2u;

// The hero's world for 0x517a20: Lorencia, the hero in slot 42 and two monsters, one 30 from the first segment and
// another near the sixth (turn 5).
struct HeroWorld
{
    HeroWorld()
    {
        world.Runtime().map = 0;
        world.Runtime().hero = HeroSlot;
        world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
        world.OwnCharacter().SetSkillAt(SkillPlace, Skill);
        ActorState& hero = world.Actors()[HeroSlot];
        hero.alive = true;
        hero.seen = true;
        hero.byte88 = 1;
        hero.key = 0x0101;
        hero.position = {5000.0f, 5000.0f, 0.0f};
        Put(5, 0x1005, {1000.0f, 1850.0f, 0.0f});
        Put(6, 0x1006, {850.0f, 1900.0f, 0.0f});
    }

    void Put(std::size_t slot, std::uint16_t key, glm::vec3 position)
    {
        ActorState& monster = world.Actors()[slot];
        monster.alive = true;
        monster.seen = true;
        monster.byte88 = 2;
        monster.key = key;
        monster.position = position;
    }

    [[nodiscard]] std::function<void(Client::Protocol::Outgoing)> Capture()
    {
        return [this](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); };
    }

    std::unique_ptr<Client::Kernel::WorldState> ownedWorld = std::make_unique<Client::Kernel::WorldState>();
    Client::Kernel::WorldState& world = *ownedWorld;
    Client::Terrain::WorldTerrain terrain;
    std::vector<Client::Protocol::Outgoing> sent;
};

// The game's services (GameSceneLogic.cpp) over the hero's world: the siege and the attributes as they are when called,
// and 0x517a20 with the sweep's RNG.
void Attach(SweepWorld& sweep, HeroWorld& hero)
{
    sweep.volley =
        std::make_unique<Client::Joints::JointKernels::VolleyServices>(Client::Joints::JointKernels::VolleyServices{
            .siegeActive = hero.world.Runtime().castleFlags.siegeActive,
            .attributes = hero.terrain.attributes.get(),
            .skillHits = [&sweep, &hero](const Client::Actors::SkillHitsRequest& request)
                -> Client::Core::Expected<void, std::string>
            {
                const auto hit = Client::Actors::HitTheTargetsAround(request, hero.world, hero.terrain, sweep.random,
                                                                     hero.Capture());
                if (!hit.HasValue())
                    return Client::Core::Unexpected(hit.Error());
                return {};
            }});
}

// The particle 0x5d9 with rand() & 1 at 0 and the widths of a segment's two bolts (0x53b4a0..0x53b51d), in the replay.
void ReplaySegment(CrtRandom& replay, Client::Particles::Particles& twin, glm::vec3 point)
{
    if ((replay.Next() & 1) == 0)
    {
        ASSERT_TRUE(
            twin.CreateChecked(0x5d9, point, glm::vec3(0.0f), glm::vec3(1.0f), 0, 1.0f, 0.0f, replay).HasValue());
    }
    static_cast<void>(replay.Next());
    static_cast<void>(replay.Next());
}

// The segment point with the sweep's velocity: the target (the start) plus (0, −180, 0) turned.
glm::vec3 SegmentPoint(glm::vec3 velocity)
{
    const auto swing =
        Client::Math::OriginalVectorRotate({0.0f, -180.0f, 0.0f}, Client::Math::OriginalAngleMatrix(velocity));
    return {F32(static_cast<double>(swing.x) + 1000.0), F32(static_cast<double>(swing.y) + 2000.0),
            F32(static_cast<double>(swing.z) + 400.0)};
}
} // namespace

// 0x5342e4..0x534399: without rand(); the angles' z + 30, the velocity (+0x9dc) with those angles, the target at the
// start, life 20, 18 points, the count at −1, without the common tail, the width; the position, (0, −180, 0) turned by
// the new angles and added, and the light at 1. With z at −30 the angles end at 0 and the turn is exact.
TEST(SweepJointsTest, TheSweepBornsHundredEightyAway)
{
    SweepWorld tested;
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}).HasValue());
    EXPECT_EQ(tested.random.State(), CrtRandom{Seed}.State());
    const JointState& joint = tested.joints[Last];
    EXPECT_TRUE(joint.alive);
    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.width, 150.0f);
    EXPECT_EQ(joint.angles, glm::vec3(0.0f));
    EXPECT_EQ(joint.velocity, glm::vec3(0.0f));
    EXPECT_EQ(joint.targetPosition, tested.start);
    EXPECT_EQ(joint.position, (glm::vec3{1000.0f, 1820.0f, 400.0f}));
    EXPECT_EQ(joint.life, 20);
    EXPECT_EQ(joint.maximumCount, 18);
    EXPECT_EQ(joint.count, -1);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.key, 7);
    EXPECT_EQ(joint.skill, 3);

    SweepWorld turned;
    ASSERT_TRUE(turned.Create({10.0f, 0.0f, 60.0f}).HasValue());
    const JointState& other = turned.joints[Last];
    EXPECT_EQ(other.angles, (glm::vec3{10.0f, 0.0f, 90.0f}));
    EXPECT_EQ(other.velocity, other.angles);
    const auto reach =
        Client::Math::OriginalVectorRotate({0.0f, -180.0f, 0.0f}, Client::Math::OriginalAngleMatrix(other.angles));
    EXPECT_EQ(other.position,
              (glm::vec3{F32(static_cast<double>(reach.x) + 1000.0), F32(static_cast<double>(reach.y) + 2000.0),
                         F32(static_cast<double>(reach.z) + 400.0)}));
}

// 0x53b3f1..0x53b5c3: without an owner, eight segments: the position is the target plus (0, −180, 0) turned by the
// velocity, a segment, vz − 11; with rand() & 1 at 0 the particle 0x5d9 and always two 0x614/3 without an owner from
// the previous position to the new one, with key 5 (their life) and skill 0xa (their points), of width rand() % 10 + 5
// and (rand() & 7) + 4. Then the light is life · 0.033333335f (0x53b611).
TEST(SweepJointsTest, TheSweepShedsTwoSparksForEachOfItsEightSegments)
{
    SweepWorld tested;
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}).HasValue());
    ASSERT_TRUE(tested.Step().HasValue());

    const JointState& joint = tested.joints[Last];
    EXPECT_EQ(joint.count, 7);
    EXPECT_EQ(joint.velocity.z, -88.0f);
    EXPECT_EQ(joint.life, 19);
    const float light = F32(20.0 * static_cast<double>(std::bit_cast<float>(0x3d088889u)));
    EXPECT_EQ(joint.light, glm::vec3(light));
    // The first segment, with the velocity at 0: 180 toward −y from the target.
    EXPECT_EQ(tested.joints[0].targetPosition, (glm::vec3{1000.0f, 1820.0f, 400.0f}));
    EXPECT_EQ(tested.joints[0].position, (glm::vec3{1000.0f, 1820.0f, 400.0f}));

    CrtRandom replay{Seed};
    auto twin = std::make_unique<Client::Particles::Particles>();
    int fires = 0;
    glm::vec3 velocity{};
    for (std::size_t turn = 0; turn < 8; ++turn)
    {
        SCOPED_TRACE(turn);
        const auto swing =
            Client::Math::OriginalVectorRotate({0.0f, -180.0f, 0.0f}, Client::Math::OriginalAngleMatrix(velocity));
        const glm::vec3 point{F32(static_cast<double>(swing.x) + 1000.0), F32(static_cast<double>(swing.y) + 2000.0),
                              F32(static_cast<double>(swing.z) + 400.0)};
        velocity.z = F32(static_cast<double>(velocity.z) - 11.0);
        if ((replay.Next() & 1) == 0)
        {
            ASSERT_TRUE(
                twin->CreateChecked(0x5d9, point, glm::vec3(0.0f), glm::vec3(1.0f), 0, 1.0f, 0.0f, replay).HasValue());
            ++fires;
        }
        const auto first = static_cast<float>(replay.Next() % 10 + 5);
        const auto second = static_cast<float>((replay.Next() & 7) + 4);
        const JointState& small = tested.joints[2 * turn];
        const JointState& large = tested.joints[2 * turn + 1];
        for (const JointState* spark : {&small, &large})
        {
            EXPECT_TRUE(spark->alive);
            EXPECT_EQ(spark->type, 0x614);
            EXPECT_EQ(spark->subtype, 3);
            EXPECT_EQ(spark->life, 5);
            EXPECT_EQ(spark->maximumCount, 10);
            EXPECT_EQ(spark->ownerContext, nullptr);
            EXPECT_EQ(spark->targetPosition, point);
        }
        EXPECT_EQ(small.width, first);
        EXPECT_EQ(large.width, second);
    }
    EXPECT_EQ(tested.random.State(), replay.State());
    std::ptrdiff_t alive = 0;
    for (const auto& particle : tested.particles->All())
        alive += particle.alive && particle.type == 0x5d9 ? 1 : 0;
    EXPECT_EQ(alive, fires);
}

// 0x53b3f8: with the count at points − 1 (17) it adds no segments and draws no rand(); with life under 10, the light's
// x times 0.6666667f in the three channels (0x53b5fa).
TEST(SweepJointsTest, TheFullSweepOnlyDims)
{
    SweepWorld tested;
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}).HasValue());
    JointState& joint = tested.joints[Last];
    joint.count = 17;
    joint.life = 9;
    joint.light = {0.5f, 0.25f, 0.75f};
    const glm::vec3 before = joint.position;

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_EQ(tested.random.State(), CrtRandom{Seed}.State());
    EXPECT_EQ(joint.position, before);
    EXPECT_EQ(joint.count, 17);
    const float dim = F32(0.5 * static_cast<double>(std::bit_cast<float>(0x3f2aaaabu)));
    EXPECT_EQ(joint.light, glm::vec3(dim));
    EXPECT_EQ(joint.life, 8);
    EXPECT_FALSE(tested.joints[0].alive);
}

// 0x53b53e..0x53b567: with the owner equal to the hero ([0x743a170]), life over 18 and the turn a multiple of 5, the
// siege or the skill's targets (0x53b569); without them (JointKernels::VolleyServices), MuRet stops there. With another
// owner it goes on as without an owner (the same rand(), bolts and particles). Without the hero in the step it stops at
// 0x53b548, except with life 18, where it does not compare.
TEST(SweepJointsTest, OnlyTheHeroSweepAsksForTheSiegeAndTheTargets)
{
    ActorState owner{};
    ActorState other{};
    SweepWorld tested;
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    const auto cut = tested.Step(&owner);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x53b569")) << cut.Error();
    EXPECT_EQ(tested.joints[Last].count, 0);

    SweepWorld foreign;
    ASSERT_TRUE(foreign.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    SweepWorld ownerless;
    ASSERT_TRUE(ownerless.Create({0.0f, 0.0f, -30.0f}).HasValue());
    ASSERT_TRUE(foreign.Step(&other).HasValue());
    ASSERT_TRUE(ownerless.Step(&other).HasValue());
    EXPECT_EQ(foreign.joints[Last].count, 7);
    EXPECT_EQ(foreign.joints[Last].life, 19);
    EXPECT_EQ(foreign.random.State(), ownerless.random.State());
    EXPECT_EQ(foreign.joints[Last].position, ownerless.joints[Last].position);
    for (std::size_t slot = 0; slot < Last; ++slot)
    {
        SCOPED_TRACE(slot);
        EXPECT_EQ(foreign.joints[slot].alive, ownerless.joints[slot].alive);
        EXPECT_EQ(foreign.joints[slot].width, ownerless.joints[slot].width);
    }
    std::size_t differences = 0;
    for (std::size_t slot = 0; slot < foreign.particles->All().size(); ++slot)
        differences += foreign.particles->All()[slot].alive == ownerless.particles->All()[slot].alive ? 0U : 1U;
    EXPECT_EQ(differences, 0U);

    SweepWorld blind;
    ASSERT_TRUE(blind.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    const auto unknown = blind.Step();
    ASSERT_FALSE(unknown.HasValue());
    EXPECT_TRUE(unknown.Error().starts_with("0x53b548")) << unknown.Error();

    SweepWorld late;
    ASSERT_TRUE(late.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    late.joints[Last].life = 18;
    ASSERT_TRUE(late.Step().HasValue());
    EXPECT_EQ(late.joints[Last].count, 7);
}

// 0x53b569..0x53b5b6: the hero's sweep, without the siege, in turns 0 and 5 (life 20) calls 0x517a20(+0x9ea, position,
// 150, +0x9eb, +0x9e8, 0) with the position of the segment just added: the own record's close (3848 rand(), 0x40b4d0)
// and, with targets, the C1 10 with the C3's two rand() (0x518515's padding and the serial close, 0x40b413), among the
// segments' rand(). In turn 0 the monster at 30 gets in, with cell (9, 18): 1000 · 0.01f (0x6b86c0) stays under 10. In
// turn 5 (vz −55) the segment lands at (1000 − 180 · sin 55°, 2000 − 180 · cos 55°) ≈ (852.56; 1896.76): the 0x1006 is
// at ~4.1 and the 0x1005 at ~154.7 (outside 150), with cell (8, 18). The step goes on with all the turns.
TEST(SweepJointsTest, TheHeroSweepHitsTheTargetsAroundEveryFiveTurns)
{
    HeroWorld hero;
    SweepWorld tested;
    Attach(tested, hero);
    ActorState& owner = hero.world.Actors()[HeroSlot];
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    tested.joints[Last].volley = Volley;

    const auto stepped = tested.Step(&owner);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.joints[Last].count, 7);
    EXPECT_EQ(tested.joints[Last].life, 19);

    // Each call: the own record's close (0x517af3, without looking at the targets); with targets, also the C3's two
    // rand(). Both calls of this sweep have targets.
    constexpr std::uint32_t HitRandoms = Client::Models::OwnRecordCloseRandoms + TargetRandoms;
    CrtRandom replay{Seed};
    auto particles = std::make_unique<Client::Particles::Particles>();
    glm::vec3 velocity{};
    for (int turn = 0; turn < 8; ++turn)
    {
        SCOPED_TRACE(turn);
        const glm::vec3 point = SegmentPoint(velocity);
        velocity.z = F32(static_cast<double>(velocity.z) - 11.0);
        ReplaySegment(replay, *particles, point);
        if (turn % 5 == 0)
            replay.Discard(HitRandoms);
    }
    EXPECT_EQ(tested.random.State(), replay.State());
    ASSERT_EQ(hero.sent.size(), 2u);
    Client::Protocol::Skills::SkillHitsSend first{.skill = Skill, .x = 9, .y = 18, .volley = Volley, .hits = 1};
    first.keys = {0x1005, 0, 0, 0, 0};
    EXPECT_TRUE(Client::Tests::SameBytes(hero.sent[0].packet.Bytes(),
                                         Client::Protocol::Skills::WriteSkillHits(first).packet.Bytes()));
    Client::Protocol::Skills::SkillHitsSend second{.skill = Skill, .x = 8, .y = 18, .volley = Volley, .hits = 1};
    second.keys = {0x1006, 0, 0, 0, 0};
    EXPECT_TRUE(Client::Tests::SameBytes(hero.sent[1].packet.Bytes(),
                                         Client::Protocol::Skills::WriteSkillHits(second).packet.Bytes()));
}

// 0x517a44..0x517af3: 0x517a20 opens and closes the own record on entry, with or without targets: the close (3848
// rand(), 0x40b4d0 at 0x517af3) runs on every call, and only the C3's two rand() (0x518515 and 0x40b413) go with the C1
// 10. With the 0x1006 far away, turn 5's call finds nobody within 150 (the 0x1005 is at ~154.7): it sends nothing and
// draws only the close.
TEST(SweepJointsTest, AHeroSweepCallWithoutTargetsStillClosesTheOwnRecord)
{
    HeroWorld hero;
    hero.Put(6, 0x1006, {9000.0f, 9000.0f, 0.0f});
    SweepWorld tested;
    Attach(tested, hero);
    ActorState& owner = hero.world.Actors()[HeroSlot];
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    tested.joints[Last].volley = Volley;

    const auto stepped = tested.Step(&owner);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.joints[Last].count, 7);
    CrtRandom replay{Seed};
    auto particles = std::make_unique<Client::Particles::Particles>();
    glm::vec3 velocity{};
    for (int turn = 0; turn < 8; ++turn)
    {
        SCOPED_TRACE(turn);
        const glm::vec3 point = SegmentPoint(velocity);
        velocity.z = F32(static_cast<double>(velocity.z) - 11.0);
        ReplaySegment(replay, *particles, point);
        if (turn == 0)
            replay.Discard(Client::Models::OwnRecordCloseRandoms + TargetRandoms);
        else if (turn == 5)
            replay.Discard(Client::Models::OwnRecordCloseRandoms);
    }
    EXPECT_EQ(tested.random.State(), replay.State());
    ASSERT_EQ(hero.sent.size(), 1u);
    Client::Protocol::Skills::SkillHitsSend first{.skill = Skill, .x = 9, .y = 18, .volley = Volley, .hits = 1};
    first.keys = {0x1005, 0, 0, 0, 0};
    EXPECT_TRUE(Client::Tests::SameBytes(hero.sent[0].packet.Bytes(),
                                         Client::Protocol::Skills::WriteSkillHits(first).packet.Bytes()));
}

// 0x53b56e..0x53b583 and 0x53b5cb..0x53b5e6: with the siege on (0x4294b0) and the bit 0x100 in the attribute of the
// segment's cell (0x5f82c0), speed at 0, life at ftol(20 · 0.2f) = 4 and it leaves the turns after the first segment,
// without 0x517a20: with life under 10 the light goes out (0x53b5fa) and life drops to 3. With the cell without the
// bit, it goes on as without the siege.
TEST(SweepJointsTest, TheSiegeStopsTheHeroSweepOnTheMarkedCells)
{
    for (const std::uint32_t attribute : {0x100u, 0x4u})
    {
        SCOPED_TRACE(attribute);
        HeroWorld hero;
        hero.world.Runtime().castleFlags.siegeActive = true;
        hero.terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        hero.terrain.attributes->cells.fill(static_cast<std::uint16_t>(attribute));
        SweepWorld tested;
        Attach(tested, hero);
        ActorState& owner = hero.world.Actors()[HeroSlot];
        ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
        JointState& joint = tested.joints[Last];
        joint.speed = 5.0f;

        ASSERT_TRUE(tested.Step(&owner).HasValue());

        if (attribute != 0x100u)
        {
            HeroWorld open;
            SweepWorld plain;
            Attach(plain, open);
            ActorState& same = open.world.Actors()[HeroSlot];
            ASSERT_TRUE(plain.Create({0.0f, 0.0f, -30.0f}, &same).HasValue());
            ASSERT_TRUE(plain.Step(&same).HasValue());
            EXPECT_EQ(joint.count, 7);
            EXPECT_EQ(joint.speed, 5.0f);
            EXPECT_EQ(tested.random.State(), plain.random.State());
            EXPECT_EQ(hero.sent.size(), open.sent.size());
            continue;
        }
        CrtRandom replay{Seed};
        auto particles = std::make_unique<Client::Particles::Particles>();
        ReplaySegment(replay, *particles, SegmentPoint(glm::vec3(0.0f)));
        EXPECT_EQ(tested.random.State(), replay.State());
        EXPECT_TRUE(hero.sent.empty());
        EXPECT_EQ(joint.count, 0);
        EXPECT_EQ(joint.speed, 0.0f);
        EXPECT_EQ(joint.life, 3);
        const float dim = F32(1.0 * static_cast<double>(std::bit_cast<float>(0x3f2aaaabu)));
        EXPECT_EQ(joint.light, glm::vec3(dim));
    }
}
