#include "Joints/FaunaJointCreation.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Models/IModelLibrary.h"
#include "Kernel/DrawTailState.h"
#include "Math/Ftol.h"
#include "Kernel/JointRequest.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalRotation.h"
#include "Math/OriginalAngles.h"
#include "Particles/KalimaParticleKernels.h"
#include "Particles/Particles.h"
#include "Effects/ModelEffects.h"
#include "Particles/Sprites.h"
#include "Audio/SoundId.h"
#include <cmath>
#include "Math/OriginalRetainedMath.h"
#include "Kernel/ActorState.h"
#include "Kernel/ActorSpirit.h"
#include "Fauna/FaunaObject.h"
#include "Kernel/HelperTable.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Terrain/DynamicWorldLight.h"
#include <glm/ext/vector_float3.hpp>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace Client::Joints
{
namespace
{
void Prefix(Kernel::JointState& joint, const Kernel::JointRequest& request, Kernel::JointOwnerKind ownerKind)
{
    joint.alive = true;
    joint.type = request.type;
    joint.texture = static_cast<std::uint16_t>(request.type);
    joint.subtype = request.subtype;
    joint.blend = 1;
    joint.drawEnabled = 3;
    joint.field9d4 = 0;
    joint.key = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(request.key));
    joint.skill = static_cast<std::uint8_t>(request.skill);
    joint.skillSerial = static_cast<std::uint8_t>(request.skillSerial);
    joint.speed = 0;
    joint.ownerContext = nullptr;
    joint.ownerKind = ownerKind;
    joint.tailEnabled = true;
    joint.category = 0;
    joint.repeatedTexture = false;
    joint.orientation = 0;
    joint.position = request.firstPosition;
    joint.angles = request.angles;
    joint.light = {1, 1, 1};
    joint.velocity = {};
    if (request.ownerContext == nullptr)
        joint.targetPosition = request.secondPosition;
    else
    {
        // 0x530967..0x530995: with an owner, the 0x10f/2 takes the second point and the /4 and /9 their target.
        if (request.type == JointFamilies::AuraJoint && request.subtype == 2)
            joint.targetPosition = request.secondPosition;
        else if (request.type == JointFamilies::AuraJoint && (request.subtype == 4 || request.subtype == 9) &&
                 request.target != -1)
            joint.target = request.target;
        joint.ownerContext = request.ownerContext;
    }
    joint.count = 0;
    joint.icarusRotations.reset();
}
using JointKernels::AppendJointTail;
using JointKernels::FirstQuad;
Core::Expected<void, std::string> CreateHelperBeam(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    joint.width = request.scale;
    joint.life = 0;
    joint.maximumCount = 10;
    joint.angles = {};
    joint.light = {0.3f, 0.3f, 1};
    joint.blend = 1;
    joint.speed = request.type == 0x614 ? 50.0f : 0.0f;
    for (int axis = 0; axis < 3; ++axis)
        joint.pathIncrement[axis] =
            static_cast<float>((static_cast<double>(request.secondPosition[axis]) - joint.position[axis]) * 0.1f);
    for (int sample = 0; sample < 9; ++sample)
    {
        for (int axis = 0; axis < 3; ++axis)
            joint.position[axis] =
                static_cast<float>(static_cast<double>(joint.position[axis]) + joint.pathIncrement[axis]);
        if (auto appended = AppendJointTail(joint); !appended.HasValue())
            return appended;
        // 0x5318e0/0x533385 keep these two stores, even though they cancel out algebraically.
        for (int axis = 0; axis < 2; ++axis)
            joint.position[axis] = static_cast<float>(
                (static_cast<double>(joint.position[axis]) - joint.pathIncrement[axis]) + joint.pathIncrement[axis]);
    }
    joint.position = joint.targetPosition; // 0x5333a7 reads the retained +0x58 with a non-null opaque owner.
    return {};
}

// 0x5309d6 and 0x531691..0x53180e: the 614/10 of sword 0x224 (0x50ee26). Speed 50, the width, the repeating texture
// (+0x9f0), life 0, angles at 0, light (0.3; 0.3; 1), 10 points and a tenth of what is left to the second point. Nine
// segments: each shifts the step by (rand() & 7) − 4 in x and y (with an owner, (rand() & 15) − 8), appends the tail
// with the zero-angle matrix and undoes the shift. At the end, the position is the target (+0x58).
Core::Expected<void, std::string> CreateSwordBeam(Kernel::JointState& joint, const Kernel::JointRequest& request,
                                                  Crt::CrtRandom& random)
{
    constexpr float Speed = 50;
    constexpr std::int32_t Points = 10;
    joint.speed = Speed;
    joint.width = request.scale;
    joint.repeatedTexture = true;
    joint.life = 0;
    joint.angles = {};
    joint.light = {0.3f, 0.3f, 1};
    joint.maximumCount = Points;
    for (int axis = 0; axis < 3; ++axis)
        joint.pathIncrement[axis] = static_cast<float>(
            (static_cast<double>(request.secondPosition[axis]) - static_cast<double>(joint.position[axis])) *
            static_cast<double>(0.1f));
    const auto matrix = Math::OriginalAngleMatrix(joint.angles);
    const bool owned = joint.ownerContext != nullptr;
    const int mask = owned ? 15 : 7;
    const double center = owned ? 8.0 : 4.0; // 0x6b86c4 and 0x6b8a44
    for (std::int32_t sample = 0; sample < joint.maximumCount - 1; ++sample)
    {
        const auto dx = static_cast<float>(static_cast<double>(random.Next() & mask) +
                                           static_cast<double>(joint.pathIncrement.x) - center);
        const auto dy = static_cast<float>(static_cast<double>(random.Next() & mask) +
                                           static_cast<double>(joint.pathIncrement.y) - center);
        joint.position.x = static_cast<float>(static_cast<double>(dx) + static_cast<double>(joint.position.x));
        joint.position.y = static_cast<float>(static_cast<double>(dy) + static_cast<double>(joint.position.y));
        joint.position.z =
            static_cast<float>(static_cast<double>(joint.pathIncrement.z) + static_cast<double>(joint.position.z));
        if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
        joint.position.x = static_cast<float>(static_cast<double>(joint.position.x) - static_cast<double>(dx) +
                                              static_cast<double>(joint.pathIncrement.x));
        joint.position.y = static_cast<float>(static_cast<double>(joint.position.y) - static_cast<double>(dy) +
                                              static_cast<double>(joint.pathIncrement.y));
    }
    joint.position = joint.targetPosition;
    return {};
}

// 0x531c43..0x531cc7, 0x531d24..0x531dcb and 0x532003: the 610/4 of sword 0x223 (0x50ec26), with the character's
// ActorState as owner (+0x54). The common 0x610 part sets life to the key, 50 points and the key to 0, and the skill
// color (+0x9ea) picks the width and the light; case 4 overwrites everything but the key. Blend 3, angles at 0, life 0,
// the width, light at 1, 10 points and a tenth of what is left to the second point (+0x24). Ten segments, each adding
// the step and appending the tail with the zero-angle matrix, and the target (+0x58) at the second point. No rand().
// With life 0, it lives one step (Q-C365).
Core::Expected<void, std::string> CreateSwordTrail(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    constexpr std::int32_t Points = 10;
    constexpr std::uint8_t OwnerColor = 3;
    // 0x531c71..0x531c8c: color 3 reads the owner's light (+0xf4), which case 4 overwrites; without an owner, the
    // original crashes.
    if (joint.skill == OwnerColor && joint.ownerContext == nullptr)
        return Core::Unexpected(std::string("0x531c7b: falta el color del dueño del joint 0x610/4"));
    joint.key = 0;
    joint.blend = 3;
    joint.angles = {};
    joint.life = 0;
    joint.width = request.scale;
    joint.light = {1, 1, 1};
    joint.maximumCount = Points;
    for (int axis = 0; axis < 3; ++axis)
        joint.pathIncrement[axis] = static_cast<float>(
            (static_cast<double>(request.secondPosition[axis]) - static_cast<double>(joint.position[axis])) *
            static_cast<double>(0.1f));
    const auto matrix = Math::OriginalAngleMatrix(joint.angles);
    for (std::int32_t sample = 0; sample < joint.maximumCount; ++sample)
    {
        for (int axis = 0; axis < 3; ++axis)
            joint.position[axis] = static_cast<float>(static_cast<double>(joint.position[axis]) +
                                                      static_cast<double>(joint.pathIncrement[axis]));
        if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
    }
    joint.targetPosition = request.secondPosition;
    return {};
}

// 0x537d49..0x537ee1: the 610/4 step. With the character dead, life −1 and the position 50 above the target. Alive, the
// target goes to the character's +0x17c and moves three times a third of what is left to its position (+0x10); each
// time the position is the target plus the speed rotated by the head angles (+0x28..+0x30), and the tail is appended,
// with the joint's angle matrix, only if it is not full yet or there is a skill. With life below 15 the light drops to
// two thirds. Both branches go on with the common tail and life.
Core::Expected<void, std::string> StepSwordTrail(Kernel::JointState& joint, std::size_t index)
{
    constexpr std::int32_t Steps = 3;
    constexpr std::int32_t FadingLife = 15;
    constexpr double Third = Math::ThirdAsDouble;                                   // 0x6ba2a0.
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f2aaaabu)); // 0x6b8c64.
    constexpr double Above = 50.0;                                                  // 0x6b86b0.
    const auto* actor = static_cast<const Kernel::ActorState*>(joint.ownerContext);
    if (actor == nullptr)
        return Core::Unexpected(std::format("0x537d49: falta el personaje dueño del joint {} (0x610/4)", index));
    if (!actor->alive)
    {
        joint.life = -1;
        joint.position = {joint.targetPosition.x, joint.targetPosition.y,
                          static_cast<float>(static_cast<double>(joint.targetPosition.z) + Above)};
        return {};
    }
    if (joint.velocity != glm::vec3(0.0f))
        return Core::Unexpected(
            std::format("0x537e3c: el joint {} (0x610/4) tiene velocidad y su giro pide +0x30 del personaje", index));
    glm::vec3 delta{};
    for (int axis = 0; axis < 3; ++axis)
        delta[axis] = static_cast<float>(static_cast<double>(actor->position[axis]) -
                                         static_cast<double>(actor->trailOrigin[axis]));
    joint.targetPosition = actor->trailOrigin;
    const auto matrix = Math::OriginalAngleMatrix(joint.angles);
    for (int axis = 0; axis < 3; ++axis)
        delta[axis] = static_cast<float>(static_cast<double>(delta[axis]) * Third);
    for (std::int32_t step = 0; step < Steps; ++step)
    {
        for (int axis = 0; axis < 3; ++axis)
            joint.targetPosition[axis] =
                static_cast<float>(static_cast<double>(delta[axis]) + static_cast<double>(joint.targetPosition[axis]));
        // 0x537e2b..0x537e65: the 610/4's speed (+0x9dc) stays at the prefix's 0, so rotating by the head angles (+0x30
        // is not in MuRet) gives only zeros, and the position is the target (Q-C364).
        joint.position = joint.targetPosition;
        const auto last = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.maximumCount) - 1u);
        if (joint.count < last || joint.skill != 0)
            if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
                return appended;
    }
    if (joint.life < FadingLife)
        for (int axis = 0; axis < 3; ++axis)
            joint.light[axis] = static_cast<float>(static_cast<double>(joint.light[axis]) * Fade);
    return {};
}

// 0x5319ea..0x531a30 and 0x533242: the 615/0 of the Icarus sky (0x605d70). Texture 0x614, the width, 50 points, life 20
// (0x531a12 stores the divisor; the remainder only gives the speed rand() % 20 + 80) and the light's z at 50. +0x24
// stores the start. One rand().
Core::Expected<void, std::string> CreateSkyBolt(Kernel::JointState& joint, const Kernel::JointRequest& request,
                                                Crt::CrtRandom& random)
{
    constexpr std::uint16_t BoltTexture = 0x614;
    constexpr std::int32_t Points = 50;
    constexpr int Divisor = 20;
    constexpr double SpeedBase = 80;
    joint.texture = BoltTexture;
    joint.width = request.scale;
    joint.maximumCount = Points;
    const int speed = random.Next() % Divisor;
    joint.life = Divisor;
    joint.speed = static_cast<float>(static_cast<double>(speed) + SpeedBase);
    joint.light.z = static_cast<float>(joint.maximumCount);
    joint.pathIncrement = joint.position;
    return {};
}

// 0x5319ea, 0x531b0c..0x531bd2 and 0x534cb6: the 615/1 of arriving summons (0x51747c..0x517550, ownerless, the tail
// off at 0x531b9e), the 615/2 of the Chaos Castle trap (0x405d49), the /5 of Kalima's 0x615 (0x52b560) and the /6 of
// the siege's 0x61c/1 pulse (0x52c31e and 0x52c342). Texture 0x614, the width, 50 points and life 20; x and y shift by
// rand() % 10 − 5 (fild and fadd, stored as float) and z rises 800 (0x6b8ac8; the /7, 1050). Zero angles, the target
// is the second point and +0x24 stores the already shifted start. The /5 with light (1; 0.5; 0.2f); the /6 with speed
// 0.3f, light at 1 and z another 700 higher (after storing +0x24). Two rand().
Core::Expected<void, std::string> CreateTrapBolt(Kernel::JointState& joint, const Kernel::JointRequest& request,
                                                 Crt::CrtRandom& random)
{
    constexpr std::uint16_t BoltTexture = 0x614;
    constexpr std::int32_t Points = 50;
    constexpr std::int32_t Life = 20;
    constexpr int JitterSpan = 10, JitterCenter = 5;
    constexpr double TrapRise = static_cast<double>(std::bit_cast<float>(0x44480000u));
    constexpr double ArrivalRise = static_cast<double>(std::bit_cast<float>(0x44834000u));
    joint.texture = BoltTexture;
    joint.width = request.scale;
    joint.maximumCount = Points;
    joint.life = Life;
    joint.position.x =
        static_cast<float>(static_cast<double>(random.Next() % JitterSpan - JitterCenter) + joint.position.x);
    joint.position.y =
        static_cast<float>(static_cast<double>(random.Next() % JitterSpan - JitterCenter) + joint.position.y);
    if (request.subtype == 7)
        joint.tailEnabled = false; // 0x531b59: the /7 keeps this in the step too.
    joint.position.z =
        static_cast<float>(static_cast<double>(joint.position.z) + (request.subtype == 7 ? ArrivalRise : TrapRise));
    joint.angles = {};
    joint.targetPosition = request.secondPosition;
    if (request.subtype == 1)
        joint.tailEnabled = false; // 0x531b9e: the /1 keeps this in the step too.
    joint.pathIncrement = joint.position;
    if (request.subtype == 5)
        joint.light = {1, 0.5f, std::bit_cast<float>(0x3e4ccccdu)}; // 0x531bbe..0x531bcb
    if (request.subtype == 6)
    {
        // 0x531bdc..0x531bfc.
        constexpr double StrikeRise = 700.0; // 0x6b8bec
        joint.speed = std::bit_cast<float>(0x3e99999au);
        joint.light = {1, 1, 1};
        joint.position.z = static_cast<float>(static_cast<double>(joint.position.z) + StrikeRise);
    }
    return {};
}

// 0x53265e (+0x10 = 3 in every 613 and 6ee), 0x5333f6..0x533449 and 0x5331a9: the 6ee/9 meteor of the Chaos Castle
// prelude (0x4054ad). Subtractive, the second face only, the width, speed 10, white light, angles (−90, 0, 0), zero
// deviation, life 20 and 15 points; +0x24 stores the start. No rand().
void CreateMeteor(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    constexpr std::uint8_t Subtractive = 3, SecondFace = 2;
    constexpr float Speed = 10, Pitch = -90;
    constexpr std::int32_t Life = 20, Points = 15;
    joint.blend = Subtractive;
    joint.drawEnabled = SecondFace;
    joint.width = request.scale;
    joint.speed = Speed;
    joint.light = {1, 1, 1};
    joint.angles = {Pitch, 0, 0};
    joint.velocity = {};
    joint.life = Life;
    joint.maximumCount = Points;
    joint.pathIncrement = joint.position;
}

