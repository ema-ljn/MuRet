#pragma once

#include "Network/PacketCode.h"

#include <cstddef>
#include <cstdint>

namespace Client::Network
{
// A packet the client reads: its code, the bytes the original's reader needs, header included, and that reader.
struct PacketLayout
{
    PacketCode code;
    std::size_t minimumSize = 0;
    std::uint32_t reader = 0;
};
} // namespace Client::Network
