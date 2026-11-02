#include "FixedStep.h"

#include <algorithm>

namespace Labs
{
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
    // nanoseconds guarda un entero de 64 bits: hasta unos 292 años, sin peligro con un frame.
    m_rest += std::max(frame, std::chrono::nanoseconds{0});
    const auto steps = m_rest / Step; // duración / duración: un número, sin unidad
    m_rest %= Step;
    return static_cast<int>(steps);
}

std::chrono::nanoseconds ChronoFixedStep::Rest() const
{
    return m_rest;
}
} // namespace Labs
