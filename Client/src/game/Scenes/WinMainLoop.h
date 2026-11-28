#pragma once

#include "Core/IClock.h"
#include "Models/Animation.h"

#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
struct ActorState;
struct WorldRuntime;
class WorldState;
} // namespace Client::Kernel

// WinMain's turn rand()s (P-C61, Q-C457, Q-C461), which belong to no scene: each turn without pending messages opens
// and closes the 4-byte protected counter 0x5763578 and the focus byte 0x5763580 before the frame (0x63945d), and the
// frame, after the scene (0x637700), opens and closes the counter again. With the window minimized the focus is at 0
// and the turn takes another branch (SetForegroundWindow, 0x576357c and ShowWindow), which MuRet does not model; nor
// the focus rand() the WndProc takes per message.
namespace Client::Scenes
{
// 0x5763578 and 0x5763574, in the .bss.
struct WinMainCounters
{
    // The turns: the counter that rises on each one (0x4bbd35).
    std::int32_t loops = 0;
    // The rounds of 31 turns (0x4bbec6): with the window not minimized it goes back to 0 in the same turn.
    std::int32_t rounds = 0;
};

// What lasts from WinMain's start to its end.
struct WinMainLoopState
{
    WinMainCounters counters;
    Models::OriginalFrameCarry frames;
};

// 0x4bbc57..0x4bdf6d and 0x4bdf76..0x4be02f: the counter and the focus, before the frame. 5 rand(); on turn 31, with
// the window not minimized, 21, and both counters at 0.
void StepTheWinMainLoop(WinMainCounters& counters, Crt::CrtRandom& random);

// 0x63950d..0x63b066: 0x63945d opens and closes the counter after 0x637700 (4 rand()). Its branch above 31
// (0x6395e7) does not run: WinMain resets it to 0 on turn 31.
void CloseTheFrameCounter(Crt::CrtRandom& random);

// 0x638ae4..0x6391e4: in Devias, each comparison of the rectangle opens and closes the cell on its own.
[[nodiscard]] bool InTheDeviasChurch(Kernel::ActorState& hero, Crt::CrtRandom& random);

// 0x63869c..0x6388a8: the world scene's ambience and stops precede the music selection.
void StepWorldAmbience(Kernel::WorldRuntime& runtime, Crt::CrtRandom& random);

// The end of a loop turn. 0x63b0b7: the tail of 0x63945d switches off the dialogs' Enter (0x5763588). Then, once per
// frame of the original in this turn: in the game scene the ambience, the map's music and Crywolf's (0x430250); the
// frame counter of 0x63945d; the Slide banner's WM_TIMER 0x3eb if it is due (P-C80; once, with the loop's time in ms
// as timeGetTime); and the counters of the next turn (P-C61).
void FinishTheWinMainLoop(WinMainLoopState& loop, Kernel::WorldState& world, Crt::CrtRandom& random, Core::Duration now,
                          Models::OriginalFramePace pace, bool gameScene);
} // namespace Client::Scenes