// The full set's joints (0x50ba30, 0x5102c3..0x5104cb) and the one its 5ac/3 releases (0x52aa5d), with the
// character's ActorState as owner (+0x54).
constexpr std::int32_t SparkleJoint = 0x60f;
// Shares the 0x60f's creation (0x533677) and step (0x539f1b).
constexpr std::int32_t SparkImpactJoint = 0x62b;
constexpr std::int32_t SparkleSubtype = 0, FallenSparkleSubtype = 1, WideSparkleSubtype = 0x12;
// The quest reward's 60f/0xa (A3 0xc9..0xcb, 0x4de0fe): created like the 60f/0, it falls by vz each step.
constexpr std::int32_t FallingSparkleSubtype = 0xa;
constexpr std::int32_t SetTrailJoint = 0x610, SetTrailSubtype = 7;
// Arrow trails 0x610/0xc (the 0x11c), /0xd (Crywolf's 0xe1/1 missile) and /0xe (the 0xf9), owned by the arrow: the
// same creation (0x531ff2) and step (0x537ee6) case.
constexpr std::int32_t ArrowTrailFirst = 0xc, ArrowTrailLast = 0xe;
constexpr std::int32_t EnergyJoint = 0x619, SetEnergySubtype = 9, SkillEnergySubtype = 0xa;
// The 619/1 that the pet's 0x61e/1 decal releases (0x518900), owned by the hero (+0x54), and the 619/3 that the /3
// decal of the 0x1c and of summons releases (0x5189cc), owned by the summoning character.
constexpr std::int32_t SummonEnergySubtype = 1, SummonDecalEnergySubtype = 3;
// The 619/6 that the 0x813 releases three times per step (0x52cb85), ownerless.
constexpr std::int32_t BoltEnergySubtype = 6;
// The 619/8 of the wizard's charged skill (0x5189f0): three per step of a 0x20a in action 0x44 or 0x45, owned by the
// character (+0x54), which neither creation nor step reads.
constexpr std::int32_t ChargedEnergySubtype = 8;
// The 0x114/1's 6e8/0 and /1 swirls (0x51c25f), ownerless.
constexpr std::int32_t SwirlJoint = 0x6e8;
// The 61c/1 of the wizards' 0xd0/1 laser (0x51a5dd), owned by the effect.
constexpr std::int32_t LaserJoint = 0x61c, LaserSubtype = 1;

// The 0x60f/7, /0xb and /0x19 (WanderingJoints.cpp).
bool IsWandering(std::int32_t type, std::int32_t subtype)
{
    return type == JointFamilies::DropJoint &&
           (subtype == JointFamilies::WanderSubtype || subtype == JointFamilies::ArrowWanderSubtype ||
            subtype == JointFamilies::PinkWanderSubtype);
}

using Math::D;
using Math::F32;

// +0xf4 of the 60f's owner, its own light: the character's or the horse's (0x443d70 sets it to 3).
const glm::vec3* SparkleOwnerLight(const Kernel::JointState& joint)
{
    if (joint.ownerContext == nullptr)
        return nullptr;
    if (joint.ownerKind == Kernel::JointOwnerKind::Helper)
        return &static_cast<const Kernel::HelperState*>(joint.ownerContext)->light;
    if (joint.ownerKind == Kernel::JointOwnerKind::Raven)
        return &static_cast<const Kernel::DarkSpirit*>(joint.ownerContext)->light;
    return &static_cast<const Kernel::ActorState*>(joint.ownerContext)->light;
}

// +0x111 of the 60f's owner, the action: the character's or the horse's.
std::optional<std::uint8_t> SparkleOwnerAction(const Kernel::JointState& joint)
{
    if (joint.ownerContext == nullptr)
        return std::nullopt;
    if (joint.ownerKind == Kernel::JointOwnerKind::Helper)
        return static_cast<const Kernel::HelperState*>(joint.ownerContext)->horse.animation.action;
    if (joint.ownerKind == Kernel::JointOwnerKind::Raven)
        return static_cast<const Kernel::DarkSpirit*>(joint.ownerContext)->animation.action;
    return static_cast<const Kernel::ActorState*>(joint.ownerContext)->animation.action;
}

// The 610/8's owner (0x531c43..0x531f70 at creation, 0x53801e..0x538055 in the step): the Atlans fish (FaunaObject, by
// type) or, with `ownerKind` at Actor, the player entering Kalima (ActorState, 0x4c91dc). Both are the owner object:
// the alive byte at +0, the position at +0x10, the angles at +0x1c and the light at +0xf4. Nothing without an owner.
struct KalimaBeamOwner
{
    bool alive = false;
    const glm::vec3* position = nullptr;
    const glm::vec3* angles = nullptr;
    const glm::vec3* light = nullptr;
};

std::optional<KalimaBeamOwner> KalimaBeamOwnerOf(const Kernel::JointState& joint)
{
    if (joint.ownerContext == nullptr)
        return std::nullopt;
    if (joint.ownerKind == Kernel::JointOwnerKind::Actor)
    {
        const auto* actor = static_cast<const Kernel::ActorState*>(joint.ownerContext);
        return KalimaBeamOwner{actor->alive, &actor->position, &actor->angles, &actor->light};
    }
    const auto* fish = static_cast<const Fauna::FaunaObject*>(joint.ownerContext);
    return KalimaBeamOwner{fish->active != 0, &fish->position, &fish->angles, &fish->light};
}

void FadeJointLight(Kernel::JointState& joint, std::uint32_t factorBits)
{
    const double factor = D(std::bit_cast<float>(factorBits));
    for (int axis = 0; axis < 3; ++axis)
        joint.light[axis] = F32(D(joint.light[axis]) * factor);
}

// 0x533677..0x53369e, 0x5341fc..0x5342d2 and 0x534cb6: the full set's 60f/0 (0x51037c) and 60f/0x12 (0x5103be,
// 0x5103db, 0x510435 and 0x510452). Life 100, 20 points and the width (overwriting the 10 of 0x53368e); the /0x12
// subtracts (blend 4). The velocity's z (rand() % 150) · 0.01f, its y rand() % 500 − 250 and speed 40; with the width
// above 10, life 50 and another z, (rand() % 250 + 200) · 0.01f. The light is the owner's (+0xf4: the character's or,
// with the dark horse ones, the horse's) and the target the second point. Two rand(), or three with the width.
// The quest reward's 60f/0xa (0x4de0fe) goes through 0x53373a..0x5337da and continues at 0x53428f: the same in the same
// order (the speed 140 of 0x53378d is only the /0x19's), without the /0x12's blend.
Core::Expected<void, std::string> CreateSparkle(Kernel::JointState& joint, const Kernel::JointRequest& request,
                                                Crt::CrtRandom& random)
{
    constexpr std::int32_t SparkleLife = 100, WideLife = 50, Points = 20;
    constexpr float FirstWidth = 10, SparkleSpeed = 40;
    constexpr std::uint8_t Subtractive = 4;
    constexpr int RiseSpan = 150, DriftSpan = 500, DriftCenter = 250, WideRiseSpan = 250, WideRiseBase = 200;
    constexpr double Hundredth = Math::HundredthAsDouble; // 0x6b86c0, 0.01f
    const glm::vec3* ownerLight = SparkleOwnerLight(joint);
    if (ownerLight == nullptr)
        return Core::Unexpected(std::format("0x53429b: falta el dueño del joint 0x60f/{:#x}", request.subtype));
    joint.life = SparkleLife;
    joint.maximumCount = Points;
    joint.width = FirstWidth;
    if (request.subtype == WideSparkleSubtype)
        joint.blend = Subtractive;
    joint.width = request.scale;
    joint.velocity.z = F32(static_cast<double>(random.Next() % RiseSpan) * Hundredth);
    joint.velocity.y = static_cast<float>(random.Next() % DriftSpan - DriftCenter);
    joint.speed = SparkleSpeed;
    // 0x534252..0x53426a: fcomp with 10 and test ah,0x41: only above 10 (not NaN either).
    if (joint.width > FirstWidth)
    {
        joint.life = WideLife;
        joint.velocity.z = F32(static_cast<double>(random.Next() % WideRiseSpan + WideRiseBase) * Hundredth);
    }
    joint.light = *ownerLight;
    joint.targetPosition = request.secondPosition;
    return {};
}

// 0x531c43..0x531cc7, 0x531e2c..0x531ec7, 0x532003 and 0x534cb6: the full set's 610/7 at +13 (0x5104a8). The common
// 0x610 part sets life to the key, 50 points and the key to 0; case 7 overwrites the skill color's (+0x9ea) width and
// light: light at 1, vy and vz at 0, subtractive (blend 4), speed 10, texture 0x60f, the width, 16 points and vx 15. x
// and y shift 40 from the second point with fcos and fsin of rand() % 360 not converted to radians (Q-C384): x plus the
// cosine and y minus the sine. +0x24 stores the start and +0x58 the second point. Two rand().
Core::Expected<void, std::string> CreateSetTrail(Kernel::JointState& joint, const Kernel::JointRequest& request,
                                                 Crt::CrtRandom& random)
{
    constexpr std::uint8_t OwnerColor = 3;
    constexpr std::int32_t SharedPoints = 50, Points = 16;
    constexpr std::uint8_t Subtractive = 4;
    constexpr float Speed = 10, Sway = 15, Reach = 40; // 0x6b8bf4
    constexpr std::uint16_t FlareTexture = 0x60f;
    constexpr int FullTurn = Math::FullTurn;
    // 0x531c71..0x531c8c: color 3 reads the owner's light (+0xf4), which case 7 overwrites; without an owner, the
    // original crashes.
    if (joint.skill == OwnerColor && joint.ownerContext == nullptr)
        return Core::Unexpected(std::string("0x531c7b: falta el color del dueño del joint 0x610/7"));
    joint.life = joint.key;
    joint.key = 0;
    joint.maximumCount = SharedPoints;
    joint.light = {1, 1, 1};
    joint.velocity.y = 0;
    joint.velocity.z = 0;
    joint.blend = Subtractive;
    joint.speed = Speed;
    joint.texture = FlareTexture;
    joint.width = request.scale;
    joint.maximumCount = Points;
    joint.velocity.x = Sway;
    const auto cosine = Math::Detail::CosineProductPC53(static_cast<double>(random.Next() % FullTurn), Reach);
    if (!cosine)
        return Core::Unexpected(std::string("0x531e89: el coseno del joint 0x610/7 excede el perfil matemático"));
    joint.position.x = F32(*cosine + D(request.secondPosition.x));
    const auto sine = Math::Detail::SineProductPC53(static_cast<double>(random.Next() % FullTurn), Reach);
    if (!sine)
        return Core::Unexpected(std::string("0x531ea8: el seno del joint 0x610/7 excede el perfil matemático"));
    joint.position.y = F32(D(request.secondPosition.y) - *sine);
    joint.pathIncrement = joint.position;
    joint.targetPosition = request.secondPosition;
    return {};
}

// 0x531c43..0x531cc7, 0x531e04..0x531e27, 0x532003 and 0x534cb6: the 610/6 of the 0x11 of Kalima's 0x95 (0x43b0ed),
// owned by the target character (+0x54). The common 0x610 part sets life to the key, 50 points and the key to 0; case 6
// overwrites the skill color's (+0x9ea) width and light: light at 1, subtractive (blend 4), the width, 16 points and
// speed 5. The target (+0x58) is the second point. No rand().
Core::Expected<void, std::string> CreateTargetBolt(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    constexpr std::uint8_t OwnerColor = 3;
    constexpr std::int32_t SharedPoints = 50, Points = 16;
    constexpr std::uint8_t Subtractive = 4;
    constexpr float Speed = 5; // 0x40a00000
    // 0x531c71..0x531c8c: color 3 reads the owner's light (+0xf4), which case 6 overwrites; without an owner, the
    // original crashes.
    if (joint.skill == OwnerColor && joint.ownerContext == nullptr)
        return Core::Unexpected(std::string("0x531c7b: falta el color del dueño del joint 0x610/6"));
    joint.life = joint.key;
    joint.key = 0;
    joint.maximumCount = SharedPoints;
    joint.light = {1, 1, 1};
    joint.blend = Subtractive;
    joint.width = request.scale;
    joint.maximumCount = Points;
    joint.speed = Speed;
    joint.targetPosition = request.secondPosition;
    return {};
}

// 0x5345bf..0x5346d3 and 0x534cb6: the 6e8/0 and /1, the 0x114/1's swirls (0x51c25f, ownerless). No common tail (+0x9f2
// at 0); the width, speed 20, angles (−90, 0, z), life 10, 30 points and the +0x9dc speed at 0; the /1 also the
// repeated texture (+0x9f0) at 1. Light at 1 with an odd rand() and (1; 0.8f; 0.6f) with an even one, and the +0x24
// path at the position. One rand().
void CreateSwirl(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random)
{
    constexpr float Speed = 20, Upright = -90; // 0x41a00000 and 0xc2b40000
    constexpr std::int32_t Life = 10, Points = 30;
    joint.tailEnabled = false;
    if (request.subtype == 1)
        joint.repeatedTexture = true;
    joint.width = request.scale;
    joint.speed = Speed;
    joint.angles.x = Upright;
    joint.life = Life;
    joint.maximumCount = Points;
    joint.angles.y = 0;
    joint.angles.z = request.angles.z;
    joint.velocity = {};
    if ((random.Next() & 1) != 0)
        joint.light = {1, 1, 1};
    else
        joint.light = {1, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f19999au)};
    joint.pathIncrement = joint.position;
}

// 0x53439e..0x53442d and 0x534cb6: the 61c/1, the red ray of the wizards' 0xd0/1 laser (0x51a5dd), owned by the effect.
// Speed 0, the width, pass 1 (+0x64), light at 1, +0x9dc at 0, 15 points, life 30 and the batch (+0x9eb) at 0; the
// position and angles are the owner's (+0x10 and +0x1c). Without an owner, the original turns the joint off,
// dereferences the null pointer (+0x10, 0x5343fb) and crashes: MuRet stops. No rand().
Core::Expected<void, std::string> CreateLaserJoint(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    constexpr std::uint8_t FirstPass = 1;
    constexpr std::int32_t Points = 15, Life = 30;
    if (joint.ownerContext == nullptr)
        return Core::Unexpected(std::string("0x5343f8: falta el efecto dueño del joint 0x61c/1"));
    const auto& owner = *static_cast<const Effects::ModelEffect*>(joint.ownerContext);
    joint.speed = 0;
    joint.width = request.scale;
    joint.category = FirstPass;
    joint.light = {1, 1, 1};
    joint.velocity = {};
    joint.maximumCount = Points;
    joint.life = Life;
    joint.volley = 0;
    joint.position = owner.transform.position;
    joint.angles = owner.transform.angles;
    return {};
}

// 0x5323d3..0x5323f8, 0x53254f..0x5325c2 and 0x534cb6: the 619/9 of the full set's 5ac/3 (0x52aa5d). The common 0x619
// part (speed 0, life 12, the width and 2 points) and case 9: life 90, 20 points, the tail at 0, +0x9ec at the ftol of
// the z turn, vx 50, light (0.09090909f; 0.04545455f; 0.09090909f) and the target and position at the owner's (+0x10).
// No rand(). Case 0xa (0x5325c7..0x532659, the 0x35's four, 0x4fe856): life 80, vx 80 and +0x9d4 at 1 with the turn
// 225 or 405 (the direction of the turn in the step).
Core::Expected<void, std::string> CreateSetEnergy(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    constexpr std::int32_t SharedLife = 12, SharedPoints = 2, EnergyLife = 90, SkillLife = 80, Points = 20;
    constexpr float Orbit = 50, SkillOrbit = 80;
    constexpr std::int32_t FirstForward = 0xe1, SecondForward = 0x195;
    constexpr float Pale = std::bit_cast<float>(0x3dba2e8cu), Dim = std::bit_cast<float>(0x3d3a2e8cu);
    const bool skill = request.subtype == SkillEnergySubtype;
    const auto* owner = static_cast<const Kernel::ActorState*>(joint.ownerContext);
    if (owner == nullptr)
        return Core::Unexpected(std::format("{}: falta el personaje dueño del joint 0x619/{}",
                                            skill ? "0x53263c" : "0x5325a2", request.subtype));
    joint.speed = 0;
    joint.life = SharedLife;
    joint.width = request.scale;
    joint.maximumCount = SharedPoints;
    joint.life = skill ? SkillLife : EnergyLife;
    joint.maximumCount = Points;
    joint.count = 0;
    joint.field9ec = Math::FtolLow(D(joint.angles.z));
    joint.velocity.x = skill ? SkillOrbit : Orbit;
    if (skill)
        joint.field9d4 = joint.field9ec == FirstForward || joint.field9ec == SecondForward ? 1 : 0;
    joint.light = {Pale, Dim, Pale};
    joint.targetPosition = owner->position;
    joint.position = owner->position;
    return {};
}

