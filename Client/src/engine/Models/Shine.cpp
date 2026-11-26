#include "Models/Shine.h"

#include "Core/IClock.h"
#include "Math/FloatConstants.h"
#include "Math/RoundedMath.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <chrono>
#include <cstdint>

namespace Client::Models
{
namespace
{
// The floats of 0x4ecc30: 0.0001 (0x6b8a94), 0.001 (0x6b89c8), 0.002 (0x6b89bc), 3 (0x6b86b8), 0.5 (0x6b882c), 0.2
// (0x6b8854) and 1 (0x6b87d4). The sine and cosine go on the x87, with the clock and the factor in float: here in
// double, with the same floats.
constexpr std::int64_t ClockPeriod = 10000;
// Those of 0x200 and 0x800 (06, *static*): the 5 s clock, its scale and offset, the 0.8 and the light of 0x800.
constexpr std::int64_t Chrome2Period = 5000;
constexpr float Chrome2Scale = 0.00024f;
constexpr float Chrome2Offset = 0.4f;
constexpr float Chrome2Normal = 0.8f;
constexpr float Two = 2.0f;
constexpr glm::vec3 Chrome3Light{0.0f, -0.1f, -0.8f};
constexpr float ClockScale = 0.0001f;
constexpr float CosineScale = 0.001f;
constexpr float SineScale = 0.002f;
constexpr float Three = 3.0f;
using Math::Half;
constexpr float ShinyOffset = 0.2f;
constexpr float One = 1.0f;
// Those of 0x4000 (0x4ed25e..0x4ed2b2): 2.5 (0x6b89a8) on nz in v and 5 (0x6b87f8) on s in u.
constexpr double Chrome5Normal = 2.5;
constexpr double Chrome5Sine = 5.0;
} // namespace

glm::vec2 ShineTexCoords(ShinePass pass, const glm::vec3& worldNormal, Core::Duration frameClock)
{
    std::int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(frameClock).count();
    if (ms < 0)
    {
        ms = 0;
    }
    const float t = static_cast<float>(ms % ClockPeriod) * ClockScale;
    const float nx = worldNormal.x;
    const float ny = worldNormal.y;
    const float nz = worldNormal.z;

    switch (pass)
    {
    case ShinePass::Shiny:
        return {nz * Half + ShinyOffset, (ny + One) * Half};
    case ShinePass::Chrome:
        return {nz * Half + t, t + t + ny * Half};
    case ShinePass::Chrome4:
    {
        const auto clock = static_cast<double>(ms);
        const auto c = static_cast<float>(Math::RoundedCos(clock * static_cast<double>(CosineScale)));
        const auto s = static_cast<float>(Math::RoundedSin(clock * static_cast<double>(SineScale)));
        // Like 0x4ed1f5..: the sum of the three, its complement minus t·3 and nz·0.5, and to u ny·0.5 and s·3.
        const float sum = s * ny + c * nx + nz;
        const float v = (One - sum) - (t * Three + nz * Half);
        const float u = ny * Half + s * Three + sum;
        return {u, v};
    }
    case ShinePass::Chrome2:
    {
        const float f = static_cast<float>(ms % Chrome2Period) * Chrome2Scale - Chrome2Offset;
        return {(nz + nx) * Chrome2Normal + Two * f, nx + ny + Three * f};
    }
    case ShinePass::Chrome3:
    {
        const float dot = Chrome3Light.x * nx + Chrome3Light.y * ny + Chrome3Light.z * nz;
        return {dot, One - dot};
    }
    case ShinePass::Chrome5:
    {
        const auto clock = static_cast<double>(ms);
        const auto c =
            static_cast<double>(static_cast<float>(Math::RoundedCos(clock * static_cast<double>(CosineScale))));
        const auto s =
            static_cast<double>(static_cast<float>(Math::RoundedSin(clock * static_cast<double>(SineScale))));
        // 0x4ed25e..0x4ed2b2: the sum of the three goes to u; v is its complement minus nz·2.5 and t; then u adds s·5
        // and ny·3 to the float it stored.
        const double sum = c * nx + s * ny + nz;
        const auto u = static_cast<float>(sum);
        const auto v = static_cast<float>((1.0 - sum) - (nz * Chrome5Normal + static_cast<double>(t)));
        return {static_cast<float>(s * Chrome5Sine + ny * static_cast<double>(Three) + static_cast<double>(u)), v};
    }
    }
    return {};
}
} // namespace Client::Models
