#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRotation.h"
#include "Actors/ActorFlags.h"
#include "Kernel/ActorState.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Math/Ftol.h"
#include "Kernel/JointRequest.h"
#include "Terrain/DynamicWorldLight.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>

// The 0x610/5, the sparks that Kalima's sixth helper (0x196) releases from its bones 63..67 toward bone 60 (0x43e904):
// creation 0x531c43..0x531cc7 and 0x531dd0..0x531dff, step 0x537a00..0x537b31. And the 0x60f/0x2a, the one that turns
// around NPC 257 (0x5165d1, owned by it), with the 0x60f/0x29 it releases (0x53a0eb): creation 0x5339a5 and 0x533915,
// step 0x53a039 and 0x539fc2.
namespace Client::Joints::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;

constexpr double Glimmer = static_cast<double>(std::bit_cast<float>(0x3cf5c28fu)); // 0x6b8bd4, 0.03f

// 0x531c43..0x531cc7 and 0x531dd0..0x532003: the common 0x610 part (life to the key, the key to 0, 50 points and the
// skill color's width: 20 with 0 and 3, 40 with 1; with another, the width the slot had) and the /5: light at 1 (over
// the color's), blend 4, only the second face (+0x11 at 2), speed 3, life 50, 8 points and pass 2 (+0x64). The target
// is the second point. No rand().
Core::Expected<void, std::string> CreateHelperSpark(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    constexpr std::uint8_t OwnerColor = 3;
    // 0x531c71..0x531c8c: color 3 reads the owner's light (+0xf4), which the /5 overwrites; without an owner, the
    // original crashes.
    if (joint.skill == OwnerColor && joint.ownerContext == nullptr)
        return Core::Unexpected(std::string("0x531c7b: falta el color del dueño del joint 0x610/5"));
    joint.life = joint.key;
    joint.key = 0;
    constexpr std::int32_t SharedPoints = 50;
    joint.maximumCount = SharedPoints;
    constexpr float NarrowWidth = 20.0f, WideWidth = 40.0f; // 0x41a00000 and 0x42200000
    if (joint.skill == 0 || joint.skill == OwnerColor)
        joint.width = NarrowWidth;
    else if (joint.skill == 1)
        joint.width = WideWidth;
    constexpr std::uint8_t Subtractive = 4, SecondFace = 2, SecondPass = 2;
    constexpr float Speed = 3.0f; // 0x40400000
    constexpr std::int32_t Life = 50, Points = 8;
    joint.light = glm::vec3(1.0f);
    joint.blend = Subtractive;
    joint.drawEnabled = SecondFace;
    joint.speed = Speed;
    joint.life = Life;
    joint.maximumCount = Points;
    joint.category = SecondPass;
    joint.targetPosition = request.secondPosition;
    return {};
}

// 0x537a00..0x537b31, after the common move: with life 25 or more it turns z back 10 and rises 2.5. Then it speeds up
// by 5 up to 30 and turns toward the target at that speed (0x4e9810); near (70 or less, or NaN), if the heading changed
// more than 20 and the speed is 20 or more, it brakes 10. With life under 10, the three lights are the red one times
// 0.8333333f. Always, the radius-2 dynamic light at its position, (0.4; 1; 0.8) times (rand() % 4 + 8) · 0.03f.
Core::Expected<void, std::string> StepHelperSpark(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    if (services.random == nullptr)
        return Core::Unexpected(std::string("0x537ae0: falta el RNG del joint 0x610/5"));
    constexpr std::int32_t RisingLife = 25, FadeLife = 10;
    if (joint.life >= RisingLife)
    {
        constexpr double Turn = 10.0, Rise = 2.5; // 0x6b87a8 and 0x6b89a8
        joint.angles.z = F32(D(joint.angles.z) - Turn);
        joint.position.z = F32(D(joint.position.z) + Rise);
    }
    else
    {
        constexpr double Accel = 5.0, TopSpeed = 30.0; // 0x6b87f8 and 0x6b8858
        const double faster = D(joint.speed) + Accel;
        joint.speed = F32(faster);
        if (faster >= TopSpeed) // 0x537a38: compares the unrounded sum.
            joint.speed = static_cast<float>(TopSpeed);
        const float heading = joint.angles.z; // [ebp+8]
        const glm::vec3 target = joint.targetPosition;
        const double distance = Math::MoveToward(joint.position, joint.angles, target, joint.speed);
        constexpr double Near = 70.0, Swerve = 20.0, Brake = 10.0; // 0x6ba1b0, 0x6ba2f8 (double) and 0x6b86d0
        if (!(distance > Near) && std::abs(D(heading) - D(joint.angles.z)) > Swerve && D(joint.speed) >= Swerve)
            joint.speed = F32(D(joint.speed) - Brake);
        if (joint.life < FadeLife)
        {
            constexpr double Fading = static_cast<double>(std::bit_cast<float>(0x3f555555u)); // 0x6b8b98
            const float faded = F32(D(joint.light.x) * Fading);
            joint.light = glm::vec3(faded);
        }
    }
    const double glow = static_cast<double>((services.random->Next() & 3) + 8) * Glimmer;
    constexpr double Red = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));  // 0x6b87b4, 0.4f
    constexpr double Blue = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38, 0.8f
    const glm::vec3 color{F32(glow * Red), F32(glow), F32(glow * Blue)};
    constexpr std::int32_t Radius = 2;
    return Terrain::AddDynamicLight(services.lighting, joint.position, color, Radius, "0x537b29");
}

