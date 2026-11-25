#include "Network/Crypto/SimpleModulus.h"
#include "Network/EncryptedPacket.h"
#include "Network/IdentityObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"

#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"
#include "test_golden_encrypted_packet_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// DecryptPacket y EncryptPacket contra el cable del laboratorio (support/WireCapture.h,
// MU_CLIENT_WIRE_DIR) y las claves reales: los C3 que mandó el cliente original se descifran
// con Dec1.dat, la del servidor, y los C3/C4 que le contestó el GameServer, con Dec2.dat, la del
// cliente. Cada paquete se corta con el PacketReader (CutPackets de support/WirePackets.h), y lo
// del GameServer, con el 0xAA de FoxAnti: el --golden de c3c4_reference.py mira solo el primero de
// cada segmento. Lo que da DecryptPacket se
// compara con los bloques descifrados uno por uno con DecryptBlock, y lo del cliente se cifra de
// nuevo con EncryptPacket.
//
// El cable lleva el login del jugador: estos tests cuentan paquetes y comparan con
// SameBytesWithoutValues. Ningún mensaje imprime un byte del cable ni de lo descifrado.
namespace
{
using Client::Network::EncryptPacket;
using Client::Network::IdentityObfuscation;
using Client::Network::PacketSerials;
using Client::Network::Crypto::CipherBlockSize;
using Client::Network::Crypto::DecryptedBlock;
using Client::Network::Crypto::DecryptionKey;
using Client::Network::Crypto::EncryptionKey;
using Client::Network::Crypto::PlainBlockSize;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::WireDirection;
using Client::Tests::GoldenEncryptedPacketSupport::CountOf;
using Client::Tests::GoldenEncryptedPacketSupport::DecryptBlocks;
using Client::Tests::GoldenEncryptedPacketSupport::DecryptedCounts;
using Client::Tests::GoldenEncryptedPacketSupport::ExpectSameAsItsBlocks;
using Client::Tests::GoldenEncryptedPacketSupport::GoldenEncryptedPacket;
using Client::Tests::GoldenEncryptedPacketSupport::Where;
using Client::Tests::GoldenEncryptedPacketSupport::WirePacket;

// El relleno que puso el original: el byte que sigue a lo útil en el último bloque. Con el bloque
// lleno no hay, y el que se pase no cambia nada.
std::byte PaddingOf(const DecryptedBlock& last)
{
    return last.usefulSize < PlainBlockSize ? last.bytes[last.usefulSize] : std::byte{};
}

// Si lo que sigue al relleno en el último bloque, que el original toma de su pila, está en 0, como
// lo pone EncryptPacket.
bool IsZeroAfterThePadding(const DecryptedBlock& last)
{
    const std::size_t afterPadding = std::min<std::size_t>(last.usefulSize + std::size_t{1}, PlainBlockSize);
    return std::ranges::all_of(std::span(last.bytes).subspan(afterPadding),
                               [](std::byte value) { return value == std::byte{0}; });
}

// El C1 de `packet` cifrado de nuevo con EncryptPacket y el relleno del original, contra lo que
// pasó por el cable: la cabecera y los bloques menos el último, iguales, y el último, igual justo
// cuando el original dejó ceros detrás del relleno. Devuelve si salió igual entero.
bool ExpectEncryptedAgainLikeTheWire(const WirePacket& packet, const DecryptedBlock& last,
                                     const EncryptionKey& encryption, PacketSerials& serials)
{
    const std::string where = Where(packet.connection, packet.index);
    const auto again = EncryptPacket(*packet.plain, encryption, serials, PaddingOf(last));
    if (!again.HasValue())
    {
        ADD_FAILURE() << where << ": EncryptPacket falla con " << testing::PrintToString(again.Error());
        return false;
    }
    const std::span<const std::byte> mine = again.Value().Bytes();
    const std::span<const std::byte> wire = packet.encrypted.Bytes();
    if (mine.size() != wire.size())
    {
        ADD_FAILURE() << where << ": mide " << mine.size() << " bytes y en el cable " << wire.size();
        return false;
    }
    const std::size_t beforeLast = wire.size() - CipherBlockSize;
    const testing::AssertionResult sameBefore = SameBytesWithoutValues(mine.first(beforeLast), wire.first(beforeLast));
    const testing::AssertionResult sameLast =
        SameBytesWithoutValues(mine.last(CipherBlockSize), wire.last(CipherBlockSize));
    EXPECT_TRUE(sameBefore) << where;
    EXPECT_EQ(static_cast<bool>(sameLast), IsZeroAfterThePadding(last)) << where << ": el último bloque";
    return sameBefore && sameLast;
}

// Los 183 C3 que mandó el cliente original al GameServer, en sus 8 conexiones: el checksum bien, el
// serial desde 0 y +1 en cada conexión, el C1 rearmado igual a lo útil de los bloques, y los
// bloques, cifrados de nuevo con Enc1.dat del cliente, los mismos bytes.
TEST_F(GoldenEncryptedPacket, DecryptsWhatTheClientSent)
{
    const std::vector<WirePacket> sent =
        DecryptAll(WireDirection::ClientToServer, IdentityObfuscation::Shared(), *m_dec1);

    ExpectSameAsItsBlocks(sent, *m_dec1, *m_enc1);
    const DecryptedCounts counts = CountOf(sent);
    EXPECT_EQ(counts.c3, 183u);
    EXPECT_EQ(counts.c4, 0u);
    EXPECT_EQ(counts.connections, 8u);
}

// Los 183 C3 del cliente, descifrados con DecryptPacket y cifrados de nuevo con EncryptPacket, con
// Enc1.dat, unos seriales por conexión desde 0 y de relleno el byte que puso el original: la
// cabecera y todos los bloques menos el último dan los mismos bytes. Detrás del relleno, el
// original deja lo que haya en su pila y EncryptPacket pone ceros: el último bloque da lo mismo
// justo cuando la pila tenía ceros, y el paquete entero, en 110 de los 183.
TEST_F(GoldenEncryptedPacket, EncryptPacketGivesWhatTheClientSent)
{
    const std::vector<WirePacket> sent =
        DecryptAll(WireDirection::ClientToServer, IdentityObfuscation::Shared(), *m_dec1);
    PacketSerials serials;
    std::size_t whole = 0;

    for (const WirePacket& packet : sent)
    {
        if (packet.index == 0)
        {
            serials.Reset(); // cada conexión arranca en 0, como 0x4c1340 al conectar al GameServer
        }
        const auto blocks = DecryptBlocks(packet.encrypted, *m_dec1);
        if (!packet.plain || !blocks || blocks->empty())
        {
            ADD_FAILURE() << Where(packet.connection, packet.index) << ": no descifra";
            [[maybe_unused]] const std::uint8_t skipped = serials.TakeSend();
            continue;
        }
        if (ExpectEncryptedAgainLikeTheWire(packet, blocks->back(), *m_enc1, serials))
        {
            ++whole;
        }
    }
    EXPECT_EQ(sent.size(), 183u);
    EXPECT_EQ(whole, 110u);
}
} // namespace
