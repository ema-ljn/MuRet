#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Kernel/WorldState.h"
#include "Kernel/JointRequest.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

// The 0x62c (0x5346d8 and 0x53b93f): the rising ones of the 0x62c/0 effect (the 0x41) and of the 0x4c, and those on
// the owner's bone.
namespace Client::Joints::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;
using Math::OriginalAngleMatrix;
using Math::OriginalVectorRotate;

constexpr std::uint16_t SpiralTexture = 0x614;
constexpr std::uint16_t OrbitTexture = 0x60d;
constexpr float Pale = std::bit_cast<float>(0x3f4ccccdu); // 0.8f

// The /1..4 and /0xb..0xd, which spin around their rise (0x53476c, 0x53ba2c).
bool Turns(std::int32_t subtype)
{
    return (subtype >= 1 && subtype <= 4) || (subtype >= 0xb && subtype <= 0xd);
}

// The /5..7: they follow an owner bone (0x53bbe6).
bool Orbits(std::int32_t subtype)
{
    return subtype >= 5 && subtype <= 7;
}

glm::vec3 Sum(const glm::vec3& first, const glm::vec3& second)
{
    return {F32(D(first.x) + D(second.x)), F32(D(first.y) + D(second.y)), F32(D(first.z) + D(second.z))};
}

glm::vec3 Difference(const glm::vec3& first, const glm::vec3& second)
{
    return {F32(D(first.x) - D(second.x)), F32(D(first.y) - D(second.y)), F32(D(first.z) - D(second.z))};
}

void Fade(glm::vec3& light, float factor)
{
    light = {F32(D(light.x) * D(factor)), F32(D(light.y) * D(factor)), F32(D(light.z) * D(factor))};
}

// One segment of the rise: the tail with the angle matrix and the advance (0, speed, 0) rotated by it, added to the
// path increment (+0x24).
Core::Expected<Math::OriginalMatrix, std::string> Rise(Kernel::JointState& joint)
{
    const auto matrix = OriginalAngleMatrix(joint.angles);
    if (auto appended = JointKernels::AppendJointTail(joint, matrix); !appended.HasValue())
        return Core::Unexpected(appended.Error());
    joint.pathIncrement = Sum(OriginalVectorRotate({0, joint.speed, 0}, matrix), joint.pathIncrement);
    return matrix;
}

// 0x53b974..0x53ba27: the /0 rises straight, with more segments each step (+0x9ec, which grows by 2).
Core::Expected<void, std::string> StepTheColumn(Kernel::JointState& joint)
{
    constexpr double Brake = 2.0; // 0x6b8818
    for (std::int32_t step = 1; step < joint.field9ec; ++step)
    {
        if (auto risen = Rise(joint); !risen.HasValue())
            return Core::Unexpected(risen.Error());
        joint.position = joint.pathIncrement;
        joint.speed = F32(D(joint.speed) - Brake);
    }
    joint.field9ec += 2;
    return {};
}

// 0x53ba4c..0x53bbe1: the /1..4 and /0xb..0xd wait for their batch (+0x9eb) and then rise like the /0, shifted from the
// path by (0, 0, +0x58) rotated by (0, +0x5c, the angles' z): +0x5c drops 20 and +0x58 2.5 per segment. The /3 and /4
// subtract the shift; the others add it.
Core::Expected<void, std::string> StepTheSpiral(Kernel::JointState& joint)
{
    if (joint.volley > 0)
    {
        --joint.volley;
        return {};
    }
    constexpr double Brake = 2.0;  // 0x6b8818
    constexpr double Turn = 20.0;  // 0x6b86d0
    constexpr double Shrink = 2.5; // 0x6b89a8
    for (std::int32_t step = 1; step < joint.field9ec; ++step)
    {
        if (auto risen = Rise(joint); !risen.HasValue())
            return Core::Unexpected(risen.Error());
        // 0x53bab9..0x53bafc: the original's condition always leaves the subtraction (Q-C1624).
        joint.targetPosition.y = F32(D(joint.targetPosition.y) - Turn);
        const auto turn = OriginalAngleMatrix({0, joint.targetPosition.y, joint.angles.z});
        const glm::vec3 offset = OriginalVectorRotate({0, 0, joint.targetPosition.x}, turn);
        joint.position = joint.subtype == 3 || joint.subtype == 4 ? Difference(joint.pathIncrement, offset)
                                                                  : Sum(offset, joint.pathIncrement);
        joint.speed = F32(D(joint.speed) - Brake);
        joint.targetPosition.x = F32(D(joint.targetPosition.x) - Shrink);
    }
    joint.field9ec += 2;
    return {};
}

