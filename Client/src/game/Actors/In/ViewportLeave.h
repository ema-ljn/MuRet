#pragma once

namespace Client::Protocol::Actors
{
struct Leave;
} // namespace Client::Protocol::Actors

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Actors
{
class IViewportLeaveHooks;
} // namespace Client::Actors

// The other players and monsters leave the viewport: the 0x14. Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Los otros jugadores en el viewport".
namespace Client::Actors::In
{
// 0x4ca780: for each key, with the siege information (0x5767924), the full key 0x13d sets its bytes 4..6 to 0 and any
// other key bytes 0x1f..0x21 (Q-C802); 0x510ff0 (RemoveActor) with the key without bit 15; and 0x451630 looks up the
// personal shop sign of that key in the map at 0x7245c8 (the first in its order) and 0x451430 removes it. What other
// modules do, through `hooks`.
void ApplyViewportLeave(const Protocol::Actors::Leave& leave, Kernel::WorldState& world,
                        const IViewportLeaveHooks& hooks);
} // namespace Client::Actors::In
