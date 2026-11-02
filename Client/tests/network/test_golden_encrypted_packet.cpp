#include "Network/Crypto/SimpleModulus.h"
#include "Network/Crypto/SimpleModulusKeyFile.h"
#include "Network/EncryptedPacket.h"
#include "Network/FoxAntiObfuscation.h"
#include "Network/IdentityObfuscation.h"
#include "Network/IObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/KeyFiles.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"
#include "support/WirePackets.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// DecryptPacket y EncryptPacket contra el cable del laboratorio (support/WireCapture.h,
// MURET_CLIENT_WIRE_DIR) y las claves reales: los C3 que mandó el cliente original se descifran
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
using Client::Network::DecryptPacket;
using Client::Network::EncryptPacket;
using Client::Network::FoxAntiObfuscation;
using Client::Network::HeaderSize;
using Client::Network::IdentityObfuscation;
using Client::Network::IObfuscation;
using Client::Network::Packet;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::SerialCheck;
using Client::Network::Crypto::CipherBlockSize;
using Client::Network::Crypto::DecryptBlock;
using Client::Network::Crypto::DecryptedBlock;
using Client::Network::Crypto::DecryptionKey;
using Client::Network::Crypto::EncryptBlock;
using Client::Network::Crypto::EncryptionKey;
using Client::Network::Crypto::LoadDecryptionKey;
using Client::Network::Crypto::LoadEncryptionKey;
using Client::Network::Crypto::PlainBlockSize;
using Client::Tests::CutPackets;
using Client::Tests::LoadKeyFile;
using Client::Tests::ParseWireCapture;
using Client::Tests::ReadFileBytes;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::WireConnection;
using Client::Tests::WireDirection;

using ByteVector = std::vector<std::byte>;

// El log que escribió el proxy del laboratorio.
constexpr std::string_view CaptureFile = "muproxy.log";

// El puerto del proxy delante del GameServer del laboratorio: los C3/C4 van y vienen por ahí.
constexpr std::uint16_t GameServerPort = 55901;

// El serial: el primer byte de lo útil, que el paquete rearmado no lleva.
constexpr std::size_t SerialSize = 1;

// Dónde está un C3/C4, para los mensajes: su conexión en el log y su orden entre los de ella.
std::string Where(std::size_t connection, std::size_t index)
{
    return "conexión " + std::to_string(connection) + ", C3/C4 " + std::to_string(index);
}

// Un C3/C4 del cable y lo que dio DecryptPacket.
struct WirePacket
{
    std::size_t connection = 0;  // en el log
    std::size_t index = 0;       // su orden entre los C3/C4 de su conexión, desde 0
    Packet encrypted;            // como pasó por el cable, con la máscara deshecha
    std::optional<Packet> plain; // el C1/C2 rearmado, o std::nullopt si no descifró
};

// `encrypted` descifrado con DecryptPacket, que tiene que darlo con el serial en orden.
std::optional<Packet> DecryptInOrder(const Packet& encrypted, const DecryptionKey& decryption, PacketSerials& serials,
                                     const std::string& where)
{
    auto decrypted = DecryptPacket(encrypted, decryption, serials);
    if (!decrypted.HasValue())
    {
        ADD_FAILURE() << where << ": " << testing::PrintToString(decrypted.Error());
        return std::nullopt;
    }
    EXPECT_EQ(decrypted.Value().serial, SerialCheck::InOrder) << where;
    return std::move(decrypted).Value().packet;
}

// Cuántos C3/C4 hay de cada tipo, y en cuántas conexiones había alguno.
struct DecryptedCounts
{
    std::size_t c3 = 0;
    std::size_t c4 = 0;
    std::size_t m_connections = 0;
};

DecryptedCounts CountOf(const std::vector<WirePacket>& packets)
{
    DecryptedCounts counts;
    for (const WirePacket& packet : packets)
    {
        ++(packet.encrypted.Type() == PacketType::C3 ? counts.c3 : counts.c4);
        counts.m_connections += packet.index == 0 ? 1 : 0;
    }
    return counts;
}

