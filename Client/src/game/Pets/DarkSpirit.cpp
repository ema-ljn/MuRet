#include "Pets/DarkSpirit.h"

#include "Crt/CrtRandom.h"
#include "Maps/MapId.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Models/Animation.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Particles/Particles.h"
#include "Tables/PlayerBones.h"
#include "Terrain/TerrainDefines.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Client::Pets
{
namespace
{
// 0x4167b0: the rand() of the protected cells' closes (0x4128e7, eight) and of the counter (0x40b9a1, four), and the
// height above the terrain (0x6b8900).
constexpr int CellSize = static_cast<int>(Terrain::CellSize);
constexpr int CellCenter = 50;
constexpr int CellMask = 0xff;
constexpr double BirthHeight = 300.0;

constexpr std::uint32_t OwnerStops = 0x1000;
constexpr std::uint32_t OwnerHidesSparks = 0x4000;
// 0x416480: the animation step, and each state's action on wrapping (0x417f0c).
constexpr float FrameStep = 0.4f;
constexpr std::array<std::uint8_t, 6> ActionOfState{1, 0, 3, 2, 1, 3};
// 0x417b87: the spark, from one of the first 66 bones.
constexpr int SparkBones = 66;
constexpr glm::vec3 SparkLight{std::bit_cast<float>(0x3E99999Au), std::bit_cast<float>(0x3ECCCCCDu),
                               std::bit_cast<float>(0x3F333333u)};
constexpr float SparkScale = std::bit_cast<float>(0x3F4CCCCDu);
// 150² (0x6b8938): closer, it does not turn toward the owner and uses the near draw; 640², far.
constexpr double NearSquared = 22500.0;
constexpr float NearSquaredFloat = 22500.0f;
constexpr double FarSquared = 409600.0;
constexpr int TurnSpan = 15;
constexpr double TurnBase = 5.0;
// 0x417c95: the flight height over the owner, 350 on map 0x37 and 250 on the others (Q-C131), and the upper margin.
constexpr std::uint8_t LoginMap = Maps::LoginMap;
// 0x417c95 compares the pet word with 0x1a04, the Dark Horse's index (13, 4), not its model (0x1c16): with the 0x512a30
// equipment it never matches (Q-C413).
constexpr std::uint16_t HorseIndex = 0x1a04;
constexpr double LoginFlight = 350.0;
constexpr double Flight = 250.0;
constexpr double FlightBand = 100.0;
constexpr double PitchStep = 2.0;
constexpr float PitchLimit = 15.0f;
constexpr int LowDraw = 10;
constexpr int HighDraw = 20;
constexpr int CruiseDraw = 28;
constexpr int NearSpan = 8;
constexpr int NearBase = 32;
constexpr int NearTurnSpan = 60;
constexpr int FarSpan = 64;
constexpr int FarBase = 128;
constexpr int ClimbSpan = 64;
constexpr int ClimbCenter = 32;
constexpr double Tenth = Math::TenthAsDouble;
constexpr double MinusTenth = static_cast<double>(-0.1f);
constexpr double Half = Math::HalfAsDouble;
constexpr float FastBelow = -12.0f;
constexpr int ShoutSpan = 100;
constexpr int ShoutCounter = 60;
constexpr double Below = 200.0;
constexpr int FarSteps = 90;
constexpr double JumpHeight = 250.0;
constexpr std::uint8_t FastState = 0;
constexpr std::uint8_t SlowState = 1;
constexpr std::uint8_t PerchedState = 3;
constexpr std::uint8_t LandingState = 4;
// 0x4176f0: the safe zone is +0x3e2 exactly 1; the speed when starting to land (0x40400000).
constexpr std::uint8_t InSafeZone = 1;
constexpr float LandingSpeed = 3.0f;
// 0x4178a6: perched, the owner's bone 37 (0x6f0 / 0x30) with (−10, 0, 10) (0xc1200000, 0x41200000), and the owner's
// heading minus 120 (0x6b86c8).
constexpr std::size_t PerchBone = 37;
constexpr glm::vec3 PerchOffset{-10.0f, 0.0f, 10.0f};
constexpr double PerchTurn = 120.0;
// 0x417974: landing, toward the owner's bone 42 (0x7e0 / 0x30); accelerates 1 (0x6b87d4) and perches under 50
// (0x6b86b0).
constexpr std::size_t LandingBone = static_cast<std::size_t>(Tables::PlayerLeftHandBone);
constexpr double LandingAcceleration = 1.0;
constexpr double PerchReach = 50.0;
// The attack states (0x416ae3, 0x417833).
constexpr std::uint8_t ReturningState = 2;
constexpr std::uint8_t AttackingState = 5;
constexpr std::uint8_t ShoutingState = 6;
// 0x416b1d: +0x3b0 to 0xffff after the notice; state 6 does not turn with it (0x41784e).
constexpr std::uint16_t NoAttackTarget = 0xffff;
// 0x417a59: the attack destination, 50 (0x6b86b0) higher; under 20 (0x6b86d0) or with the far count over 20 it returns
// (0x417a84), with pitch at −45 (0xc2340000); order 3 keeps +0x1ad (0x417a9d).
constexpr double AttackLift = 50.0;
constexpr double AttackReach = 20.0;
constexpr std::int32_t AttackSteps = 20;
constexpr float ReturnPitch = -45.0f;
constexpr std::uint8_t FollowTargetOrder = 3;
// 0x417acf: +0xf0 rises 0.2 (0x6b8854) per step; 0x417b17: returning brakes 1 (0x6b87d4); at 250² (0x6b893c) from the
// destination, state 0.
constexpr double AttackAccelerationStep = static_cast<double>(0.2f);
constexpr double ReturnBrake = 1.0;
constexpr double ReturnedSquared = 62500.0;
constexpr std::uint8_t AttackAction = 3;
// 0x41788b: state 6 turns 40 (0x42200000) toward the +0x3b0 character.
constexpr float ShoutTurn = 40.0f;
// 0x417e33: +0x38d rises in states 5 and 6 up to 15.
constexpr std::uint8_t AttackCounterTop = 15;

using Math::F32;

// 0x4164f0, whatever the previous state.
void SetState(DarkSpirit& spirit, std::uint8_t state)
{
    spirit.state = state;
    spirit.far = 0;
}

void SetStateOf(DarkSpirit& spirit, std::uint8_t state)
{
    if (spirit.state != state)
    {
        SetState(spirit, state);
    }
}

// 0x4ec110(owner's model, own bones + bone · 0x30, offset, point, 1). With no bones (before the first draw), a zero
// matrix, like OwnBonePoint (Q-C318).
glm::vec3 OwnerBonePoint(const DarkSpiritOwner& owner, std::size_t bone, const glm::vec3& offset)
{
    const Math::BoneTransform matrix = bone < owner.pose.size() ? owner.pose[bone] : Math::BoneTransform(0.0f);
    return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, owner.modelScale, owner.modelOrigin);
}

// 0x4176f0..0x417775: in the safe zone, when neither perched nor landing, 0x4164f0(3) then 0x4164f0(4) with speed 3:
// the comparison of the owner distance with 50 reads the state already at 3 and decides nothing (Q-C414).
void FollowTheSafeZone(DarkSpirit& spirit, const DarkSpiritOwner& owner)
{
    const bool resting = spirit.state == PerchedState || spirit.state == LandingState;
    if (owner.safeZone == InSafeZone)
    {
        if (!resting)
        {
            SetState(spirit, LandingState);
            spirit.speed = LandingSpeed;
        }
        return;
    }
    if (resting)
    {
        SetState(spirit, FastState);
    }
}

// 0x4178a6..0x4178ec: on the owner's shoulder.
void Perch(DarkSpirit& spirit, const DarkSpiritOwner& owner)
{
    spirit.position = OwnerBonePoint(owner, PerchBone, PerchOffset);
    spirit.angles.x = owner.angles.x;
    spirit.angles.y = owner.angles.y;
    spirit.angles.z = F32(static_cast<double>(owner.angles.z) - PerchTurn);
}

// 0x4178fc..0x4179ea: the angle matrix before turning; with speed (fcomp with 0 and C3: neither 0 nor unordered), it
// advances (0, −speed, 0) rotated; turns toward bone 42 with 0x4e9810; with the x87 distance below 50 (or unordered),
// 0x4164f0(3).
void Land(DarkSpirit& spirit, const DarkSpiritOwner& owner)
{
    const Math::OriginalMatrix matrix = Math::OriginalAngleMatrix(spirit.angles);
    if (!(spirit.speed == 0.0f || std::isnan(spirit.speed)))
    {
        const glm::vec3 move = Math::OriginalVectorRotate(glm::vec3(0.0f, -spirit.speed, 0.0f), matrix);
        spirit.position.x = F32(static_cast<double>(move.x) + static_cast<double>(spirit.position.x));
        spirit.position.y = F32(static_cast<double>(move.y) + static_cast<double>(spirit.position.y));
        spirit.position.z = F32(static_cast<double>(move.z) + static_cast<double>(spirit.position.z));
    }
    const glm::vec3 shoulder = OwnerBonePoint(owner, LandingBone, glm::vec3(0.0f));
    const double distance = Math::MoveToward(spirit.position, spirit.angles, shoulder, spirit.speed);
    spirit.speed = F32(static_cast<double>(spirit.speed) + LandingAcceleration);
    if (!(distance >= PerchReach))
    {
        SetState(spirit, PerchedState);
    }
}

// 0x417b33..0x417e10: states 0 and 1, the free flight toward `destination` (the owner or, with +0x1ad at 1, the +8
// target). The spark checks the owner's flags, and the height its pet.
void Fly(DarkSpirit& spirit, const DarkSpiritOwner& owner, const glm::vec3& destination, std::uint8_t map,
         Particles::Particles& particles, float worldClock, Crt::CrtRandom& random)
{
    spirit.byte1ad = 0;
    const glm::vec3 start = spirit.position;
    const float dx = F32(static_cast<double>(destination.x) - static_cast<double>(start.x));
    const float dy = F32(static_cast<double>(destination.y) - static_cast<double>(start.y));
    // 0x417b75: from the translation of a bone of the last draw's pose (0x4ec110 untranslated). Before the first draw
    // the original reads uninitialized memory; MuRet uses the position (Q-C133).
    if ((owner.flags & OwnerHidesSparks) == 0)
    {
        const auto bone = static_cast<std::size_t>(random.Next() % SparkBones);
        const glm::vec3 from = bone < spirit.pose.size()
                                   ? Math::OriginalVectorTransform(glm::vec3(0.0f), spirit.pose[bone])
                                   : spirit.position;
        (void)particles.Create(Particles::SparkParticle, from, spirit.angles, SparkLight, Particles::RavenSparkSubtype,
                               SparkScale, worldClock, random);
    }
    // 0x417bcb: distance² on the x87 (fst stores it as float and compares the 53-bit one).
    const double distance =
        (static_cast<double>(dy) * static_cast<double>(dy)) + (static_cast<double>(dx) * static_cast<double>(dx));
    const float distanceFloat = F32(distance);
    if (distance >= NearSquared)
    {
        const float target = F32(Math::CreateAngle(start.x, start.y, destination.x, destination.y));
        const int turn = random.Next() % TurnSpan;
        spirit.angles.z = F32(Math::TurnAngle(spirit.angles.z, target, F32(static_cast<double>(turn) + TurnBase)));
    }
    const glm::vec3 move = Math::OriginalVectorRotate(spirit.direction, Math::OriginalAngleMatrix(spirit.angles));
    spirit.position.x = F32(static_cast<double>(move.x) + static_cast<double>(spirit.position.x));
    spirit.position.y = F32(static_cast<double>(move.y) + static_cast<double>(spirit.position.y));
    const double z = static_cast<double>(move.z) + static_cast<double>(spirit.position.z);
    spirit.position.z = F32(z);
    const double flight =
        ((owner.pet == HorseIndex || map == LoginMap) ? LoginFlight : Flight) + static_cast<double>(destination.z);
    int draw = CruiseDraw;
    if (z < flight)
    {
        spirit.angles.x = F32(static_cast<double>(spirit.angles.x) - PitchStep);
        draw = LowDraw;
        if (spirit.angles.x < -PitchLimit)
        {
            spirit.angles.x = -PitchLimit;
        }
    }
    else if (z > flight + FlightBand)
    {
        spirit.angles.x = F32(static_cast<double>(spirit.angles.x) + PitchStep);
        draw = HighDraw;
        if (spirit.angles.x > PitchLimit)
        {
            spirit.angles.x = PitchLimit;
        }
    }
    // 0x417d25: the direction draw. Near (with distance² as float), the rand() & 7 push and a rand() % 60 turn; far,
    // the rand() & 63 push, without rounding.
    if (random.Next() % draw == 0)
    {
        double push = 0.0;
        if (distanceFloat < NearSquaredFloat)
        {
            push = static_cast<double>(F32(static_cast<double>((random.Next() % NearSpan) + NearBase) * MinusTenth));
            spirit.angles.z =
                F32(static_cast<double>(random.Next() % NearTurnSpan) + static_cast<double>(spirit.angles.z));
        }
        else
        {
            push = static_cast<double>((random.Next() % FarSpan) + FarBase) * MinusTenth;
        }
        spirit.direction.x = 0.0f;
        spirit.direction.y = F32((push + static_cast<double>(spirit.direction.y)) * Half);
        spirit.direction.z = F32(static_cast<double>((random.Next() % ClimbSpan) - ClimbCenter) * Tenth);
    }
    SetStateOf(spirit, spirit.direction.y < FastBelow ? FastState : SlowState);
}

// 0x416a88..0x4176e6: with the +0x3b0 target among the 400 and with 0x4000 (0x416ab6), or the owner with 0x4000
// (0x416ac5, 0x4170d9), in states 5, 2 and 6, +0x3b0 to 0xffff and the A7 (the two copies, 0x416b05 and 0x417113,
// build the same one). Then 0x4164f0(3) and, beyond 50 from the owner (0x4170a9, 0x4176bf) or with the state already
// 3, which is not 4 and not above 3, 0x4164f0(4) with speed 3: always 4 (Q-C1206). The distance draws no rand().
bool StopTheHiddenAttack(DarkSpirit& spirit, const DarkSpiritOwner& owner, const DarkSpiritTargets& targets)
{
    const bool targetHides = targets.attackInPool && (targets.attackFlags & OwnerHidesSparks) == OwnerHidesSparks;
    const bool ownerHides = (owner.flags & OwnerHidesSparks) == OwnerHidesSparks;
    const bool attacking =
        spirit.state == AttackingState || spirit.state == ReturningState || spirit.state == ShoutingState;
    if (!(targetHides || ownerHides) || !attacking)
    {
        return false;
    }
    spirit.target3b0 = NoAttackTarget;
    SetState(spirit, PerchedState);
    SetState(spirit, LandingState);
    spirit.speed = LandingSpeed;
    return true;
}

// 0x4179ef..0x417b2e: states 5 and 2. The angle matrix (0x5fc8f0) and the advance of (0, −speed, 0) rotated, without
// checking whether the speed is 0. `dx` and `dy` are the destination's before advancing (0x417814).
void Charge(DarkSpirit& spirit, const Models::Skeleton& skeleton, glm::vec3 destination, float dx, float dy)
{
    const Math::OriginalMatrix matrix = Math::OriginalAngleMatrix(spirit.angles);
    const glm::vec3 move = Math::OriginalVectorRotate(glm::vec3(0.0f, -spirit.speed, 0.0f), matrix);
    spirit.position.x = F32(static_cast<double>(move.x) + static_cast<double>(spirit.position.x));
    spirit.position.y = F32(static_cast<double>(move.y) + static_cast<double>(spirit.position.y));
    spirit.position.z = F32(static_cast<double>(move.z) + static_cast<double>(spirit.position.z));
    if (spirit.state == AttackingState)
    {
        // 0x417a56..0x417aa6: fcomp with 20 and C0 (less or unordered).
        destination.z = F32(static_cast<double>(destination.z) + AttackLift);
        const double distance = Math::MoveToward(spirit.position, spirit.angles, destination, spirit.speed);
        if (!(distance >= AttackReach) || spirit.far > AttackSteps)
        {
            SetState(spirit, ReturningState);
            spirit.angles.x = ReturnPitch;
            if (spirit.order != FollowTargetOrder)
            {
                spirit.byte1ad = 0;
            }
        }
        // 0x417aad..0x417ad5: the speed adds the previous +0xf0.
        ++spirit.far;
        spirit.speed = F32(static_cast<double>(spirit.speed) + static_cast<double>(spirit.floatF0));
        spirit.floatF0 = F32(static_cast<double>(spirit.floatF0) + AttackAccelerationStep);
    }
    else
    {
        // 0x417aec..0x417b1d: fcomp with 250² and C0: with the sum at 250² or more (not unordered), 0x4164f0(0).
        const double squared =
            (static_cast<double>(dy) * static_cast<double>(dy)) + (static_cast<double>(dx) * static_cast<double>(dx));
        if (!(squared < ReturnedSquared || std::isnan(squared)))
        {
            SetState(spirit, FastState);
        }
        spirit.speed = F32(static_cast<double>(spirit.speed) - ReturnBrake);
    }
    // 0x417adb and 0x417b26: 0x4ea030(3).
    Models::SetAction(spirit.animation, skeleton, AttackAction);
}

// 0x417847..0x41789d: state 6 turns toward that character (0x4e93f0 from its position before the step, and 0x4e9560).
void Shout(DarkSpirit& spirit, const DarkSpiritTargets& targets)
{
    if (spirit.target3b0 == NoAttackTarget)
    {
        return;
    }
    const float angle = F32(
        Math::CreateAngle(spirit.position.x, spirit.position.y, targets.attackPosition.x, targets.attackPosition.y));
    spirit.angles.z = F32(Math::TurnAngle(spirit.angles.z, angle, ShoutTurn));
}
} // namespace

