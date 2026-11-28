#pragma once

#include "Core/IClock.h"

#include <chrono>
#include <cstdint>

// The loop constants (CL3.2): the logic step, the frame cap and the faithful mode pacing. They come from
// 02 § El ritmo and from the table "Lo que dice el binario del bucle" of the CL3 plan.
namespace Client::Kernel
{
// 02 § El ritmo: CMP [0x6d5ee4],0x28 at 0x637791, SUB EAX,0x28 at 0x638167.
inline constexpr Core::Duration LogicStep = std::chrono::milliseconds{40};
// Question 2: a longer frame counts as this one and the rest is lost. It is not like MuMain (ADR 0001): there, below
// 25 FPS, the frame factor is capped at 1 step and the game goes in slow motion (ZzzAI.cpp:678); MuRet catches up
// with up to 25 steps per frame.
inline constexpr Core::Duration MaxFrameTime = std::chrono::seconds{1};
// GetTickCount's tick in the XP VM: 15.625 ms, 64 per second (02 § El ritmo).
inline constexpr Core::Duration FaithfulTick = std::chrono::microseconds{15'625};
// The wait of 0x6385fd..0x638624 (CMP ...,0x28 at 0x63860f + JGE at 0x638616), in GetTickCount ms.
inline constexpr std::int32_t FaithfulWaitMs = 40;
// The original's initial accumulator value: the .data at 0x6d5ee4 holds 0x28 (CL3.R).
inline constexpr std::int32_t FaithfulInitialAccumulatorMs = 40;
// The FPS caps that [Video] MaxFps and --max-fps accept, besides 0 (no cap). They are not from the original, which
// has no cap because it always waits 46.875 ms. The lowest is one frame per logic step (LogicStep), 25 per second:
// with fewer, some frames would run two steps (at 24, one in 24), and with 1 each frame would last exactly
// MaxFrameTime, which any sleep delay clamps (MuMain's menu starts at 24, OptionWindow.cpp). The highest is 1000:
// with more, a frame would last under 1 ms and the cap would no longer slow down anything noticeable.
inline constexpr int MinFpsLimit = static_cast<int>(std::chrono::seconds{1} / LogicStep);
static_assert(MinFpsLimit == 25);
inline constexpr int MaxFpsLimit = 1000;

// The rule for [Video] MaxFps and --max-fps, in one place: 0 (no cap), or MinFpsLimit to MaxFpsLimit.
[[nodiscard]] constexpr bool IsValidMaxFps(int maxFps)
{
    return maxFps == 0 || (maxFps >= MinFpsLimit && maxFps <= MaxFpsLimit);
}
} // namespace Client::Kernel