// Lo cifrado de un C3/C4: lo que sigue a la cabecera.
std::span<const std::byte> CipherOf(const Packet& encrypted)
{
    return encrypted.Bytes().subspan(HeaderSize(encrypted.Type()));
}

// Los bloques de `encrypted`, descifrados uno por uno con DecryptBlock, sin DecryptPacket.
// std::nullopt si uno no descifra.
std::optional<std::vector<DecryptedBlock>> DecryptBlocks(const Packet& encrypted, const DecryptionKey& decryption)
{
    const std::span<const std::byte> cipher = CipherOf(encrypted);
    std::vector<DecryptedBlock> blocks;
    blocks.reserve(cipher.size() / CipherBlockSize);
    for (std::size_t offset = 0; offset + CipherBlockSize <= cipher.size(); offset += CipherBlockSize)
    {
        const std::optional<DecryptedBlock> block =
            DecryptBlock(decryption, cipher.subspan(offset).first<CipherBlockSize>());
        if (!block)
        {
            return std::nullopt;
        }
        blocks.push_back(*block);
    }
    return blocks;
}

// Los bloques cifrados de nuevo con `encryption`, cada uno con sus 8 bytes, relleno incluido, y el
// largo útil que traía.
ByteVector EncryptBlocksAgain(const std::vector<DecryptedBlock>& blocks, const EncryptionKey& encryption)
{
    ByteVector again;
    again.reserve(blocks.size() * CipherBlockSize);
    for (const DecryptedBlock& block : blocks)
    {
        const auto cipher = EncryptBlock(encryption, block.bytes, block.usefulSize);
        again.insert(again.end(), cipher.begin(), cipher.end());
    }
    return again;
}

// Lo útil de cada bloque, uno detrás del otro: el serial, el opcode y los campos.
ByteVector UsefulBytes(const std::vector<DecryptedBlock>& blocks)
{
    ByteVector useful;
    useful.reserve(blocks.size() * PlainBlockSize);
    for (const DecryptedBlock& block : blocks)
    {
        const auto size = static_cast<std::ptrdiff_t>(std::min<std::size_t>(block.usefulSize, PlainBlockSize));
        useful.insert(useful.end(), block.bytes.begin(), block.bytes.begin() + size);
    }
    return useful;
}

// Si `plain`, lo que dio DecryptPacket, es lo que dan los bloques uno por uno: un C1 de un C3 y un
// C2 de un C4, con lo útil sin el serial detrás de la cabecera. El mensaje no dice qué bytes son.
testing::AssertionResult IsRebuiltFrom(const Packet& plain, PacketType encryptedType,
                                       const std::vector<DecryptedBlock>& blocks)
{
    const PacketType rebuiltType = encryptedType == PacketType::C3 ? PacketType::C1 : PacketType::C2;
    if (plain.Type() != rebuiltType)
    {
        return testing::AssertionFailure() << "no se rearmó como " << (encryptedType == PacketType::C3 ? "C1" : "C2");
    }
    const ByteVector useful = UsefulBytes(blocks);
    if (useful.size() < SerialSize)
    {
        return testing::AssertionFailure() << "los bloques no traen el serial";
    }
    return SameBytesWithoutValues(plain.Bytes().subspan(HeaderSize(rebuiltType)),
                                  std::span<const std::byte>(useful).subspan(SerialSize));
}

// Cada C3/C4 contra sus bloques descifrados uno por uno: el paquete rearmado es lo útil sin el
// serial, y los bloques, cifrados de nuevo con `encryption`, dan lo cifrado entero. El último
// también, porque lleva sus 8 bytes como llegaron.
void ExpectSameAsItsBlocks(const std::vector<WirePacket>& packets, const DecryptionKey& decryption,
                           const EncryptionKey& encryption)
{
    for (const WirePacket& packet : packets)
    {
        const std::string where = Where(packet.connection, packet.index);
        const auto blocks = DecryptBlocks(packet.encrypted, decryption);
        if (!blocks)
        {
            ADD_FAILURE() << where << ": un bloque no descifra";
            continue;
        }
        EXPECT_TRUE(SameBytesWithoutValues(EncryptBlocksAgain(*blocks, encryption), CipherOf(packet.encrypted)))
            << where;
        if (packet.plain)
        {
            EXPECT_TRUE(IsRebuiltFrom(*packet.plain, packet.encrypted.Type(), *blocks)) << where;
        }
    }
}

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