DarkSpirit CreateDarkSpirit(int cellX, int cellY, double groundHeight, Crt::CrtRandom& random,
                            DarkSpiritBirthAccesses accesses)
{
    // 0x4167b0 reads the owner's Y and X, and 0x415be0 the counter: they only spend if they close at zero.
    const std::array closes{accesses.cellY == 0, accesses.cellX == 0, accesses.counter == 0};
    for (const bool closesInZero : closes)
    {
        if (!closesInZero)
        {
            continue;
        }
        for (std::uint16_t k = 0; k < Models::SmallLogicalCloseRandoms; ++k)
        {
            static_cast<void>(random.Next());
        }
    }
    DarkSpirit spirit;
    // 0x415c25: the cells with `and 0xff`; 0x415ea7 stores the terrain height as float and 0x4169ec adds 300 to that.
    const int x = cellX & CellMask;
    const int y = cellY & CellMask;
    spirit.position =
        glm::vec3(static_cast<float>((x * CellSize) + CellCenter), static_cast<float>((y * CellSize) + CellCenter),
                  F32(static_cast<double>(F32(groundHeight)) + BirthHeight));
    spirit.previousPosition = spirit.position;
    return spirit;
}

DarkSpiritStepped StepDarkSpirit(DarkSpirit& spirit, const Models::Skeleton& skeleton, const DarkSpiritOwner& owner,
                                 std::uint8_t map, Particles::Particles& particles, float worldClock,
                                 Crt::CrtRandom& random, std::int32_t counter, const DarkSpiritTargets& targets)
{
    spirit.previousPosition = spirit.position;
    DarkSpiritStepped stepped;
    if ((owner.flags & OwnerStops) != 0)
    {
        return stepped;
    }
    // 0x416a82..0x416a8b: the raven CHARACTER's byte +0x1d6 goes to +0x8e; MuRet has neither.
    stepped.stopsTheAttack = StopTheHiddenAttack(spirit, owner, targets);
    FollowTheSafeZone(spirit, owner);
    if (spirit.animation.action < skeleton.actions.size() &&
        Models::AdvanceAnimation(spirit.animation, skeleton.actions[spirit.animation.action], FrameStep,
                                 Models::AnimationScene::Other))
    {
        // 0x417791: beyond the table, action 0.
        Models::SetAction(spirit.animation, skeleton,
                          spirit.state < ActionOfState.size() ? ActionOfState[spirit.state] : 0);
    }
    // 0x4177b1..0x417822: the destination, and the float distance before moving.
    const glm::vec3 destination = spirit.byte1ad == 1 ? targets.target : owner.position;
    const float dx = F32(static_cast<double>(destination.x) - static_cast<double>(spirit.position.x));
    const float dy = F32(static_cast<double>(destination.y) - static_cast<double>(spirit.position.y));
    if (spirit.state == FastState || spirit.state == SlowState)
    {
        Fly(spirit, owner, destination, map, particles, worldClock, random);
    }
    else if (spirit.state == AttackingState || spirit.state == ReturningState)
    {
        Charge(spirit, skeleton, destination, dx, dy);
    }
    else if (spirit.state == ShoutingState)
    {
        Shout(spirit, targets);
    }
    else if (spirit.state == PerchedState)
    {
        Perch(spirit, owner);
    }
    else if (spirit.state == LandingState)
    {
        Land(spirit, owner);
    }
    // 0x417e15..0x417e37: +0x38d rises (with the byte wrap) and stays at 15.
    if (spirit.state == AttackingState || spirit.state == ShoutingState)
    {
        ++spirit.byte38d;
        if (spirit.byte38d >= AttackCounterTop)
        {
            spirit.byte38d = AttackCounterTop;
        }
    }
    // 0x417e3e: the shout, with rand() % 100 and the signed counter read from 0x7d3a0f0.
    stepped.shouts = random.Next() % ShoutSpan == 0 && counter % ShoutCounter == 0;
    const double farX = static_cast<double>(owner.position.x) - static_cast<double>(spirit.position.x);
    const double farY = static_cast<double>(owner.position.y) - static_cast<double>(spirit.position.y);
    if (static_cast<double>(owner.position.z) - Below > static_cast<double>(spirit.position.z) ||
        (farY * farY) + (farX * farX) > FarSquared)
    {
        ++spirit.far;
    }
    if (spirit.far > FarSteps)
    {
        spirit.position =
            glm::vec3(owner.position.x, owner.position.y, F32(static_cast<double>(owner.position.z) + JumpHeight));
        spirit.previousPosition = spirit.position;
        spirit.far = 0;
    }
    return stepped;
}
} // namespace Client::Pets
