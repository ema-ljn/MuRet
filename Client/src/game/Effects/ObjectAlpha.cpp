#include "Effects/ObjectAlpha.h"

#include <bit>
#include <cstdint>
#include <limits>

namespace Client::Effects
{
namespace
{
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::digits == 24);
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53);

// Exact f32 data of 0x4e9980's closed ordinary contract (0x6b8a48 and 0x6b87b8).
constexpr float AlphaStep = std::bit_cast<float>(0x3d4ccccdu);
constexpr float AlphaRate = std::bit_cast<float>(0x3dcccccdu);

void InterpolateAlpha(double target, float& current)
{
    // The double stores force R53 per operation even with higher-precision registers; splitting them also prevents
    // contracting product+sum. They are not intermediate f32 stores.
    volatile double difference = target - static_cast<double>(current);
    volatile double scaled = difference * static_cast<double>(AlphaRate);
    volatile double result = scaled + static_cast<double>(current);
    current = static_cast<float>(result);
}

void StepAlpha(double target, const float& targetView, float& current)
{
    if (target > static_cast<double>(current))
    {
        volatile double result = static_cast<double>(current) + static_cast<double>(AlphaStep);
        current = static_cast<float>(result);
        // FST does not replace the retained result: R53 is compared, not the stored f32.
        if (result > 1.0)
        {
            current = 1.0f;
        }
        return;
    }
    // The second comparison includes unordered on the descent and rereads target/current.
    if (!(static_cast<double>(targetView) >= static_cast<double>(current)))
    {
        volatile double result = static_cast<double>(current) - static_cast<double>(AlphaStep);
        current = static_cast<float>(result);
        if (!(result >= 0.0))
        {
            current = 0.0f;
        }
    }
}
} // namespace

void StepObjectAlpha(const float& target, float& current, float& ceiling, std::uint8_t mode)
{
    const double loadedTarget = static_cast<double>(target);
    if (mode == 0)
    {
        InterpolateAlpha(loadedTarget, current);
    }
    else
    {
        StepAlpha(loadedTarget, target, current);
    }
    // Only the ordered greater copies; equality (even ±0) and NaN keep ceiling's bits.
    if (ceiling > current)
    {
        ceiling = current;
    }
}
} // namespace Client::Effects
