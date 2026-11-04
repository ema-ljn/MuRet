#include "Render/Models/ItemGlow.h"

#include "Render/Models/Shine.h"

#include <algorithm>
#include <cstdint>

namespace Client::Render::Models
{
namespace
{
// Los factores del color base de la tabla de 0x60b3d0.
constexpr float SevenFactor = 0.8f;
constexpr float NineFactor = 0.9f;
} // namespace

ItemGlow ItemGlowOf(std::uint8_t level, bool excellent, bool ancient, std::uint8_t detail)
{
    if (detail < DefaultGlowDetail)
    {
        level = std::min(level, static_cast<std::uint8_t>(2 * detail + 5));
    }
    ItemGlow glow;
    switch (level)
    {
    case 7:
    case 8:
        glow.baseFactor = SevenFactor;
        glow.passes = {GlowPass{ShinePass::Chrome}};
        break;
    case 9:
    case 10:
        glow.baseFactor = NineFactor;
        glow.passes = {GlowPass{ShinePass::Chrome}, GlowPass{ShinePass::Shiny}};
        break;
    case 11:
    case 12:
        glow.baseFactor = NineFactor;
        glow.passes = {GlowPass{ShinePass::Chrome2}, GlowPass{ShinePass::Shiny}, GlowPass{ShinePass::Chrome}};
        break;
    case 13:
        glow.baseFactor = NineFactor;
        glow.passes = {GlowPass{ShinePass::Chrome4}, GlowPass{ShinePass::Shiny}, GlowPass{ShinePass::Chrome}};
        break;
    default:
        break;
    }
    // 0x60b3d0 vuelve después del 0x42 (LAB_0060cb3b): el de set va solo sin excelente.
    if (excellent)
    {
        glow.passes.push_back(GlowPass{});
    }
    else if (ancient)
    {
        glow.passes.push_back(GlowPass{ShinePass::Chrome3});
    }
    return glow;
}
} // namespace Client::Render::Models
