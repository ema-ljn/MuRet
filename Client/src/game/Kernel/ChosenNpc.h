#pragma once

#include <cstdint>

namespace Client::Kernel
{
// 0x6c8e54 (.data −1): the NPC the arrival talks to (0x5650c0 with +0x379 at 2). The click on the chosen NPC writes it
// (0x59db1f); the arrival (0x56959b) and the return to the selection (0x42fdf8) set it back to −1. 0x4eac00 reads it
// for Crywolf's altar.
struct ChosenNpc
{
    std::int32_t index = -1;
    // 0x7798bf0 and 0x77f36cc (.bss): its type (+2, MOVSX) and its z angle (+0x24, the raw dword), which the same click
    // writes (0x59db25 and 0x59db2b). Another case of 0x5650c0 reads them (0x569859.., not ported).
    std::int32_t type = 0;
    std::uint32_t angle = 0;
};
} // namespace Client::Kernel
