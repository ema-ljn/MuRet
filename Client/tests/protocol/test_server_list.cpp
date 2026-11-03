#include "Protocol/Connection/ServerList.h"

#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/NetworkPrinters.h"
#include "support/Packets.h"
#include "support/ProtocolPrinters.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// Client::Protocol::Connection, la lista de servidores: pedirla (F4 02), leerla (F4 06), elegir uno
// (F4 03) y leer adónde conectarse, con los bytes del cable del 04.
namespace
{
using Client::Network::Delivery;
using Client::Protocol::ProtocolError;
using Client::Protocol::Connection::ReadServerAddress;
using Client::Protocol::Connection::ReadServerList;
using Client::Protocol::Connection::ServerEntry;
using Client::Protocol::Connection::WriteServerListRequest;
using Client::Protocol::Connection::WriteServerSelect;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;

// Lo que mide la cabecera de la lista: C2, el tamaño, F4, 06 y la cantidad.
constexpr std::size_t ListHeaderSize = 7;

// Una entrada: el código, la carga y un byte que no se lee.
constexpr std::size_t EntrySize = 4;

// Una lista F4 06 con estas entradas y esta cantidad, que puede no coincidir con las que trae. El
// cuarto byte de cada entrada va en 0xEE, para ver que no se lee.
ByteVector ListOf(const std::vector<ServerEntry>& entries, std::uint16_t count)
{
    const std::size_t size = ListHeaderSize + EntrySize * entries.size();
    ByteVector bytes = Bytes({0xC2, static_cast<int>(size >> 8), static_cast<int>(size & 0xFF), 0xF4, 0x06,
                              static_cast<int>(count >> 8), static_cast<int>(count & 0xFF)});
    for (const ServerEntry& entry : entries)
    {
        const ByteVector more = Bytes({entry.code & 0xFF, entry.code >> 8, entry.load, 0xEE});
        bytes.insert(bytes.end(), more.begin(), more.end());
    }
    return bytes;
}

// El pedido del 04: C1 04 F4 02, en claro, como lo arma 0x4c6070 sin el motor de envío.
TEST(ServerListTest, RequestsTheListInClear)
{
    const auto request = WriteServerListRequest();

    EXPECT_TRUE(SameBytes(request.packet.Bytes(), Bytes({0xC1, 0x04, 0xF4, 0x02})));
    EXPECT_EQ(request.delivery, Delivery::Clear);
}

// La lista del cable del laboratorio, como la deja FoxAnti: un servidor, el código 400 y carga 0.
TEST(ServerListTest, ReadsTheListOfTheWireAfterFoxAnti)
{
    const auto list =
        ReadServerList(PacketOf(Bytes({0xC2, 0x00, 0x0B, 0xF4, 0x06, 0x00, 0x01, 0x90, 0x01, 0x00, 0x00})));

    ASSERT_TRUE(list.HasValue());
    ASSERT_EQ(list.Value().size(), 1u);
    EXPECT_EQ(list.Value()[0].code, 400);
    EXPECT_EQ(list.Value()[0].load, 0);
}

// Cada entrada en su orden, con el código en little-endian: 0x01A4 llega como A4 01.
TEST(ServerListTest, ReadsEveryEntryInOrder)
{
    const std::vector<ServerEntry> entries{{0x0190, 0}, {0x0191, 2}, {0x01A4, 100}};

    const auto list = ReadServerList(PacketOf(ListOf(entries, 3)));

    ASSERT_TRUE(list.HasValue());
    ASSERT_EQ(list.Value().size(), entries.size());
    for (std::size_t k = 0; k < entries.size(); ++k)
    {
        EXPECT_EQ(list.Value()[k].code, entries[k].code) << "entrada " << k;
        EXPECT_EQ(list.Value()[k].load, entries[k].load) << "entrada " << k;
    }
}

// La cantidad va en big-endian, como el tamaño del C2: 01 00 son 256 entradas, y no una.
TEST(ServerListTest, TheCountIsBigEndian)
{
    const std::vector<ServerEntry> entries(256, ServerEntry{400, 0});

    const auto list = ReadServerList(PacketOf(ListOf(entries, 256)));

    ASSERT_TRUE(list.HasValue());
    EXPECT_EQ(list.Value().size(), 256u);
}

// Una cantidad que no entra en el paquete es TooShort, también si le falta un byte a la última
// entrada. FoxAnti ya lo rechaza en el F4 02 (Q-C09), pero la lista se lee sola.
TEST(ServerListTest, ACountThatDoesNotFitIsTooShort)
{
    ByteVector oneShort = ListOf({{400, 0}}, 1);
    oneShort.pop_back();
    oneShort[2] = static_cast<std::byte>(oneShort.size());

    EXPECT_EQ(ErrorOf(ReadServerList(PacketOf(ListOf({{400, 0}}, 2)))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadServerList(PacketOf(oneShort))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadServerList(PacketOf(ListOf({}, 0xFFFF)))), ProtocolError::TooShort);
}

// Sin servidores la lista está vacía, y lo que sobra detrás de la última entrada no se lee.
TEST(ServerListTest, ReadsJustTheEntriesOfTheCount)
{
    const auto empty = ReadServerList(PacketOf(ListOf({}, 0)));
    const auto first = ReadServerList(PacketOf(ListOf({{400, 0}, {401, 1}}, 1)));

    ASSERT_TRUE(empty.HasValue());
    EXPECT_TRUE(empty.Value().empty());
    ASSERT_TRUE(first.HasValue());
    ASSERT_EQ(first.Value().size(), 1u);
    EXPECT_EQ(first.Value()[0].code, 400);
}

// Solo se lee la F4 06 que deja FoxAnti: la F4 02 del cable, con la cantidad en un byte, no; ni una
// F4 06 en un C1, que 0x4c1860 leería corrida un byte.
TEST(ServerListTest, OnlyReadsTheC2F406)
{
    const ByteVector wire = Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00, 0xCC});
    const ByteVector inAC1 = Bytes({0xC1, 0x0A, 0xF4, 0x06, 0x00, 0x01, 0x90, 0x01, 0x00, 0x00});
    const ByteVector otherOpcode = Bytes({0xC2, 0x00, 0x0B, 0xF3, 0x06, 0x00, 0x01, 0x90, 0x01, 0x00, 0x00});

    EXPECT_EQ(ErrorOf(ReadServerList(PacketOf(wire))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(ReadServerList(PacketOf(inAC1))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(ReadServerList(PacketOf(otherOpcode))), ProtocolError::WrongPacket);
}

// Sin la cantidad entera, o sin el subcódigo, es TooShort.
TEST(ServerListTest, AListWithoutItsCountIsTooShort)
{
    EXPECT_EQ(ErrorOf(ReadServerList(PacketOf(Bytes({0xC2, 0x00, 0x06, 0xF4, 0x06, 0x00})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadServerList(PacketOf(Bytes({0xC2, 0x00, 0x04, 0xF4})))), ProtocolError::TooShort);
}

// La elección del 04, antes de FoxAnti: main.exe manda el 400 de la lista, en little-endian y en
// claro; FoxAnti lo pasa a 00 00 en el cable.
TEST(ServerListTest, SelectsAServerInClear)
{
    const auto select = WriteServerSelect(400);
    const auto other = WriteServerSelect(0x1234);

    EXPECT_TRUE(SameBytes(select.packet.Bytes(), Bytes({0xC1, 0x06, 0xF4, 0x03, 0x90, 0x01})));
    EXPECT_EQ(select.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(other.packet.Bytes(), Bytes({0xC1, 0x06, 0xF4, 0x03, 0x34, 0x12})));
}

// La respuesta del cable del laboratorio: "172.16.198.1" con cuatro \0, y el puerto 5D DA, que en
// little-endian es 55901.
TEST(ServerListTest, ReadsTheAddressOfTheWire)
{
    const auto address =
        ReadServerAddress(PacketOf(Bytes({0xC1, 0x16, 0xF4, 0x03, 0x31, 0x37, 0x32, 0x2E, 0x31, 0x36, 0x2E,
                                          0x31, 0x39, 0x38, 0x2E, 0x31, 0x00, 0x00, 0x00, 0x00, 0x5D, 0xDA})));

    ASSERT_TRUE(address.HasValue());
    EXPECT_EQ(address.Value().host, "172.16.198.1");
    EXPECT_EQ(address.Value().port, 55901);
}

// Del host se leen los primeros 15 bytes, hasta el primer \0: uno de 16 sin \0 queda en 15, porque
// el original (0x4e3341) copia pkt[4..18] a un buffer de 16 bytes en 0 y pkt[19] no lo lee; no se
// sigue en el puerto (Q-C07). Con un \0 en el medio, llega hasta ahí.
TEST(ServerListTest, TheHostIsItsFirst15BytesUpToTheFirstNul)
{
    const auto full =
        ReadServerAddress(PacketOf(Bytes({0xC1, 0x16, 0xF4, 0x03, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
                                          0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x01, 0x00})));
    const auto cut =
        ReadServerAddress(PacketOf(Bytes({0xC1, 0x16, 0xF4, 0x03, 0x31, 0x30, 0x2E, 0x30, 0x2E, 0x30, 0x2E,
                                          0x31, 0x00, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x01, 0x00})));

    ASSERT_TRUE(full.HasValue());
    EXPECT_EQ(full.Value().host, "abcdefghijklmno");
    EXPECT_EQ(full.Value().port, 1);
    ASSERT_TRUE(cut.HasValue());
    EXPECT_EQ(cut.Value().host, "10.0.0.1");
}

// Sin el puerto entero es TooShort; con un byte de más, se lee igual.
TEST(ServerListTest, AnAddressWithoutItsPortIsTooShort)
{
    ByteVector address = Bytes({0xC1, 0x16, 0xF4, 0x03, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5D, 0xDA});
    ByteVector longer = address;
    longer.push_back(std::byte{0x00});
    longer[1] = static_cast<std::byte>(longer.size());
    address.pop_back();
    address[1] = static_cast<std::byte>(address.size());

    EXPECT_EQ(ErrorOf(ReadServerAddress(PacketOf(address))), ProtocolError::TooShort);
    EXPECT_TRUE(ReadServerAddress(PacketOf(longer)).HasValue());
}

// Un host cortado antes de sus 16 bytes es TooShort, aunque detrás queden dos bytes que podrían ser
// un puerto; sin host, también.
TEST(ServerListTest, AnAddressWithAShortHostIsTooShort)
{
    EXPECT_EQ(ErrorOf(ReadServerAddress(PacketOf(Bytes({0xC1, 0x08, 0xF4, 0x03, 0x31, 0x2E, 0x5D, 0xDA})))),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadServerAddress(PacketOf(Bytes({0xC1, 0x04, 0xF4, 0x03})))), ProtocolError::TooShort);
}

// La respuesta es un C1 F4 03: un C1 F4 02 o un F1 03 no lo son.
TEST(ServerListTest, AnotherPacketIsNotAnAddress)
{
    EXPECT_EQ(ErrorOf(ReadServerAddress(PacketOf(Bytes({0xC1, 0x04, 0xF4, 0x02})))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(ReadServerAddress(PacketOf(Bytes({0xC1, 0x06, 0xF1, 0x03, 0x06, 0x00})))),
              ProtocolError::WrongPacket);
}
} // namespace
