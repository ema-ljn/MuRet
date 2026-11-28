#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Models/Cloth.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The 0x10f with a character as owner (AuraJoints.cpp): creation 0x530edd..0x531214 and step 0x535d4f..0x5366a7. 06,
// "Los joints de las familias (CL11)".
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointOwnerKind;
using Client::Kernel::JointState;

constexpr std::int32_t Aura = 0x10f;

using Client::Math::D;
using Client::Math::F32;

struct Pool
{
    Pool()
    {
        actors[0].alive = true;
        actors[0].position = {1000, 2000, 300};
        cloth->logicSteps = 77;
        cloth->openCounterAccesses = 1; // without the four rand() of the close.
    }

    Client::Core::Expected<std::optional<std::size_t>, std::string>
    Create(std::int32_t subtype, float width, std::int32_t target = -1, glm::vec3 to = {0, 0, 0})
    {
        glm::vec3 from = actors[0].position;
        glm::vec3 angles = startAngles;
        const Client::Kernel::JointRequest request{.type = Aura,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = actors.data(),
                                                   .scale = width,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = target};
        return Client::Joints::CreateFaunaJoint(joints, request, cloth->random, JointOwnerKind::Actor);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(joints, cloth->random, cloth.get(), nullptr, 0, nullptr, nullptr, {}, {},
                                          actors, {}, effects);
    }

    std::vector<JointState> joints = std::vector<JointState>(4);
    std::vector<ActorState> actors = std::vector<ActorState>(3);
    std::unique_ptr<Client::Models::ClothWorld> cloth = std::make_unique<Client::Models::ClothWorld>();
    glm::vec3 startAngles{};
    Client::Effects::ModelEffects* effects = nullptr;
};

// 0x5363bf..0x536450 with the integer frequency, computed apart from MuRet's code (Python's Fraction and Decimal): FSIN
// and FCOS at 64 bits, the products in PC53, the sine of z straight to float (0x53642f) and the spills to float. Phase
// −77 is slot 0's with 77 steps; 53808, slot 1's.
glm::vec3 GoldenDirection(std::int32_t phase)
{
    if (phase == -77)
        return {std::bit_cast<float>(0x3f3ffc03u), std::bit_cast<float>(0xbf29561bu),
                std::bit_cast<float>(0xbbe42472u)};
    EXPECT_EQ(phase, 53808);
    return {std::bit_cast<float>(0xbe5d83c5u), std::bit_cast<float>(0xbf7610b0u), std::bit_cast<float>(0x3e2f529eu)};
}

// 0x536388..0x5364d2: the point of slot `index` around `target`, with the integer frequency.
glm::vec3 RingPoint(std::size_t index, std::int32_t steps, const glm::vec3& target, double lift)
{
    const auto slot = static_cast<std::uint32_t>(index);
    const auto phase =
        std::bit_cast<std::int32_t>(slot * 53731u + ((slot & 1u) != 0 ? std::bit_cast<std::uint32_t>(steps)
                                                                      : 0u - std::bit_cast<std::uint32_t>(steps)));
    const glm::vec3 direction = GoldenDirection(phase);
    return {F32(D(direction[0]) * 80 + D(target.x)), F32(D(direction[1]) * 80 + D(target.y)),
            F32(D(direction[2]) * 120 + D(target.z) + lift)};
}
} // namespace

// 0x530f05 and 0x536073: the 10f/0 is created with full light, 999999 life and 30 points, and turns around the owner,
// 110 above its +0x10 + 10. When the owner moves, the segments shift with it (0x5360dc..0x536185).
TEST(AuraJointsTest, TheRingsCircleTheirOwner)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0, 50).HasValue());
    const JointState& ring = pool.joints[0];
    EXPECT_EQ(ring.texture, 0x62b);
    EXPECT_EQ(ring.life, 999999);
    EXPECT_EQ(ring.maximumCount, 30);
    EXPECT_EQ(ring.light, glm::vec3(1));
    EXPECT_EQ(ring.width, 50.0f);
    EXPECT_EQ(ring.targetPosition, pool.actors[0].position);
    ASSERT_TRUE(pool.Step().HasValue());
    glm::vec3 target = pool.actors[0].position;
    target.z = 310;
    EXPECT_EQ(ring.targetPosition, target);
    EXPECT_EQ(ring.position, RingPoint(0, 77, target, 110));
    EXPECT_EQ(ring.count, 1);
    EXPECT_EQ(ring.life, 999998);
    const glm::vec3 before = ring.points[0][0];
    pool.actors[0].position.x = 1100;
    ASSERT_TRUE(pool.Step().HasValue());
    // The old segment 0 (already shifted) is now 1.
    EXPECT_EQ(ring.points[1][0].x, F32(D(F32(D(before.x) - 1000)) + 1100));
    // With the owner dead: life at 0, no drawing, and the common tail subtracts one.
    pool.actors[0].alive = false;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_FALSE(ring.alive);
    EXPECT_EQ(ring.life, -1);
}

