#include "FixedStep.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace Labs
{
namespace
{
// El frame más largo que cuenta ChronoFixedStep: INT_MAX pasos de 40 ms, 85.899.345.880 ms. Con
// el resto, que es menor que un paso, la suma no llega a INT_MAX + 1 pasos ni a los 64 bits.
constexpr std::chrono::nanoseconds MaxChronoFrame = Step * std::numeric_limits<int>::max();
} // namespace

int FixedStep::Advance(int frameMs)
{
    // m_restMs es menor que StepMs, así que la suma con el int más grande entra en 64 bits, y la
    // cantidad de pasos, que es esa suma dividida por 40, entra en un int.
    const std::int64_t total = std::int64_t{m_restMs} + std::max(frameMs, 0);
    m_restMs = static_cast<int>(total % StepMs);
    return static_cast<int>(total / StepMs);
}

int FixedStep::RestMs() const
{
    return m_restMs;
}

int ChronoFixedStep::Advance(std::chrono::nanoseconds frame)
{
    // nanoseconds llega a unos 292 años: m_rest + frame se pasaría con un frame cerca de
    // nanoseconds::max(), y la cantidad de pasos no entraría en un int pasados los 994 días. Por
    // eso el frame se recorta, como el int de FixedStep, antes de sumar.
    m_rest += std::clamp(frame, std::chrono::nanoseconds{0}, MaxChronoFrame);
    const auto steps = m_rest / Step; // duración / duración: un número, sin unidad
    m_rest %= Step;
    return static_cast<int>(steps);
}

std::chrono::nanoseconds ChronoFixedStep::Rest() const
{
    return m_rest;
}
} // namespace Labs
