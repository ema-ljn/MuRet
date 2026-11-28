#include "Core/Expected.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointFamilies.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"

#include <cstdint>
#include <string>

// The 0x5f2 (creation 0x531229..0x53128f and 0x532073, step 0x5366bd..0x5366ee): the fire trail that the wizards'
// 0xc2/6 and /8 meteors (0x51b098 and 0x51b268) release, owned by them. The drawing (the /0 without segment 0 and with
// the light times life · 0.1f) is in GameJointDrawing.cpp.
namespace Client::Joints::JointFamilies
{
void CreateFireTrailJoint(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    // 0x531229..0x531231: the width. 0x531238..0x531260: the /0, speed 0, 20 points, life 20 and the 0x60f's texture.
    // 0x531265..0x53128f: the /1, speed 0, 15 points and life 15. 0x532073: the others only the light, with the points
    // and life the slot had. All with light at 1. No rand().
    joint.width = request.scale;
    if (joint.subtype == 0)
    {
        constexpr std::int32_t Points = 20;
        constexpr std::uint16_t SparkleTexture = 0x60f;
        joint.speed = 0;
        joint.maximumCount = Points;
        joint.life = Points;
        joint.texture = SparkleTexture;
    }
    else if (joint.subtype == 1)
    {
        constexpr std::int32_t Points = 15;
        joint.speed = 0;
        joint.maximumCount = Points;
        joint.life = Points;
    }
    joint.light = {1, 1, 1};
    // 0x534cb6: the points cap.
    constexpr std::int32_t MostPoints = 50;
    if (joint.maximumCount > MostPoints)
        joint.maximumCount = MostPoints;
}

Core::Expected<void, std::string> StepFireTrailJoint(Kernel::JointState& joint)
{
    // 0x5366bd..0x5366ee: with a live (+0) owner (+0x54) of type 0xc2 (+0x2), the position is the owner's (+0x10);
    // otherwise it stays where the common move left it. The common tail follows (0x53c393).
    if (joint.ownerContext == nullptr)
        return {};
    if (joint.ownerKind != Kernel::JointOwnerKind::ByType && joint.ownerKind != Kernel::JointOwnerKind::ModelEffect)
        return Core::Unexpected(std::string("0x5366c8: el dueño del joint 0x5f2 no es un efecto con modelo"));
    constexpr std::int16_t MeteorEffect = 0xc2;
    const auto& owner = *static_cast<const Effects::ModelEffect*>(joint.ownerContext);
    if (owner.alive && owner.type == MeteorEffect)
        joint.position = owner.transform.position;
    return {};
}
} // namespace Client::Joints::JointFamilies
