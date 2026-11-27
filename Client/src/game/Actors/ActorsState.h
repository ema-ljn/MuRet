#pragma once

#include "Actors/ViewportPacketRing.h"

#include <cstdint>

// What the actors own of the original's globals.
namespace Client::Actors
{
// 0x743a180, 0x743a184 and 0x743a188 (.bss 0): the hero's cell and the world clock that 0x4fbe10 stores on maps 18 to
// 23. Nothing else writes them: they survive another map's load.
struct ReportedCell
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    float time = 0.0f;
};

struct ActorsState
{
    // 0x77f3ba0, 0x77e1bb0 and 0x77f27b4: the queue of the last 0x0B, 0x12, 0x1C, 0x24 and 0x2A.
    ViewportPacketRing viewportPackets{};
    ReportedCell reportedCell{};
};
} // namespace Client::Actors
