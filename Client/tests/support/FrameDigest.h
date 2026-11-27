#pragma once

#include "support/RecordingFrameTarget.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

// What the behavior golden (tests/characterization) keeps of each frame. Every field is an FNV-1a of values, never of
// pointers or addresses, so the same run gives the same digest on every machine.
namespace Client::Tests
{
struct FrameDigest
{
    // Every event the frame recorded, in order.
    std::uint64_t draw = 0;
    // The bytes the client sent during the frame, as they left the socket.
    std::uint64_t sent = 0;
    // The CRT rand() state after the frame (CrtRandom::State).
    std::uint32_t randState = 0;
    // A small summary of the world: the map, the living actors' key, type, position and action, the own character's
    // life, mana and zen, the open panels and the chat lines.
    std::uint64_t state = 0;

    friend bool operator==(const FrameDigest&, const FrameDigest&) = default;
};

FrameDigest DigestOf(const RecordedFrame& frame, std::span<const std::byte> sentThisFrame, std::uint32_t randState,
                     const Kernel::WorldState& world);
} // namespace Client::Tests
