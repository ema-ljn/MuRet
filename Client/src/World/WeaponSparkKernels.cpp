#include "World/WeaponSparkKernels.h"

#include "Render/Models/Cloth.h"
#include "World/Particles.h"

#include <bit>
#include <cstdint>

namespace Client::World
{
namespace
{
constexpr std::int32_t WeaponSparkLife = 30;
constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));     // 0x6b8a34.
constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f73cf3eu));      // 0x6ba264.
constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3d23d70au));    // 0x6b8aec.
constexpr double Countdown = static_cast<double>(std::bit_cast<float>(0x3a83126fu)); // 0x6b89c8.

float F32(double value)
{
    return static_cast<float>(value);
}
} // namespace

bool IsWeaponSpark(std::int32_t type, std::int32_t subtype)
{
    return type == SparkParticle && subtype == WeaponSparkSubtype;
}

void CreateWeaponSpark(Particle& particle, Render::Models::CrtRandom& random)
{
    constexpr int SideMask = 7, OtherMask = 3, ScaleSpan = 5;
    constexpr double SideCenter = 4.0; // 0x6b8a44.
    particle.field38 = std::bit_cast<std::uint32_t>(1.0f);
    particle.velocity.x = F32(static_cast<double>(random.Next() & SideMask) - SideCenter);
    particle.velocity.y = static_cast<float>(random.Next() & OtherMask);
    particle.life = WeaponSparkLife;
    particle.velocity.z = static_cast<float>(random.Next() & OtherMask);
    particle.scale = F32(static_cast<double>(random.Next() % ScaleSpan) * Tenth + 1.0);
}

void StepWeaponSpark(Particle& particle)
{
    for (int axis = 0; axis < 3; ++axis)
        particle.light[axis] = F32(static_cast<double>(particle.light[axis]) * Fade);
    particle.scale = F32(static_cast<double>(particle.scale) - Shrink);
    const double left = static_cast<double>(std::bit_cast<float>(particle.field38)) - Countdown;
    particle.field38 = std::bit_cast<std::uint32_t>(F32(left));
    if (!(left > 0.0))
        particle.alive = false;
}
} // namespace Client::World
