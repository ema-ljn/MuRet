#include "Network/Connection.h"

#include "Crypto/SimpleModulus.h"
#include "Network/EncryptedPacket.h"
#include "Network/IdentityObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"

#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"
#include "support/WirePackets.h"
#include "test_golden_connection_support.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// Client::Network::Connection contra el cable del laboratorio (support/WireCapture.h,
// MU_CLIENT_WIRE_DIR) y las claves reales del cliente, Enc1.dat y Dec2.dat (el plan de la CL4,
// CL4.4, PR A). Lo que contestó el GameServer, leído por un FakeSocket con las lecturas del cable,
// sale descifrado como lo dan DecryptPacket y el BeforeDispatch de FoxAnti. Lo que mandó el
// cliente original, descifrado con Dec1.dat, la del servidor, y mandado otra vez Encrypted, sale
// con los bytes del cable salvo el último bloque, que lleva el relleno. Cada test usa una sola
// Connection y la abre de nuevo para cada conexión del log: los seriales vuelven a 0 en cada Open.
//
// El cable lleva el login del jugador: estos tests cuentan paquetes y comparan con
// SameBytesWithoutValues. Ningún mensaje imprime un byte del cable ni de lo descifrado, ni un
// aviso del log, que dice el opcode.
namespace
{
using Client::Network::Connection;
using Client::Network::DecryptPacket;
using Client::Network::Event;
using Client::Network::IdentityObfuscation;
using Client::Network::Packet;
using Client::Network::PacketSerials;
using Client::Network::SerialCheck;
using Client::Network::Crypto::CipherBlockSize;
using Client::Network::Crypto::DecryptionKey;
using Client::Tests::CutPackets;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::WireDirection;
using Client::Tests::GoldenConnectionSupport::GameServerPort;
using Client::Tests::GoldenConnectionSupport::GoldenConnection;
using Client::Tests::GoldenConnectionSupport::Keys;
using Client::Tests::GoldenConnectionSupport::PaddingSeed;
using Client::Tests::GoldenConnectionSupport::Resent;
using Client::Tests::GoldenConnectionSupport::Where;

// Cada `mine` contra su `wire`: los mismos bytes salvo el último bloque, el del relleno, y,
// descifrado con `server`, el mismo `plain`, con los seriales en orden desde 0.
void ExpectLikeTheWire(const Resent& resent, const DecryptionKey& server, std::size_t index)
{
    ASSERT_EQ(resent.mine.size(), resent.wire.size()) << "conexión " << index;
    PacketSerials serverSerials;
    for (std::size_t packet = 0; packet < resent.wire.size(); ++packet)
    {
        const std::span<const std::byte> mine = resent.mine[packet].Bytes();
        const std::span<const std::byte> wire = resent.wire[packet].Bytes();
        ASSERT_EQ(mine.size(), wire.size()) << Where(index, packet);
        const std::size_t beforeLast = wire.size() - CipherBlockSize;
        EXPECT_TRUE(SameBytesWithoutValues(mine.first(beforeLast), wire.first(beforeLast))) << Where(index, packet);
        const auto again = DecryptPacket(resent.mine[packet], server, serverSerials);
        ASSERT_TRUE(again.HasValue()) << Where(index, packet);
        EXPECT_EQ(again.Value().serial, SerialCheck::InOrder) << Where(index, packet);
        EXPECT_TRUE(SameBytesWithoutValues(again.Value().packet.Bytes(), resent.plain[packet].Bytes()))
            << Where(index, packet);
    }
}

// Los 183 C3 que mandó el cliente original en sus 8 conexiones al GameServer, descifrados con
// Dec1.dat y mandados otra vez por la Connection, Encrypted y sin ofuscación (lo descifrado ya
// trae el BeforeEncrypt de FoxAnti). Salen con Enc1.dat y el serial del original, desde 0 en cada
// Open: la cabecera y los bloques menos el último son los del cable. El último lleva el relleno,
// que el original saca de rand(), y detrás lo que tenía en la pila; descifrado con Dec1.dat, el
// paquete es el mismo, con el serial en orden.
TEST_F(GoldenConnection, EncryptsLikeTheClient)
{
    if (!m_keys)
    {
        FAIL() << "SetUp no cargó las claves";
    }
    const Keys& keys = *m_keys;
    Connection connection(m_socket, IdentityObfuscation::Shared(), m_clock, m_log, PaddingSeed);
    std::size_t total = 0;

    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        if (m_connections[index].port != GameServerPort)
        {
            continue;
        }
        const std::vector<Packet> sent = CutPackets(m_connections[index].Reads(WireDirection::ClientToServer), index);
        ASSERT_NO_FATAL_FAILURE(OpenTheGameServer(connection, keys.client, index));
        Resent resent;
        ASSERT_NO_FATAL_FAILURE(ResendEncrypted(connection, sent, keys.server, index, resent));
        ASSERT_NO_FATAL_FAILURE(ExpectLikeTheWire(resent, keys.server, index));
        total += resent.wire.size();
    }
    EXPECT_EQ(total, 183u);
    EXPECT_EQ(WarningCount(), 0u);
}
} // namespace