// FaunaDirectionPC53 (0x53d1a0 with scale 1, the same block as 0x5363bf) gives the golden values.
TEST(AuraJointsTest, TheDirectionMatchesTheGoldenValues)
{
    for (const std::int32_t phase : {-77, 53808})
    {
        SCOPED_TRACE(phase);
        const auto direction = Client::Math::Detail::FaunaDirectionPC53(phase);
        ASSERT_TRUE(direction.has_value());
        const glm::vec3 golden = GoldenDirection(phase);
        for (int axis = 0; axis < 3; ++axis)
            EXPECT_EQ(std::bit_cast<std::uint32_t>((*direction)[static_cast<std::size_t>(axis)]),
                      std::bit_cast<std::uint32_t>(golden[axis]));
    }
}

// 0x536187..0x536383: with no other open access to the step counter, each ring opens and closes it, and the close draws
// four rand() (0x536383 → 0x412890), per ring and per step.
TEST(AuraJointsTest, EachRingClosesTheCounterWithFourRandoms)
{
    Pool pool;
    pool.cloth->openCounterAccesses = 0;
    ASSERT_TRUE(pool.Create(0, 50).HasValue());
    ASSERT_TRUE(pool.Create(0, 50).HasValue());
    CrtRandom expected = pool.cloth->random;

    ASSERT_TRUE(pool.Step().HasValue());

    for (int draw = 0; draw < 2 * 4; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(pool.cloth->random.State(), expected.State());
    glm::vec3 target = pool.actors[0].position;
    target.z = 310;
    EXPECT_EQ(pool.joints[0].position, RingPoint(0, 77, target, 110));
    EXPECT_EQ(pool.joints[1].position, RingPoint(1, 77, target, 110));
    EXPECT_EQ(pool.cloth->openCounterAccesses, 0);
}

// 0x53120e and 0x536073..0x536452: a subtype without a case (0xa, which no caller creates) is created with only the
// width and in its step reads the owner, puts the target 10 higher, opens and closes the counter access (four rand()
// with no other open) and goes to the tail, without shifting the segments or moving.
TEST(AuraJointsTest, TheUnhandledSubtypesOnlyReadTheirOwner)
{
    Pool pool;
    pool.cloth->openCounterAccesses = 0;
    ASSERT_TRUE(pool.Create(0xa, 20).HasValue());
    JointState& joint = pool.joints[0];
    joint.life = 5; // creation does not write it
    const glm::vec3 before = joint.position;
    CrtRandom expected = pool.cloth->random;

    ASSERT_TRUE(pool.Step().HasValue());

    for (int draw = 0; draw < 4; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(pool.cloth->random.State(), expected.State());
    EXPECT_EQ(joint.targetPosition, glm::vec3(1000, 2000, 310));
    EXPECT_EQ(joint.position, before);
    EXPECT_EQ(joint.count, -1); // no points: +0x6c stayed at 0 and the count reaches −1 (0x535520).
    pool.actors[0].alive = false;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, -1);
}

// 0x530f35 and 0x535d4f: the 10f/4 lives while its owner has 8 (or 0x2000000) in +0x78, with life at 100; with the
// character of its +0xa0c on Fenrir outside the safe zone it becomes /9 (140 high) and goes back to /4 on dismounting.
TEST(AuraJointsTest, TheBindingFollowsItsTargetOnFenrir)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(4, 20, 2).HasValue());
    JointState& binding = pool.joints[0];
    EXPECT_EQ(binding.target, 2);
    EXPECT_EQ(binding.life, 10000);
    EXPECT_EQ(binding.light, glm::vec3(std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3f4ccccdu),
                                       std::bit_cast<float>(0x3e4ccccdu)));
    pool.actors[0].flags78 = 8;
    pool.actors[2].pet.model = 0x1c37;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(binding.subtype, 9);
    EXPECT_EQ(binding.life, 99);
    EXPECT_EQ(binding.position, RingPoint(0, 77, {1000, 2000, 310}, 140));
    pool.actors[2].safeZone = 1;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(binding.subtype, 4);
    // Without the bit, it turns off.
    pool.actors[0].flags78 = 0;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_FALSE(binding.alive);
    // Another /4 with target −1 keeps the +0xa0c of the slot's previous use.
    ASSERT_TRUE(pool.Create(4, 20).HasValue());
    EXPECT_EQ(pool.joints[0].target, 2);
    // A target outside the characters stops.
    pool.joints[0].target = 7;
    pool.actors[0].flags78 = 8;
    const auto cut = pool.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x535d63"), std::string::npos) << cut.Error();
}

