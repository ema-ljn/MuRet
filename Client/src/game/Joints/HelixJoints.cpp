#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalRetainedMath.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"

#include <bit>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

// The 0x60f/4 and /0xc (and the 0x62b, which shares their creation and step): the sparks that the 0x812 releases each
// step (0x52c9da and 0x52c9a7, owned by it). They turn around their target, which advances (vy, vz) per step, in a
// vertical circle oriented by the z of their angles; the /4 with a radius that shrinks with life and the /0xc with a
// fixed one of 26 that sinks 0.3 per step. Their step repeats in the same frame until life is a multiple of 12
// (0x53c3c6).
namespace Client::Joints::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;

// 0x6ba2c4 and 0x6b8cf8: degrees to radians, negated (creation) and not (step).
constexpr double NegativeRadians = static_cast<double>(std::bit_cast<float>(0xbc8efa35u));
constexpr double Radians = Math::DegreesToRadiansAsDouble;
} // namespace

bool IsHelix(std::int32_t type, std::int32_t subtype)
{
    return (type == DropJoint || type == SecondHelixJoint) && (subtype == HelixSubtype || subtype == WideHelixSubtype);
}

Core::Expected<void, std::string> CreateHelixJoint(Kernel::JointState& joint, const Kernel::JointRequest& request,
                                                   Crt::CrtRandom& random)
{
    // 0x533677..0x53368e: the common 0x60f part (life 100, 20 points and width 10), which the subtype overwrites.
    // 0x533faf: the width.
    joint.life = 100;
    joint.maximumCount = 20;
    joint.width = 10;
    joint.width = request.scale;
    const bool wide = joint.subtype == WideHelixSubtype;
    double reach = 0;
    if (wide)
    {
        // 0x533fba..0x533fd5: life 70, light (0.1f; 0.1f; 1) and 50 points.
        constexpr float Dim = std::bit_cast<float>(0x3dcccccdu);
        joint.life = 0x46;
        joint.light = {Dim, Dim, 1};
        joint.maximumCount = 0x32;
        reach = -4.0; // 0x6ba2bc
    }
    else
    {
        // 0x53405c..0x53408d: life 110, 200 points (the cap of 0x534cb6 leaves them at 50) and the owner's light
        // (+0xf4: the 0x812 effect); without an owner, the original dereferences the null pointer (0x534070) and
        // crashes.
        if (joint.ownerKind != Kernel::JointOwnerKind::ModelEffect || joint.ownerContext == nullptr)
            return Core::Unexpected(std::format("0x534070: falta el efecto dueño del joint {:#x}/4", joint.type));
        joint.life = 0x6e;
        joint.maximumCount = 0xc8;
        joint.light = static_cast<const Effects::ModelEffect*>(joint.ownerContext)->light;
        reach = -2.0; // 0x6ba21c
    }
    // 0x533fdc..0x53401d and 0x534090..0x5340d1: vx at rand() % 360; vy and vz, the sine and cosine of −z in radians
    // times −4 (the /0xc) or −2 (the /4).
    constexpr int FullTurn = Math::FullTurn;
    joint.velocity.x = static_cast<float>(random.Next() % FullTurn);
    const double argument = D(joint.angles.z) * NegativeRadians;
    const auto sine = Math::Detail::SineDoubleProductPC53(argument, reach);
    const auto cosine = Math::Detail::CosineDoubleProductPC53(argument, reach);
    if (!sine || !cosine)
        return Core::Unexpected(std::format("0x533ffe: el giro del joint {:#x}/{:#x} excede el perfil matemático",
                                            joint.type, joint.subtype));
    joint.velocity.y = F32(*sine);
    joint.velocity.z = F32(*cosine);
    // 0x5342be: the target is the second point. 0x534cb6: the 50-point cap.
    joint.targetPosition = request.secondPosition;
    constexpr std::int32_t MostPoints = 50;
    if (joint.maximumCount > MostPoints)
        joint.maximumCount = MostPoints;
    return {};
}

Core::Expected<void, std::string> StepHelixJoint(Kernel::JointState& joint)
{
    // 0x53b08a..0x53b10e: the turn a = (life + vx) · 0.1f and, on the x87 stack, the horizontal radius h and the
    // vertical v.
    constexpr double Tenth = Math::TenthAsDouble; // 0x6b87b8
    const double turn = (static_cast<double>(joint.life) + D(joint.velocity.x)) * Tenth;
    const float stored = F32(turn); // [ebp+8]
    std::optional<double> horizontal;
    std::optional<double> vertical;
    if (joint.subtype == WideHelixSubtype)
    {
        // 0x53b0a7..0x53b0d0: fst leaves the turn unrounded for the fcos; the fsin reads the stored one. h = cos · −26
        // and v = sin · 26 − (90 − life) · 0.3f (fsubp st(1), st).
        constexpr double Radius = 26.0;                                                 // 0x6ba2cc and 0x6b88e0
        constexpr double Sink = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc, 0.3f
        constexpr std::uint32_t Top = 0x5a;
        horizontal = Math::Detail::CosineDoubleProductPC53(turn, -Radius);
        const auto rise = Math::Detail::SineDoubleProductPC53(D(stored), Radius);
        if (rise)
        {
            const auto below = std::bit_cast<std::int32_t>(Top - std::bit_cast<std::uint32_t>(joint.life));
            vertical = *rise - static_cast<double>(below) * Sink;
        }
    }
    else
    {
        // 0x53b0d4..0x53b10e: the radius (life + 40, no less than 10, the ecx of 0x539f1e) · 0.65f; h = −(cos · radius)
        // and v = sin · radius.
        constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3f266666u)); // 0x6b8bc0, 0.65f
        constexpr std::uint32_t Growth = 0x28;
        constexpr std::int32_t Least = 0xa;
        const auto grown = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.life) + Growth);
        const double radius = static_cast<double>(grown > Least ? grown : Least) * Shrink;
        const auto across = Math::Detail::CosineDoubleProductPC53(D(stored), radius);
        if (across)
            horizontal = -*across;
        vertical = Math::Detail::SineDoubleProductPC53(D(stored), radius);
    }
    if (!horizontal || !vertical)
        return Core::Unexpected(std::format("0x53b0aa: el giro del joint {:#x}/{:#x} excede el perfil matemático",
                                            joint.type, joint.subtype));
    // 0x53b110..0x53b14c: the target advances (vy, vz) and the position is the target plus (sin f · h, cos f · h, v),
    // with f = (90 − z) in radians.
    joint.targetPosition.x = F32(D(joint.velocity.y) + D(joint.targetPosition.x));
    joint.targetPosition.y = F32(D(joint.velocity.z) + D(joint.targetPosition.y));
    const double facing = (90.0 - D(joint.angles.z)) * Radians;
    const auto x = Math::Detail::SineDoubleProductPC53(facing, *horizontal);
    const auto y = Math::Detail::CosineDoubleProductPC53(facing, *horizontal);
    if (!x || !y)
        return Core::Unexpected(std::format("0x53b137: la dirección del joint {:#x}/{:#x} excede el perfil matemático",
                                            joint.type, joint.subtype));
    joint.position = {F32(*x + D(joint.targetPosition.x)), F32(*y + D(joint.targetPosition.y)),
                      F32(*vertical + D(joint.targetPosition.z))};
    // 0x53b318..0x53b363: with less than 10 life, the light times 0.7692308f.
    constexpr std::int32_t FadeLife = 10;
    if (joint.life < FadeLife)
    {
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * Fade);
    }
    return {};
}
} // namespace Client::Joints::JointFamilies