// 0x533677..0x53368e: the common 0x60f part (life 100, 20 points and width 10), which the subtype overwrites.
void CreateCommonSparkle(Kernel::JointState& joint)
{
    constexpr std::int32_t Life = 100, Points = 20;
    constexpr float Width = 10.0f;
    joint.life = Life;
    joint.maximumCount = Points;
    joint.width = Width;
}

// 0x533915..0x53399b: the /0x29, life 40, the request's width, 50 points, light (1 − a, 1 − b, 1) with a and b at
// rand() % 300 · 0.001f (a stored as float, b not) and vz at rand() % 5 + 5. Three rand().
void CreateFallingSparkle(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random)
{
    constexpr std::int32_t Life = 40, Points = 50, Spread = 300, Lift = 5;
    constexpr double Thousandth = Math::ThousandthAsDouble; // 0x6ba128
    joint.life = Life;
    joint.width = request.scale;
    joint.maximumCount = Points;
    const float first = F32(static_cast<double>(random.Next() % Spread) * Thousandth);
    const double second = static_cast<double>(random.Next() % Spread) * Thousandth;
    joint.light = {F32(1.0 - D(first)), F32(1.0 - second), 1.0f};
    joint.velocity.z = static_cast<float>(random.Next() % Lift + Lift);
}

// 0x5339a5..0x533a1e: the /0x2a is created at (0, −15, 0) rotated by its angles further on, with the request's width;
// the path (+0x24) is that position, the velocity (0, −50, 0), without the common tail (+0x9f2) and light at 1. No
// rand().
void CreateSpinningSparkle(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    constexpr float Start = -15.0f, Radius = -50.0f; // 0xc1700000 and 0xc2480000
    joint.velocity.y = Start;
    const auto moved = Math::OriginalVectorRotate(joint.velocity, Math::OriginalAngleMatrix(joint.angles));
    joint.width = request.scale;
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    joint.pathIncrement = joint.position;
    joint.velocity.y = Radius;
    joint.tailEnabled = false;
    joint.light = glm::vec3(1.0f);
}

// 0x539fc2..0x53a028: the /0x29 with life 20 or less stays over the target, rises by vz (which grows by 1) and dims the
// light by 0.9090909f; always, the radius-1 dynamic light with its light at its position.
Core::Expected<void, std::string> StepFallingSparkle(Kernel::JointState& joint,
                                                     const JointKernels::StepServices& services)
{
    constexpr std::int32_t RisingLife = 20;
    if (joint.life <= RisingLife)
    {
        const double rise = D(joint.velocity.z) + 1.0;
        joint.position.x = joint.targetPosition.x;
        joint.position.y = joint.targetPosition.y;
        joint.velocity.z = F32(rise);
        joint.position.z = F32(rise + D(joint.position.z));
        constexpr double Fading = static_cast<double>(std::bit_cast<float>(0x3f68ba2eu)); // 0x6ba26c
        for (int axis = 0; axis < 3; ++axis)
            joint.light[axis] = F32(D(joint.light[axis]) * Fading);
    }
    constexpr std::int32_t Radius = 1;
    return Terrain::AddDynamicLight(services.lighting, joint.position, joint.light, Radius, "0x53a023");
}

