#include "Kernel/FaithfulPacer.h"

#include "Kernel/LoopTiming.h"
#include "Core/IClock.h"

#include <chrono>
#include <cstdint>
#include <ratio>

namespace Client::Kernel
{
namespace
{
// A GetTickCount tick as a chrono unit: 1/64 s, FaithfulTick's 15.625 ms.
using Tick = std::chrono::duration<std::int64_t, std::ratio<1, 64>>;
static_assert(Tick{1} == FaithfulTick);

// The tick `now` falls in: drops what passed since the last one, like GetTickCount, which only changes tick by tick.
// floor rounds down also with a negative `now`, which no IClock gives (not even the FakeClock goes below 0) and only
// comes from calling TickCountMs directly, like its test.
Core::Duration TickStart(Core::Duration now)
{
    return std::chrono::floor<Tick>(now);
}

// LogicStep in the original's whole ms: the SUB EAX,0x28 of 0x638167.
constexpr std::int32_t FaithfulStepMs =
    static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(LogicStep).count());
} // namespace

std::uint32_t TickCountMs(Core::Duration now)
{
    // floor and not duration_cast, which truncates toward 0: with a negative `now` (TickStart says when) tick -1 reads
    // -16 and not -15, and the grid follows the same count, floor(tick × 15.625), on both sides of 0. The cast to
    // unsigned 32 bits is the DWORD wrap: it is defined, modulo 2^32.
    const auto milliseconds = std::chrono::floor<std::chrono::milliseconds>(TickStart(now));
    return static_cast<std::uint32_t>(milliseconds.count());
}

int FaithfulPacer::BeginFrame()
{
    // CMP [0x6d5ee4],0x28 at 0x637791 + JL at 0x637798: no cap on steps per frame.
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
    // The wait of 0x6385fd..0x638624: the subtraction is DWORD and the comparison, signed. The original spins; MuRet
    // sleeps until the next tick, which is when GetTickCount changes.
    Core::Duration now = clock.Now();
    while (static_cast<std::int32_t>(TickCountMs(now) - m_startMs) < FaithfulWaitMs)
    {
        clock.SleepFor(TickStart(now) + FaithfulTick - now);
        now = clock.Now();
    }
    m_lastMeasuredMs = static_cast<std::int32_t>(TickCountMs(now) - m_startMs);
    // The ADD of 0x638632, without clamping: on x86 it wraps, so it adds in unsigned 32 bits (defined, modulo 2^32) and
    // goes back to signed int32, without the UB of overflowing an int.
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
} // namespace Client::Kernel
