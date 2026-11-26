#pragma once

#include "Network/Connection.h"
#include "Crypto/SimpleModulus.h"
#include "Network/EncryptedPacket.h"
#include "Network/Packet.h"
#include "Network/PacketReader.h"
#include "Network/PacketSerials.h"

#include "support/Packets.h"
#include "support/SimpleModulusKeys.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// What the GameServer of the Connection, ServerSession and FakeServers tests does with C3/C4, with
// SimpleModulusKeys.h's synthetic keys: what the client sends goes with Pair17 and what the server answers with
// Pair18, so a swapped key does not decrypt. The tests do not compare the encrypted bytes: the server decrypts what
// went out and the test looks at the C1/C2.
namespace Client::Tests
{
// The keys of the client's connection with the GameServer: it encrypts with the outgoing one and decrypts with the
// incoming one.
inline Network::ConnectionKeys GameClientKeys()
{
    return {Pair17().encryption, Pair18().decryption};
}

// Decrypts what the client sends.
inline Crypto::DecryptionKey GameServerDecryption()
{
    return Pair17().decryption;
}

// Encrypts what the GameServer answers.
inline Crypto::EncryptionKey GameServerEncryption()
{
    return Pair18().encryption;
}

// Both on the GameServer's side, like a connection's: they encrypt what it answers and decrypt what it receives (the
// ones of support/FakeServers.h).
inline Network::ConnectionKeys GameServerKeys()
{
    return {GameServerEncryption(), GameServerDecryption()};
}

// A C1/C2 as the GameServer sends it: encrypted with its key and serials.
inline std::vector<std::byte> FromTheGameServer(const std::vector<std::byte>& plain, Network::PacketSerials& serials)
{
    const auto encrypted = Network::EncryptPacket(PacketOf(plain), GameServerEncryption(), serials);
    EXPECT_TRUE(encrypted.HasValue());
    if (!encrypted.HasValue())
    {
        return {};
    }
    const std::span<const std::byte> bytes = encrypted.Value().Bytes();
    return {bytes.begin(), bytes.end()};
}

// What the GameServer receives from what the client sent.
struct GameServerReceived
{
    std::vector<Network::Packet> packets;      // the C1/C2 rebuilt from each C3/C4, or the one that came as is
    std::vector<Network::SerialCheck> serials; // each C3/C4's, in order
};

// `sent` split and each C3/C4 decrypted with the GameServer's key and serials. A failed split, leftover bytes or a
// C3/C4 that does not decrypt fail the test.
inline GameServerReceived AsTheGameServerReceives(std::span<const std::byte> sent, Network::PacketSerials& serials)
{
    GameServerReceived received;
    Network::PacketReader reader;
    reader.Feed(sent);
    while (true)
    {
        auto next = reader.Next();
        EXPECT_TRUE(next.HasValue());
        if (!next.HasValue())
        {
            break;
        }
        std::optional<Network::Packet> cut = std::move(next).Value();
        if (!cut)
        {
            break;
        }
        Network::Packet packet = std::move(*cut);
        if (packet.Type() == Network::PacketType::C1 || packet.Type() == Network::PacketType::C2)
        {
            received.packets.push_back(std::move(packet));
            continue;
        }
        auto decrypted = Network::DecryptPacket(packet, GameServerDecryption(), serials);
        EXPECT_TRUE(decrypted.HasValue());
        if (!decrypted.HasValue())
        {
            break;
        }
        received.serials.push_back(decrypted.Value().serial);
        received.packets.push_back(std::move(decrypted).Value().packet);
    }
    EXPECT_EQ(reader.Pending(), 0u);
    return received;
}
} // namespace Client::Tests