// 0x53bbf1..0x53be17: the /5..7 with an owner. Puts the owner's position into its type's shared model (only the origin,
// +0x6c..+0x74; the scale stays whatever it is) and builds the path at the point (0, 20, 0) of its bone +0x9ec
// (0x4ec110 with +0x120 + bone · 0x30). Then it takes as many segments as the lesser of +0x6c and the batch, each with
// that matrix's tail, and adds one to +0x6c, which thus grows by one up to the batch (30).
Core::Expected<void, std::string> FollowTheOwnerBone(Kernel::JointState& joint,
                                                     const JointKernels::StepServices& services)
{
    const Kernel::ActorState* owner = JointKernels::ActorOwner(joint);
    if (owner == nullptr)
        return Core::Unexpected(
            std::format("0x53bc27: el dueño del joint 0x62c/{} no es un personaje (CL11)", joint.subtype));
    if (services.world == nullptr)
        return Core::Unexpected(
            std::format("0x53bc42: faltan los modelos compartidos del joint 0x62c/{}", joint.subtype));
    // 0x53bc21..0x53bc57.
    Kernel::SharedModelPlacement placement = services.world->ModelPlacement(owner->type);
    placement.origin = owner->position;
    services.world->PlaceModel(owner->type, placement);
    // Without its own bones (before the first draw) or outside them, the matrix at zero (Q-C318), like BonePoint in
    // AttackFrameSkills.cpp.
    const auto bone = static_cast<std::size_t>(static_cast<std::uint32_t>(joint.field9ec));
    const Math::BoneTransform pose = bone < owner->bodyPose.size() ? owner->bodyPose[bone] : Math::BoneTransform(0.0f);
    const auto matrix = Math::OriginalMatrixOf(pose);
    constexpr glm::vec3 Above{0.0f, 20.0f, 0.0f}; // 0x53bc0d, 0x41a00000
    joint.pathIncrement = Math::OriginalTransformPosition(matrix, Above, placement.scale, placement.origin);
    // 0x53bc76..0x53bcae: the position at the path, the tail empty, +0x6c at the lesser (signed against the batch byte)
    // and the +0x60 turn into +0x5c.
    joint.position = joint.pathIncrement;
    joint.count = 0;
    const std::int32_t segments = std::min(joint.maximumCount, static_cast<std::int32_t>(joint.volley));
    joint.maximumCount = segments;
    joint.targetPosition.y = joint.targetPosition.z;
    // [ebp−0x3c]: the velocity (+0x9dc) in a local. With the key at −1 it drops 0.1f per segment only there (0x53bdbf).
    glm::vec3 velocity = joint.velocity;
    constexpr double Swing = 40.0;                                                    // 0x6b8bf4
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3e19999au)); // 0x6b8ae8, 0.15f
    constexpr double Lower = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));  // 0x6b87b8, 0.1f
    constexpr double HalfLight = 0.5;                                                 // 0x6b882c
    constexpr std::uint16_t Glint = 0x60f;                                            // 0x53bdf8
    for (std::int32_t segment = 0; segment < segments; ++segment)
    {
        // 0x53bcbe..0x53bd05: the path advances by the velocity rotated by the bone.
        joint.pathIncrement = Sum(OriginalVectorRotate(velocity, matrix), joint.pathIncrement);
        // 0x53bce8..0x53bd22: the /5 and /7 turn 40 to one side, the /6 to the other.
        const double turned = D(joint.targetPosition.y);
        joint.targetPosition.y = F32(joint.subtype % 2 != 0 ? turned + Swing : turned - Swing);
        // 0x53bd25..0x53bd96: the position, (0, 0, radius) rotated by (+0x5c, 0, 0) over the path.
        const auto turn = OriginalAngleMatrix({joint.targetPosition.y, 0.0f, 0.0f});
        joint.position = Sum(OriginalVectorRotate({0.0f, 0.0f, joint.targetPosition.x}, turn), joint.pathIncrement);
        // 0x53bda1: 0x535520 with the bone matrix.
        if (auto appended = JointKernels::AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
        joint.targetPosition.x = F32(D(joint.targetPosition.x) - Shrink);
        if (joint.key == -1)
            velocity.y = F32(D(velocity.y) - Lower);
        // 0x53bdcb..0x53be02: on even segments, the 0x60f sprite on the path with half the red light as scale.
        if (segment % 2 == 0)
        {
            if (services.sprites == nullptr)
                return Core::Unexpected(std::format("0x53bdfd: faltan los sprites del joint 0x62c/{}", joint.subtype));
            static_cast<void>(services.sprites->Create(Glint, joint.pathIncrement, F32(D(joint.light.x) * HalfLight),
                                                       joint.light, 0.0f, 0));
        }
    }
    ++joint.maximumCount; // 0x53be17
    return {};
}
} // namespace

