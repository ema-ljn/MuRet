#pragma once

#include "Network/IdentityObfuscation.h"
#include "Network/IObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketReader.h"

#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

// The lab's wire packets (WireCapture.h), split with the PacketReader, for the golden tests that look at what they
// carry. No message prints a wire byte.
namespace Client::Tests
{
// The packets of `reads`, in order, split with a PacketReader with `obfuscation`: one Feed per read. If the split fails
// or bytes are left over, the test fails with the connection's number, and it returns what came out.
inline std::vector<Network::Packet> CutPackets(const std::vector<std::span<const std::byte>>& reads,
                                               const Network::IObfuscation& obfuscation, std::size_t connection)
{
    Network::PacketReader reader(obfuscation);
    std::vector<Network::Packet> packets;
    for (const std::span<const std::byte> read : reads)
    {
        reader.Feed(read);
        while (true)
        {
            auto next = reader.Next();
            if (!next.HasValue())
            {
                ADD_FAILURE() << "conexión " << connection << ": el corte falla con "
                              << ::testing::PrintToString(next.Error());
                return packets;
            }
            if (!next.Value())
            {
                break;
            }
            packets.push_back(std::move(*std::move(next).Value()));
        }
    }
    EXPECT_EQ(reader.Pending(), 0u) << "conexión " << connection;
    return packets;
}

// The same, without obfuscation.
inline std::vector<Network::Packet> CutPackets(const std::vector<std::span<const std::byte>>& reads,
                                               std::size_t connection)
{
    return CutPackets(reads, Network::IdentityObfuscation::Shared(), connection);
}

// A copy of each read with each byte XORed with `mask`: FoxAnti's 0xAA on everything the lab's GameServer sends, undone
// by hand until the reader does it (CL2.4).
inline std::vector<std::vector<std::byte>> Unmasked(const std::vector<std::span<const std::byte>>& reads,
                                                    std::byte mask)
{
    std::vector<std::vector<std::byte>> unmasked;
    unmasked.reserve(reads.size());
    for (const std::span<const std::byte> read : reads)
    {
        std::vector<std::byte> copy(read.begin(), read.end());
        for (std::byte& value : copy)
        {
            value ^= mask;
        }
        unmasked.push_back(std::move(copy));
    }
    return unmasked;
}
} // namespace Client::Tests