// 0x53a039..0x53a122: the /0x2a turns its z by 15. With the owner dead (its alive byte), it turns off all of that
// owner's 0x60f/0x2a (0x534f90, itself included) and goes to the common tail. Alive: life 100, the position at the path
// plus the velocity rotated by the angles; rand() % 100 and, if the z in ftol is a multiple of 45 and the rand() is not
// over 50, it releases a 0x60f/0x29 at its position, with angles at 0, ownerless and its width. Then, the radius-2
// dynamic light at the target.
Core::Expected<void, std::string> StepSpinningSparkle(Kernel::JointState& joint,
                                                      const JointKernels::StepServices& services)
{
    constexpr double Spin = 15.0; // 0x6b87bc
    joint.angles.z = F32(D(joint.angles.z) + Spin);
    const Kernel::ActorState* owner = JointKernels::ActorOwner(joint);
    if (owner == nullptr)
        return Core::Unexpected(std::string("0x53a048: el dueño del joint 0x60f/0x2a no es un personaje"));
    if (!owner->alive)
    {
        Actors::KillJointsOf(services.joints, DropJoint, owner, SpinningSparkleSubtype);
        return {};
    }
    if (services.random == nullptr)
        return Core::Unexpected(std::string("0x53a09f: falta el RNG del joint 0x60f/0x2a"));
    constexpr std::int32_t Life = 100;
    joint.life = Life;
    const auto moved = Math::OriginalVectorRotate(joint.velocity, Math::OriginalAngleMatrix(joint.angles));
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(moved[axis]) + D(joint.pathIncrement[axis]));
    constexpr int Chance = 100, Most = 50, Beat = 45;
    const int roll = services.random->Next() % Chance;
    const auto turn = Math::OriginalFtolLow(D(joint.angles.z));
    if (!turn)
        return Core::Unexpected(std::string("0x53a0b2: el giro del joint 0x60f/0x2a excede ftol"));
    if (*turn % Beat == 0 && roll <= Most)
    {
        glm::vec3 still(0.0f); // [ebp−0x3c]
        const Kernel::JointRequest sparkle{.type = DropJoint,
                                           .firstPosition = joint.position,
                                           .secondPosition = joint.position,
                                           .angles = still,
                                           .subtype = FallingSparkleSubtype,
                                           .ownerContext = nullptr,
                                           .scale = joint.width,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
        if (auto created = CreateFaunaJoint(services.joints, sparkle, *services.random); !created.HasValue())
            return Core::Unexpected(std::format("0x53a0eb: {}", created.Error()));
    }
    constexpr std::int32_t Radius = 2;
    return Terrain::AddDynamicLight(services.lighting, joint.targetPosition, joint.light, Radius, "0x53a105");
}
} // namespace

bool IsSpark(std::int32_t type, std::int32_t subtype)
{
    return (type == HelperSparkJoint && subtype == HelperSparkSubtype) ||
           (type == DropJoint && (subtype == FallingSparkleSubtype || subtype == SpinningSparkleSubtype));
}

Core::Expected<void, std::string> CreateSparkJoint(Kernel::JointState& joint, const Kernel::JointRequest& request,
                                                   Crt::CrtRandom& random)
{
    if (request.type == HelperSparkJoint)
        return CreateHelperSpark(joint, request);
    CreateCommonSparkle(joint);
    if (request.subtype == FallingSparkleSubtype)
        CreateFallingSparkle(joint, request, random);
    else
        CreateSpinningSparkle(joint, request);
    // 0x5342be: the target is the second point. 0x534cb6: the 50-point cap changes nothing.
    joint.targetPosition = request.secondPosition;
    return {};
}

Core::Expected<void, std::string> StepSparkJoint(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    if (joint.type == HelperSparkJoint)
        return StepHelperSpark(joint, services);
    const auto stepped = joint.subtype == FallingSparkleSubtype ? StepFallingSparkle(joint, services)
                                                                : StepSpinningSparkle(joint, services);
    if (!stepped.HasValue())
        return stepped;
    // 0x53b318..0x53b363: with less than 10 life, the light times 0.7692308f. The /0x2a arrives with life 100 (and with
    // the owner dead goes straight to the tail, 0x53a122): it never fades here.
    constexpr std::int32_t FadeLife = 10;
    constexpr double Fading = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
    if (joint.subtype == FallingSparkleSubtype && joint.life < FadeLife)
        for (int axis = 0; axis < 3; ++axis)
            joint.light[axis] = F32(D(joint.light[axis]) * Fading);
    return {};
}
} // namespace Client::Joints::JointFamilies
