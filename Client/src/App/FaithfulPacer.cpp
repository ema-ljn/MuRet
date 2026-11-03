#include "App/FaithfulPacer.h"

#include "App/LoopTiming.h"
#include "Core/IClock.h"

#include <chrono>
#include <cstdint>
#include <ratio>

namespace Client::App
{
namespace
{
// Un tick de GetTickCount como unidad de chrono: 1/64 s, los 15,625 ms de FaithfulTick.
using Tick = std::chrono::duration<std::int64_t, std::ratio<1, 64>>;
static_assert(Tick{1} == FaithfulTick);

// El tick en el que cae `now`: tira lo que pasó desde el último, como GetTickCount, que solo
// cambia de tick en tick. floor redondea hacia abajo también con `now` negativo, que ningún IClock
// da (ni el FakeClock baja de 0) y solo llega llamando a TickCountMs directo, como su test.
Core::Duration TickStart(Core::Duration now)
{
    return std::chrono::floor<Tick>(now);
}

// LogicStep en los ms enteros del original: el SUB EAX,0x28 de 0x638167.
constexpr std::int32_t FaithfulStepMs =
    static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(LogicStep).count());
} // namespace

std::uint32_t TickCountMs(Core::Duration now)
{
    // floor y no duration_cast, que trunca hacia 0: con `now` negativo (TickStart dice cuándo) el
    // tick -1 lee -16 y no -15, y la grilla sigue la misma cuenta, floor(tick × 15,625), a los dos
    // lados del 0. El cast a 32 bits sin signo es la vuelta del DWORD: está definido, módulo 2^32.
    const auto milliseconds = std::chrono::floor<std::chrono::milliseconds>(TickStart(now));
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
