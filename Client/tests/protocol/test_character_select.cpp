#include "Protocol/Character/CharacterSelect.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"

#include "support/Bytes.h"
#include "support/CharacterPackets.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

// Client::Protocol::Character, entrar al mundo con el elegido (F3 03, C->S, 0x636290), con nombres
// sintéticos.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::Outgoing;
using Client::Protocol::Character::WriteCharacterSelect;
using Client::Tests::Bytes;
using Client::Tests::NameBytes;
using Client::Tests::SameBytes;

// C1 0E F3 03 y los 10 bytes del nombre como vienen en la lista, con el XOR32 desde el byte 3 y en
// claro (0x6362b2..0x63638d).
TEST(CharacterSelectTest, TheRequestIsC10EF303WithTheName)
{
    const Outgoing select = WriteCharacterSelect(NameBytes("char0001"));

    EXPECT_EQ(select.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(select.packet).Bytes(),
                          Bytes({0xC1, 0x0E, 0xF3, 0x03, 'c', 'h', 'a', 'r', '0', '0', '0', '1', 0x00, 0x00})));
    EXPECT_FALSE(SameBytes(select.packet.Bytes(), Xor32Decode(select.packet).Bytes())) << "salió sin el XOR32";
}

// Un nombre de 10 bytes sale entero, sin terminador.
TEST(CharacterSelectTest, AFullNameGoesWithoutATerminator)
{
    const Outgoing select = WriteCharacterSelect(NameBytes("abcdefghij"));

    EXPECT_TRUE(SameBytes(Xor32Decode(select.packet).Bytes(),
                          Bytes({0xC1, 0x0E, 0xF3, 0x03, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'})));
}
} // namespace
