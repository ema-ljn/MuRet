// DecryptPacket contra el cable del laboratorio (support/WireCapture.h, MURET_CLIENT_WIRE_DIR) y
// las claves reales: los C3 que mandó el cliente original se descifran con Dec1.dat, la del
// servidor, y los C3/C4 que le contestó el GameServer, con Dec2.dat, la del cliente. Cada paquete
// se corta con el PacketReader: el --golden de c3c4_reference.py mira solo el primero de cada
// segmento.
//
// El cable lleva el login del jugador: estos tests cuentan paquetes y comparan con
// SameBytesWithoutValues. Ningún mensaje imprime un byte del cable ni de lo descifrado.

#include "Network/Crypto/SimpleModulus.h"
#include "Network/Crypto/SimpleModulusKeyFile.h"
#include "Network/EncryptedPacket.h"
#include "Network/Packet.h"
#include "Network/PacketReader.h"
#include "Network/PacketSerials.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/KeyFiles.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using Client::Network::DecryptPacket;
using Client::Network::HeaderSize;
using Client::Network::Packet;
using Client::Network::PacketReader;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::SerialCheck;
using Client::Network::Crypto::CipherBlockSize;
using Client::Network::Crypto::DecryptBlock;
using Client::Network::Crypto::DecryptionKey;
using Client::Network::Crypto::EncryptBlock;
using Client::Network::Crypto::EncryptionKey;
using Client::Network::Crypto::LoadDecryptionKey;
using Client::Network::Crypto::LoadEncryptionKey;
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

// La máscara de FoxAnti en todo lo que manda el GameServer del laboratorio: cada byte con XOR 0xAA.
// La deshace este test, a mano, hasta que la aplique el lector en la CL2.4.
constexpr std::byte FoxAntiMask{0xAA};

// Los paquetes de `reads`, cortados con el PacketReader. Si el corte falla o sobran bytes, el test
// falla, y vuelve lo que haya salido.
std::vector<Packet> CutPackets(const std::vector<std::span<const std::byte>>& reads, std::size_t index)
{
    PacketReader reader;
    std::vector<Packet> packets;
    for (const std::span<const std::byte> read : reads)
    {
        reader.Feed(read);
        while (true)
        {
            auto next = reader.Next();
            if (!next.HasValue())
            {
                ADD_FAILURE() << "conexión " << index << ": el corte falla con "
                              << testing::PrintToString(next.Error());
                return packets;
            }
            if (!next.Value())
            {
                break;
            }
            packets.push_back(std::move(*std::move(next).Value()));
        }
    }
    EXPECT_EQ(reader.Pending(), 0u) << "conexión " << index;
    return packets;
}

// Lo cifrado de `encrypted`, cifrado de nuevo bloque a bloque: cada bloque se descifra con
// `decryption`, y sus 8 bytes, relleno incluido, se cifran con `encryption` y el largo útil que
// traía. EncryptPacket no sirve acá, porque el relleno del original trae basura de la pila.
// std::nullopt si un bloque no descifra.
std::optional<ByteVector> EncryptBlocksAgain(const Packet& encrypted, const DecryptionKey& decryption,
                                             const EncryptionKey& encryption)
{
    const std::span<const std::byte> cipher = encrypted.Bytes().subspan(HeaderSize(encrypted.Type()));
    ByteVector again;
    again.reserve(cipher.size());
    for (std::size_t offset = 0; offset + CipherBlockSize <= cipher.size(); offset += CipherBlockSize)
    {
        const auto block = DecryptBlock(decryption, cipher.subspan(offset).first<CipherBlockSize>());
        if (!block)
        {
            return std::nullopt;
        }
        const auto cipherAgain = EncryptBlock(encryption, block->bytes, block->usefulSize);
        again.insert(again.end(), cipherAgain.begin(), cipherAgain.end());
    }
    return again;
}