// 0x5323d3..0x5323f8, 0x532552..0x5325ca and 0x534cb6: the 619/1 and 619/3 of the 0x61e decals (0x518900). The common
// 0x619 part (speed 0, life 12, the width and 2 points); subtypes 1 and 3 have no case. No rand().
void CreateSummonEnergy(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    constexpr std::int32_t SharedLife = 12, SharedPoints = 2;
    joint.speed = 0;
    joint.life = SharedLife;
    joint.width = request.scale;
    joint.maximumCount = SharedPoints;
}

// 0x5323d3..0x532415 and 0x534cb6: the 619/6 of the 0x813's bolts (0x52cb85). The common 0x619 part and case 6: light
// (1; 1; 0.5) and 4 points. No rand().
void CreateBoltEnergy(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    constexpr std::int32_t BoltPoints = 4;
    CreateSummonEnergy(joint, request);
    joint.light = {1, 1, 0.5f};
    joint.maximumCount = BoltPoints;
}

// 0x5323d3..0x5323f8, 0x5324ea..0x53254a and 0x534cb6: the charged skill's 619/8 (0x5189f0). The common 0x619 part and
// case 8: light (0.5; 0.5; (rand() & 0x7f) · 1/255f + 0.5), life 0x11, 3 points and the target (+0x58) at the second
// point with z + 100. One rand(), after the light, life and points writes.
void CreateChargedEnergy(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random)
{
    constexpr std::int32_t ChargedLife = 0x11, ChargedPoints = 3, ShadeMask = 0x7f;
    using Math::Half;                                              // 0x5324ef
    constexpr float ShadeStep = std::bit_cast<float>(0x3b808081u); // 0x6b88f8, 1/255
    constexpr double ShadeBase = 0.5, Lift = 100.0;                // 0x6b882c and 0x6b86b4
    CreateSummonEnergy(joint, request);
    joint.life = ChargedLife;
    joint.light.x = Half;
    joint.maximumCount = ChargedPoints;
    joint.light.y = Half;
    const int shade = random.Next() & ShadeMask;
    joint.light.z = F32(static_cast<double>(shade) * D(ShadeStep) + ShadeBase);
    joint.targetPosition = {request.secondPosition.x, request.secondPosition.y,
                            F32(D(request.secondPosition.z) + Lift)};
}

// 0x533e60..0x533f9e: skill 39's fan, after the quad with the retained width.
void CreateSkill39Bolt(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    constexpr std::int32_t BoltLife = 24, BoltPoints = 15;
    joint.bendAngles = joint.angles;
    joint.speed = 0;
    joint.velocity = {1, 5, 1};
    joint.count = -1;
    joint.maximumCount = BoltPoints;
    joint.tailEnabled = false;
    joint.width = request.scale;
    joint.life = BoltLife - joint.key;
    joint.field9ec = joint.key;
    switch (joint.key)
    {
    case 0:
    case 1:
        joint.bendAngles.z = F32(D(joint.bendAngles.z) + 90.0);
        joint.position.z = F32(D(joint.position.z) + (joint.key == 0 ? 200.0 : 10.0));
        break;
    case 2:
    case 3:
        joint.bendAngles.z = F32(D(joint.bendAngles.z) - 90.0);
        joint.position.z = F32(D(joint.position.z) + (joint.key == 2 ? 200.0 : 10.0));
        break;
    case 4:
        joint.bendAngles.z = F32(D(joint.bendAngles.z) + 180.0);
        joint.position.z = F32(D(joint.position.z) + 100.0);
        break;
    case 5:
        joint.bendAngles.x = 90.0f;
        joint.position.z = F32(D(joint.position.z) + 100.0);
        joint.life = 10;
        joint.maximumCount = 20;
        joint.drawEnabled = 2;
        break;
    default:
        break;
    }
    joint.pathIncrement = joint.position;
    joint.targetPosition = request.secondPosition;
}

// 0x533677..0x53369e and 0x533faf..0x534028: the turn of the PiercingShot impact. The 0x62b/6 that the 0x61c/4 releases
// (0x53b7a7) shares the 0x60f's creation (table 0x534d30 sends it to 0x533677) with life and points at 15 (the ECX of
// 0x5337e8, 0x534044).
Core::Expected<void, std::string> CreatePiercingImpact(Kernel::JointState& joint, const Kernel::JointRequest& request,
                                                       Crt::CrtRandom& random)
{
    const bool spark = request.type == SparkImpactJoint;
    const std::int32_t life = spark ? 15 : 20, points = spark ? 15 : 30;
    constexpr int FullTurn = Math::FullTurn;
    constexpr double NegativeRadians = static_cast<double>(std::bit_cast<float>(0xbc8efa35u));
    joint.life = life;
    joint.maximumCount = points;
    joint.width = request.scale;
    joint.velocity.x = static_cast<float>(random.Next() % FullTurn);
    const double argument = D(joint.angles.z) * NegativeRadians;
    const auto sine = Math::Detail::SineProductPC53(argument, 2.0f);
    const auto cosine = Math::Detail::CosineProductPC53(argument, 2.0f);
    if (!sine || !cosine)
        return Core::Unexpected(std::string("0x533fd1: el yaw del impacto60f/6 excede el perfil matemático"));
    joint.velocity.y = F32(-*sine);
    joint.velocity.z = F32(-*cosine);
    joint.targetPosition = request.secondPosition;
    return {};
}

// 0x531c43..0x531cfa: the four rays that follow model 105/0.
Core::Expected<void, std::string> CreatePiercingTrailJoint(Kernel::JointState& joint,
                                                           const Kernel::JointRequest& request)
{
    constexpr std::int32_t Points = 50;
    constexpr std::array<float, 4> Turns{0, 90, 180, 240};
    joint.life = joint.key;
    joint.key = 0;
    joint.maximumCount = Points;
    if (joint.skill == 0)
    {
        joint.width = 20;
        joint.light = glm::vec3(0.5f);
    }
    if (joint.skill == 1)
    {
        joint.width = 40;
        joint.light = {1, 0.5f, 0};
    }
    if (joint.skill == 3)
    {
        if (joint.ownerKind != Kernel::JointOwnerKind::ModelEffect || joint.ownerContext == nullptr)
            return Core::Unexpected(std::string("0x531c9e: falta el color del ModelEffect dueño del610/0..3"));
        joint.width = 20;
        joint.light = static_cast<const Effects::ModelEffect*>(joint.ownerContext)->light;
    }
    joint.velocity = {F32(D(request.scale) * 1.5), 0, 0};
    joint.speed = Turns[static_cast<std::size_t>(joint.subtype)];
    joint.targetPosition = request.secondPosition;
    return {};
}

Core::Expected<void, std::string> CreateCase(Kernel::JointState& joint, const Kernel::JointRequest& request,
                                             Crt::CrtRandom& random, const JointKernels::CreationServices* services)
{
    const auto* owner = (joint.type == 0x613 || joint.type == 0x614)
                            ? nullptr
                            : static_cast<const Fauna::FaunaObject*>(joint.ownerContext);
    // 0x530a2a..0x530a55: the 0x697 (the fish trail of 0x4473f9, its only requester, with subtype 0) does not read the
    // subtype: the count at −1, without the first segment.
    if (request.type == 0x697)
    {
        joint.count = -1;
        joint.width = request.scale;
        joint.life = 120;
        joint.maximumCount = 28;
        joint.speed = 3;
        return {};
    }
    if (request.type == 0x610 && request.subtype == 8)
    {
        const std::optional<KalimaBeamOwner> beamOwner = KalimaBeamOwnerOf(joint);
        joint.count = -1;
        joint.life = joint.key; // 0x531c43..0x531c60, before the color selector and the /8 writes.
        joint.maximumCount = 50;
        joint.key = 0;
        if (joint.skill == 3)
        {
            joint.width = 20;
            if (!beamOwner)
                return Core::Unexpected(std::string("0x531c7b: falta el color del dueño del joint 0x610"));
            joint.light = *beamOwner->light;
        }
        joint.width = request.scale;
        joint.life = 100;
        joint.maximumCount = 20;
        joint.drawEnabled = 2;
        joint.blend = 4;
        joint.category = 1;
        joint.field9ec = joint.skill;
        joint.light = {1, 1, 1};
        if (!beamOwner)
        {
            joint.alive = false;
            return Core::Unexpected(std::string("0x531f50: falta el dueño durable del joint 0x610/8"));
        }
        joint.position = *beamOwner->position;
        joint.angles = *beamOwner->angles;
        joint.position.z = 300;
        joint.targetPosition = request.secondPosition;
        return {};
    }
    // 0x530c5d..0x530c71: the 10f/5..7 skips the first segment; 0x530a46, the 632/6.
    if (request.type == JointFamilies::AuraJoint && request.subtype >= 5 && request.subtype <= 7)
        return JointFamilies::CreateAuraJoint(joint, request);
    if (request.type == JointFamilies::StormJoint && request.subtype == JointFamilies::BoltSubtype)
        return JointFamilies::CreateStormJoint(joint, request, random);
    // 0x530aa5..0x530abf: the 62c/5..7 too.
    if (request.type == JointFamilies::SpiralJoint && request.subtype >= 5 && request.subtype <= 7)
        return JointFamilies::CreateSpiralJoint(joint, request, random);
    // 0x530a32..0x530a55: the 618/0xa, /0xb, /0xe and /0xf (like the 0x697) leave the count at −1 and jump to table
    // 0x530a5c without the first segment of 0x530afa: point 0 keeps what the slot had.
    if (request.type == JointFamilies::BeamJoint &&
        (request.subtype == 0xa || request.subtype == 0xb || request.subtype == 0xe || request.subtype == 0xf))
    {
        joint.count = -1;
        return JointFamilies::CreateBeamJoint(joint, request, random);
    }
    FirstQuad(joint);
    if (JointFamilies::IsHelix(request.type, request.subtype))
        return JointFamilies::CreateHelixJoint(joint, request, random);
    if (request.type == SparkleJoint && request.subtype == 0x17)
    {
        CreateSkill39Bolt(joint, request);
        return {};
    }
    if ((request.type == SparkleJoint || request.type == SparkImpactJoint) && request.subtype == 6)
        return CreatePiercingImpact(joint, request, random);
    if (request.type == 0x610 && request.subtype >= 0 && request.subtype <= 3)
        return CreatePiercingTrailJoint(joint, request);
    // 0x5332b8: the 613/4 and the 6ee/4 (the same table 0x534f18) and 0x531813, the 614/14.
    if (((request.type == 0x613 || request.type == 0x6ee) && request.subtype == 4) ||
        (request.type == 0x614 && request.subtype == 14))
        return CreateHelperBeam(joint, request);
    if (request.type == 0x614 && request.subtype == 10)
        return CreateSwordBeam(joint, request, random);
    if (request.type == JointFamilies::LightningJoint)
        return JointFamilies::CreateLightningJoint(joint, request, random, services);
    if (request.type == JointFamilies::RedHandBoltJoint || request.type == JointFamilies::BlueHandBoltJoint)
    {
        JointFamilies::CreateHandBoltJoint(joint);
        return {};
    }
    if (request.type == 0x615 && request.subtype == 0)
        return CreateSkyBolt(joint, request, random);
    if (request.type == 0x615 && (request.subtype == 1 || request.subtype == 2 || request.subtype == 5 ||
                                  request.subtype == 6 || request.subtype == 7))
        return CreateTrapBolt(joint, request, random);
    if ((request.type == 0x6ee || request.type == 0x613) && request.subtype == 9) // 0x5333f6, the same table.
    {
        CreateMeteor(joint, request);
        return {};
    }
    if (request.type == JointFamilies::SpiritJoint || request.type == JointFamilies::SecondSpiritJoint)
        return JointFamilies::CreateSpiritJoint(joint, request, random, services);
    if (JointFamilies::IsDrop(request.type, request.subtype))
    {
        JointFamilies::CreateDropJoint(joint, request, random);
        return {};
    }
    if (JointFamilies::IsOrbit(request.type, request.subtype))
        return JointFamilies::CreateOrbitJoint(joint, request, random);
    if (JointFamilies::IsSpark(request.type, request.subtype))
        return JointFamilies::CreateSparkJoint(joint, request, random);
    if (JointFamilies::IsInferno(request.type, request.subtype))
    {
        JointFamilies::CreateInfernoJoint(joint, request);
        return {};
    }
    if (IsWandering(request.type, request.subtype))
    {
        JointFamilies::CreateWanderingJoint(joint, request, random);
        return {};
    }
    if (request.type == JointFamilies::FireTrailJoint)
    {
        JointFamilies::CreateFireTrailJoint(joint, request);
        return {};
    }
    if (request.type == JointFamilies::AuraJoint && request.subtype != 1 && request.subtype != 3)
        return JointFamilies::CreateAuraJoint(joint, request);
    if (request.type == JointFamilies::StormJoint)
        return JointFamilies::CreateStormJoint(joint, request, random);
    if (request.type == 0x617 && request.subtype >= 0 && request.subtype <= 3)
    {
        // 534e9c: /0→53202f,/1→5320a4,/2→5320dd,/3→532085.
        joint.width = request.subtype == 2 ? 4.0f : 2.0f;
        joint.speed =
            request.subtype == 2 ? 30.0f : static_cast<float>(random.Next() % 20 + (request.subtype == 1 ? 16 : 6));
        joint.life = random.Next() % (request.subtype == 1 || request.subtype == 2 ? 4 : 8) +
                     (request.subtype == 1 || request.subtype == 2 ? 4 : 8);
        joint.maximumCount = 2;
        joint.light = request.subtype == 2 ? glm::vec3(0.3f, 0.3f, 1) : glm::vec3(1);
        return {};
    }
    if (request.type == JointFamilies::BeamJoint)
        return JointFamilies::CreateBeamJoint(joint, request, random);
    if (request.type == JointFamilies::SpiralJoint)
        return JointFamilies::CreateSpiralJoint(joint, request, random);
    if (request.type == JointFamilies::FlareJoint)
    {
        JointFamilies::CreateFlareJoint(joint, request, random);
        return {};
    }
    if (request.type == JointFamilies::InfernoJoint && request.subtype == JointFamilies::SweepSubtype)
    {
        JointFamilies::CreateSweepJoint(joint, request);
        return {};
    }
    if (request.type == JointFamilies::PlasmaJoint)
    {
        JointFamilies::CreatePlasmaJoint(joint, request);
        return {};
    }
    // 0x531c43..0x531cc7 and 0x531fa3..0x53200e: arrow trails 0x610/0xc (the 0x11c, 0x51c713), /0xd (Crywolf's 0xe1/1
    // missile) and /0xe (the 0xf9, 0x51c56c). The key is the life; light (0.6f; 0.2f; 0.8f) with blend 4 for the /0xc,
    // (0.7f; 0.7f; 0.3f) with 4 for the /0xd and 1 with 1 for the /0xe; the width, 16 points and speed 70.
    if (request.type == 0x610 && request.subtype >= ArrowTrailFirst && request.subtype <= ArrowTrailLast)
    {
        if (joint.skill == 3 && joint.ownerContext == nullptr)
            return Core::Unexpected(std::format("0x531c7b: falta la luz del dueño del 0x610/{}", request.subtype));
        joint.life = joint.key;
        joint.key = 0;
        joint.maximumCount = 16; // 0x531ff2
        if (request.subtype == ArrowTrailFirst)
        {
            joint.light = {std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3e4ccccdu),
                           std::bit_cast<float>(0x3f4ccccdu)}; // 0x531fa6..0x531fb3
            joint.blend = 4;
        }
        else if (request.subtype == ArrowTrailLast)
        {
            joint.light = glm::vec3(1.0f); // 0x531fe3..0x531fe8
            joint.blend = 1;
        }
        else
        {
            joint.light = {.7f, .7f, .3f};
            joint.blend = 4;
        }
        joint.width = request.scale;
        joint.speed = 70; // 428c0000 en531ff9.
        joint.targetPosition = request.secondPosition;
        return {};
    }
    if (request.type == 0x610 && request.subtype == 4)
        return CreateSwordTrail(joint, request);
    if (request.type == 0x610 && request.subtype == 6)
        return CreateTargetBolt(joint, request);
    if (request.type == LaserJoint && request.subtype == LaserSubtype)
        return CreateLaserJoint(joint, request);
    if (request.type == SwirlJoint && (request.subtype == 0 || request.subtype == 1))
    {
        CreateSwirl(joint, request, random);
        return {};
    }
    if (request.type == SparkleJoint && (request.subtype == SparkleSubtype || request.subtype == WideSparkleSubtype ||
                                         request.subtype == FallingSparkleSubtype))
        return CreateSparkle(joint, request, random);
    if (request.type == SetTrailJoint && request.subtype == SetTrailSubtype)
        return CreateSetTrail(joint, request, random);
    if (request.type == EnergyJoint && (request.subtype == SetEnergySubtype || request.subtype == SkillEnergySubtype))
        return CreateSetEnergy(joint, request);
    if (request.type == EnergyJoint &&
        (request.subtype == SummonEnergySubtype || request.subtype == SummonDecalEnergySubtype))
    {
        CreateSummonEnergy(joint, request);
        return {};
    }
    if (request.type == EnergyJoint && request.subtype == BoltEnergySubtype)
    {
        CreateBoltEnergy(joint, request);
        return {};
    }
    if (request.type == EnergyJoint && request.subtype == ChargedEnergySubtype)
    {
        CreateChargedEnergy(joint, request, random);
        return {};
    }
    if (request.type == 0x10f && (request.subtype == 1 || request.subtype == 3))
    {
        if (owner == nullptr)
            return Core::Unexpected(std::string("0x530ee0: falta el dueño durable del joint 0x10f"));
        joint.targetPosition = owner->position;
        if (request.subtype == 1)
            joint.light = {0.2f, 0.2f, 0.2f};
        else
        {
            joint.light = owner->light;
            joint.angles = {-90, 0, 0};
            joint.velocity.y = static_cast<float>(random.Next() % 500);
            joint.speed = static_cast<float>(random.Next() % 5 + 5);
        }
        joint.life = 999999;
        joint.maximumCount = 30;
        joint.texture = 0x613;
        joint.width = request.scale;
        return {};
    }
    return Core::Unexpected(
        std::format("0x530c18: la creación del joint {:#x}/{} no está portada", request.type, request.subtype));
}
} // namespace

