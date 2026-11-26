#pragma once

#include "Network/Packet.h"

#include <cstddef>
#include <optional>

namespace Client::Network
{
// What 0x4e2b10 dispatches on. A packet without a subcode is dispatched on its opcode alone.
struct PacketCode
{
    PacketType type = PacketType::C1;
    Opcode opcode{};
    std::optional<Subcode> subcode;
};

// The byte after the opcode: pkt[3] in a C1 or C3, pkt[4] in a C2 or C4 (04, "El dispatcher").
[[nodiscard]] constexpr std::size_t SubcodeOffset(PacketType type)
{
    return type == PacketType::C2 || type == PacketType::C4 ? 4 : 3;
}
} // namespace Client::Network
