#include "World/ObjectAlpha.h"

#include <bit>
#include <cstdint>
#include <limits>

namespace Client::World
{
namespace
{
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::digits == 24);
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53);

// Datos f32 exactos del contrato ordinario cerrado de 0x4e9980 (0x6b8a48 y 0x6b87b8).
constexpr float AlphaStep = std::bit_cast<float>(0x3d4ccccdu);
constexpr float AlphaRate = std::bit_cast<float>(0x3dcccccdu);

void InterpolateAlpha(double target, float& current)
{
    // Los stores double fuerzan R53 por operación incluso con registros de mayor precisión;
    // separarlos también impide contraer producto+suma. No son stores f32 intermedios.
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
        // FST no reemplaza el resultado retenido: se compara R53, no el f32 almacenado.
        if (result > 1.0)
        {
            current = 1.0f;
        }
        return;
    }
    // La segunda comparación incluye unordered en el descenso y relee target/current.
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
    // Sólo el mayor ordenado copia; igualdad (incluso ±0) y NaN preservan los bits de ceiling.
    if (ceiling > current)
    {
        ceiling = current;
    }
}
} // namespace Client::World