namespace
{
// 0x534b3f..0x534bc0: the 632/7's three 632/6, from (k, −50, 0) rotated by its angles and added to its position, with
// k at −100, 0 and 100; ownerless, width 40, key −1. With the pool full, those that do not fit are missing.
Core::Expected<void, std::string> CreateStormBolts(std::span<Kernel::JointState> joints,
                                                   const Kernel::JointState& storm, Crt::CrtRandom& random)
{
    constexpr float Down = std::bit_cast<float>(0xc2480000u); // 0x534b5e, −50
    constexpr float Width = 40;
    glm::vec3 angles = storm.angles;
    const glm::vec3 origin = storm.position;
    for (std::int32_t offset = -100; offset < 200; offset += 100)
    {
        const auto matrix = Math::OriginalAngleMatrix(angles);
        const auto turned = Math::OriginalVectorRotate({static_cast<float>(offset), Down, 0}, matrix);
        glm::vec3 from{F32(D(turned.x) + D(origin.x)), F32(D(turned.y) + D(origin.y)), F32(D(turned.z) + D(origin.z))};
        const Kernel::JointRequest request{.type = JointFamilies::StormJoint,
                                           .firstPosition = from,
                                           .secondPosition = from,
                                           .angles = angles,
                                           .subtype = JointFamilies::BoltSubtype,
                                           .ownerContext = nullptr,
                                           .scale = Width,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
        if (auto created = CreateFaunaJoint(joints, request, random); !created.HasValue())
            return Core::Unexpected(std::format("0x534baa: {}", created.Error()));
    }
    return {};
}
} // namespace

Core::Expected<std::optional<std::size_t>, std::string>
CreateFaunaJoint(std::span<Kernel::JointState> joints, const Kernel::JointRequest& request, Crt::CrtRandom& random,
                 Kernel::JointOwnerKind ownerKind, const JointKernels::CreationServices* services)
{
    // What MuRet reads from a helper or raven owner: only the light and the action of the 60f/0 and the 60f/0x12.
    const bool helperSparkle =
        request.type == SparkleJoint && (request.subtype == SparkleSubtype || request.subtype == WideSparkleSubtype);
    // And from a character outside its type: the position, angles, light and alive byte of the 610/8 (0x4c91dc); and
    // the target's position for the 610/6 of Kalima's 0x95 (0x43b0ed).
    const bool kalimaBeam = request.type == 0x610 && (request.subtype == 8 || request.subtype == 6);
    // The JointFamilies.h families read the owner character with JointKernels::ActorOwner.
    const bool familyJoint =
        request.type == JointFamilies::LightningJoint || request.type == JointFamilies::RedHandBoltJoint ||
        request.type == JointFamilies::BlueHandBoltJoint || request.type == JointFamilies::SpiritJoint ||
        request.type == JointFamilies::SecondSpiritJoint || request.type == JointFamilies::BeamJoint ||
        request.type == JointFamilies::SpiralJoint ||
        (request.type == JointFamilies::DropJoint && request.subtype == JointFamilies::WanderSubtype) ||
        (JointFamilies::IsOrbit(request.type, request.subtype) && request.subtype == JointFamilies::ColumnSubtype) ||
        request.type == JointFamilies::PlasmaJoint ||
        (request.type == JointFamilies::AuraJoint && request.subtype != 1 && request.subtype != 3) ||
        (request.type == JointFamilies::StormJoint && request.subtype >= 0 &&
         request.subtype <= JointFamilies::LastFallingSubtype) ||
        (request.type == JointFamilies::DropJoint && request.subtype == JointFamilies::SpinningSparkleSubtype);
    // The 0x618 that follows an effect with a model (the arrows' /5 and the 0x11b/1's /0xa and /0xb) and the 0xf8
    // arrow's 0x60f/0xb and /0x19.
    const bool effectBeam =
        (request.type == JointFamilies::BeamJoint &&
         (request.subtype == 5 || request.subtype == 0xa || request.subtype == 0xb)) ||
        (request.type == JointFamilies::DropJoint &&
         (request.subtype == JointFamilies::ArrowWanderSubtype || request.subtype == JointFamilies::PinkWanderSubtype));
    // Those of the siege's 0x61c pulse, owned by it, which do not read it: the 0x632/5 (0x52c266) and the 0x615/6
    // (0x52c31e); and those of the 0x812 (0x52c9a7 and 0x52c9da), the 0x60f/0xc and the /4, which reads its light. Also
    // the 0x13a's 0x632/4 (0x51e01a), which follows it, and the 0x632/2 and /3 of the 0x13a's step (0x529eca,
    // 0x529eed), which do not read it (on the ground they release the 0x6eb/4 wave, 0x53c1b5).
    constexpr std::int32_t FirstRainSubtype = 2; // 0x529eca; the /3 (0x529eed) is JointFamilies::LastFallingSubtype.
    const bool rainJoint = request.subtype >= FirstRainSubtype && request.subtype <= JointFamilies::LastFallingSubtype;
    const bool pulseJoint =
        (request.type == JointFamilies::StormJoint && (request.subtype == JointFamilies::FallingStarSubtype ||
                                                       request.subtype == JointFamilies::BlastSubtype || rainJoint)) ||
        (request.type == 0x615 && request.subtype == 6) || JointFamilies::IsHelix(request.type, request.subtype);
    // The 0x6ee/0x12 of Kundun's 0x125 (0x51ce91), which writes its alpha and position in its step (0x539494), and the
    // 0x126's 0x613/0xf (0x51cf45), which writes its angles, alpha and position (0x539249).
    const bool kundunSpirit = (request.type == JointFamilies::SecondSpiritJoint && request.subtype == 0x12) ||
                              (request.type == JointFamilies::SpiritJoint && request.subtype == 0xf);
    // The 0x60f effect's 0x62b/0xe and /0xf (0x52b408, 0x52b42b), which only check whether there is an owner
    // (0x53b18a).
    const bool whirl =
        JointFamilies::IsOrbit(request.type, request.subtype) && request.subtype != JointFamilies::ColumnSubtype;
    for (std::size_t i = 0; i < joints.size(); ++i)
    {
        if (joints[i].alive)
            continue;
        if ((ownerKind == Kernel::JointOwnerKind::Helper || ownerKind == Kernel::JointOwnerKind::Raven) &&
            !helperSparkle)
            return Core::Unexpected(std::format("0x530860: el joint {:#x}/{} con un ayudante o el cuervo de dueño no "
                                                "está portado",
                                                request.type, request.subtype));
        if (ownerKind == Kernel::JointOwnerKind::Actor && !kalimaBeam && !familyJoint)
            return Core::Unexpected(
                std::format("0x530860: el joint {:#x}/{} con el ActorState de dueño fuera de su tipo no está portado",
                            request.type, request.subtype));
        if (ownerKind == Kernel::JointOwnerKind::ModelEffect &&
            !((request.type == EnergyJoint &&
               (request.subtype == SummonEnergySubtype || request.subtype == SummonDecalEnergySubtype)) ||
              (request.type == 0x610 && ((request.subtype >= ArrowTrailFirst && request.subtype <= ArrowTrailLast) ||
                                         (request.subtype >= 0 && request.subtype <= 3))) ||
              (request.type == SparkleJoint && request.subtype == 6) || effectBeam || pulseJoint || kundunSpirit ||
              whirl || request.type == JointFamilies::FireTrailJoint))
            return Core::Unexpected(
                std::string("0x530860: el dueño ModelEffect no está portado para este tipo y subtipo"));
        Prefix(joints[i], request, ownerKind);
        if (auto created = CreateCase(joints[i], request, random, services); !created.HasValue())
            return Core::Unexpected(created.Error());
        if (request.type == JointFamilies::StormJoint && request.subtype == JointFamilies::StormSubtype)
            if (auto bolts = CreateStormBolts(joints, joints[i], random); !bolts.HasValue())
                return Core::Unexpected(bolts.Error());
        return std::optional<std::size_t>(i);
    }
    return std::optional<std::size_t>{};
}

Core::Expected<std::optional<std::size_t>, std::string> CreateJoint(std::span<Kernel::JointState> joints,
                                                                    const Kernel::JointRequest& request,
                                                                    Crt::CrtRandom& random,
                                                                    Kernel::JointOwnerKind ownerKind)
{
    return CreateFaunaJoint(joints, request, random, ownerKind);
}

namespace
{
std::int32_t AddWrapped(std::int32_t value, std::uint32_t amount)
{
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value) + amount);
}

Core::Expected<void, std::string> StepBloodCastleJoint(Kernel::JointState& joint, const Fauna::FaunaObject& owner,
                                                       Crt::CrtRandom* random)
{
    if (owner.active == 0)
    {
        joint.life = 0;
        joint.alive = false;
        return {}; // 0x535f13 goes on to the common tail, even with alive=0.
    }
    if (random == nullptr)
        return Core::Unexpected(std::string("0x535f18: falta el RNG del paso del joint 0x10f/3"));
    if (random->Next() % 5 == 0)
        joint.life = AddWrapped(joint.life, 0xfffffffbu);
    else if ((random->Next() & 1) == 0)
        joint.life = AddWrapped(joint.life, 1);
    const auto angle = (static_cast<double>(joint.life) - joint.velocity.y) * static_cast<double>(0.05f);
    const auto product = Math::Detail::SineProductPC53(angle, 3.0f);
    if (!product.has_value())
        return Core::Unexpected(std::string("0x535f6b: el seno del joint 0x10f/3 excede el perfil matemático"));
    joint.velocity.x = static_cast<float>(*product);
    // FSTS keeps the retained product for the next FADD: do not add the rounded velocity.x.
    joint.position.x = static_cast<float>(static_cast<double>(joint.position.x) + *product);
    double factor =
        owner.timer > 50
            ? static_cast<double>(std::bit_cast<std::int32_t>(100u - std::bit_cast<std::uint32_t>(owner.timer))) *
                  static_cast<double>(0.025f)
            : static_cast<double>(owner.timer) * static_cast<double>(0.1f);
    if (factor > 1)
        factor = 1;
    joint.light = {static_cast<float>(factor * owner.light.x), static_cast<float>(factor * owner.light.y),
                   static_cast<float>(factor * owner.light.z)};
    return {};
}

Core::Expected<void, std::string> StepFairyJoint(Kernel::JointState& joint, const Fauna::FaunaObject& owner,
                                                 std::size_t index, Crt::CrtRandom* random, Models::ClothWorld* cloth)
{
    if (owner.active == 0)
    {
        joint.life = 0;
        joint.alive = false;
        return {}; // 0x53607b goes on to tail and life without reading the counter.
    }
    joint.targetPosition = owner.position;
    joint.targetPosition.z = static_cast<float>(static_cast<double>(owner.position.z) + 10);
    if (cloth == nullptr || random == nullptr)
        return Core::Unexpected(std::string("0x536187: falta el contador compartido del joint0x10f/1"));
    const auto steps = std::bit_cast<std::uint32_t>(cloth->logicSteps);
    if (cloth->openCounterAccesses == 0)
        for (std::uint16_t draw = 0; draw < Models::SmallLogicalCloseRandoms; ++draw)
            static_cast<void>(random->Next());
    const auto physicalIndex = static_cast<std::uint32_t>(index);
    constexpr std::uint32_t PhaseStride = 53731;
    const auto phase =
        std::bit_cast<std::int32_t>(physicalIndex * PhaseStride + ((physicalIndex & 1u) != 0 ? steps : 0u - steps));
    const auto direction = Math::Detail::JointDirectionPC53(phase);
    if (!direction)
        return Core::Unexpected(std::string("0x536409: la dirección del joint0x10f/1 excede el perfil PC53"));
    joint.light.x = 0.2f;
    joint.light.y = 0.2f;
    joint.position = {static_cast<float>(static_cast<double>((*direction)[0]) * 70 + joint.targetPosition.x),
                      static_cast<float>(static_cast<double>((*direction)[1]) * 70 + joint.targetPosition.y),
                      static_cast<float>(static_cast<double>((*direction)[2]) * 140 + joint.targetPosition.z)};
    joint.light.z = static_cast<float>(static_cast<double>((*direction)[3]) * 0.2f + 0.4f);
    return {};
}

// 0x4e9810(position, angles, target, turn): the X/Y squares and the Z delta are spilled before the two roots. Returns
// the distance as the fsqrt of 0x4e98c4 leaves it in st(0), not rounded to float.
double SteerJoint(Kernel::JointState& joint, const glm::vec3& target, float turnStep)
{
    using Math::CreateAngle;
    using Math::TurnAngle;
    constexpr double FullTurn = Math::FullTurnAsDouble;
    const auto yaw = static_cast<float>(CreateAngle(joint.position.x, joint.position.y, target.x, target.y));
    joint.angles.z = static_cast<float>(TurnAngle(joint.angles.z, yaw, turnStep));
    const double x = static_cast<double>(joint.position.x) - target.x;
    const double y = static_cast<double>(joint.position.y) - target.y;
    const auto z = static_cast<float>(static_cast<double>(joint.position.z) - target.z);
    const auto xSquared = static_cast<float>(x * x);
    const auto ySquared = static_cast<float>(y * y);
    const auto horizontal = static_cast<float>(std::sqrt(static_cast<double>(xSquared) + ySquared));
    const auto pitch = static_cast<float>(FullTurn - CreateAngle(joint.position.z, horizontal, target.z, 0));
    joint.angles.x = static_cast<float>(TurnAngle(joint.angles.x, pitch, turnStep));
    return std::sqrt((static_cast<double>(z) * z + xSquared) + ySquared);
}

void MoveJoint(Kernel::JointState& joint, const glm::vec3& moved)
{
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = static_cast<float>(static_cast<double>(moved[axis]) + joint.position[axis]);
}

