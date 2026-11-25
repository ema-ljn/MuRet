#pragma once

#include "Crypto/SimpleModulus.h"
#include "Crypto/SimpleModulusKeyFile.h"
#include "Network/EncryptedPacket.h"
#include "Network/IObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Protocol/Character/InventoryList.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/KeyFiles.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"
#include "support/WirePackets.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// What test_golden_encrypted_packet.cpp and test_golden_encrypted_packet_xor_remap.cpp share: the C3/C4 of the lab's
// wire capture, decrypted with the real keys and compared block by block.
namespace Client::Tests::GoldenEncryptedPacketSupport
{
using Crypto::CipherBlockSize;
using Crypto::DecryptBlock;
using Crypto::DecryptedBlock;
using Crypto::DecryptionKey;
using Crypto::EncryptBlock;
using Crypto::EncryptionKey;
using Crypto::LoadDecryptionKey;
using Crypto::LoadEncryptionKey;
using Crypto::PlainBlockSize;
using Network::DecryptPacket;
using Network::HeaderSize;
using Network::IObfuscation;
using Network::Packet;
using Network::PacketSerials;
using Network::PacketType;
using Network::SerialCheck;
using Protocol::Character::InventoryList;
using Protocol::Character::ReadInventoryList;

using ByteVector = std::vector<std::byte>;

inline constexpr std::string_view CaptureFile = "muproxy.log";

// The proxy's port in front of the lab's GameServer: the C3/C4 go through it.
inline constexpr std::uint16_t GameServerPort = 55901;

// The first useful byte, which the rebuilt packet does not carry.
inline constexpr std::size_t SerialSize = 1;

inline std::string Where(std::size_t connection, std::size_t index)
{
    return "connection " + std::to_string(connection) + ", C3/C4 " + std::to_string(index);
}

struct WirePacket
{
    std::size_t connection = 0;  // in the log
    std::size_t index = 0;       // its place among its connection's C3/C4, from 0
    Packet encrypted;            // as it went through the wire, with the mask undone
    std::optional<Packet> plain; // the rebuilt C1/C2, or std::nullopt if it did not decrypt
};

// Fails the test unless DecryptPacket gives the packet with its serial in order.
inline std::optional<Packet> DecryptInOrder(const Packet& encrypted, const DecryptionKey& decryption,
                                            PacketSerials& serials, const std::string& where)
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

struct DecryptedCounts
{
    std::size_t c3 = 0;
    std::size_t c4 = 0;
    std::size_t connections = 0;
};

inline DecryptedCounts CountOf(const std::vector<WirePacket>& packets)
{
    DecryptedCounts counts;
    for (const WirePacket& packet : packets)
    {
        ++(packet.encrypted.Type() == PacketType::C3 ? counts.c3 : counts.c4);
        counts.connections += packet.index == 0 ? 1 : 0;
    }
    return counts;
}

inline std::span<const std::byte> CipherOf(const Packet& encrypted)
{
    return encrypted.Bytes().subspan(HeaderSize(encrypted.Type()));
}

// One DecryptBlock per block, without DecryptPacket. std::nullopt if a block does not decrypt.
inline std::optional<std::vector<DecryptedBlock>> DecryptBlocks(const Packet& encrypted,
                                                                const DecryptionKey& decryption)
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

// Each block keeps its 8 bytes, padding included, and the useful length it brought.
inline ByteVector EncryptBlocksAgain(const std::vector<DecryptedBlock>& blocks, const EncryptionKey& encryption)
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

// The serial, the opcode and the fields.
inline ByteVector UsefulBytes(const std::vector<DecryptedBlock>& blocks)
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

// A C1 from a C3 and a C2 from a C4, with the blocks' useful bytes minus the serial. The message prints no bytes.
inline testing::AssertionResult IsRebuiltFrom(const Packet& plain, PacketType encryptedType,
                                              const std::vector<DecryptedBlock>& blocks)
{
    const PacketType rebuiltType = encryptedType == PacketType::C3 ? PacketType::C1 : PacketType::C2;
    if (plain.Type() != rebuiltType)
    {
        return testing::AssertionFailure() << "not rebuilt as " << (encryptedType == PacketType::C3 ? "C1" : "C2");
    }
    const ByteVector useful = UsefulBytes(blocks);
    if (useful.size() < SerialSize)
    {
        return testing::AssertionFailure() << "the blocks do not bring the serial";
    }
    return SameBytesWithoutValues(plain.Bytes().subspan(HeaderSize(rebuiltType)),
                                  std::span<const std::byte>(useful).subspan(SerialSize));
}

// The blocks encrypted again give the whole cipher, the last one too: it keeps its 8 bytes as they arrived.
inline void ExpectSameAsItsBlocks(const std::vector<WirePacket>& packets, const DecryptionKey& decryption,
                                  const EncryptionKey& encryption)
{
    for (const WirePacket& packet : packets)
    {
        const std::string where = Where(packet.connection, packet.index);
        const auto blocks = DecryptBlocks(packet.encrypted, decryption);
        if (!blocks)
        {
            ADD_FAILURE() << where << ": a block does not decrypt";
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

// ADR 0003: the Python oracle's C2 has the count at 5 and 8-byte entries from 6. Only slots and sizes are printed.
inline void ExpectInventoryEntries(const InventoryList& list, std::span<const std::byte> reference)
{
    constexpr std::size_t EntriesOffset = 6;
    constexpr std::size_t ReferenceStride = 8;
    ASSERT_GE(reference.size(), EntriesOffset);
    const std::uint8_t count = std::to_integer<std::uint8_t>(reference[5]);
    ASSERT_TRUE(count == 2 || count == 5 || count == 6);
    ASSERT_EQ(reference.size(), EntriesOffset + ReferenceStride * count);
    ASSERT_EQ(list.items.size(), count);
    constexpr std::array<std::uint8_t, 6> ExpectedSlots = {0, 12, 14, 17, 23, 28};
    for (std::size_t index = 0; index < count; ++index)
    {
        SCOPED_TRACE(index);
        const std::size_t offset = EntriesOffset + ReferenceStride * index;
        const auto slot = std::to_integer<std::uint8_t>(reference[offset]);
        EXPECT_EQ(slot, count == 2 ? index : ExpectedSlots[index]);
        EXPECT_EQ(list.items[index].slot, slot);
        EXPECT_TRUE(SameBytesWithoutValues(list.items[index].item, reference.subspan(offset + 1, 7)));
    }
}

class GoldenEncryptedPacket : public GoldenAndWireDataTest
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
        ASSERT_TRUE(file.has_value()) << CaptureFile << " could not be read";
        auto parsed = ParseWireCapture(std::string_view(file->data(), file->size()));
        ASSERT_TRUE(parsed.HasValue()) << CaptureFile << ": line " << parsed.Error().line << " does not parse";
        m_connections = std::move(parsed).Value();

        // Client to server: Enc1.dat and Dec1.dat. Server to client: Enc2.dat and Dec2.dat.
        m_enc1 = LoadKeyFile(DataFile("Enc1.dat"), LoadEncryptionKey);
        m_dec1 = LoadKeyFile(WireFile("Dec1.dat"), LoadDecryptionKey);
        m_enc2 = LoadKeyFile(WireFile("Enc2.dat"), LoadEncryptionKey);
        m_dec2 = LoadKeyFile(DataFile("Dec2.dat"), LoadDecryptionKey);
        ASSERT_TRUE(m_enc1 && m_dec1 && m_enc2 && m_dec2);
    }

    // Each GameServer connection's C3/C4, cut with `obfuscation` and decrypted with its own serials from 0.
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

    std::optional<std::vector<char>> LoadWireFile(std::string_view name) const
    {
        const auto path = WireFile(name);
        if (!path)
        {
            return std::nullopt;
        }
        auto file = ReadFileBytes(*path);
        EXPECT_TRUE(file.has_value()) << name << " could not be read";
        return file;
    }

    void ExpectInventoryReference(const WirePacket& packet, std::size_t index) const
    {
        const std::string suffix = std::to_string(index);
        const auto capture = LoadWireFile("inventory-capture-" + suffix + ".c4");
        const auto reference = LoadWireFile("inventory-reference-" + suffix + ".c2");
        ASSERT_TRUE(capture && reference && packet.plain);
        const std::span<const std::byte> expected = std::as_bytes(std::span(*reference));
        EXPECT_TRUE(SameBytesWithoutValues(packet.encrypted.Bytes(), std::as_bytes(std::span(*capture))));
        EXPECT_TRUE(SameBytesWithoutValues(packet.plain->Bytes(), expected));
        const auto list = ReadInventoryList(*packet.plain);
        ASSERT_TRUE(list.HasValue());
        ExpectInventoryEntries(list.Value(), expected);
    }

    std::vector<WireConnection> m_connections;
    std::optional<EncryptionKey> m_enc1;
    std::optional<DecryptionKey> m_dec1;
    std::optional<EncryptionKey> m_enc2;
    std::optional<DecryptionKey> m_dec2;
};
} // namespace Client::Tests::GoldenEncryptedPacketSupport
