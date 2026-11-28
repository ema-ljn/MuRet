#pragma once

#include <cstdint>

// Its own header because the loop, the command line and the config (CL3.4) read it, and none of them needs the rest
// of the loop.
namespace Client::Kernel
{
enum class PacingMode : std::uint8_t
{
    // MuRet's: the whole frame goes to the accumulator, capped at MaxFrameTime, and the drawing is interpolated between
    // steps (D-C4).
    Decoupled,
    // The original's (0x637700): only the drawing, the swap and the 40 ms GetTickCount wait go to the accumulator, with
    // no cap and no interpolation.
    Faithful,
};
} // namespace Client::Kernel