class GoldenEncryptedPacket : public Client::Tests::GoldenAndWireDataTest
{
protected:
    void SetUp() override
    {
        GoldenAndWireDataTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        const auto path = WireFile(CaptureFile);
        ASSERT_TRUE(path.has_value());
        const auto file = ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value()) << "no se pudo leer " << CaptureFile;
        auto parsed = ParseWireCapture(std::string_view(file->data(), file->size()));
        ASSERT_TRUE(parsed.HasValue()) << CaptureFile << ": la línea " << parsed.Error().line << " no se entiende";
        m_connections = std::move(parsed).Value();

        // Lo que manda el cliente lo cifra con Enc1.dat y el servidor lo descifra con Dec1.dat; lo
        // que contesta el servidor lo cifra con Enc2.dat y el cliente lo descifra con Dec2.dat.
        m_enc1 = LoadKeyFile(DataFile("Enc1.dat"), LoadEncryptionKey);
        m_dec1 = LoadKeyFile(WireFile("Dec1.dat"), LoadDecryptionKey);
        m_enc2 = LoadKeyFile(WireFile("Enc2.dat"), LoadEncryptionKey);
        m_dec2 = LoadKeyFile(DataFile("Dec2.dat"), LoadDecryptionKey);
        ASSERT_TRUE(m_enc1 && m_dec1 && m_enc2 && m_dec2);
    }

    // Los C3/C4 de cada conexión al GameServer, en el sentido `direction` y cortados con
    // `obfuscation`, descifrados con DecryptPacket y unos seriales por conexión que arrancan en 0:
    // cada uno tiene que descifrar con su serial en orden.
    std::vector<WirePacket> DecryptAll(WireDirection direction, const IObfuscation& obfuscation,
                                       const DecryptionKey& decryption) const
    {
        std::vector<WirePacket> packets;
        for (std::size_t connection = 0; connection < m_connections.size(); ++connection)
        {
            if (m_connections[connection].port != GameServerPort)
            {
                continue;
            }
            PacketSerials serials;
            std::size_t index = 0;
            for (Packet& packet : CutPackets(m_connections[connection].Reads(direction), obfuscation, connection))
            {
                if (packet.Type() != PacketType::C3 && packet.Type() != PacketType::C4)
                {
                    continue;
                }
                std::optional<Packet> plain = DecryptInOrder(packet, decryption, serials, Where(connection, index));
                packets.push_back({connection, index, std::move(packet), std::move(plain)});
                ++index;
            }
        }
        return packets;
    }

    std::vector<WireConnection> m_connections;
    std::optional<EncryptionKey> m_enc1;
    std::optional<DecryptionKey> m_dec1;
    std::optional<EncryptionKey> m_enc2;
    std::optional<DecryptionKey> m_dec2;
};

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
    EXPECT_EQ(counts.m_connections, 8u);
}

// Los 25 C3/C4 que contestó el GameServer (18 C3 y 7 C4, de 36, 69 y 80 bytes), en 7 conexiones,
// cortados con el 0xAA de FoxAntiObfuscation: el checksum bien y el serial desde 0, sin un
// Resynced, el C1/C2 rearmado igual a lo útil de los bloques, y los bloques, cifrados de nuevo con
// Enc2.dat del servidor, los mismos bytes.
TEST_F(GoldenEncryptedPacket, DecryptsWhatTheGameServerAnswered)
{
    const FoxAntiObfuscation foxAnti;
    const std::vector<WirePacket> answered = DecryptAll(WireDirection::ServerToClient, foxAnti, *m_dec2);

    ExpectSameAsItsBlocks(answered, *m_dec2, *m_enc2);
    const DecryptedCounts counts = CountOf(answered);
    EXPECT_EQ(counts.c3, 18u);
    EXPECT_EQ(counts.c4, 7u);
    EXPECT_EQ(counts.m_connections, 7u);
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
