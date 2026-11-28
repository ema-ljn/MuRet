#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Kernel/JointRequest.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <string>

// The 0x5ac (creation 0x530c93..0x530dd8 and 0x53323b..0x533253, step 0x5359d7..0x535b32): the rays that the 0x114/0
// (0x51c1b8) and the BlastModelEffects combo (0x51c004) release sixty at a time, ownerless. Without the common tail
// (+0x9f2 at 0) or the common move (0x53585e): the step itself appends the segments and advances.
namespace Client::Joints::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;

// 0x535a07..0x535a61 and 0x535aa0..0x535b0c: a segment with the angle matrix (0x535520) and then the advance,
// (0, −speed, 0) rotated and added to the position. 0x535a1d..0x535a3f and 0x535ab6..0x535ad8: 0x5fcbd0 gets the same
// vector [ebp−0x2c] as input and output, so y rereads the already rotated x and z rereads both.
Core::Expected<void, std::string> Advance(Kernel::JointState& joint, const Math::OriginalMatrix& matrix)
{
    if (auto appended = JointKernels::AppendJointTail(joint, matrix); !appended.HasValue())
        return appended;
    glm::vec3 moved{0, -joint.speed, 0};
    Math::OriginalVectorRotateInPlace(moved, matrix);
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    return {};
}
} // namespace

void CreateFlareJoint(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random)
{
    // 0x530c93: all without the common tail.
    joint.tailEnabled = false;
    constexpr int FullTurn = Math::FullTurn;
    if (joint.subtype == 0)
    {
        // 0x530ca5..0x530d35: life 20, 30 points, the width; speed rand() % 10 + 10, the angles' x 30 − rand() % 40, y
        // at 0 and z rand() % 360; the speed at 0 and light (0.1f; 0.5f; 1) (0x53323b).
        constexpr std::int32_t Life = 20, Points = 30;
        constexpr double SpeedBase = 10.0, Tilt = 30.0;
        constexpr float Dim = std::bit_cast<float>(0x3dcccccdu);
        joint.life = Life;
        joint.maximumCount = Points;
        joint.width = request.scale;
        const std::int32_t speed = random.Next() % 10;
        joint.speed = F32(static_cast<double>(speed) + SpeedBase);
        const std::int32_t tilt = random.Next() % 40;
        joint.angles.x = F32(Tilt - static_cast<double>(tilt));
        joint.angles.y = 0;
        joint.angles.z = static_cast<float>(random.Next() % FullTurn);
        joint.velocity = {0, 0, 0};
        joint.light = {Dim, 0.5f, 1};
        // 0x533242..0x533250: +0x24 stores the position.
        joint.pathIncrement = joint.position;
    }
    else if (joint.subtype == 1)
    {
        // 0x530d43..0x530dd8: life 10, 20 points, the width; the skill (+0x9ea) rand() % 5, speed rand() % 5 + 1,
        // angles (30, 0, rand() % 360), the speed at 0 and light (1; 0.8f; 0.6f). 0x533242: +0x24.
        constexpr std::int32_t Life = 10, Points = 20;
        constexpr float Tilt = 30;
        joint.life = Life;
        joint.maximumCount = Points;
        joint.width = request.scale;
        joint.skill = static_cast<std::uint8_t>(random.Next() % 5);
        const std::int32_t speed = random.Next() % 5;
        joint.speed = F32(static_cast<double>(speed) + 1.0);
        joint.angles.x = Tilt;
        joint.angles.y = 0;
        joint.angles.z = static_cast<float>(random.Next() % FullTurn);
        joint.velocity = {0, 0, 0};
        joint.light = {1, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f19999au)};
        joint.pathIncrement = joint.position;
    }
    // 0x534cb6: the points cap (the other subtypes keep the slot's).
    constexpr std::int32_t MostPoints = 50;
    if (joint.maximumCount > MostPoints)
        joint.maximumCount = MostPoints;
}

Core::Expected<void, std::string> StepFlareJoint(Kernel::JointState& joint)
{
    if (joint.subtype == 0 && joint.life > 16)
    {
        // 0x5359e2..0x535a61: with life over 16, ten segments with the angle matrix.
        constexpr int Steps = 10;
        const auto matrix = Math::OriginalAngleMatrix(joint.angles);
        for (int step = 0; step < Steps; ++step)
            if (auto advanced = Advance(joint, matrix); !advanced.HasValue())
                return advanced;
    }
    else if (joint.subtype == 1 && (joint.skill == 0 || joint.life > 3))
    {
        // 0x535a71..0x535b0c: with the skill at 0 or life over 3, five segments; the speed adds 0.5 in each.
        constexpr int Steps = 5;
        const auto matrix = Math::OriginalAngleMatrix(joint.angles);
        for (int step = 0; step < Steps; ++step)
        {
            if (auto advanced = Advance(joint, matrix); !advanced.HasValue())
                return advanced;
            joint.speed = F32(D(joint.speed) + 0.5);
        }
    }
    // 0x535b0e..0x535b2f: all dim the light by 0.8333333f (0x6b8b98).
    constexpr double Fading = static_cast<double>(std::bit_cast<float>(0x3f555555u));
    for (int axis = 0; axis < 3; ++axis)
        joint.light[axis] = F32(D(joint.light[axis]) * Fading);
    return {};
}
} // namespace Client::Joints::JointFamilies