// Cuántos C3/C4 se descifraron, de cada tipo, y en cuántas conexiones había alguno.
struct DecryptedCounts
{
    std::size_t c3 = 0;
    std::size_t c4 = 0;
    std::size_t connections = 0;
};

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
        connections = std::move(parsed).Value();

        // Lo que manda el cliente lo cifra con Enc1.dat y el servidor lo descifra con Dec1.dat; lo
        // que contesta el servidor lo cifra con Enc2.dat y el cliente lo descifra con Dec2.dat.
        enc1 = LoadKeyFile(DataFile("Enc1.dat"), LoadEncryptionKey);
        dec1 = LoadKeyFile(WireFile("Dec1.dat"), LoadDecryptionKey);
        enc2 = LoadKeyFile(WireFile("Enc2.dat"), LoadEncryptionKey);
        dec2 = LoadKeyFile(DataFile("Dec2.dat"), LoadDecryptionKey);
        ASSERT_TRUE(enc1 && dec1 && enc2 && dec2);
    }

    // Descifra con `decryption` los C3/C4 de cada conexión al GameServer, en el sentido `direction`
    // y con la máscara `mask` deshecha, con unos seriales por conexión que arrancan en 0. Cada uno
    // tiene que descifrar con su serial en orden, y cifrado de nuevo con `encryption`, dar los
    // mismos bytes.
    DecryptedCounts DecryptAll(WireDirection direction, std::byte mask, const DecryptionKey& decryption,
                               const EncryptionKey& encryption) const
    {
        DecryptedCounts counts;
        for (std::size_t index = 0; index < connections.size(); ++index)
        {
            const WireConnection& connection = connections[index];
            if (connection.port != GameServerPort)
            {
                continue;
            }
            const std::vector<ByteVector> unmasked = Unmasked(connection.Reads(direction), mask);
            const std::vector<std::span<const std::byte>> reads(unmasked.begin(), unmasked.end());
            PacketSerials serials;
            std::size_t decrypted = 0;
            for (const Packet& packet : CutPackets(reads, index))
            {
                if (packet.Type() != PacketType::C3 && packet.Type() != PacketType::C4)
                {
                    continue;
                }
                const auto plain = DecryptPacket(packet, decryption, serials);
                EXPECT_TRUE(plain.HasValue())
                    << "conexión " << index << ", C3/C4 " << decrypted << ": "
                    << testing::PrintToString(plain.HasValue() ? std::nullopt : std::optional(plain.Error()));
                if (plain.HasValue())
                {
                    EXPECT_EQ(plain.Value().serial, SerialCheck::InOrder)
                        << "conexión " << index << ", C3/C4 " << decrypted;
                }
                const std::optional<ByteVector> again = EncryptBlocksAgain(packet, decryption, encryption);
                EXPECT_TRUE(again.has_value()) << "conexión " << index << ", C3/C4 " << decrypted;
                if (again)
                {
                    EXPECT_TRUE(SameBytesWithoutValues(*again, packet.Bytes().subspan(HeaderSize(packet.Type()))))
                        << "conexión " << index << ", C3/C4 " << decrypted;
                }
                ++(packet.Type() == PacketType::C3 ? counts.c3 : counts.c4);
                ++decrypted;
            }
            counts.connections += decrypted > 0 ? 1 : 0;
        }
        return counts;
    }

    std::vector<WireConnection> connections;
    std::optional<EncryptionKey> enc1;
    std::optional<DecryptionKey> dec1;
    std::optional<EncryptionKey> enc2;
    std::optional<DecryptionKey> dec2;

private:
    // Una copia de cada lectura con cada byte con XOR `mask`.
    static std::vector<ByteVector> Unmasked(const std::vector<std::span<const std::byte>>& reads, std::byte mask)
    {
        std::vector<ByteVector> unmasked;
        unmasked.reserve(reads.size());
        for (const std::span<const std::byte> read : reads)
        {
            ByteVector copy(read.begin(), read.end());
            for (std::byte& value : copy)
            {
                value ^= mask;
            }
            unmasked.push_back(std::move(copy));
        }
        return unmasked;
    }
};

// Los 183 C3 que mandó el cliente original al GameServer, en sus 8 conexiones: el checksum bien, el
// serial desde 0 y +1 en cada conexión, y cifrados de nuevo con Enc1.dat del cliente, los mismos
// bytes.
TEST_F(GoldenEncryptedPacket, DecryptsWhatTheClientSent)
{
    const DecryptedCounts counts = DecryptAll(WireDirection::ClientToServer, std::byte{0}, *dec1, *enc1);

    EXPECT_EQ(counts.c3, 183u);
    EXPECT_EQ(counts.c4, 0u);
    EXPECT_EQ(counts.connections, 8u);
}

// Los 25 C3/C4 que contestó el GameServer (18 C3 y 7 C4, de 36, 69 y 80 bytes), en 7 conexiones,
// con el 0xAA deshecho: el checksum bien y el serial desde 0, sin un Resynced, y cifrados de nuevo
// con Enc2.dat del servidor, los mismos bytes.
TEST_F(GoldenEncryptedPacket, DecryptsWhatTheGameServerAnswered)
{
    const DecryptedCounts counts = DecryptAll(WireDirection::ServerToClient, FoxAntiMask, *dec2, *enc2);

    EXPECT_EQ(counts.c3, 18u);
    EXPECT_EQ(counts.c4, 7u);
    EXPECT_EQ(counts.connections, 7u);
}

} // namespace