// 0x53106d and 0x536526: the 10f/5 is created without the first segment, 800 from its +0x24, and each step takes three
// turns that slow it (0.95) and make it fall; at batch 40 it releases the 0x6eb/3 wave (0x536626) and, without the
// effect table, stops. The /7 does not.
TEST(AuraJointsTest, TheRiseFallsUntilItsEffect)
{
    Pool pool;
    pool.startAngles = {0, 0, 30};
    const glm::vec3 base{500, 600, 700};
    // 0x53115b runs before 0x531166: the first segment uses the width of the slot's previous use.
    constexpr float PreviousWidth = 12.0f;
    pool.joints[0].width = PreviousWidth;
    ASSERT_TRUE(pool.Create(5, 25, -1, base).HasValue());
    const JointState& rise = pool.joints[0];
    for (const auto& [first, second] : {std::pair{0, 1}, std::pair{2, 3}})
    {
        const glm::vec3 across =
            rise.points[0][static_cast<std::size_t>(second)] - rise.points[0][static_cast<std::size_t>(first)];
        EXPECT_NEAR(std::sqrt(across.x * across.x + across.y * across.y + across.z * across.z), PreviousWidth, 1e-3);
    }
    EXPECT_EQ(rise.texture, 0x610);
    EXPECT_EQ(rise.drawEnabled, 1);
    EXPECT_FALSE(rise.tailEnabled);
    EXPECT_EQ(rise.life, 60);
    EXPECT_EQ(rise.light, glm::vec3(1, 1, std::bit_cast<float>(0x3f4ccccdu)));
    EXPECT_EQ(rise.count, 0);
    EXPECT_EQ(rise.width, 25.0f);
    const auto matrix = Client::Math::OriginalAngleMatrix(pool.startAngles);
    const auto turned = Client::Math::OriginalVectorRotate({0, 800, 0}, matrix);
    EXPECT_EQ(rise.position, glm::vec3(F32(D(turned.x) + 500), F32(D(turned.y) + 600), F32(D(turned.z) + 700)));
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(rise.angles.z, 60.0f);
    EXPECT_EQ(rise.count, 3);
    EXPECT_EQ(rise.width, 20.0f);
    Client::Core::Expected<void, std::string> stepped{};
    for (int step = 0; step < 60 && stepped.HasValue(); ++step)
        stepped = pool.Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x536626"), std::string::npos) << stepped.Error();
    EXPECT_EQ(rise.volley, 40);

    // With the table: the wave at that turn's +0x24, with the joint's angles and light, life 15 and scale 1, without
    // rand(); full, nothing and the same RNG.
    auto table = std::make_unique<Client::Effects::ModelEffects>();
    auto full = std::make_unique<Client::Effects::ModelEffects>();
    for (auto& effect : full->Effects())
        effect.alive = true;
    Pool withTable;
    Pool withFull;
    for (Pool* each : {&withTable, &withFull})
    {
        each->startAngles = pool.startAngles;
        each->joints[0].width = PreviousWidth;
        ASSERT_TRUE(each->Create(5, 25, -1, base).HasValue());
    }
    withTable.effects = table.get();
    withFull.effects = full.get();
    for (int step = 0; step < 60 && withTable.joints[0].volley <= 40; ++step)
    {
        const JointState before = withTable.joints[0];
        ASSERT_TRUE(withTable.Step().HasValue());
        ASSERT_TRUE(withFull.Step().HasValue());
        EXPECT_EQ(withTable.cloth->random.State(), withFull.cloth->random.State());
        if (before.volley <= 40 && withTable.joints[0].volley > 40)
        {
            const Client::Effects::ModelEffect& wave = table->Effects()[0];
            ASSERT_TRUE(wave.alive);
            EXPECT_EQ(wave.type, 0x6eb);
            EXPECT_EQ(wave.subtype, 3);
            EXPECT_EQ(wave.life, 15);
            EXPECT_EQ(wave.transform.scale, 1.0f);
            EXPECT_EQ(wave.transform.position.x, withTable.joints[0].pathIncrement.x);
            EXPECT_EQ(wave.transform.position.y, withTable.joints[0].pathIncrement.y);
            EXPECT_EQ(wave.light, withTable.joints[0].light);
        }
    }
    EXPECT_GT(withTable.joints[0].volley, 40);
    EXPECT_FALSE(table->Effects()[1].alive);

    Pool other;
    ASSERT_TRUE(other.Create(7, 25, -1, base).HasValue());
    for (int step = 0; step < 61; ++step)
        ASSERT_TRUE(other.Step().HasValue()) << step;
    EXPECT_FALSE(other.joints[0].alive);
    EXPECT_GT(other.joints[0].volley, 40);
}

