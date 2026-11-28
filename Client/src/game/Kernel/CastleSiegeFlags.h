#pragma once

#include <cstdint>

namespace Client::Kernel
{
// 0x7239d8..0x7239f0: the siege's flags and Castle Siege's map (30) globals, which the characters, the monsters, the
// effects and the objects read.
struct CastleSiegeFlags
{
    bool siegeActive = false;         // 0x7239ec, apart from the map number (0x4294b0).
    bool previousSiegeActive = false; // 0x7239ed: 0x4294c0 copies it before each change.
    // 0x7239f0: when 0x42b540 chose map 30's music, timeGetTime's ms in a float. Map 30's load (0x42a795) and
    // 0x4294c0 clear it.
    float siegeMusicTime = 0.0f;
    // 0x7239d8: map 30's load clears it (0x42a78e) and 0x42d890 sets it to 2 when the 286 or 287 end action 5
    // (0x42d8f6); the 286 and 287 read and write it in 0x42da90 (0x42db7b..0x42dc0f).
    std::uint8_t castleByte7239d8 = 0;
    // 0x7239d9: map 30's 277 sets it each step (0x42db10); 0x42da60, after the characters, keeps the gates closed
    // while it is 1 and clears it.
    std::uint8_t castleGatesClosed = 0;
    // 0x7239dc and 0x7239e0: the x and y of map 30's 278 with +0x42d at 0x80 (0x42dc38), cleared by 0x42da60. Read by
    // 0x42ddb0 (0x42ddf8), the siege drawing over the characters, which only runs in the siege (0x42b00b).
    float castleMarkX = 0.0f;
    float castleMarkY = 0.0f;
    // 0x7239e4: the +0x78 of map 30's 216 (0x42daea), compared with 0x20000 by 0x42c850 (0x42c977) and 0x42de90
    // (0x42ded1, the map 30 helper of 0x5085db).
    std::uint32_t castleFlags7239e4 = 0;
};
} // namespace Client::Kernel
