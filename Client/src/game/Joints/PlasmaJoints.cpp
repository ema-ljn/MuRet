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
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "WorldObjects/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

// The 0x14f (creation 0x530ddd..0x530ed8, step 0x535b37..0x535d4a): the four rays that the skill of 0x5025b8 casts
// toward its target, owned by the target character: 0x5026a4 and 0x5027f0 of width 100 (0x42c80000) with subtype
// 0x4f2230(o), and 0x5026ed and 0x502841 of width 80 (0x42a00000) with that subtype + 3. Like the 0x614
// (LightningJoints.cpp), it takes one turn per point: it turns toward the owner 80 higher (0x4e9810, by 50), shakes,
// appends a segment and advances, until it arrives. The subtype comes from 0x4f2230 (the +0x31b, pet.flags1b, Fenrir's
// excellent option).
namespace Client::Joints::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;

Core::Expected<void, std::string> Particle(const Kernel::JointState& joint, std::int32_t type,
                                           const JointKernels::StepServices& services, std::string_view address)
{
    if (services.particles == nullptr)
        return Core::Unexpected(std::format("{}: faltan las partículas del joint 0x14f/{}", address, joint.subtype));
    const auto created = services.particles->CreateChecked(type, joint.position, joint.angles, joint.light, 0, 1.0f,
                                                           services.worldClock, *services.random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: falló la partícula {:#x} del joint 0x14f/{} ({})", address, type,
                                            joint.subtype, static_cast<int>(created.Error())));
    return {};
}

// 0x535cdf..0x535d4a: on arrival, only with the width at exactly 50 (the immediate 0x42480000), the 0x5ca particle;
// then, with rand() % 8 at 0, three unused rand() and the 0x5f2/0.
Core::Expected<void, std::string> Arrive(const Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    constexpr std::uint32_t ArrivalWidthBits = 0x42480000u;
    if (std::bit_cast<std::uint32_t>(joint.width) != ArrivalWidthBits)
        return {};
    if (auto made = Particle(joint, Particles::ThunderParticle, services, "0x535d03"); !made.HasValue())
        return made;
    if (services.random->Next() % 8 != 0)
        return {};
    for (int unused = 0; unused < 3; ++unused)
        static_cast<void>(services.random->Next());
    return Particle(joint, Particles::BreathParticle, services, "0x535d45");
}

// 0x535c26..0x535c77: with the width at 50 or more, the radius-2 dynamic light with ((rand() & 3) + 4) · 0.04f · 0.4f
// in all three channels.
Core::Expected<void, std::string> Glow(const Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    constexpr float BrightWidth = 50; // 0x6b86b0
    if (!(joint.width >= BrightWidth))
        return {};
    constexpr double Step = static_cast<double>(std::bit_cast<float>(0x3d23d70au)); // 0x6b8aec, 0.04f
    constexpr double Dim = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));  // 0x6b87b4, 0.4f
    const std::int32_t shade = (services.random->Next() & 3) + 4;
    const float value = F32(static_cast<double>(shade) * Step * Dim);
    return WorldObjects::AddDynamicLight(services.lighting, joint.position, glm::vec3(value), 2, "0x535c77");
}
} // namespace

