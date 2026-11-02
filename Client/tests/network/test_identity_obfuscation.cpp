#include "Network/IdentityObfuscation.h"

#include "Network/Crypto/SimpleModulus.h"
#include "Network/EncryptedPacket.h"
#include "Network/Packet.h"
#include "Network/PacketError.h"
#include "Network/PacketReader.h"
#include "Network/PacketSerials.h"

#include "support/Bytes.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/SimpleModulusKeys.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// Client::Network::IdentityObfuscation: no toca nada, y es la que usan el PacketReader y
// EncryptPacket cuando no les pasan otra (plan de la CL2, CL2.4).
namespace
{
using Client::Network::EncryptPacket;
using Client::Network::IdentityObfuscation;
using Client::Network::Packet;
using Client::Network::PacketError;
using Client::Network::PacketReader;
using Client::Network::PacketSerials;
using Client::Tests::Bytes;
using Client::Tests::IdentityPair;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;

// Un Packet con estos bytes. Si no lo forman, el test falla (y Value() corta el programa).
Packet PacketOf(ByteVector bytes)
{
    auto built = Packet::FromBytes(std::move(bytes));
    EXPECT_TRUE(built.HasValue());
    return std::move(built).Value();
}

// Unos de cada forma: un C1 con XOR32 (el aviso F1 03), la lista F4 02 del laboratorio, el F3 11
// del laboratorio, un C3 (FIX_C3_SERIAL0) y un C2 con el opcode D3, que FoxAnti remapearía en un
// C1.
std::vector<ByteVector> SomePackets()
{
    return {
        Bytes({0xC1, 0x06, 0xF1, 0x7B, 0xC1, 0x73}),
        Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00, 0xCC}),
        Bytes({0xC1, 0x08, 0xF3, 0x11, 0xFE, 0x04, 0x16, 0x68}),
        Bytes({0xC3, 0x0D, 0x7B, 0xC1, 0x1C, 0xC0, 0x00, 0x00, 0x00, 0x20, 0xC0, 0xF8, 0xC0}),
        Bytes({0xC2, 0x00, 0x05, 0xD3, 0x01}),
    };
}

TEST(IdentityObfuscationTest, NoByteIsMasked)
{
    const IdentityObfuscation identity;
    for (int value = 0; value < 256; ++value)
    {
        EXPECT_EQ(identity.InboundMask(static_cast<std::byte>(value)), std::byte{0}) << "byte " << value;
    }
}

TEST(IdentityObfuscationTest, EveryPacketPassesAsItCame)
{
    const IdentityObfuscation identity;
    for (const ByteVector& bytes : SomePackets())
    {
        const auto afterCut = identity.AfterCut(PacketOf(bytes));
        ASSERT_TRUE(afterCut.HasValue());
        EXPECT_TRUE(SameBytes(afterCut.Value().Bytes(), bytes));
        const auto beforeDispatch = identity.BeforeDispatch(PacketOf(bytes));
        ASSERT_TRUE(beforeDispatch.HasValue());
        EXPECT_TRUE(SameBytes(beforeDispatch.Value().Bytes(), bytes));
        EXPECT_TRUE(SameBytes(identity.BeforeSend(PacketOf(bytes)).Bytes(), bytes));
    }
}

// Lo que se va a cifrar de un C1 con el opcode 0x1E, que FoxAnti remapearía: [serial][opcode], dos
// campos y el relleno.
TEST(IdentityObfuscationTest, WhatGoesEncryptedStaysTheSame)
{
    const IdentityObfuscation identity;
    ByteVector sealed = Bytes({0x03, 0x1E, 0x29, 0x41, 0xA5});
    const ByteVector before = sealed;

    identity.BeforeEncrypt(sealed);

    EXPECT_TRUE(SameBytes(sealed, before));
}

// Un lector sin ofuscación no desenmascara: un C1 con el 0xAA de FoxAnti empieza con 6B, que no es
// un tipo.
TEST(IdentityObfuscationTest, AReaderWithoutObfuscationDoesNotUnmask)
{
    PacketReader reader;
    reader.Feed(Bytes({0x6B, 0xAE, 0xE8, 0xAB}));

    const auto next = reader.Next();

    ASSERT_FALSE(next.HasValue());
    EXPECT_EQ(next.Error(), PacketError::InvalidType);
}

// EncryptPacket con la identidad da lo mismo que sin ofuscación: FIX_C3_SERIAL0.
TEST(IdentityObfuscationTest, EncryptingWithItIsEncryptingWithout)
{
    const auto keys = IdentityPair();
    const Packet notice = PacketOf(Bytes({0xC1, 0x06, 0xF1, 0x7B, 0xC1, 0x73}));
    PacketSerials with;
    PacketSerials without;

    const auto withIdentity = EncryptPacket(notice, keys.encryption, with, IdentityObfuscation::Shared());
    const auto withoutObfuscation = EncryptPacket(notice, keys.encryption, without);

    ASSERT_TRUE(withIdentity.HasValue());
    ASSERT_TRUE(withoutObfuscation.HasValue());
    EXPECT_TRUE(SameBytes(withIdentity.Value().Bytes(), withoutObfuscation.Value().Bytes()));
    EXPECT_TRUE(SameBytes(withIdentity.Value().Bytes(),
                          Bytes({0xC3, 0x0D, 0x7B, 0xC1, 0x1C, 0xC0, 0x00, 0x00, 0x00, 0x20, 0xC0, 0xF8, 0xC0})));
}

TEST(IdentityObfuscationTest, TheSharedOneIsAlwaysTheSame)
{
    EXPECT_EQ(&IdentityObfuscation::Shared(), &IdentityObfuscation::Shared());
}
} // namespace
