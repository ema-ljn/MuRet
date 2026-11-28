#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/ext/vector_float3.hpp>

// The globals the event packets write (CL9 milestone 11, 04 § Los eventos) that belong neither to the [0x723c60] object
// (EventMapObject) nor to Crywolf (CrywolfGlobals). They start like the original's .data and .bss.
namespace Client::Events
{
// 0x6c9dd8, 0x6c9dd4, 0x6c9ddc and 0x6c9de0 (.data −1, −1, −1 and −1.0f; 0x5ff910 resets them): the object collapse
// that 0x5ff930(map, type, count, delay) requests and 0x5ff960 performs on each live object of the step (0x606560),
// only with the three integers at 0 or more and the same current map. 0x606560 subtracts one from the count at the
// end of each step, also at −1.
struct ObjectCollapse
{
    std::int32_t map = -1;
    std::int32_t type = -1;
    std::int32_t counter = -1;
    float delay = -1.0f;
};

// 0x57678d0..0x57678e2 and 0x57678b4, 0x57678b8, 0x576796f (.bss 0): what F3 23 (0x4d9f00) leaves, the Battle Soccer
// scoreboard read by 0x5a7640 (the guild war, milestone 8) and 0x50ba30.
struct SoccerScoreGlobals
{
    // 0x57678d0: the packet's 8 bytes and a '\0' in the ninth (0x4d9f34).
    std::array<char, 9> firstName{};
    // 0x57678d9: same (0x4d9f7e).
    std::array<char, 9> secondName{};
    // 0x57678b4 and 0x57678b8, from pkt[0xc] and pkt[0x15].
    // The scoreboards 0x57678b4/8 are GuildState::ourScore/theirScore, shared with the war.
    // 0x576796f: pkt[0xc] other than 0xff.
    bool active = false;
};

// 0x57678ec..0x5767905: the F3 24 result (0x4deef0), shown by 0x5a800e and cleared by 0xa7.
struct GuildWarOutcomeState
{
    std::uint8_t present = 0;
    std::array<char, 10> firstName{};
    std::array<char, 10> secondName{};
    std::uint16_t firstScore = 0;
    std::uint16_t secondScore = 0;
};

// The camera tour (CCameraMove, the 0x6e57a8 object in [0x6e5904]) that 0x403250 builds among the static
// initializers, with the values of 0x403300 (which also turn the selection around, 0x4c26f2, and stage 3). The fields
// follow the object's order (Events/Crywolf/CameraTour.h).
struct CameraTour
{
    // +0x4: where the tour camera is (0x4034e0), the one the game camera looks at while the tour runs (0x4034b0, from
    // 0x635ead).
    glm::vec3 position{};
    // +0x10: where the leg starts: the hero in the first (0x403430) and then the previous one's target (0x40357c).
    glm::vec3 start{};
    // +0x1c: where it goes (0x403d90, the cell's center: · 100 + 50).
    glm::vec3 target{};
    // +0x28: the heading, target − start, which 0x4034e0 normalizes (0x5fc800) every step.
    glm::vec3 direction{};
    // +0x34 (1): the new leg, which 0x403430 measures and 0x4034e0 clears.
    bool begin = true;
    // +0x35: the camera on its way (0x403d90); meanwhile the script does not run.
    bool moving = false;
    // +0x36 (1): arrived; the stage script runs with it and clears it when done (and 0x403d90 moves to the next leg).
    bool arrived = true;
    // +0x37 (1), +0x148 and +0x140: the clock of 0x404c90 (starts again on the call after the one that fires).
    // +0x140 is also the stage script's step: 0x404c90 advances it when it fires, 0x4039f0 and 0x403900 with their flag
    // and the script when a leg ends; 0x403d90 resets it to 0.
    bool timerRestart = true;
    // +0x38: per script character (key − 10000), whether it still turns toward its cell (0x403760); 0x403300 sets them
    // to 1.
    std::array<bool, 50> turning = []
    {
        std::array<bool, 50> all{};
        all.fill(true);
        return all;
    }();
    // +0x6a (1): the wait before starting, which 0x4033f0 clears after 5000 ms of timeGetTime with stage 2.
    bool waiting = true;
    // +0x6c: how far the camera has gone in the leg, by +0x74 per step; +0x70: the leg's length.
    float travelled = 0.0f;
    float distance = 0.0f;
    // +0x74 (100): the distance per step, from 0x403d90.
    float speed = 100.0f;
    // +0x78: per script character (key − 10000), how many times 0x403900 changed its action.
    std::array<std::int32_t, 50> actionCounts{};
    std::int32_t step = 0;
    // +0x144: the script stage (0x4035f0: 0 to 5), which 0x403d90 increments.
    std::int32_t scriptStage = 0;
    std::uint32_t timerStart = 0;
    // +0x14c (10000): the key of the next script character; 0x403d60 deletes from 10000 to this − 1 (0x510ff0).
    std::int32_t nextActorKey = 10000;
    // +0x150: what 0x403d40 adds up to 4 (stage 1 of the script); the game camera copies it to 0x7d3a0ec (0x635ed8).
    std::int32_t level = 0;
    // +0x154: the Crywolf stage that BD 00 leaves with 0x4033b0 (pkt[5]) after 0x42fe80.
    std::int32_t stage = 0;
    // +0x158: the tour cut short (Esc, 0x623edc) or finished (0x404bc6).
    bool skipped = false;
};

struct EventGlobals
{
    ObjectCollapse objectCollapse{};
    SoccerScoreGlobals soccer{};
    GuildWarOutcomeState guildWarOutcome{};
    // 0x5767814 and 0x5767816 (.bss 0): F3 22 (0x4e31f8), read by 0x5a7eb0.
    std::uint8_t timeKind = 0;
    std::uint16_t timeValue = 0;
    // 0x6c9328 (dword) and 0x6c932c (word), .data all 0xff: the three Muto numbers of 94 with type 0 and of 96
    // (0x4dd8f0, 0x4e4a22), read by the window of 0x78abb00.
    std::array<std::uint16_t, 3> mutoNumbers{0xffff, 0xffff, 0xffff};
    // 0x78ab078 (.bss 0): the 0x40 bytes of 9D (0x4e4ab9), which 0x94 with type 2 empties (0x4dd8f5).
    std::array<std::byte, 0x40> serial{};
    // 0x77f3b6b (.bss 0): the serial number entry, set by 94 with type 2 (0x4dd919) and cleared by SendMove (0x56b72a)
    // and its window's close button (0x5e74b6); 94 with another type leaves it as it is. Read by the WndProc's Enter
    // (0x4b5616 and 0x4b8888) and, with 0x6c7b8c at 0, its keys (Events/EventChipWindow.h).
    bool serialEntry = false;
    // The camera tour ([0x6e5904], Events/Crywolf/CameraTour.h).
    CameraTour cameraTour{};
    // 0x78ab9cc (.bss 0): the levels of Devil Square's six squares, {minimum, maximum}, written by its window's drawing
    // (0x5f5a3f, with the Magic Gladiator and Dark Lord table by the own record's class) and read by its step when one
    // is chosen (0x5e5a8b, Q-C1336).
    std::array<std::array<std::int32_t, 2>, 6> devilSquareLevels{};
};
} // namespace Client::Events
