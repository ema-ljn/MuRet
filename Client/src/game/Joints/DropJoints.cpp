#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Kernel/JointRequest.h"

#include <bit>
#include <cstdint>

// 0x60f/2 and 0x60f/0x18: the drop that 0x613/2 releases on its second step (0x5396ff). Born with white light and its
// second target point, still (no velocity) and, with life of 25 or less, it falls on the target: each step the z adds
// a velocity that grows by 5. /0x13 (that of 0x62b of circle 0xc3's columns, 0x5236ce and 0x523898) is the same with
// the velocity negated: it goes down. 0x62b shares 0x60f's creation and step.
namespace Client::Joints::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;

// 0x53b339..0x53b363: with life below 10, the light times 0.7692308f (0x6ba240).
void Fade(Kernel::JointState& joint)
{
    constexpr double Fading = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu));
    constexpr std::int32_t FadeLife = 10;
    if (joint.life >= FadeLife)
        return;
    for (int axis = 0; axis < 3; ++axis)
        joint.light[axis] = F32(D(joint.light[axis]) * Fading);
}
} // namespace

bool IsDrop(std::int32_t type, std::int32_t subtype)
{
    return (type == DropJoint || type == SecondHelixJoint) &&
           (subtype == DropSubtype || subtype == DarkDropSubtype || subtype == SinkingDropSubtype);
}

void CreateDropJoint(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random)
{
    // 0x533677..0x53368e: 0x60f's part before the subtype (life 100 and width 10, which the subtype overwrites) and 20
    // points.
    constexpr std::int32_t Points = 20;
    joint.maximumCount = Points;
    // 0x5340dc..0x53412d: /0x18 with texture 0x62d; vz at rand() % 20 + 35, the width, white light and life
    // rand() % 50 + 25. 0x533a28..0x533a40: /0x13 negates vz (fchs) and continues at 0x5340fe, without the texture.
    // 0x5342be: the target is the second point.
    constexpr std::uint16_t DarkDropTexture = 0x62d;
    constexpr std::int32_t FallBase = 35, LifeBase = 25;
    if (joint.subtype == DarkDropSubtype)
        joint.texture = DarkDropTexture;
    const std::int32_t fall = random.Next() % 20 + FallBase;
    joint.velocity.z = static_cast<float>(joint.subtype == SinkingDropSubtype ? -fall : fall);
    joint.width = request.scale;
    const std::int32_t life = random.Next() % 50;
    joint.light = {1, 1, 1};
    joint.life = life + LifeBase;
    joint.targetPosition = request.secondPosition;
}

void StepDropJoint(Kernel::JointState& joint)
{
    // 0x53b156..0x53b185: with life of 25 or less, vz + 5 (the float in +0x9e4, the sum unrounded in the z), x and y at
    // the target. 0x53a12c..0x53a15b: /0x13, vz − 5.
    constexpr std::int32_t FallLife = 25;
    if (joint.life <= FallLife)
    {
        const double fall = joint.subtype == SinkingDropSubtype ? D(joint.velocity.z) - 5.0 : D(joint.velocity.z) + 5.0;
        joint.position.x = joint.targetPosition.x;
        joint.position.y = joint.targetPosition.y;
        joint.velocity.z = F32(fall);
        joint.position.z = F32(fall + D(joint.position.z));
    }
    Fade(joint);
}
} // namespace Client::Joints::JointFamilies
