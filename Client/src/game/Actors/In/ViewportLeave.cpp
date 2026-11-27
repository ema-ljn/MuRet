#include "Actors/In/ViewportLeave.h"

#include "Actors/ActorCreation.h"
#include "Actors/ViewportHooks.h"
#include "Protocol/Actors/Viewport.h"

#include <cstdint>

namespace Client::Actors::In
{
void ApplyViewportLeave(const Protocol::Actors::Leave& leave, Kernel::WorldState& world,
                        const IViewportLeaveHooks& hooks)
{
    for (const std::uint16_t word : leave.keys)
    {
        hooks.ForgetTheSwitchHolder(word);
        // 0x4ca7e1..0x4ca7e8.
        const auto key = static_cast<std::int32_t>(word & Protocol::Actors::KeyMask);
        RemoveActor(world, key);
        hooks.RemoveTheShopTitle(key);
    }
}
} // namespace Client::Actors::In
