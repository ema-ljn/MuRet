#include "Protocol/Character/CharacterList.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PacketFields.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/CharacterPackets.h"
#include "support/ExpectedError.h"
#include "support/NetworkPrinters.h"
#include "support/Packets.h"
#include "support/ProtocolPrinters.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Client::Protocol::Character, la lista de personajes: pedirla (F3 00, C->S) y leerla (F3 00, S->C,
// 0x4c20a0), con los bytes del 04 (La selección de personaje) escritos a mano y nombres sintéticos
// (support/CharacterPackets.h).
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::Outgoing;
using Client::Protocol::ProtocolError;
using Client::Protocol::TextField;
using Client::Protocol::Character::CharacterClassOf;
using Client::Protocol::Character::CharacterEntry;
using Client::Protocol::Character::CharacterList;
using Client::Protocol::Character::IsLocked;
using Client::Protocol::Character::ReadCharacterList;
using Client::Protocol::Character::WriteCharacterListRequest;
using Client::Tests::Bytes;
using Client::Tests::CharacterEntryFields;
using Client::Tests::CharacterListBytes;
using Client::Tests::ErrorOf;
using Client::Tests::NameBytes;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;

// La lista de `entries`, leída: falla el test si no lee.
CharacterList Read(const std::vector<CharacterEntryFields>& entries)
{
    const auto list = ReadCharacterList(PacketOf(CharacterListBytes(entries)));
    EXPECT_TRUE(list.HasValue());
    return list.HasValue() ? list.Value() : CharacterList{};
}

// Un equipo con un valor distinto en cada byte, para ver que cada uno queda en su lugar.
std::array<std::uint8_t, 17> DistinctEquipment(std::uint8_t first)
{
    std::array<std::uint8_t, 17> equipment{};
    for (std::size_t k = 0; k < equipment.size(); ++k)
    {
        equipment[k] = static_cast<std::uint8_t>(first + k);
    }
    return equipment;
}

// El pedido sin cuerpo, con el XOR32 desde el byte 3 y en claro (0x634e0e): F3 ^ 89, el byte 3 de
// la tabla, da el 7A del cable.
TEST(CharacterListTest, TheRequestIsC104F300)
{
    const Outgoing request = WriteCharacterListRequest();

    EXPECT_EQ(request.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(request.packet.Bytes(), Bytes({0xC1, 0x04, 0xF3, 0x7A})));
    EXPECT_TRUE(SameBytes(Xor32Decode(request.packet).Bytes(), Bytes({0xC1, 0x04, 0xF3, 0x00})));
}

// Cada campo de cada entrada, en el orden del paquete y no en el de los lugares.
TEST(CharacterListTest, ReadsTheEntriesInOrder)
{
    const CharacterList list = Read({
        {3, "char0001", 0x0102, 0x01, 0x20, DistinctEquipment(0x40), 0x80},
        {0, "char0002", 0x0304, 0x00, 0x51, DistinctEquipment(0x60), 0xFF},
    });

    ASSERT_EQ(list.characters.size(), 2u);
    const CharacterEntry& first = list.characters[0];
    EXPECT_EQ(first.slot, 3);
    EXPECT_EQ(first.name, NameBytes("char0001"));
    EXPECT_EQ(first.level, 0x0102);
    EXPECT_EQ(first.flags, 0x01);
    EXPECT_EQ(first.classByte, 0x20);
    EXPECT_EQ(first.equipment[0], std::byte{0x40});
    EXPECT_EQ(first.equipment[16], std::byte{0x50});
    EXPECT_EQ(first.guildStatus, 0x80);
    const CharacterEntry& second = list.characters[1];
    EXPECT_EQ(second.slot, 0);
    EXPECT_EQ(second.name, NameBytes("char0002"));
    EXPECT_EQ(second.level, 0x0304);
    EXPECT_EQ(second.flags, 0x00);
    EXPECT_EQ(second.classByte, 0x51);
    EXPECT_EQ(second.equipment[0], std::byte{0x60});
    EXPECT_EQ(second.equipment[16], std::byte{0x70});
    EXPECT_EQ(second.guildStatus, 0xFF);
}