// 0x53727d..0x537408 and 0x5379af..0x5379ef: the 615/0. With life over 15, it rebuilds the bolt from +0x24 toward a
// target drawn once per step (±100 in x, ±50 in y of +0x58), with 50 segments that turn rand() % 80 + 60, shake by
// (rand() % 1400 − 700) / width in x and z (z unrounded) and advance with the in-place turn of 0x5fcbd0; otherwise the
// light's z drops 10.12f. Then the common tail, only with life over 10.
Core::Expected<void, std::string> StepSkyBolt(Kernel::JointState& joint, Crt::CrtRandom* random)
{
    constexpr std::int32_t FadeLife = 15;
    constexpr std::int32_t TailLife = 10;
    if (joint.life <= FadeLife)
    {
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x4121eb85u));
        joint.light.z = static_cast<float>(static_cast<double>(joint.light.z) - Fade);
    }
    else
    {
        if (random == nullptr)
            return Core::Unexpected(std::string("0x537295: falta el RNG del joint 0x615/0"));
        constexpr int SpreadX = 200, SpreadY = 100, TurnSpan = 80, JitterSpan = 1400, JitterCenter = 700;
        constexpr double CenterX = 100, CenterY = 50, TurnBase = 60;
        glm::vec3 target;
        target.x = static_cast<float>(
            (static_cast<double>(random->Next() % SpreadX) + static_cast<double>(joint.targetPosition.x)) - CenterX);
        target.y = static_cast<float>(
            (static_cast<double>(random->Next() % SpreadY) + static_cast<double>(joint.targetPosition.y)) - CenterY);
        target.z = joint.targetPosition.z;
        joint.position = joint.pathIncrement;
        for (std::int32_t iteration = 0; iteration < joint.maximumCount; ++iteration)
        {
            const auto turn = static_cast<float>(static_cast<double>(random->Next() % TurnSpan) + TurnBase);
            static_cast<void>(SteerJoint(joint, target, turn));
            const double width = static_cast<double>(joint.width);
            joint.velocity.x =
                static_cast<float>(static_cast<double>(random->Next() % JitterSpan - JitterCenter) / width);
            // 0x53736c: fst, the z turn's sum reads the quotient from the x87 stack.
            const double roll = static_cast<double>(random->Next() % JitterSpan - JitterCenter) / width;
            joint.velocity.z = static_cast<float>(roll);
            const glm::vec3 perturbed{static_cast<float>(static_cast<double>(joint.angles.x) + joint.velocity.x),
                                      static_cast<float>(static_cast<double>(joint.angles.y) + joint.velocity.y),
                                      static_cast<float>(roll + static_cast<double>(joint.angles.z))};
            const auto matrix = Math::OriginalAngleMatrix(perturbed);
            if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
                return appended;
            glm::vec3 moved{0, -joint.speed, 0};
            Math::OriginalVectorRotateInPlace(moved, matrix);
            MoveJoint(joint, moved);
        }
    }
    joint.tailEnabled = joint.life > TailLife;
    return {};
}

// 0x53768f..0x5379ef: the 615/1, /2, /5, /6 and /7. Rebuilds the bolt from +0x24 with its angle matrix (0x5fc8f0): the
// first 50 − 5 segments shift x and y by rand() % 20 − 10 and lower z by 16 (0x6b8f00; the /6, 13, 0x6b8834; the /7,
// 20, 0x6b86d0); the next four advance a fifth of what is left to the target (x with the subtraction unrounded, y and z
// stored before the 0.2f) and shift x and y again; the last is the target. With life below 4 the light is multiplied by
// 0.8333333f (0x6b8b98). Then the /2 leaves two 0x5fd sprites of 1.5 at the target with a rand() % 360 turn and the
// 6ed/0 particle at scale 1, with light (x · 0.5, y · 0.6f, z); the /6 puts the target's z 30 above the terrain
// (0x5f93c0) and leaves the 6ed/0 at scale 2 there. Except for the /7, the common tail follows only with life over 10.
Core::Expected<void, std::string> StepTrapBolt(Kernel::JointState& joint, Crt::CrtRandom* random,
                                               Particles::FrameSprites* sprites, Particles::Particles* particles,
                                               float worldClock,
                                               const std::function<double(float, float)>& terrainHeight)
{
    constexpr std::int32_t ChaosSubtype = 2, StrikeSubtype = 6, ArrivalSubtype = 7;
    const bool chaos = joint.subtype == ChaosSubtype;
    const bool strike = joint.subtype == StrikeSubtype;
    if (random == nullptr || (chaos && (sprites == nullptr || particles == nullptr)) ||
        (strike && (particles == nullptr || !terrainHeight)))
        return Core::Unexpected(std::format("0x53770a: faltan el RNG, los sprites, las partículas o la altura del "
                                            "terreno del joint 0x615/{}",
                                            joint.subtype));
    constexpr std::int32_t Approach = 5;
    constexpr int JitterSpan = 20, JitterCenter = 10;
    const double drop = strike                            ? 13.0  // 0x6b8834
                        : joint.subtype == ArrivalSubtype ? 20.0  // 0x6b86d0
                                                          : 16.0; // 0x6b8f00
    constexpr double Step = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    constexpr std::int32_t FadeLife = 4, TailLife = 10;
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f555555u));
    const auto jitter = [&](float& axis)
    { axis = static_cast<float>(static_cast<double>(random->Next() % JitterSpan - JitterCenter) + axis); };
    joint.position = joint.pathIncrement;
    const auto matrix = Math::OriginalAngleMatrix(joint.angles);
    for (std::int32_t i = 0; i < joint.maximumCount - Approach; ++i)
    {
        jitter(joint.position.x);
        jitter(joint.position.y);
        joint.position.z = static_cast<float>(static_cast<double>(joint.position.z) - drop);
        if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
    }
    // 0x53777b..0x5377b9: the x subtraction stays on the x87 stack; the y and z ones are stored before multiplying.
    const double toX = static_cast<double>(joint.targetPosition.x) - joint.position.x;
    const auto toY = static_cast<float>(static_cast<double>(joint.targetPosition.y) - joint.position.y);
    const auto toZ = static_cast<float>(static_cast<double>(joint.targetPosition.z) - joint.position.z);
    const glm::vec3 step{static_cast<float>(toX * Step), static_cast<float>(static_cast<double>(toY) * Step),
                         static_cast<float>(static_cast<double>(toZ) * Step)};
    for (std::int32_t i = joint.maximumCount - Approach; i < joint.maximumCount - 1; ++i)
    {
        MoveJoint(joint, step);
        jitter(joint.position.x);
        jitter(joint.position.y);
        if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
    }
    joint.position = joint.targetPosition;
    if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
        return appended;
    if (joint.life < FadeLife)
        joint.light = {static_cast<float>(static_cast<double>(joint.light.x) * Fade),
                       static_cast<float>(static_cast<double>(joint.light.y) * Fade),
                       static_cast<float>(static_cast<double>(joint.light.z) * Fade)};
    // 0x5379af..0x5379ca: the /7 and the /1 keep the tail that creation turned off.
    constexpr std::int32_t SummonSubtype = 1;
    if (joint.subtype == ArrivalSubtype || joint.subtype == SummonSubtype)
        return {};
    if (chaos)
    {
        constexpr std::uint16_t ShinyTexture = 0x5fd;
        constexpr float ShinyScale = 1.5f;
        constexpr int FullTurn = Math::FullTurn;
        constexpr double HalfRed = 0.5;
        constexpr double GreenShare = static_cast<double>(std::bit_cast<float>(0x3f19999au));
        const glm::vec3 light{static_cast<float>(static_cast<double>(joint.light.x) * HalfRed),
                              static_cast<float>(static_cast<double>(joint.light.y) * GreenShare), joint.light.z};
        for (int sprite = 0; sprite < 2; ++sprite)
        {
            // 0x5491e0(0x5fd, target, 1.5, light, 0, turn, 0): MuRet does not store the sprite's owner (Q-C303).
            const auto rotation = static_cast<float>(random->Next() % FullTurn);
            static_cast<void>(sprites->Create(ShinyTexture, joint.targetPosition, ShinyScale, light, rotation, 0));
        }
        auto created = particles->CreateChecked(Particles::KalimaFanta, joint.targetPosition, joint.angles, light, 0, 1,
                                                worldClock, *random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x537920: falló la partícula 0x6ed del joint 0x615/2 ({})",
                                                static_cast<int>(created.Error())));
    }
    if (strike)
    {
        // 0x53797b..0x5379ac: the target's z, the terrain height at its x and y plus 30 (0x6b8858), and the 6ed/0 at
        // scale 2 there with the angles. The light is [ebp−0x18..−0x10], which this path does not write (Q-C1625): the
        // 6ed does not read it at creation and its step (0x547d2b, which runs later in the same logical step, 0x637128)
        // overwrites it with life · 0.04f before any draw, so MuRet passes (0, 0, 0).
        constexpr double Above = 30.0;
        constexpr float StrikeScale = 2.0f; // 0x40000000
        joint.targetPosition.z = F32(terrainHeight(joint.targetPosition.x, joint.targetPosition.y) + Above);
        auto created = particles->CreateChecked(Particles::KalimaFanta, joint.targetPosition, joint.angles,
                                                glm::vec3(0), 0, StrikeScale, worldClock, *random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x5379a7: falló la partícula 0x6ed del joint 0x615/6 ({})",
                                                static_cast<int>(created.Error())));
    }
    joint.tailEnabled = joint.life > TailLife;
    return {};
}

// 0x538ea4..0x538f37: the 6ee/9. After the common advance, x and y shift by (rand() % 10) − 5.0f, z rises by the speed
// and the speed by 1; with life below 10 the light is multiplied by 0.8695652f (0x6ba2e4).
Core::Expected<void, std::string> StepMeteor(Kernel::JointState& joint, Crt::CrtRandom* random)
{
    if (random == nullptr)
        return Core::Unexpected(std::string("0x538ead: falta el RNG del joint 0x6ee/9"));
    constexpr int JitterSpan = 10;
    constexpr double JitterCenter = 5;
    constexpr double Acceleration = 1;
    constexpr std::int32_t FadeLife = 10;
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f5e9bd4u));
    joint.position.x =
        static_cast<float>((static_cast<double>(random->Next() % JitterSpan) - JitterCenter) + joint.position.x);
    joint.position.y =
        static_cast<float>((static_cast<double>(random->Next() % JitterSpan) - JitterCenter) + joint.position.y);
    joint.position.z = static_cast<float>(static_cast<double>(joint.speed) + joint.position.z);
    joint.speed = static_cast<float>(static_cast<double>(joint.speed) + Acceleration);
    if (joint.life < FadeLife)
        joint.light = {static_cast<float>(static_cast<double>(joint.light.x) * Fade),
                       static_cast<float>(static_cast<double>(joint.light.y) * Fade),
                       static_cast<float>(static_cast<double>(joint.light.z) * Fade)};
    return {};
}

// FSIN or FCOS times a float of any sign (the 619/9's vx reaches 0): the R53 product by |f| with the sign of f; with f
// at ±0, zero with the sign of the product.
std::optional<double> SignedTrigonometricProduct(double argument, float multiplier, bool cosine)
{
    const float magnitude = multiplier < 0 ? -multiplier : multiplier;
    const auto product = cosine ? Math::Detail::CosineProductPC53(argument, magnitude > 0 ? magnitude : 1.0f)
                                : Math::Detail::SineProductPC53(argument, magnitude > 0 ? magnitude : 1.0f);
    if (!product)
        return std::nullopt;
    if (!(magnitude > 0))
    {
        if (multiplier != 0)
            return std::nullopt;
        return std::signbit(*product) != std::signbit(multiplier) ? -0.0 : 0.0;
    }
    return std::signbit(multiplier) ? -*product : *product;
}

// 0x53b029..0x53b156: h and the trigonometric products stay retained until their sums. The 0x62b/6 turns with life ·
// 0.8f (0x53b03d) instead of · 0.5.
Core::Expected<void, std::string> StepPiercingImpact(Kernel::JointState& joint)
{
    const double Half =
        joint.type == SparkImpactJoint ? static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)) : 0.5; // 0x6b8a38
    constexpr double Radians = Math::DegreesToRadiansAsDouble;
    const float argument = F32(static_cast<double>(joint.life) * Half + D(joint.velocity.x));
    const float radius = F32(2.0 * static_cast<double>(joint.life > 1 ? joint.life : 1));
    const auto cosine = Math::Detail::CosineProductPC53(D(argument), radius);
    const auto sine = Math::Detail::SineProductPC53(D(argument), radius);
    if (!cosine || !sine)
        return Core::Unexpected(std::string("0x53b066: el giro del impacto60f/6 excede el perfil matemático"));
    const double horizontal = -*cosine;
    joint.targetPosition.x = F32(D(joint.targetPosition.x) + D(joint.velocity.y));
    joint.targetPosition.y = F32(D(joint.targetPosition.y) + D(joint.velocity.z));
    const double facing = (90.0 - D(joint.angles.z)) * Radians;
    const auto x = Math::Detail::SineDoubleProductPC53(facing, horizontal);
    const auto y = Math::Detail::CosineDoubleProductPC53(facing, horizontal);
    if (!x || !y)
        return Core::Unexpected(std::string("0x53b12f: la dirección del impacto60f/6 excede el perfil matemático"));
    joint.position = {F32(*x + D(joint.targetPosition.x)), F32(*y + D(joint.targetPosition.y)),
                      F32(*sine + D(joint.targetPosition.z))};
    if (joint.life < 10)
        FadeJointLight(joint, 0x3f44ec4fu);
    return {};
}

// 0x53b239..0x53b3d1: the 60f/0, 60f/0xa and 60f/0x12, after the common advance (0x539f1b). With the key at −1, the
// argument is (life + vy) · 0.1f, with life doubled for the /0x12 and negated with skill 1 (0x6b8b9c); with another
// key, (life + vy) / key. x is the target plus cos · speed and y the target minus sin · speed. The /0 rises by vz; the
// /0xa falls by it (0x53b2d5); the /0x12 rises by vz · 1.1f, and the speed grows 0.1f and the width 0.2f. With life
// below 10 the light times 0.7692308f (0x6ba240). With the width below 40 and the owner's action (+0x111, the
// character's or the horse's) in [0xd, 0x21] or [0x38, 0x3c], it becomes the /1: no tail and life at most 10 (Q-C383).
// At 40 or more it does not read the owner.
Core::Expected<void, std::string> StepSparkle(Kernel::JointState& joint, std::size_t index)
{
    constexpr std::int16_t NoKey = -1;
    constexpr double Tenth = Math::TenthAsDouble; // 0x6b87b8
    constexpr double Mirror = -1.0;               // 0x6b8b9c
    constexpr std::uint8_t MirroredSkill = 1;
    constexpr double Climb = static_cast<double>(std::bit_cast<float>(0x3f8ccccdu)); // 0x6b8830, 1.1f
    constexpr double Widen = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8854, 0.2f
    constexpr std::int32_t FadeLife = 10, FallenLife = 10;
    constexpr float FallWidth = 40; // 0x6b8bf4
    constexpr std::uint8_t FirstFall = 0xd, LastFall = 0x21, SecondFirstFall = 0x38, SecondLastFall = 0x3c;
    double argument = 0;
    if (joint.key != NoKey)
        argument = (static_cast<double>(joint.life) + D(joint.velocity.y)) / static_cast<double>(joint.key);
    else if (joint.subtype == WideSparkleSubtype)
    {
        const auto twice = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.life) << 1u);
        argument = (static_cast<double>(twice) + D(joint.velocity.y)) * Tenth;
        if (joint.skill == MirroredSkill)
            argument = argument * Mirror;
    }
    else
        argument = (static_cast<double>(joint.life) + D(joint.velocity.y)) * Tenth;
    const auto cosine = Math::Detail::CosineProductPC53(argument, joint.speed);
    const auto sine = Math::Detail::SineProductPC53(argument, joint.speed);
    if (!cosine || !sine)
        return Core::Unexpected(std::format("0x53b2a2: el giro del joint {} (0x60f/{:#x}) excede el perfil matemático",
                                            index, joint.subtype));
    joint.position.x = F32(*cosine + D(joint.targetPosition.x));
    joint.position.y = F32(D(joint.targetPosition.y) - *sine);
    if (joint.subtype == SparkleSubtype)
        joint.position.z = F32(D(joint.velocity.z) + D(joint.position.z));
    else if (joint.subtype == FallingSparkleSubtype)
        joint.position.z = F32(D(joint.position.z) - D(joint.velocity.z));
    else
    {
        joint.position.z = F32(D(joint.velocity.z) * Climb + D(joint.position.z));
        joint.speed = F32(D(joint.speed) + Tenth);
        joint.width = F32(D(joint.width) + Widen);
    }
    if (joint.life < FadeLife)
        FadeJointLight(joint, 0x3f44ec4fu);
    // 0x53b378: fcomp with 40 and test ah,1: below 40 or NaN.
    if (joint.width >= FallWidth)
        return {};
    const std::optional<std::uint8_t> ownerAction = SparkleOwnerAction(joint);
    if (!ownerAction)
        return Core::Unexpected(
            std::format("0x53b38c: falta el dueño del joint {} (0x60f/{:#x})", index, joint.subtype));
    const std::uint8_t action = *ownerAction;
    if (!((action >= FirstFall && action <= LastFall) || (action >= SecondFirstFall && action <= SecondLastFall)))
        return {};
    joint.subtype = FallenSparkleSubtype;
    joint.tailEnabled = false;
    if (joint.life > FallenLife)
        joint.life = FallenLife;
    return {};
}

