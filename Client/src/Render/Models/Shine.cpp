#include "Render/Models/Shine.h"

#include "Core/IClock.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>

namespace Client::Render::Models
{
namespace
{
// Los floats de 0x4ecc30: 0,0001 (0x6b8a94), 0,001 (0x6b89c8), 0,002 (0x6b89bc), 3 (0x6b86b8),
// 0,5 (0x6b882c), 0,2 (0x6b8854) y 1 (0x6b87d4). El seno y el coseno van en la x87, con el reloj y el
// factor en float: acá en double, con los mismos floats.
constexpr std::int64_t ClockPeriod = 10000;
constexpr float ClockScale = 0.0001f;
constexpr float CosineScale = 0.001f;
constexpr float SineScale = 0.002f;
constexpr float Three = 3.0f;
constexpr float Half = 0.5f;
constexpr float ShinyOffset = 0.2f;
constexpr float One = 1.0f;
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
        const auto c = static_cast<float>(std::cos(clock * static_cast<double>(CosineScale)));
        const auto s = static_cast<float>(std::sin(clock * static_cast<double>(SineScale)));
        // Como 0x4ed1f5..: la suma de las tres, su complemento menos t·3 y nz·0,5, y a la u ny·0,5 y s·3.
        const float sum = s * ny + c * nx + nz;
        const float v = (One - sum) - (t * Three + nz * Half);
        const float u = ny * Half + s * Three + sum;
        return {u, v};
    }
    }
    return {};
}
} // namespace Client::Render::Models
