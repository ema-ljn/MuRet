#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// The queue of the last packets: ten 0x100-byte slots at 0x77f27b4, their sizes at 0x77e1bb0 and the count, a signed
// char, at 0x77f3ba0. The name comes from the 0x12, but the queue is not only its own. The dispatcher 0x4e2b10 writes
// it with the same sequence before five handlers, without looking at the encryption flag or at whether the packet is
// read: the 0x0B (0x4e3594..0x4e361c, before its case in the same dispatcher, 0x4e3621..0x4e365e, which calls
// 0x4455c0), the 0x12 (0x4e372e..0x4e37b8, before 0x4c8860), the 0x1C (0x4e3e98..0x4e3f1d, before 0x4c6e10), the 0x24
// (0x4e38ca..0x4e394f, before 0x4d1400) and the 0x2A (0x4e3ace..0x4e3b53, before 0x4d3cc0); in MuRet, the session with
// ISessionEventSink::CopyToPacketRing. And 0x4c8860 writes it again with each state 1 record of the 0x12
// (0x4c8a43..0x4c8ad4, Q-C801). Its only reader is 0x557830, a hex dump nobody calls, and 0x4c12a0, another copy of the
// store, has no callers either (*static*): nothing reads it. In the .bss: all 0, and nobody clears it.
namespace Client::Actors
{
inline constexpr std::size_t ViewportPacketSlots = 10;
inline constexpr std::size_t ViewportPacketSlotSize = 0x100;

struct ViewportPacketRing
{
    std::int8_t count = 0;
    std::array<std::int32_t, ViewportPacketSlots> sizes{};
    std::array<std::array<std::byte, ViewportPacketSlotSize>, ViewportPacketSlots> slots{};
};

// Storing in the queue: with the count over 9, it sets it to 9 and shifts slots 1..9 to 0..8 with their sizes (copying
// `sizes[i]` bytes); then, if the packet is up to 0x100 long (signed), it copies it into the count's slot, stores its
// size and adds 1. A longer packet does not go in, but the shift has already happened.
void StoreViewportPacket(ViewportPacketRing& ring, std::span<const std::byte> packet);
} // namespace Client::Actors