// 0x537c82..0x537d17: the 610/7, after the common advance. x is the start (+0x24) plus sin(life · 0.4f) · vx, with the
// argument stored as float for 0x52e3a0; y is the start's and z rises by the speed. The speed and vx drop 0.1f, and
// with life below 15 the light times 0.8333333f (0x6b8b98).
Core::Expected<void, std::string> StepSetTrail(Kernel::JointState& joint, std::size_t index)
{
    constexpr double Pace = static_cast<double>(std::bit_cast<float>(0x3ecccccdu)); // 0x6ba2f4, 0.4f
    constexpr double Tenth = Math::TenthAsDouble;                                   // 0x6b87b8
    constexpr std::int32_t FadeLife = 15;
    const float argument = F32(static_cast<double>(joint.life) * Pace);
    const auto sine = SignedTrigonometricProduct(D(argument), joint.velocity.x, false);
    if (!sine)
        return Core::Unexpected(
            std::format("0x537c9b: el seno del joint {} (0x610/7) excede el perfil matemático", index));
    joint.position.x = F32(*sine + D(joint.pathIncrement.x));
    joint.position.y = joint.pathIncrement.y;
    joint.position.z = F32(D(joint.speed) + D(joint.position.z));
    joint.speed = F32(D(joint.speed) - Tenth);
    joint.velocity.x = F32(D(joint.velocity.x) - Tenth);
    if (joint.life < FadeLife)
        FadeJointLight(joint, 0x3f555555u);
    return {};
}

// 0x5387a9..0x5388e2 and 0x5389f7: the 619/9. Reads the owner (+0x54) live: shifts the segment points from count − 1
// to 0 by how much the owner moved from the target (+0x58), which becomes its position. The position turns around the
// target with the argument +0x9ec · 0.1f (+0x9ec rises 2) at distance vx; z is the target's plus 90 − life and 10
// more. vx drops 1 and, below 0 unrounded, stays at 0 with life at 0. With life below 50 the light times 0.9090909f
// (0x6ba26c) and above 80 times 1.25 (0x6b8ad4). The 619/0xa (0x5388e7..0x538a4d) the same, except z (the target plus
// life · 0.0125f · 300), +0x9ec, which rises 2 with +0x9d4 and otherwise drops 2, and the light: times 0.9090909f below
// 40 and times 1.2f (0x6b8bb0) above 68.
Core::Expected<void, std::string> StepSetEnergy(Kernel::JointState& joint, std::size_t index)
{
    constexpr double Tenth = Math::TenthAsDouble; // 0x6b87b8
    constexpr std::int32_t Rise = 90;
    constexpr double Lift = 10.0, Shrink = 1.0;
    constexpr double Climb = static_cast<double>(std::bit_cast<float>(0x3c4ccccdu)), Height = 300.0; // 0x6ba208
    const bool skill = joint.subtype == SkillEnergySubtype;
    const std::int32_t fadeLife = skill ? 40 : 50, brightLife = skill ? 0x44 : 80;
    const auto* owner = static_cast<const Kernel::ActorState*>(joint.ownerContext);
    if (owner == nullptr)
        return Core::Unexpected(std::format("{}: falta el personaje dueño del joint {} (0x619/{})",
                                            skill ? "0x5388f0" : "0x5387b2", index, joint.subtype));
    glm::vec3 delta{};
    for (int axis = 0; axis < 3; ++axis)
        delta[axis] = F32(D(joint.targetPosition[axis]) - D(owner->position[axis]));
    joint.targetPosition = owner->position;
    if (joint.count >= static_cast<std::int32_t>(joint.points.size()))
        return Core::Unexpected(std::format("{}: la cola del joint {} (0x619/{}) excede sus puntos",
                                            skill ? "0x53891e" : "0x5387e0", index, joint.subtype));
    for (std::int32_t segment = joint.count - 1; segment >= 0; --segment)
        for (glm::vec3& point : joint.points[static_cast<std::size_t>(segment)])
            for (int axis = 0; axis < 3; ++axis)
                point[axis] = F32(D(point[axis]) - D(delta[axis]));
    const double argument = static_cast<double>(joint.field9ec) * Tenth;
    const std::uint32_t turn = !skill || joint.field9d4 != 0 ? 2u : 0xfffffffeu;
    joint.field9ec = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.field9ec) + turn);
    const auto sine = SignedTrigonometricProduct(argument, joint.velocity.x, false);
    const auto cosine = SignedTrigonometricProduct(argument, joint.velocity.x, true);
    if (!sine || !cosine)
        return Core::Unexpected(std::format("{}: el giro del joint {} (0x619/{}) excede el perfil matemático",
                                            skill ? "0x53897b" : "0x53884e", index, joint.subtype));
    joint.position.x = F32(*sine + D(joint.targetPosition.x));
    joint.position.y = F32(*cosine + D(joint.targetPosition.y));
    if (skill)
        joint.position.z = F32(static_cast<double>(joint.life) * Climb * Height + D(joint.targetPosition.z));
    else
    {
        const auto height =
            std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(Rise) - std::bit_cast<std::uint32_t>(joint.life));
        joint.position.z = F32((static_cast<double>(height) + D(joint.targetPosition.z)) + Lift);
    }
    // 0x538885: fst, and fcomp with 0 of the unrounded subtraction.
    const double reduced = D(joint.velocity.x) - Shrink;
    joint.velocity.x = F32(reduced);
    if (!(reduced >= 0))
    {
        joint.velocity.x = 0;
        joint.life = 0;
    }
    if (joint.life < fadeLife)
        FadeJointLight(joint, 0x3f68ba2eu);
    else if (joint.life > brightLife)
        FadeJointLight(joint, skill ? 0x3f99999au : 0x3fa00000u);
    return {};
}

// 0x537d49..0x537ec0: three samples toward the live owner and the fourth from the common tail.
Core::Expected<void, std::string> StepPiercingTrailJoint(Kernel::JointState& joint)
{
    if (joint.ownerKind != Kernel::JointOwnerKind::ModelEffect || joint.ownerContext == nullptr)
        return Core::Unexpected(std::string("0x537d49: falta el ModelEffect dueño del610/0..3"));
    const auto& owner = *static_cast<const Effects::ModelEffect*>(joint.ownerContext);
    if (!owner.alive)
    {
        joint.life = -1;
        joint.position = joint.targetPosition;
        joint.position.z = F32(D(joint.position.z) + 50.0);
        return {};
    }
    constexpr double Third = Math::ThirdAsDouble;
    glm::vec3 delta;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float distance = F32(D(owner.transform.position[axis]) - D(owner.start[axis]));
        delta[axis] = F32(D(distance) * Third);
    }
    joint.targetPosition = owner.start;
    glm::vec3 angles = owner.copiedAngles;
    angles.y = F32((D(joint.speed) - 90.0) + D(angles.y));
    for (int sample = 0; sample < 3; ++sample)
    {
        angles.y = F32(D(angles.y) + 30.0);
        for (int axis = 0; axis < 3; ++axis)
            joint.targetPosition[axis] = F32(D(joint.targetPosition[axis]) + D(delta[axis]));
        const auto offset = Math::OriginalVectorRotate(joint.velocity, Math::OriginalAngleMatrix(angles));
        joint.position = joint.targetPosition;
        MoveJoint(joint, offset);
        if (joint.count < joint.maximumCount - 1 || joint.skill != 0)
            if (auto appended = AppendJointTail(joint); !appended.HasValue())
                return appended;
    }
    if (joint.life < 15)
        FadeJointLight(joint, 0x3f2aaaabu);
    return {};
}

// 0x537b36..0x537c7d and 0x53be97: the 610/6 of Kalima's 0x95, after the common advance. Without an owner it turns off
// and the common tail follows. With 30 life or more, the angles' z drops 10 and the position rises 2.5. The speed rises
// 5 capped at 30 (the x87 sum against 30); it turns with it (0x4e9810) toward the owner target (+0x54, read live) 150
// higher and, if it is 70 or less away, the z turn changed more than 20 and the speed is 20 or more, the speed drops
// 10. With less than 15 life, the light is x times 0.8333333f in all three channels. Always, the radius-2 dynamic light
// at the position: ((rand() & 3) + 8) · 0.03f times (0.4f; 1; 0.8f), with the product unrounded.
Core::Expected<void, std::string> StepTargetBolt(Kernel::JointState& joint, Crt::CrtRandom* random,
                                                 Terrain::TerrainLighting* lighting)
{
    if (joint.ownerContext == nullptr)
    {
        joint.alive = false;
        return {};
    }
    if (random == nullptr)
        return Core::Unexpected(std::string("0x537c2c: falta el CRT del joint 0x610/6"));
    const auto& owner = *static_cast<const Kernel::ActorState*>(joint.ownerContext);
    constexpr std::int32_t RisingLife = 30, FadingLife = 15;
    constexpr double Tilt = 10.0, Rise = 2.5, Gain = 5.0, Brake = 10.0;       // 0x6b87a8, 0x6b89a8 and 0x6b87f8
    constexpr double Fastest = 30.0, Near = 70.0, Swerve = 20.0, Slow = 20.0; // 0x6b8858, 0x6ba1b0, 0x6ba2f8, 0x6b86d0
    constexpr double Lift = 150.0;                                            // 0x6b87ac
    if (joint.life >= RisingLife)
    {
        joint.angles.z = F32(D(joint.angles.z) - Tilt);
        joint.position.z = F32(D(joint.position.z) + Rise);
    }
    const double faster = D(joint.speed) + Gain;
    joint.speed = F32(faster);
    if (faster >= Fastest)
        joint.speed = static_cast<float>(Fastest);
    const float facing = joint.angles.z;
    const glm::vec3 target{owner.position.x, owner.position.y, F32(D(owner.position.z) + Lift)};
    const double distance = SteerJoint(joint, target, joint.speed);
    if (!(distance > Near) && std::abs(D(facing) - D(joint.angles.z)) > Swerve && D(joint.speed) >= Slow)
        joint.speed = F32(D(joint.speed) - Brake);
    if (joint.life < FadingLife)
    {
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f555555u)); // 0x6b8b98
        joint.light = glm::vec3(F32(D(joint.light.x) * Fade));
    }
    constexpr float Step = std::bit_cast<float>(0x3cf5c28fu); // 0x6b8bd4, 0.03f
    constexpr float Red = 0.4f, Blue = 0.8f;                  // 0x6b87b4 and 0x6b8a38
    const double luminosity = static_cast<double>((random->Next() & 3) + 8) * D(Step);
    const glm::vec3 color{F32(luminosity * D(Red)), F32(luminosity), F32(luminosity * D(Blue))};
    return Terrain::AddDynamicLight(lighting, joint.position, color, 2, "0x537c75");
}

// 0x53b7f1..0x53b93a: the 6e8/0 and /1, without the common move (0x53585e's list). With more than 9 life, thirty times
// the tail with the angle matrix (0x535520) and the position plus (0, 0, −speed) rotated by it; always, the speed 2
// less and the light times 0.7142857f. No common tail (+0x9f2 at 0). No rand().
Core::Expected<void, std::string> StepSwirl(Kernel::JointState& joint)
{
    constexpr std::int32_t LastQuietLife = 9, Turns = 30;
    constexpr double Slowdown = 2.0;                                                // 0x6b8818
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f36db6eu)); // 0x6ba268, 0.7142857f
    if (joint.life > LastQuietLife)
    {
        const auto matrix = Math::OriginalAngleMatrix(joint.angles);
        for (std::int32_t turn = 0; turn < Turns; ++turn)
        {
            if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
                return appended;
            const auto moved = Math::OriginalVectorRotate({0, 0, -joint.speed}, matrix);
            for (int axis = 0; axis < 3; ++axis)
                joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
        }
    }
    joint.speed = F32(D(joint.speed) - Slowdown);
    for (int channel = 0; channel < 3; ++channel)
        joint.light[channel] = F32(D(joint.light[channel]) * Fade);
    return {};
}

// 0x53b628..0x53b69b: the 61c/1. Without an owner it turns off; otherwise it takes the owner's alive byte, the width
// drops 1, the position is the owner's 100 higher and, with less than 15 life, the light times 0.7692308f. The common
// tail follows. No rand().
void StepLaserJoint(Kernel::JointState& joint)
{
    if (joint.ownerContext == nullptr)
    {
        joint.alive = false;
        return;
    }
    const auto& owner = *static_cast<const Effects::ModelEffect*>(joint.ownerContext);
    constexpr double Narrowing = 1.0, Lift = 100.0; // 0x6b87d4 and 0x6b86b4
    constexpr std::int32_t FadingLife = 15;
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240, 0.7692308f
    joint.alive = owner.alive;
    joint.width = F32(D(joint.width) - Narrowing);
    joint.position = owner.transform.position;
    joint.position.z = F32(D(joint.position.z) + Lift);
    if (joint.life < FadingLife)
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * Fade);
}

// 0x537f0b..0x537f94: the live /0xe leaves at its position the 0x5c6 sprite at scale 5 with its light (0x5491e0, no
// owner or turn), the 0x5f2/0x22 and /0x23 particles at scale 1 with its angles and light, and the 0x5c6/0xa at scale 4
// with light (0.7f; 0.3f; 0.3f), which it keeps; then the light becomes (1; 1; 0.2f).
Core::Expected<void, std::string> SparkTheViperTrail(Kernel::JointState& joint, Crt::CrtRandom& random,
                                                     Particles::FrameSprites* sprites, Particles::Particles* particles,
                                                     float worldClock)
{
    if (sprites == nullptr || particles == nullptr)
        return Core::Unexpected(std::string("0x537f20: faltan los sprites o las partículas del joint 0x610/0xe"));
    constexpr std::uint16_t Spark = 0x5c6;
    constexpr float SpriteScale = 5.0f; // 0x40a00000
    static_cast<void>(sprites->Create(Spark, joint.position, SpriteScale, joint.light, 0.0f, 0));
    constexpr std::int32_t Smoke = 0x5f2;
    for (const auto& [subtype, address] : {std::pair{0x22, "0x537f39"}, std::pair{0x23, "0x537f52"}})
    {
        const auto made = particles->CreateChecked(Smoke, joint.position, joint.angles, joint.light, subtype, 1.0f,
                                                   worldClock, random);
        if (!made.HasValue())
            return Core::Unexpected(
                std::format("{}: la partícula 0x5f2/{:#x} del joint 0x610/0xe falla con el error {}", address, subtype,
                            static_cast<unsigned>(made.Error())));
    }
    constexpr float Dim = std::bit_cast<float>(0x3e99999au);  // 0x537f5a
    constexpr float Red = std::bit_cast<float>(0x3f333333u);  // 0x537f79
    constexpr float Pale = std::bit_cast<float>(0x3e4ccccdu); // 0x537f94
    constexpr std::int32_t SparkSubtype = 0xa;
    constexpr float SparkScale = 4.0f; // 0x40800000
    joint.light = {Red, Dim, Dim};
    const auto spark = particles->CreateChecked(Spark, joint.position, joint.angles, joint.light, SparkSubtype,
                                                SparkScale, worldClock, random);
    if (!spark.HasValue())
        return Core::Unexpected(
            std::format("0x537f7f: la partícula 0x5c6/0xa del joint 0x610/0xe falla con el error {}",
                        static_cast<unsigned>(spark.Error())));
    joint.light = {1.0f, 1.0f, Pale};
    return {};
}