// 0x53116e and 0x536005: the 10f/8 turns 25 in z around its +0x24, appends the tail and rises 15.
TEST(AuraJointsTest, TheSpinTurnsAndRises)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(8, 30, -1, {10, 20, 30}).HasValue());
    const JointState& spin = pool.joints[0];
    EXPECT_EQ(spin.texture, 0x5ac);
    EXPECT_EQ(spin.life, 40);
    EXPECT_EQ(spin.light, glm::vec3(0.5f));
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(spin.angles.z, 25.0f);
    EXPECT_EQ(spin.pathIncrement.z, 45.0f);
    EXPECT_EQ(spin.count, 1);
    const auto matrix = Client::Math::OriginalAngleMatrix({0, 0, 25});
    const auto turned = Client::Math::OriginalVectorRotate({0, -40, 0}, matrix);
    EXPECT_EQ(spin.position, glm::vec3(F32(D(turned.x) + 10), F32(D(turned.y) + 20), F32(D(turned.z) + 30)));
}

// 0x530f7e and 0x535dbe..0x535ee4: the 10f/2 takes the second point; without a live owner it lets go. With one, the
// width is life · 3 and the position goes from the owner's hand (+0x194) to the target shifted by 0x53d1a0(17721 ·
// slot, dir, 1.4f) · 300, with a = (life − 10) · 0.1f between 0 and 1. The bits come from outside MuRet (Python's
// Fraction and Decimal): the x frequency not stored as float (with it stored, slot 0's direction would be (0xbe847c40,
// 0xbf7734eb, 0xbcc1fe0a)), FSIN and FCOS at 64 bits, PC53 and the stores to float. Phase 77 in slot 0 and 17798 in
// slot 1.
TEST(AuraJointsTest, TheBeamRunsFromTheHandToItsPoint)
{
    Pool pool;
    pool.actors[0].skillHandPoint = {1100, 2200, 400};
    ASSERT_TRUE(pool.Create(2, 10, -1, {7, 8, 9}).HasValue());
    ASSERT_TRUE(pool.Create(2, 10, -1, {7, 8, 9}).HasValue());
    EXPECT_EQ(pool.joints[0].targetPosition, glm::vec3(7, 8, 9));
    EXPECT_EQ(pool.joints[0].texture, 0x62c);
    EXPECT_EQ(pool.joints[0].maximumCount, 5);
    EXPECT_EQ(pool.joints[0].life, 20);
    pool.joints[1].life = 13;
    const CrtRandom before = pool.cloth->random;

    ASSERT_TRUE(pool.Step().HasValue());

    // Life 20: a = 1.0000000149 → 1; the position is the point.
    EXPECT_EQ(pool.joints[0].width, 60.0f);
    EXPECT_EQ(pool.joints[0].position, glm::vec3(std::bit_cast<float>(0xc28d4689u), std::bit_cast<float>(0xc38cd8c4u),
                                                 std::bit_cast<float>(0x3ff66054u)));
    // Life 13: a = 0.3f and 1 − a = 0.7f.
    EXPECT_EQ(pool.joints[1].width, 39.0f);
    EXPECT_EQ(pool.joints[1].position, glm::vec3(std::bit_cast<float>(0x44359c23u), std::bit_cast<float>(0x44b73895u),
                                                 std::bit_cast<float>(0x43876087u)));
    EXPECT_EQ(pool.joints[1].life, 12);
    EXPECT_EQ(pool.cloth->random.State(), before.State());

    // Slot 0 with life 13, and with life 5 (a = 0: the hand), with the counter closed: four rand() per access.
    for (const auto& [life, expected] :
         {std::pair{13, glm::vec3(std::bit_cast<float>(0x443b33c1u), std::bit_cast<float>(0x44b5efbeu),
                                  std::bit_cast<float>(0x438c49eau))},
          std::pair{5, glm::vec3(1100, 2200, 400)}})
    {
        SCOPED_TRACE(life);
        Pool closed;
        closed.cloth->openCounterAccesses = 0;
        closed.actors[0].skillHandPoint = {1100, 2200, 400};
        ASSERT_TRUE(closed.Create(2, 10, -1, {7, 8, 9}).HasValue());
        closed.joints[0].life = life;
        CrtRandom expectedRandom = closed.cloth->random;
        for (int draw = 0; draw < 4; ++draw)
            static_cast<void>(expectedRandom.Next());
        ASSERT_TRUE(closed.Step().HasValue());
        EXPECT_EQ(closed.joints[0].position, expected);
        EXPECT_EQ(closed.cloth->random.State(), expectedRandom.State());
    }

    pool.actors[0].alive = false;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(pool.joints[0].ownerContext, nullptr);
}