Core::Expected<void, std::string> CreateSpiralJoint(Kernel::JointState& joint, const Kernel::JointRequest& request,
                                                    Crt::CrtRandom& random)
{
    // 0x5346d8..0x534765: the common part, with z 150 higher and the path (+0x24) at the position.
    constexpr double Lift = 150.0; // 0x6b87ac
    joint.tailEnabled = false;
    joint.speed = -3.0f; // 0xc0400000
    joint.position.z = F32(D(joint.position.z) + Lift);
    joint.width = request.scale;
    joint.life = 20;
    joint.maximumCount = 30;
    joint.velocity = {};
    joint.light = {1, Pale, 1};
    joint.field9ec = 1;
    joint.pathIncrement = joint.position;
    joint.targetPosition = {};
    joint.volley = 0;
    const std::int32_t subtype = joint.subtype;
    if (Turns(subtype))
    {
        // 0x53477e..0x534887: the batch rand() % 3 + 2 (at 0 for the /3 and /4) is added to life; radius 80 and turn
        // 180 (−180 for the /2, /4 and /0xb..0xd, which also have their color) set the position.
        joint.texture = SpiralTexture;
        joint.speed = -3.0f;
        joint.targetPosition.x = 80;
        joint.targetPosition.y = 180;
        joint.volley = static_cast<std::uint8_t>(random.Next() % 3 + 2);
        if (subtype == 2 || subtype == 4 || subtype >= 0xb)
        {
            constexpr float Soft = std::bit_cast<float>(0x3f333333u); // 0.7f
            constexpr float Dim = std::bit_cast<float>(0x3f19999au);  // 0.6f
            if (subtype == 0xb)
                joint.light = {Soft, 1, Soft};
            else if (subtype == 0xc)
                joint.light = {1, Dim, Dim};
            else if (subtype == 0xd)
                joint.light = {Soft, Soft, 1};
            joint.targetPosition.y = -180;
        }
        if (subtype == 3 || subtype == 4)
            joint.volley = 0;
        joint.life += joint.volley;
        const auto matrix = OriginalAngleMatrix(joint.targetPosition);
        joint.position = Sum(OriginalVectorRotate({0, 0, joint.targetPosition.x}, matrix), joint.pathIncrement);
    }
    else if (subtype != 0)
    {
        // 0x534897..0x534926: the other subtypes (the 0x62c/1 effect's /5..7) stay still on the path, with the owner's
        // bone in +0x9ec (the request's skill), a single tail turn and the turn subtype · 90 in +0x60.
        joint.skill = 0;
        joint.speed = 0;
        joint.field9ec = request.skill & 0xff;
        joint.velocity = {0, -4, 0};
        joint.life = 15;
        joint.maximumCount = 1;
        joint.targetPosition.z =
            static_cast<float>(std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(subtype) * 90u));
        joint.volley = 30;
        joint.targetPosition.x = 30;
        if (joint.key != -1)
            joint.velocity.y = 0;
    }
    // 0x53492c..0x534954 and 0x534cb6
    if (Orbits(subtype))
        joint.texture = OrbitTexture;
    else if (subtype == 0)
        joint.texture = SpiralTexture;
    constexpr std::int32_t MostPoints = 50;
    if (joint.maximumCount > MostPoints)
        joint.maximumCount = MostPoints;
    return {};
}

Core::Expected<void, std::string> StepSpiralJoint(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    const std::int32_t subtype = joint.subtype;
    if (Orbits(subtype))
    {
        // 0x53bbe6..0x53be49: without an owner, it dies (and goes on to the tail); with an owner, it follows its bone.
        if (joint.ownerContext == nullptr)
            joint.alive = false;
        else if (auto followed = FollowTheOwnerBone(joint, services); !followed.HasValue())
            return followed;
        constexpr std::int32_t FadeLife = 7;
        if (joint.life < FadeLife)
            Fade(joint.light, std::bit_cast<float>(0x3f2aaaabu)); // 0x6b8c64, 2/3
        return {};
    }
    // 0x53b95d..0x53b966: with the tail full (the count at points − 1), it only fades.
    if (joint.count < joint.maximumCount - 1)
    {
        if (subtype == 0)
        {
            if (auto stepped = StepTheColumn(joint); !stepped.HasValue())
                return stepped;
        }
        else if (Turns(subtype))
        {
            if (auto stepped = StepTheSpiral(joint); !stepped.HasValue())
                return stepped;
        }
    }
    // 0x53be4c..0x53be71 and 0x53b671: the /0..4 and /0xb..0xd with less than 10 life.
    constexpr std::int32_t FadeLife = 10;
    if ((subtype == 0 || Turns(subtype)) && joint.life < FadeLife)
        Fade(joint.light, std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
    return {};
}
} // namespace Client::Joints::JointFamilies