// 0x537ee6..0x538016: the 0x610/0xc..0xe trails. Without an owner they turn off (0x53be97); with the owner dead, too,
// and they go on. The live /0xe releases its own (SparkTheViperTrail). With less than 15 life, the light times
// 0.8333333f; then the dynamic light (r · 0.4f; r; r · 0.8f) with r = ((rand() & 3) + 8) · 0.03f, radius 2.
Core::Expected<void, std::string> StepArrowTrail(Kernel::JointState& joint, Crt::CrtRandom* random,
                                                 Terrain::TerrainLighting* lighting, Particles::FrameSprites* sprites,
                                                 Particles::Particles* particles, float worldClock)
{
    if (joint.ownerContext == nullptr)
    {
        joint.alive = false;
        return {};
    }
    if (joint.ownerKind != Kernel::JointOwnerKind::ModelEffect)
        return Core::Unexpected(
            std::format("0x537ef8: el dueño del 0x610/{} no es un ModelEffect durable", joint.subtype));
    if (!static_cast<const Effects::ModelEffect*>(joint.ownerContext)->alive)
        joint.alive = false; // Aun muerto siguen fade,RNG,luz,cola yvida.
    if (random == nullptr)
        return Core::Unexpected(std::format("0x537fc8: falta el CRT del 0x610/{}", joint.subtype));
    if (joint.subtype == ArrowTrailLast && joint.alive)
        if (auto sparked = SparkTheViperTrail(joint, *random, sprites, particles, worldClock); !sparked.HasValue())
            return sparked;
    if (joint.life < 15)
        FadeJointLight(joint, 0x3f555555u);
    const double luminosity =
        static_cast<double>((random->Next() & 3) + 8) * static_cast<double>(std::bit_cast<float>(0x3cf5c28fu));
    const glm::vec3 color{F32(luminosity * static_cast<double>(.4f)), F32(luminosity),
                          F32(luminosity * static_cast<double>(.8f))};
    return Terrain::AddDynamicLight(lighting, joint.position, color, 2, "0x538011");
}

// 0x538a52..0x538a70, 0x538b17..0x538b4a, 0x538b5d..0x538bdf and 0x538cc9..0x538d28: the 619/1 and 619/3, after the
// common advance. The speed rises 4 (0x6b8a44); it turns 10 at a time (0x4e9810) toward the owner (+0x54, read live),
// 120 higher (0x6b86c8); the light is (12 − life) · 0.1f times (0.4f; 0.6f; 1) for the /1 and times (1; 0.6f; 0.4f)
// for the /3, with the product unrounded; with life at 1, the 0x5fd sprite at that target with the light, a rand() %
// 360 turn and scale ((rand() & 7) + 8) · 0.2f. The common tail follows (0x53c393).
Core::Expected<void, std::string> StepSummonEnergy(Kernel::JointState& joint, std::size_t index, Crt::CrtRandom* random,
                                                   Particles::FrameSprites* sprites)
{
    constexpr std::int32_t FullLife = 12, LastLife = 1;
    if (joint.life == LastLife && (random == nullptr || sprites == nullptr))
        return Core::Unexpected(std::format(
            "0x538cce: faltan el RNG o los sprites del joint {} (0x619/{}) en su última vida", index, joint.subtype));
    if (joint.ownerContext == nullptr)
        return Core::Unexpected(std::format("0x538b17: falta el dueño del joint {} (0x619/{})", index, joint.subtype));
    const glm::vec3& position = joint.ownerKind == Kernel::JointOwnerKind::ModelEffect
                                    ? static_cast<const Effects::ModelEffect*>(joint.ownerContext)->transform.position
                                    : static_cast<const Kernel::ActorState*>(joint.ownerContext)->position;
    constexpr double Climb = 4.0, Lift = 120.0;
    constexpr float TurnStep = 10;
    joint.speed = F32(Climb + D(joint.speed));
    const glm::vec3 target{position.x, position.y, F32(D(position.z) + Lift)};
    static_cast<void>(SteerJoint(joint, target, TurnStep));
    constexpr float Tenth = Math::Tenth, Low = std::bit_cast<float>(0x3ecccccdu),
                    Middle = std::bit_cast<float>(0x3f19999au); // 0x6b87b8, 0x6b87b4 and 0x6b8a20
    const double brightness = static_cast<double>(FullLife - joint.life) * D(Tenth);
    if (joint.subtype == SummonDecalEnergySubtype)
        joint.light = {F32(brightness), F32(brightness * D(Middle)), F32(brightness * D(Low))};
    else
        joint.light = {F32(brightness * D(Low)), F32(brightness * D(Middle)), F32(brightness)};
    if (joint.life != LastLife)
        return {};
    constexpr std::uint16_t ShinyTexture = 0x5fd;
    constexpr int FullTurn = Math::FullTurn, ScaleSpread = 7, ScaleBase = 8;
    constexpr float ScaleStep = std::bit_cast<float>(0x3e4ccccdu);
    // 0x5491e0(0x5fd, target, scale, light, owner, turn, 0): MuRet does not store the sprite's owner (Q-C303).
    const auto rotation = static_cast<float>(random->Next() % FullTurn);
    const auto scale = F32(static_cast<double>((random->Next() & ScaleSpread) + ScaleBase) * D(ScaleStep));
    static_cast<void>(sprites->Create(ShinyTexture, target, scale, joint.light, rotation, 0));
    return {};
}

// 0x538a52..0x538a70, 0x538b4c..0x538b5a and 0x538c25..0x538ca9: the 619/6 of the 0x813's bolts, after the common
// advance. The speed rises 2 (0x6b8818); it neither turns nor changes the light. With 10 life or less, the 0x5fd sprite
// at its target (+0x58) with the gray light (6 − |life − 6|) · 0.15f of [ebp−0x3c], a rand() % 360 turn and scale
// ((rand() & 7) + 8) · 0.05f. The common tail follows (0x53c393).
Core::Expected<void, std::string> StepBoltEnergy(Kernel::JointState& joint, std::size_t index, Crt::CrtRandom* random,
                                                 Particles::FrameSprites* sprites)
{
    constexpr double Climb = 2.0; // 0x6b8818
    constexpr std::int32_t LastDarkLife = 10, BrightestLife = 6;
    joint.speed = F32(Climb + D(joint.speed));
    if (joint.life > LastDarkLife)
        return {};
    if (random == nullptr || sprites == nullptr)
        return Core::Unexpected(
            std::format("0x538c5d: faltan el RNG o los sprites del joint {} (0x619/6) en su chispa", index));
    constexpr float GlowStep = std::bit_cast<float>(0x3e19999au);  // 0x6b8ae8, 0.15f
    constexpr float ScaleStep = std::bit_cast<float>(0x3d4ccccdu); // 0x6b8a48, 0.05f
    constexpr std::uint16_t ShinyTexture = 0x5fd;
    constexpr int FullTurn = Math::FullTurn, ScaleSpread = 7, ScaleBase = 8;
    const std::int32_t distance = joint.life < BrightestLife ? BrightestLife - joint.life : joint.life - BrightestLife;
    const float glow = F32(static_cast<double>(BrightestLife - distance) * D(GlowStep));
    // 0x5491e0(0x5fd, target, scale, light, owner, turn, 0), with the 0x813's null owner.
    const auto rotation = static_cast<float>(random->Next() % FullTurn);
    const auto scale = F32(static_cast<double>((random->Next() & ScaleSpread) + ScaleBase) * D(ScaleStep));
    static_cast<void>(sprites->Create(ShinyTexture, joint.targetPosition, scale, glm::vec3(glow), rotation, 0));
    return {};
}

// 0x538a52..0x538a70, 0x538a94..0x538b15, 0x538b5d..0x538b8d → 0x538c23 and 0x538cb7: the charged skill's 619/8, after
// the common advance. The speed rises 4 (0x6b8a44); with more than 10 life it turns 10 at a time (0x4e9810) toward the
// target (+0x58); with less than 6, each light channel times 0.5555556f (0x6ba234). In all three cases, the 0x5ac
// sprite at its position with scale 1, the light, no owner, turn 0 and blend 0 (0x5491e0). The light of 0x538b5d does
// not change: table 0x53c628 sends the /8 to 0x538c23, which discards the product. No rand(); the common tail follows
// (0x53c393).
Core::Expected<void, std::string> StepChargedEnergy(Kernel::JointState& joint, std::size_t index,
                                                    Particles::FrameSprites* sprites)
{
    constexpr double Climb = 4.0;                             // 0x6b8a44
    constexpr std::int32_t LastStraightLife = 10;             // 0x538aad
    constexpr std::int32_t FirstBrightLife = 6;               // 0x538acf
    constexpr float TurnStep = 10;                            // 0x538ab8
    constexpr float Fade = std::bit_cast<float>(0x3f0e38e4u); // 0x6ba234, 0.5555556f
    constexpr std::uint16_t ChargeSprite = 0x5ac;
    constexpr float ChargeScale = 1; // 0x538b02
    if (sprites == nullptr)
        return Core::Unexpected(std::format("0x538b0d: faltan los sprites del joint {} (0x619/8)", index));
    joint.speed = F32(Climb + D(joint.speed));
    const glm::vec3 target = joint.targetPosition;
    if (joint.life > LastStraightLife)
        static_cast<void>(SteerJoint(joint, target, TurnStep));
    else if (joint.life < FirstBrightLife)
        joint.light = {F32(D(joint.light.x) * D(Fade)), F32(D(joint.light.y) * D(Fade)),
                       F32(D(joint.light.z) * D(Fade))};
    static_cast<void>(sprites->Create(ChargeSprite, joint.position, ChargeScale, joint.light, 0, 0));
    return {};
}

// 0x53a73b..0x53a7cc: the delta stays retained until after the turn, before the persistent limit.
void AdvanceSkill39Bend(Kernel::JointState& joint)
{
    constexpr double BendStep = 1.5, BendLimit = 15.0, PitchStep = 3.0;
    const double bend = D(joint.velocity.y) + BendStep;
    joint.velocity.y = F32(bend);
    switch (joint.field9ec)
    {
    case 0:
    case 2:
        joint.bendAngles.x = F32(D(joint.bendAngles.x) + PitchStep);
        break;
    case 1:
    case 3:
        joint.bendAngles.x = F32(D(joint.bendAngles.x) - PitchStep);
        break;
    case 5:
        joint.bendAngles.x = F32(D(joint.bendAngles.x) - bend);
        break;
    default:
        break;
    }
    if (joint.field9ec == 0 || joint.field9ec == 1 || joint.field9ec == 4)
        joint.bendAngles.z = F32(D(joint.bendAngles.z) - bend);
    if (joint.field9ec == 2 || joint.field9ec == 3)
        joint.bendAngles.z = F32(D(joint.bendAngles.z) + bend);
    if (bend > BendLimit)
        joint.velocity.y = static_cast<float>(BendLimit);
}

// 0x53a6b3..0x53a875: each sample is placed over the anchor, without accumulating the previous position.
Core::Expected<void, std::string> StepSkill39Bolt(Kernel::JointState& joint)
{
    constexpr std::int32_t OpeningLife = 20, LastBendLife = 8, FadeLife = 10;
    constexpr double SampleStep = 4.0;
    constexpr float BoltSpeed = 200.0f;
    if (joint.life < LastBendLife)
    {
        joint.speed = 0;
        if (auto appended = AppendJointTail(joint, Math::OriginalAngleMatrix(joint.bendAngles)); !appended.HasValue())
            return appended;
    }
    else if (joint.life < OpeningLife)
    {
        joint.speed = BoltSpeed;
        for (int sample = 0; static_cast<double>(sample) < D(joint.velocity.z); ++sample)
        {
            AdvanceSkill39Bend(joint);
            const auto matrix = Math::OriginalAngleMatrix(joint.bendAngles);
            const auto offset = Math::OriginalVectorRotate({0, -joint.speed, 0}, matrix);
            joint.position = {F32(D(joint.pathIncrement.x) + offset.x), F32(D(joint.pathIncrement.y) + offset.y),
                              F32(D(joint.pathIncrement.z) + offset.z)};
            if (auto appended = AppendJointTail(joint, matrix); !appended.HasValue())
                return appended;
            if (joint.count >= joint.maximumCount - 1)
            {
                joint.speed = 0;
                break;
            }
        }
        joint.velocity.z = F32(D(joint.velocity.z) + SampleStep);
    }
    if (joint.life < FadeLife)
        FadeJointLight(joint, 0x3f44ec4fu);
    return {};
}

// The families with their own file (JointFamilies.h).
enum class Family : std::uint8_t
{
    Lightning,
    HandBolt,
    Spirit,
    Drop,
    Aura,
    Storm,
    Beam,
    Spiral,
    Wandering,
    Inferno,
    FireTrail,
    Helix,
    Flare,
    Orbit,
    Sweep,
    Plasma,
    Spark,
};

std::optional<Family> FamilyStep(const Kernel::JointState& joint)
{
    if (joint.type == JointFamilies::LightningJoint)
        return Family::Lightning;
    if (joint.type == JointFamilies::RedHandBoltJoint || joint.type == JointFamilies::BlueHandBoltJoint)
        return Family::HandBolt;
    // The /4 and /9 of the 613 and the 6ee go on to the step below.
    if ((joint.type == JointFamilies::SpiritJoint || joint.type == JointFamilies::SecondSpiritJoint) &&
        joint.subtype != 4 && joint.subtype != 9)
        return Family::Spirit;
    if (JointFamilies::IsDrop(joint.type, joint.subtype))
        return Family::Drop;
    if (JointFamilies::IsOrbit(joint.type, joint.subtype))
        return Family::Orbit;
    // The /1 (the fairy) and the /3 (Blood Castle) go on to the step below, owned by the FaunaObject.
    if (joint.type == JointFamilies::AuraJoint && joint.subtype != 1 && joint.subtype != 3)
        return Family::Aura;
    if (joint.type == JointFamilies::StormJoint)
        return Family::Storm;
    if (joint.type == JointFamilies::BeamJoint)
        return Family::Beam;
    if (joint.type == JointFamilies::SpiralJoint)
        return Family::Spiral;
    if (IsWandering(joint.type, joint.subtype))
        return Family::Wandering;
    if (JointFamilies::IsInferno(joint.type, joint.subtype))
        return Family::Inferno;
    if (joint.type == JointFamilies::FireTrailJoint)
        return Family::FireTrail;
    if (JointFamilies::IsHelix(joint.type, joint.subtype))
        return Family::Helix;
    if (joint.type == JointFamilies::FlareJoint)
        return Family::Flare;
    if (joint.type == JointFamilies::InfernoJoint && joint.subtype == JointFamilies::SweepSubtype)
        return Family::Sweep;
    if (joint.type == JointFamilies::PlasmaJoint)
        return Family::Plasma;
    if (JointFamilies::IsSpark(joint.type, joint.subtype))
        return Family::Spark;
    return std::nullopt;
}

// 0x53585e..0x53590c: those that neither move nor compute the matrix of 0x53590e.
bool SkipsTheCommonMove(const Kernel::JointState& joint)
{
    const std::int32_t type = joint.type;
    const std::int32_t subtype = joint.subtype;
    return (type == 0x61c && subtype == 0) || (type == 0x614 && subtype == 0xf) || type == 0x5ac || type == 0x6e8 ||
           type == 0x62c || ((type == 0x60f || type == 0x62b) && subtype == 1) ||
           (type == 0x10f && subtype >= 5 && subtype <= 8) || (type == 0x60f && subtype == 0x2a) ||
           (type == 0x632 && subtype == 6);
}

// 0x53c393..0x53c3aa: the tail with the [ebp−0xe8] matrix. Those in 0x53585e's list do not compute it and use the one
// of the last joint of this 0x53c670 loop that did (the frame is the same); with none before, whatever is on the stack
// (undefined, Q-C1623): without drawing (+0x11 at 0) it is not seen and only the count runs; otherwise, it stops.
Core::Expected<void, std::string> AppendWithFrameMatrix(Kernel::JointState& joint,
                                                        const std::optional<Math::OriginalMatrix>& matrix)
{
    if (!joint.tailEnabled)
        return {};
    if (matrix)
        return AppendJointTail(joint, *matrix);
    if (joint.drawEnabled == 0)
        return JointKernels::AdvanceJointTail(joint);
    return Core::Unexpected(std::format("0x53c3a5: la cola del joint {:#x}/{:#x} usa la matriz que quedó en la pila "
                                        "(indefinido, Q-C1623)",
                                        joint.type, joint.subtype));
}

// 0x53590e..0x53597a: the common move, with the angle matrix from before the case (the one the tail of 0x53c393 uses).
// Except those in the 0x53585e..0x53590c list (SkipsTheCommonMove), which do not compute it.
Math::OriginalMatrix MoveCommonly(Kernel::JointState& joint)
{
    const auto matrix = Math::OriginalAngleMatrix(joint.angles);
    if (joint.speed < 0 || joint.speed > 0)
    {
        const auto moved = Math::OriginalVectorRotate({0, -joint.speed, 0}, matrix);
        for (int axis = 0; axis < 3; ++axis)
            joint.position[axis] = static_cast<float>(static_cast<double>(joint.position[axis]) + moved[axis]);
    }
    return matrix;
}

