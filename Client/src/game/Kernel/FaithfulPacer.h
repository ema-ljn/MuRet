#pragma once

#include "Kernel/LoopTiming.h"
#include "Core/IClock.h"

#include <cstdint>

// The original's pacing (CL3.2, Faithful mode): the loop of 0x637700 with GetTickCount on the FaithfulTick grid. Only
// the drawing, the swap and the wait go to the accumulator; the steps and what runs before measuring are lost in
// whole ticks. Without FixedStep's rules.
namespace Client::Kernel
{
// The ms GetTickCount would read at `now`, in FaithfulTick steps: a DWORD, like the original.
[[nodiscard]] std::uint32_t TickCountMs(Core::Duration now);

// No cap and in whole ms. The accumulator starts at FaithfulInitialAccumulatorMs, like the original.
class FaithfulPacer
{
public:
    // The steps due (while the accumulator is 40 or more), like 0x637791.
    int BeginFrame();
    // Takes the start (0x63852b). Called after the steps and before drawing.
    void StartMeasure(Core::IClock& clock);
    // Sleeps tick by tick while static_cast<std::int32_t>(TickCountMs(now) - start) is below 40, and adds that
    // difference to the accumulator (0x638632). The comparison is signed, like the JGE of 0x638616: a drawing of more
    // than 24.8 days gives a negative subtraction and the wait goes on until the subtraction wraps, about 19.7 more
    // days with one of 30. That is how the original hangs, and so does MuRet.
    void EndFrame(Core::IClock& clock);
    [[nodiscard]] std::int32_t AccumulatedMs() const;
    [[nodiscard]] std::int32_t LastMeasuredMs() const;

private:
    // DAT_006d5ee4: a signed int, with no cap.
    std::int32_t m_accumulatedMs = FaithfulInitialAccumulatorMs;
    // What GetTickCount read at 0x63852b.
    std::uint32_t m_startMs = 0;
    std::int32_t m_lastMeasuredMs = 0;
};
} // namespace Client::Kernel
