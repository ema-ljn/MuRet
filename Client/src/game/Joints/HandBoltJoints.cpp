#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Particles/Particles.h"
#include "WorldObjects/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <string>

// The 0x61b and 0x5bc, the monsters' hand bolts (0x4f5b70): they share creation (0x5335cf, 0x5335d6) and step
// (0x539ccc..0x539f16); the 0x61b repeats the texture.
namespace Client::Joints::JointFamilies
{
namespace
{
using Crt::CrtRandom;
using Math::D;
using Math::F32;
} // namespace

void CreateHandBoltJoint(Kernel::JointState& joint)
{
    // 0x5335cf..0x5335f3: speed 40, width 60, 50 points and life 2.
    if (joint.type == RedHandBoltJoint)
        joint.repeatedTexture = true;
    joint.speed = 40.0f;
    joint.width = 60.0f;
    joint.maximumCount = 50;
    joint.life = 2;
    if (joint.subtype == 0)
        joint.light = glm::vec3(1.0f);
    else if (joint.subtype == 3)
    {
        // 0x533616..0x533634: still, with life 20.
        joint.speed = 0.0f;
        joint.light = glm::vec3(1.0f);
        joint.life = 20;
    }
    else if (joint.type == RedHandBoltJoint)
        joint.light = {std::bit_cast<float>(0x3eb33333u), std::bit_cast<float>(0x3dcccccdu), 1.0f};
    else
        joint.light = {0.0f, std::bit_cast<float>(0x3e99999au), 1.0f};
}

namespace
{
// 0x539def..0x539e6f: (rand() & 3) + 4 times 0.05f (0x6b8a48) in the radius-2 dynamic light.
Core::Expected<void, std::string> Glow(const Kernel::JointState& joint, CrtRandom& random,
                                       Terrain::TerrainLighting* lighting)
{
    constexpr double Step = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
    constexpr double Tenth = Math::TenthAsDouble;                                    // 0x6b87b8
    constexpr double Green = static_cast<double>(std::bit_cast<float>(0x3f19999au)); // 0x6b8a20, 0.6f
    constexpr double Blue = static_cast<double>(std::bit_cast<float>(0x3e99999au));  // 0x6b89dc, 0.3f
    constexpr double Fifth = Math::FifthAsDouble;                                    // 0x6b8854, 0.2f
    const double value = static_cast<double>((random.Next() & 3) + 4) * Step;
    glm::vec3 color;
    if (joint.type != RedHandBoltJoint)
        color = {0.0f, F32(value * Tenth), F32(value * Fifth)};
    else if (joint.subtype == 1)
        color = {F32(value), F32(value * Tenth), F32(value * Tenth)};
    else
        color = {F32(value), F32(value * Green), F32(value * Blue)};
    return WorldObjects::AddDynamicLight(lighting, joint.position, color, 2, "0x539e6f");
}

// 0x539ed4..0x539f11: on arrival, once every two, three rand() for a point nobody reads and the 0x5d9/0 fire.
Core::Expected<void, std::string> Arrive(const Kernel::JointState& joint, CrtRandom& random,
                                         const JointKernels::StepServices& services)
{
    if (random.Next() % 2 != 0)
        return {};
    for (int axis = 0; axis < 3; ++axis)
        static_cast<void>(random.Next());
    if (services.particles == nullptr)
        return Core::Unexpected(
            std::format("0x539f11: faltan las partículas del joint {:#x}/{}", joint.type, joint.subtype));
    const auto created = services.particles->CreateChecked(Particles::FireParticle, joint.position, joint.angles,
                                                           joint.light, 0, 1.0f, services.worldClock, random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x539f11: falló el 0x5d9 del joint {:#x}/{} ({})", joint.type,
                                            joint.subtype, static_cast<int>(created.Error())));
    return {};
}
} // namespace

Core::Expected<void, std::string> StepHandBoltJoint(Kernel::JointState& joint,
                                                    const JointKernels::StepServices& services)
{
    if (joint.maximumCount <= 0)
        return {};
    if (services.random == nullptr)
        return Core::Unexpected(std::format("0x539d18: falta el RNG del joint {:#x}/{}", joint.type, joint.subtype));
    CrtRandom& random = *services.random;
    constexpr double Damping = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38, 0.8f
    for (std::int32_t iteration = 0; iteration < joint.maximumCount; ++iteration)
    {
        // 0x539ce1..0x539cff: except the /2, the target is the owner character, 80 higher.
        if (joint.subtype != 2)
        {
            const Kernel::ActorState* owner = JointKernels::ActorOwner(joint);
            if (owner == nullptr)
                return Core::Unexpected(
                    std::format("0x539cea: falta el personaje dueño del joint {:#x}/{}", joint.type, joint.subtype));
            joint.targetPosition = {owner->position.x, owner->position.y, F32(D(owner->position.z) + 80.0)};
        }
        const float distance = F32(Math::MoveToward(joint.position, joint.angles, joint.targetPosition, 25.0f));
        // 0x539d18..0x539dc1: the shake accumulates in the velocity and is damped by 0.8f; z stays on the x87 stack.
        const double width = D(joint.width);
        joint.velocity.x = F32(static_cast<double>((random.Next() & 0xff) - 0x80) / width + D(joint.velocity.x));
        joint.velocity.x = F32(D(joint.velocity.x) * Damping);
        joint.velocity.y = F32(D(joint.velocity.y) * Damping);
        const double roll =
            (static_cast<double>((random.Next() & 0xff) - 0x80) / width + D(joint.velocity.z)) * Damping;
        joint.velocity.z = F32(roll);
        const glm::vec3 shaken{F32(D(joint.angles.x) + D(joint.velocity.x)),
                               F32(D(joint.velocity.y) + D(joint.angles.y)), F32(roll + D(joint.angles.z))};
        const auto matrix = Math::OriginalAngleMatrix(shaken);
        if (auto appended = JointKernels::AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
        // 0x539dd6..0x539de9: it arrives when twice the speed is no longer less than what is left (with NaN, C0 at 1,
        // it goes on).
        if (D(joint.speed) + D(joint.speed) >= D(distance))
            return Arrive(joint, random, services);
        if (auto lit = Glow(joint, random, services.lighting); !lit.HasValue())
            return lit;
        const auto moved = Math::OriginalVectorRotate({0.0f, -joint.speed, 0.0f}, matrix);
        for (int axis = 0; axis < 3; ++axis)
            joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    }
    return {};
}
} // namespace Client::Joints::JointFamilies
