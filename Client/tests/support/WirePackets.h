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

// Los paquetes del cable del laboratorio (WireCapture.h), cortados con el PacketReader, para los
// tests golden que miran lo que llevan. Ningún mensaje imprime un byte del cable.
namespace Client::Tests
{
// Los paquetes de `reads`, en orden, cortados con un PacketReader con `obfuscation`: un Feed por
// lectura. Si el corte falla o sobran bytes, el test falla con el número de la conexión, y vuelve
// lo que haya salido.
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

// Lo mismo, sin ofuscación.
inline std::vector<Network::Packet> CutPackets(const std::vector<std::span<const std::byte>>& reads,
                                               std::size_t connection)
{
    return CutPackets(reads, Network::IdentityObfuscation::Shared(), connection);
}

// Una copia de cada lectura con cada byte con XOR `mask`: el 0xAA de FoxAnti en todo lo que manda
// el GameServer del laboratorio, deshecho a mano hasta que lo haga el lector (CL2.4).
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