void CreatePlasmaJoint(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    // 0x530ddd..0x530e0a: speed 50, the repeated texture (+0x9f0), the width, 50 points and life 20. Per subtype, the
    // texture and light: /0..2 the 0x614 with (0.7f; 1; 0.7f), (1; 0.6f; 0.6f) and (0.7f; 0.7f; 1); /3..5 the 0x632
    // with (0.1f; 0.8f; 0.1f), (1; 0.3f; 0.2f) and (0.2f; 0.3f; 1). The others keep the prefix's texture and light.
    constexpr float Speed = 50;
    constexpr std::int32_t Points = 50, Life = 20;
    joint.speed = Speed;
    joint.repeatedTexture = true;
    joint.width = request.scale;
    joint.maximumCount = Points;
    joint.life = Life;
    constexpr float Tenth = Math::Tenth, Fifth = Math::Fifth;
    constexpr float Third = std::bit_cast<float>(0x3e99999au), Sixth = std::bit_cast<float>(0x3f19999au);
    constexpr float Seventh = std::bit_cast<float>(0x3f333333u), Eighth = std::bit_cast<float>(0x3f4ccccdu);
    constexpr std::uint16_t Lightning = 0x614, Storm = 0x632;
    switch (joint.subtype)
    {
    case 0:
        joint.texture = Lightning;
        joint.light = {Seventh, 1, Seventh};
        break;
    case 1:
        joint.texture = Lightning;
        joint.light = {1, Sixth, Sixth};
        break;
    case 2:
        joint.texture = Lightning;
        joint.light = {Seventh, Seventh, 1};
        break;
    case 3:
        joint.texture = Storm;
        joint.light = {Tenth, Eighth, Tenth};
        break;
    case 4:
        joint.texture = Storm;
        joint.light = {1, Third, Fifth};
        break;
    case 5:
        joint.texture = Storm;
        joint.light = {Fifth, Third, 1};
        break;
    default:
        break;
    }
}

Core::Expected<void, std::string> StepPlasmaJoint(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    // 0x535b37..0x535cd4: after the common move, one turn per point.
    for (std::int32_t point = 0; point < joint.maximumCount; ++point)
    {
        // 0x535b4c..0x535b68: with an owner, the target is its position (+0x10) 80 higher.
        if (joint.ownerContext != nullptr)
        {
            const Kernel::ActorState* owner = JointKernels::ActorOwner(joint);
            if (owner == nullptr)
                return Core::Unexpected(std::string("0x535b53: el dueño del joint 0x14f no es un personaje"));
            constexpr double Above = 80.0; // 0x6b9ffc
            joint.targetPosition = {owner->position.x, owner->position.y, F32(D(owner->position.z) + Above)};
        }
        constexpr float Turn = 50; // 0x42480000
        const float distance = F32(Math::MoveToward(joint.position, joint.angles, joint.targetPosition, Turn));
        if (services.random == nullptr)
            return Core::Unexpected(std::string("0x535b81: falta el RNG del joint 0x14f"));
        // 0x535b81..0x535bf4: vx and vz, ((rand() & 0x3ff) − 512) / width; the segment with the angles plus (vx, vy,
        // vz), z with the unrounded quotient.
        constexpr int Mask = 0x3ff, Center = 0x200;
        const std::int32_t pitch = (services.random->Next() & Mask) - Center;
        joint.velocity.x = F32(static_cast<double>(pitch) / D(joint.width));
        const std::int32_t yaw = (services.random->Next() & Mask) - Center;
        const double turn = static_cast<double>(yaw) / D(joint.width);
        joint.velocity.z = F32(turn);
        const glm::vec3 shaken{F32(D(joint.angles.x) + D(joint.velocity.x)),
                               F32(D(joint.angles.y) + D(joint.velocity.y)), F32(turn + D(joint.angles.z))};
        const auto matrix = Math::OriginalAngleMatrix(shaken);
        if (auto appended = JointKernels::AppendJointTail(joint, matrix); !appended.HasValue())
            return appended;
        // 0x535c09..0x535c20: it arrives with speed · 1.5 greater than the distance (fcomp, test ah,0x41).
        constexpr double Reach = 1.5; // 0x6b8b94
        if (D(joint.speed) * Reach > D(distance))
            return Arrive(joint, services);
        if (auto lit = Glow(joint, services); !lit.HasValue())
            return lit;
        // 0x535c7f..0x535cd1: the advance (0, −speed, 0) rotated by the segment's matrix.
        const auto moved = Math::OriginalVectorRotate({0, -joint.speed, 0}, matrix);
        for (int axis = 0; axis < 3; ++axis)
            joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    }
    return {};
}
} // namespace Client::Joints::JointFamilies