// 0x53c393..0x53c3ba: the tail with the matrix from the start of the step, and the life.
Core::Expected<void, std::string> CommonTail(Kernel::JointState& joint,
                                             const std::optional<Math::OriginalMatrix>& matrix)
{
    if (auto appended = AppendWithFrameMatrix(joint, matrix); !appended.HasValue())
        return appended;
    joint.life = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.life) - 1u);
    if (joint.life < 0)
        joint.alive = false;
    return {};
}

Core::Expected<void, std::string> RunFamily(Kernel::JointState& joint, Family family,
                                            const JointKernels::StepServices& services)
{
    switch (family)
    {
    case Family::Lightning:
        return JointFamilies::StepLightningJoint(joint, services);
    case Family::HandBolt:
        return JointFamilies::StepHandBoltJoint(joint, services);
    case Family::Spirit:
        return JointFamilies::StepSpiritJoint(joint, services);
    case Family::Drop:
        JointFamilies::StepDropJoint(joint);
        return {};
    case Family::Aura:
        return JointFamilies::StepAuraJoint(joint, services);
    case Family::Storm:
        return JointFamilies::StepStormJoint(joint, services);
    case Family::Beam:
        return JointFamilies::StepBeamJoint(joint, services);
    case Family::Spiral:
        return JointFamilies::StepSpiralJoint(joint, services);
    case Family::Wandering:
        return JointFamilies::StepWanderingJoint(joint, services);
    case Family::Inferno:
        return JointFamilies::StepInfernoJoint(joint, services);
    case Family::FireTrail:
        return JointFamilies::StepFireTrailJoint(joint);
    case Family::Helix:
        return JointFamilies::StepHelixJoint(joint);
    case Family::Flare:
        return JointFamilies::StepFlareJoint(joint);
    case Family::Orbit:
        return JointFamilies::StepOrbitJoint(joint, services);
    case Family::Sweep:
        return JointFamilies::StepSweepJoint(joint, services);
    case Family::Plasma:
        return JointFamilies::StepPlasmaJoint(joint, services);
    case Family::Spark:
        return JointFamilies::StepSparkJoint(joint, services);
    }
    return {};
}

Core::Expected<void, std::string> StepFamilyJoint(Kernel::JointState& joint, Family family,
                                                  JointKernels::StepServices services,
                                                  std::optional<Math::OriginalMatrix>& frameMatrix)
{
    // 0x53584f..0x53586c: the position minus the target in x and y, as float, before moving.
    services.startDeltaX = F32(D(joint.position.x) - D(joint.targetPosition.x));
    services.startDeltaY = F32(D(joint.position.y) - D(joint.targetPosition.y));
    if (!SkipsTheCommonMove(joint))
        frameMatrix = MoveCommonly(joint);
    bool returned = false;
    services.returned = &returned;
    if (auto stepped = RunFamily(joint, family, services); !stepped.HasValue())
        return stepped;
    if (returned)
        return {};
    if (auto tail = CommonTail(joint, frameMatrix); !tail.HasValue())
        return tail;
    // 0x53c3c6..0x53c3f3: with life left (already decremented) and not a multiple of 12, the 0x60f and 0x62b with
    // subtype 4 or 0xc take another whole step, with its own frame: this one's [ebp−0xe8] matrix does not change.
    constexpr std::int32_t Beat = 12;
    if (family == Family::Helix && joint.life >= 0 && joint.life % Beat != 0)
    {
        std::optional<Math::OriginalMatrix> innerMatrix;
        return StepFamilyJoint(joint, family, services, innerMatrix);
    }
    return {};
}

Core::Expected<void, std::string>
StepJointsChecked(std::span<Kernel::JointState> joints, Crt::CrtRandom* random, Models::ClothWorld* cloth,
                  Particles::Particles* particles, float worldClock, Particles::FrameSprites* sprites,
                  Terrain::TerrainLighting* lighting, const std::function<void(Audio::SoundId)>& soundRequest,
                  const std::function<double(float, float)>& terrainHeight, std::span<const Kernel::ActorState> actors,
                  const JointKernels::EffectHit& effectHit, Effects::ModelEffects* effects,
                  const Kernel::ActorState* hero, Kernel::WorldState* world, const JointKernels::VolleyServices* volley,
                  const Models::IModelLibrary* models)
{
    // [ebp−0xe8]: the matrix of the last joint of this loop that computed it (0x53590e).
    std::optional<Math::OriginalMatrix> frameMatrix;
    for (std::size_t i = 0; i < joints.size(); ++i)
    {
        auto& joint = joints[i];
        if (!joint.alive)
            continue;
        if (const auto family = FamilyStep(joint))
        {
            const JointKernels::StepServices services{.joints = joints,
                                                      .index = i,
                                                      .random = random,
                                                      .cloth = cloth,
                                                      .particles = particles,
                                                      .sprites = sprites,
                                                      .lighting = lighting,
                                                      .worldClock = worldClock,
                                                      .soundRequest = &soundRequest,
                                                      .terrainHeight = terrainHeight ? &terrainHeight : nullptr,
                                                      .actors = actors,
                                                      .effectHit = effectHit ? &effectHit : nullptr,
                                                      .effects = effects,
                                                      .hero = hero,
                                                      .world = world,
                                                      .volley = volley,
                                                      .models = models};
            if (auto stepped = StepFamilyJoint(joint, *family, services, frameMatrix); !stepped.HasValue())
                return stepped;
            continue;
        }
        const bool skill39 = joint.type == SparkleJoint && joint.subtype == 0x17;
        const bool piercingImpact =
            (joint.type == SparkleJoint || joint.type == SparkImpactJoint) && joint.subtype == 6;
        const bool crywolf = joint.type == 0x697; // 0x5381cb: the step does not read the subtype either.
        const bool bloodCastle = joint.type == 0x10f && joint.subtype == 3;
        const bool fairy = joint.type == 0x10f && joint.subtype == 1;
        const bool spiritBeam = (joint.type == 0x613 || joint.type == 0x6ee) && joint.subtype == 4;
        const bool icarus = joint.type == 0x610 && joint.subtype == 8;
        const bool piercingTrail = joint.type == 0x610 && joint.subtype >= 0 && joint.subtype <= 3;
        const bool crywolfArrowTrail =
            joint.type == 0x610 && joint.subtype >= ArrowTrailFirst && joint.subtype <= ArrowTrailLast;
        // The one of Kalima's 0x95 (0x43b0ed), owned by the target character.
        const bool targetBolt = joint.type == 0x610 && joint.subtype == 6;
        // The 0x114/1's swirls (0x51c25f), ownerless.
        const bool swirl = joint.type == SwirlJoint && (joint.subtype == 0 || joint.subtype == 1);
        // The ray of the 0xd0/1 laser (0x51a5dd), owned by the effect.
        const bool laser = joint.type == LaserJoint && joint.subtype == LaserSubtype;
        const bool noria = joint.type == 0x617 && joint.subtype >= 0 && joint.subtype <= 3;
        const bool skyBolt = joint.type == 0x615 && joint.subtype == 0;
        const bool trapBolt = joint.type == 0x615 && (joint.subtype == 1 || joint.subtype == 2 || joint.subtype == 5 ||
                                                      joint.subtype == 6 || joint.subtype == 7);
        const bool meteor = (joint.type == 0x6ee || joint.type == 0x613) && joint.subtype == 9;
        // Those of what hangs from a player, with the character's ActorState as owner.
        const bool swordTrail = joint.type == 0x610 && joint.subtype == 4;
        // The full set's (0x5102c3) and the 5ac/3's (0x52aa5d), with the character's ActorState.
        // And the quest reward's (0x4ddb80), the 60f/0 and the 60f/0xa.
        const bool sparkle =
            joint.type == SparkleJoint && (joint.subtype == SparkleSubtype || joint.subtype == WideSparkleSubtype ||
                                           joint.subtype == FallingSparkleSubtype);
        const bool fallenSparkle = joint.type == SparkleJoint && joint.subtype == FallenSparkleSubtype;
        const bool setTrail = joint.type == SetTrailJoint && joint.subtype == SetTrailSubtype;
        const bool setEnergy =
            joint.type == EnergyJoint && (joint.subtype == SetEnergySubtype || joint.subtype == SkillEnergySubtype);
        const bool fullSet = sparkle || fallenSparkle || setTrail || setEnergy;
        // The pet decal's (0x518900), owned by the hero.
        const bool summonEnergy = joint.type == EnergyJoint &&
                                  (joint.subtype == SummonEnergySubtype || joint.subtype == SummonDecalEnergySubtype);
        // The 0x813's bolts (0x52cb85), ownerless.
        const bool boltEnergy = joint.type == EnergyJoint && joint.subtype == BoltEnergySubtype;
        // The charged skill's (0x5189f0), owned by the character, which the step does not read.
        const bool chargedEnergy = joint.type == EnergyJoint && joint.subtype == ChargedEnergySubtype;
        if (!crywolf && !bloodCastle && !fairy && !spiritBeam && !icarus && !noria && !skyBolt && !trapBolt &&
            !meteor && !swordTrail && !fullSet && !summonEnergy && !boltEnergy && !chargedEnergy &&
            !crywolfArrowTrail && !piercingTrail && !piercingImpact && !skill39 && !targetBolt && !swirl && !laser)
            return Core::Unexpected(
                std::format("0x535840: el paso del joint {} ({:#x}/{}) no está portado", i, joint.type, joint.subtype));
        // 0x5358bc: the 60f/1 and the 6e8 (the 0x53585e list) neither move nor compute the matrix of 0x53590e; with the
        // tail, 0x53c393 uses the one the previous joint left in [ebp−0xe8] (AppendWithFrameMatrix).
        // 0x53590e..0x53597a: the common move precedes the owner read, even if it dies.
        const bool still = SkipsTheCommonMove(joint);
        if (!still)
            frameMatrix = Math::OriginalAngleMatrix(joint.angles);
        // 0x535932 also skips the sums for unordered, keeping signed zeros and the position.
        if (!still && (joint.speed < 0 || joint.speed > 0))
        {
            const auto moved = Math::OriginalVectorRotate({0, -joint.speed, 0}, *frameMatrix);
            joint.position = {static_cast<float>(static_cast<double>(joint.position.x) + moved.x),
                              static_cast<float>(static_cast<double>(joint.position.y) + moved.y),
                              static_cast<float>(static_cast<double>(joint.position.z) + moved.z)};
        }
        if (skill39)
            if (auto result = StepSkill39Bolt(joint); !result.HasValue())
                return result;
        if (skyBolt)
            if (auto result = StepSkyBolt(joint, random); !result.HasValue())
                return result;
        if (trapBolt)
            if (auto result = StepTrapBolt(joint, random, sprites, particles, worldClock, terrainHeight);
                !result.HasValue())
                return result;
        if (meteor)
            if (auto result = StepMeteor(joint, random); !result.HasValue())
                return result;
        if (piercingImpact)
            if (auto result = StepPiercingImpact(joint); !result.HasValue())
                return result;
        if (sparkle)
            if (auto result = StepSparkle(joint, i); !result.HasValue())
                return result;
        if (fallenSparkle && joint.life < 10)
            FadeJointLight(joint, 0x3f44ec4fu); // 0x53b339: the /1 only fades.
        if (setTrail)
            if (auto result = StepSetTrail(joint, i); !result.HasValue())
                return result;
        if (setEnergy)
            if (auto result = StepSetEnergy(joint, i); !result.HasValue())
                return result;
        if (piercingTrail)
            if (auto result = StepPiercingTrailJoint(joint); !result.HasValue())
                return result;
        if (crywolfArrowTrail)
            if (auto result = StepArrowTrail(joint, random, lighting, sprites, particles, worldClock);
                !result.HasValue())
                return result;
        if (summonEnergy)
            if (auto result = StepSummonEnergy(joint, i, random, sprites); !result.HasValue())
                return result;
        if (boltEnergy)
            if (auto result = StepBoltEnergy(joint, i, random, sprites); !result.HasValue())
                return result;
        if (chargedEnergy)
            if (auto result = StepChargedEnergy(joint, i, sprites); !result.HasValue())
                return result;
        if (targetBolt)
            if (auto result = StepTargetBolt(joint, random, lighting); !result.HasValue())
                return result;
        if (swirl)
            if (auto result = StepSwirl(joint); !result.HasValue())
                return result;
        if (laser)
            StepLaserJoint(joint);
        const bool actorOwned =
            swordTrail || fullSet || summonEnergy || crywolfArrowTrail || piercingTrail || targetBolt;
        const auto* owner = (joint.type == 0x613 || joint.type == 0x614 || joint.type == 0x615 || meteor ||
                             actorOwned || boltEnergy || chargedEnergy || swirl || laser)
                                ? nullptr
                                : static_cast<const Fauna::FaunaObject*>(joint.ownerContext);
        const bool ownerless = spiritBeam || icarus || noria || skyBolt || trapBolt || meteor || actorOwned ||
                               piercingImpact || skill39 || boltEnergy || chargedEnergy || swirl || laser;
        if (owner == nullptr && !ownerless)
            return Core::Unexpected(std::format("{}: falta el dueño durable del joint {:#x}/{}",
                                                fairy         ? "0x536076"
                                                : bloodCastle ? "0x535efe"
                                                              : "0x5381ce",
                                                joint.type, joint.subtype));
        // The 0x697 copies before checking active.
        if (crywolf)
            joint.position = owner->previousPosition;
        if (!bloodCastle && !fairy && !ownerless && owner->active == 0)
        {
            joint.alive = false;
            continue; // 0x5381e9: no tail and no life decrement.
        }
        if (swordTrail)
            if (auto result = StepSwordTrail(joint, i); !result.HasValue())
                return result;
        if (icarus)
        {
            // 0x53801e..0x538055: a null owner turns it off; a dead one also copies. Both go on to tail/life. The owner
            // is the fish or the Kalima player (KalimaBeamOwnerOf).
            const std::optional<KalimaBeamOwner> beamOwner = KalimaBeamOwnerOf(joint);
            joint.alive = beamOwner && beamOwner->alive;
            if (beamOwner)
            {
                joint.position = *beamOwner->position;
                joint.position.z = 300;
            }
        }
        if (fairy)
            if (auto result = StepFairyJoint(joint, *owner, i, random, cloth); !result.HasValue())
                return result;
        if (bloodCastle)
            if (auto result = StepBloodCastleJoint(joint, *owner, random); !result.HasValue())
                return result;
        if (noria && (joint.subtype == 1 || joint.subtype == 3))
        {
            // 0x538074: the other two branches go straight on to tail/life, without RNG.
            joint.speed = static_cast<float>(static_cast<double>(joint.speed) +
                                             static_cast<double>(joint.subtype == 1 ? 0.3f : 0.1f));
            constexpr float FirstFade = std::bit_cast<float>(0x3f36db6eu);
            constexpr float ThirdFade = std::bit_cast<float>(0x3f68ba2eu);
            const auto faded = static_cast<float>(static_cast<double>(joint.light.x) *
                                                  static_cast<double>(joint.subtype == 1 ? FirstFade : ThirdFade));
            joint.light = glm::vec3(faded);
        }
        // 0x53c393: 0x535520 with the matrix of 0x53590e, the one from the start of the step, even if the case turned.
        if (auto appended = AppendWithFrameMatrix(joint, frameMatrix); !appended.HasValue())
            return appended;
        joint.life = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.life) - 1u);
        if (joint.life < 0)
            joint.alive = false;
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> StepJoints(std::span<Kernel::JointState> joints)
{
    return StepJointsChecked(joints, nullptr, nullptr, nullptr, 0, nullptr, nullptr, {}, {}, {}, {}, nullptr, nullptr,
                             nullptr, nullptr, nullptr);
}

Core::Expected<void, std::string>
StepJoints(std::span<Kernel::JointState> joints, Crt::CrtRandom& random, Models::ClothWorld* cloth,
           Particles::Particles* particles, float worldClock, Particles::FrameSprites* sprites,
           Terrain::TerrainLighting* lighting, const std::function<void(Audio::SoundId)>& soundRequest,
           const std::function<double(float, float)>& terrainHeight, std::span<const Kernel::ActorState> actors,
           const JointKernels::EffectHit& effectHit, Effects::ModelEffects* effects, const Kernel::ActorState* hero,
           Kernel::WorldState* world, const JointKernels::VolleyServices* volley, const Models::IModelLibrary* models)
{
    return StepJointsChecked(joints, &random, cloth, particles, worldClock, sprites, lighting, soundRequest,
                             terrainHeight, actors, effectHit, effects, hero, world, volley, models);
}
} // namespace Client::Joints
