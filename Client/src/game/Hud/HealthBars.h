#pragma once

#include "Core/Expected.h"
#include "Kernel/WorldRuntime.h"

#include <deque>
#include <string>

// The health bars over the 0x1a8 (CL9 milestone 2): the characters' step queues them (0x42a2b0, from 0x5053fc) and the
// HUD draws them (0x42a4d0, the 5th of 0x5a7320, at 0x5a7344). Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Las barras de vida del 0x1a8".
namespace Client::Render
{
class SpriteBatch;
}

namespace Client::Kernel
{
struct ActorState;
struct GameProjection;
} // namespace Client::Kernel

namespace Client::Hud
{

// 0x42a2b0(o): with type 0x1a8 (the 278 of 0x42d4a1) and +0x93 below 5 unsigned, queues (push_back) the position with
// z at +0x18 + +0x138 + 100 (0x6b86b4), on the x87 with the FSTP to float, and +0x93. With another, nothing.
void QueueTheHealthBar(const Kernel::ActorState& actor, std::deque<Kernel::QueuedHealthBar>& queue);

// 0x42a4d0: for i from 0 while i is less than what is left in the queue, the first: 0x6205b0 with its point, the bar
// of 0x5a6af0(x − 25, y + 12, 50, 2, b · 10, 0, 1), as float from the integer, and pop_front. Since the count drops
// with each one, it draws half rounded up; the rest stays for the next frame, ahead of the new ones. Without
// `projection`, a queue with something stops.
[[nodiscard]] Core::Expected<void, std::string> DrawTheHealthBars(std::deque<Kernel::QueuedHealthBar>& queue,
                                                                  const Kernel::GameProjection* projection,
                                                                  Render::SpriteBatch& sprites);
} // namespace Client::Hud
