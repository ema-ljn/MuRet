#include "App/FaithfulPacer.h"

#include <chrono>

namespace Client::App
{
namespace
{
// El tick en el que cae `now`: la división tira lo que pasó desde el último, como GetTickCount,
// que solo cambia de tick en tick. Redondea hacia abajo también con `now` negativo (solo pasa con
// el FakeClock), donde la división entera redondearía hacia 0.
Core::Duration TickStart(Core::Duration now)
{
    auto ticks = now / FaithfulTick;
    if (now % FaithfulTick < Core::Duration::zero())
    {
        --ticks;
    }
    return ticks * FaithfulTick;
}

// LogicStep en los ms enteros del original: el SUB EAX,0x28 de 0x638167.
constexpr std::int32_t FaithfulStepMs =
    static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(LogicStep).count());
} // namespace

std::uint32_t TickCountMs(Core::Duration now)
{
    // El cast a 32 bits sin signo es la vuelta del DWORD: está definido, módulo 2^32.
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(TickStart(now));
    return static_cast<std::uint32_t>(milliseconds.count());
}

int FaithfulPacer::BeginFrame()
{
    // CMP [0x6d5ee4],0x28 en 0x637791 + JL en 0x637798: sin tope de pasos por frame.
    int steps = 0;
    while (m_accumulatedMs >= FaithfulStepMs)
    {
        m_accumulatedMs -= FaithfulStepMs;
        ++steps;
    }
    return steps;
}

void FaithfulPacer::StartMeasure(Core::IClock& clock)
{
    m_startMs = TickCountMs(clock.Now());
}

void FaithfulPacer::EndFrame(Core::IClock& clock)
{
    // La espera de 0x6385fd..0x638624: la resta es de DWORD y la comparación, con signo. El
    // original gira; MuRet duerme hasta el próximo tick, que es cuando GetTickCount cambia.
    Core::Duration now = clock.Now();
    while (static_cast<std::int32_t>(TickCountMs(now) - m_startMs) < FaithfulWaitMs)
    {
        clock.SleepFor(TickStart(now) + FaithfulTick - now);
        now = clock.Now();
    }
    m_lastMeasuredMs = static_cast<std::int32_t>(TickCountMs(now) - m_startMs);
    // El ADD de 0x638632, sin recortar: en x86 da la vuelta, así que se suma en 32 bits sin signo
    // (definido, módulo 2^32) y vuelve a int32 con signo, sin el UB de desbordar un int.
    m_accumulatedMs = static_cast<std::int32_t>(static_cast<std::uint32_t>(m_accumulatedMs) +
                                                static_cast<std::uint32_t>(m_lastMeasuredMs));
}

std::int32_t FaithfulPacer::AccumulatedMs() const
{
    return m_accumulatedMs;
}

std::int32_t FaithfulPacer::LastMeasuredMs() const
{
    return m_lastMeasuredMs;
}
} // namespace Client::App
