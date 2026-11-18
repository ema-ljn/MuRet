#include "Scenes/ViewportPacketRing.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Scenes
{
void StoreViewportPacket(ViewportPacketRing& ring, std::span<const std::byte> packet)
{
    constexpr std::int8_t LastSlot = 9;
    // 0x4e3736..0x4e3773: el JLE es con signo.
    if (ring.count > LastSlot)
    {
        ring.count = LastSlot;
        for (std::size_t slot = 1; slot < ViewportPacketSlots; ++slot)
        {
            const auto size = static_cast<std::size_t>(static_cast<std::uint32_t>(ring.sizes[slot]));
            const auto& from = ring.slots[slot];
            std::copy_n(from.begin(), std::min(size, ViewportPacketSlotSize), ring.slots[slot - 1].begin());
            ring.sizes[slot - 1] = ring.sizes[slot];
        }
    }
    // 0x4e3778..0x4e377e: JG con signo contra 0x100.
    const auto size = static_cast<std::int32_t>(packet.size());
    if (size > static_cast<std::int32_t>(ViewportPacketSlotSize))
        return;
    const auto slot = static_cast<std::size_t>(ring.count);
    std::ranges::copy(packet, ring.slots[slot].begin());
    ring.sizes[slot] = size;
    ++ring.count;
}
} // namespace Client::Scenes
