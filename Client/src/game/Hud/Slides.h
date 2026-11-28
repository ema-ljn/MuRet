#pragma once

#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

// The Slide.bmd banner on the game side (CL8 milestone 3; 06 § El cartel de Slide.bmd): what reads the own record
// (0x46b0e0) and the window's 0x3eb timer, which 0x46b010 sets up on the game's first step and the WndProc handles. The
// object and its queues are in Ui/SlideBanner, in WorldRuntime::hud.slides.
namespace Client::Hud
{

// 0x46b0e0: with scene 5, the banners on (0x6c7a8c) and the normal queue off (+0x8c0 ≤ 0), the own level (+0xe, with
// an access to the own record: 3848 rand() if it closes it at 0), a text of its range (0x46b680, a rand() if the range
// has a count) and, if there is one, to the normal queue with 0x46b720(1, 0, text, 1, [+0x1218], 0xffc8dce6).
void QueueATimedSlide(Kernel::WorldState& world, Crt::CrtRandom& random, bool gameScene);

// 0x636920 → 0x46b010, on the game's first step: both queues without text and at (0, 0x1ad) (0x46a460 and 0x45a9d0),
// the 0x3eb timer with Slide.bmd's period from `now` (SetTimer, which overrides the previous one) and 0x46b0e0.
void StartTheSlides(Kernel::WorldState& world, Crt::CrtRandom& random, std::uint32_t now);

// The WM_TIMER 0x3eb, between two WinMain loops: with the timer due, the WndProc reads the focus (0x4b1a6b..0x4b1b24,
// a rand() when closing it), with focus runs 0x46b0e0 and the common tail reads the focus again (0x4b335a, another
// rand()). The timer counts the period again from `now`. MuRet always has the focus, as in StepTheWinMainLoop.
// *Inferred* (P-C80): Windows leaves WM_TIMER for when the queue is empty and does not accumulate it, so it runs once
// in the pause after the frame in which it is due, after that pause's network messages; MuRet runs it when closing the
// loop, before the next one's Poll.
void RunTheSlideTimer(Kernel::WorldState& world, Crt::CrtRandom& random, std::uint32_t now, bool gameScene);
} // namespace Client::Hud
