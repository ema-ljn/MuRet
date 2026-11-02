#pragma once

#include "Core/IClock.h"

#include <span>
#include <vector>

// El reloj falso de los tests del bucle (CL3.2): no se mueve solo, así que un frame "tarda" lo que
// el test dice, sin tiempo real.
namespace Client::Tests
{
class FakeClock final : public Core::IClock
{
public:
    // El tiempo actual, sin moverlo.
    Core::Duration Now() override;
    // Lo mueve `duration` y lo anota. Un sueño de 0 o negativo es un error del bucle (el real
    // dormiría 0): el test falla.
    void SleepFor(Core::Duration duration) override;
    // Lo mueve sin anotarlo; `duration` puede ser negativo.
    void Advance(Core::Duration duration);
    // Lo que pidió dormir cada SleepFor, en orden.
    [[nodiscard]] std::span<const Core::Duration> Sleeps() const;

private:
    Core::Duration m_now{};
    std::vector<Core::Duration> m_sleeps;
};
} // namespace Client::Tests