// Con la cantidad en 0, la lista vacía: el original pasa al estado 0x33 y no lee más. Los dos bytes
// de antes se leen igual.
TEST(CharacterListTest, AnEmptyListHasNoEntries)
{
    const auto list = ReadCharacterList(PacketOf(CharacterListBytes({}, 0, 0x05, 0x21)));

    ASSERT_TRUE(list.HasValue());
    EXPECT_TRUE(list.Value().characters.empty());
    EXPECT_EQ(list.Value().creatableClasses, 0x05);
    EXPECT_EQ(list.Value().notice, 0x21);
}

// 0x4c20a0 no mira el largo: con una cantidad de más leería fuera del paquete. MuRet no la lee
// (diferencia nombrada): TooShort, también sin el byte de la cantidad.
TEST(CharacterListTest, ACountBeyondThePacketIsTooShort)
{
    const auto oneEntry = CharacterListBytes({{}}, 2);
    std::vector<std::byte> oneByteShort = CharacterListBytes({{}, {}});
    oneByteShort.pop_back();
    oneByteShort[1] = static_cast<std::byte>(oneByteShort.size());

    EXPECT_EQ(ErrorOf(ReadCharacterList(PacketOf(oneEntry))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadCharacterList(PacketOf(oneByteShort))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadCharacterList(PacketOf(Bytes({0xC1, 0x06, 0xF3, 0x00, 0x00, 0x00})))),
              ProtocolError::TooShort);
}

// Otro subcódigo del F3 no es la lista.
TEST(CharacterListTest, AnotherSubcodeIsTheWrongPacket)
{
    std::vector<std::byte> create = CharacterListBytes({{}});
    create[3] = std::byte{0x01};

    EXPECT_EQ(ErrorOf(ReadCharacterList(PacketOf(create))), ProtocolError::WrongPacket);
}

// Un nombre de 10 bytes llega sin terminador, y se guarda entero.
TEST(CharacterListTest, KeepsTheNameBytes)
{
    const CharacterList list = Read({{0, "abcdefghij"}});

    ASSERT_EQ(list.characters.size(), 1u);
    EXPECT_EQ(list.characters[0].name, NameBytes("abcdefghij"));
    EXPECT_EQ(TextField(list.characters[0].name), "abcdefghij");
}

// ((b & 0x10) | (b >> 4)) >> 1 (0x517270): los bits 5 a 7 son la clase y el 4, la segunda clase,
// que queda en el bit 3. Los bits 0 a 3 no cuentan.
TEST(CharacterListTest, TheClassHasTheSecondClassInBitThree)
{
    constexpr std::array<std::uint8_t, 16> Expected = {0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15};
    for (std::uint8_t high = 0; high < Expected.size(); ++high)
    {
        const auto classByte = static_cast<std::uint8_t>(high << 4);

        EXPECT_EQ(CharacterClassOf(classByte), Expected[high]) << "bits 4 a 7 en " << int{high};
        EXPECT_EQ(CharacterClassOf(static_cast<std::uint8_t>(classByte | 0x0F)), Expected[high])
            << "bits 4 a 7 en " << int{high} << ", con los de abajo prendidos";
    }
}

// El bit 0x1 de las banderas bloquea al personaje (0x6342c0 mira el de +0x1d8); los otros, no.
TEST(CharacterListTest, TheLockFlagIsBitZero)
{
    const CharacterList list = Read({{0, "char0001", 1, 0x01}, {1, "char0002", 1, 0xFE}, {2, "char0003", 1, 0xFF}});

    ASSERT_EQ(list.characters.size(), 3u);
    EXPECT_TRUE(IsLocked(list.characters[0]));
    EXPECT_FALSE(IsLocked(list.characters[1]));
    EXPECT_TRUE(IsLocked(list.characters[2]));
}

// Q-C101: el original deja sin posición al de un lugar de más de 4, y lo dibuja y lo prueba el
// picking, pero no se puede elegir. MuRet lo descarta (diferencia nombrada); los que siguen se leen
// donde están.
TEST(CharacterListTest, ASlotAboveFourIsDropped)
{
    const CharacterList list = Read({{0, "char0001"}, {5, "char0002"}, {4, "char0003"}, {255, "char0004"}});

    ASSERT_EQ(list.characters.size(), 2u);
    EXPECT_EQ(list.characters[0].slot, 0);
    EXPECT_EQ(list.characters[1].slot, 4);
    EXPECT_EQ(list.characters[1].name, NameBytes("char0003"));
}
} // namespace
