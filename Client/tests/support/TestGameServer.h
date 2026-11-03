#pragma once

#include "Network/Connection.h"
#include "Network/Crypto/SimpleModulus.h"
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

// Lo que hace con los C3/C4 el GameServer de los tests de la Connection y de la ServerSession
// (CL4.4), con las claves sintéticas de SimpleModulusKeys.h: lo que manda el cliente va con
// Pair17 y lo que contesta el servidor con Pair18, así una clave cambiada de lugar no descifra.
// Los tests no comparan lo cifrado: el servidor descifra lo que salió y el test mira el C1/C2.
namespace Client::Tests
{
// Las claves de la conexión del cliente con el GameServer: cifra con la de ida y descifra con la
// de vuelta.
inline Network::ConnectionKeys GameClientKeys()
{
    return {Pair17().encryption, Pair18().decryption};
}

// La clave con la que el GameServer descifra lo que le manda el cliente.
inline Network::Crypto::DecryptionKey GameServerDecryption()
{
    return Pair17().decryption;
}

// La clave con la que el GameServer cifra lo que contesta.
inline Network::Crypto::EncryptionKey GameServerEncryption()
{
    return Pair18().encryption;
}

// Un C1/C2 como lo manda el GameServer: cifrado con su clave y sus seriales.
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

// Lo que recibe el GameServer de lo que mandó el cliente.
struct GameServerReceived
{
    std::vector<Network::Packet> packets;      // el C1/C2 rearmado de cada C3/C4, o el que vino así
    std::vector<Network::SerialCheck> serials; // el de cada C3/C4, en orden
};

// `sent` cortado y, cada C3/C4, descifrado con la clave del GameServer y sus seriales. Un corte
// que falla, lo que sobra o un C3/C4 que no descifra hacen fallar el test.
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
