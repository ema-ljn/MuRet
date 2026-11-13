#include "Protocol/Guild.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Los paquetes del gremio, de la alianza y de la guerra (CL9 hito 8; 04 § El gremio, la alianza y la guerra): lo que
// leen 0x4e2b10 y sus funciones y lo que mandan las respuestas. Los nombres de gremio y de personaje son inventados.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;
namespace Guild = Client::Protocol::Guild;

void Append(std::vector<std::byte>& bytes, std::string_view text, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
        bytes.push_back(i < text.size() ? static_cast<std::byte>(text[i]) : std::byte{0});
}

void AppendFilled(std::vector<std::byte>& bytes, std::size_t size, int value)
{
    for (std::size_t i = 0; i < size; ++i)
        bytes.push_back(static_cast<std::byte>(value));
}

std::string TextOf(const Guild::GuildName& name)
{
    std::string text;
    for (const std::byte value : name)
    {
        if (value == std::byte{0})
            break;
        text.push_back(static_cast<char>(value));
    }
    return text;
}

template <typename Read> Read ReadAs(const std::vector<std::byte>& bytes)
{
    const auto read = Guild::ReadGuildPacket(PacketOf(bytes));
    EXPECT_TRUE(read.HasValue());
    if (!read.HasValue())
        return {};
    EXPECT_TRUE(std::holds_alternative<Read>(read.Value()));
    if (!std::holds_alternative<Read>(read.Value()))
        return {};
    return std::get<Read>(read.Value());
}

std::vector<std::byte> PlainOf(const Client::Protocol::Outgoing& sent)
{
    const auto plain = Xor32Decode(sent.packet);
    return {plain.Bytes().begin(), plain.Bytes().end()};
}

std::vector<std::byte> RawOf(const Client::Protocol::Outgoing& sent)
{
    return {sent.packet.Bytes().begin(), sent.packet.Bytes().end()};
}

// 0x4d74d6: el 50 arma la clave con el byte alto entero; 0x4e42fb, el 5D, le saca el bit 7.
TEST(GuildProtocolTest, TheJoinRequestKeepsTheHighBitAndTheViewportDeleteDropsIt)
{
    EXPECT_EQ(ReadAs<Guild::JoinRequest>(Bytes({0xc1, 0x05, 0x50, 0x81, 0x23})).key, 0x8123);
    EXPECT_EQ(ReadAs<Guild::ViewportGuildDelete>(Bytes({0xc1, 0x05, 0x5d, 0x81, 0x23})).key, 0x0123);
}

// Los de un byte de resultado en pkt[3], y el 54, que no se lee.
TEST(GuildProtocolTest, TheResultsReadTheFourthByte)
{
    EXPECT_EQ(ReadAs<Guild::JoinResult>(Bytes({0xc1, 0x04, 0x51, 0x07})).result, 7);
    EXPECT_EQ(ReadAs<Guild::LeaveResult>(Bytes({0xc1, 0x04, 0x53, 0x05})).result, 5);
    EXPECT_EQ(ReadAs<Guild::CreateResult>(Bytes({0xc1, 0x04, 0x56, 0x01})).result, 1);
    EXPECT_EQ(ReadAs<Guild::WarRequestResult>(Bytes({0xc1, 0x04, 0x60, 0x06})).result, 6);
    EXPECT_EQ(ReadAs<Guild::WarEnd>(Bytes({0xc1, 0x0c, 0x63, 0x02, 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'})).result, 2);
    static_cast<void>(ReadAs<Guild::MasterQuestion>(Bytes({0xc1, 0x03, 0x54})));
}

// 0x4d7b60: un C2 con la cuenta en pkt[5], el puntaje en pkt[8], el nombre en pkt[0xd] y las entradas de 0xd bytes
// desde pkt[0x18].
TEST(GuildProtocolTest, TheMemberListReadsItsEntriesFromTheTwentyFourthByte)
{
    std::vector<std::byte> bytes =
        Bytes({0xc2, 0x00, 0x32, 0x52, 0x01, 0x02, 0xee, 0xee, 0xfb, 0xff, 0xff, 0xff, 0xee});
    Append(bytes, "Horneros", 8);
    AppendFilled(bytes, 3, 0xee);
    Append(bytes, "Tordo", 10);
    bytes.insert(bytes.end(), {std::byte{3}, std::byte{0x84}, std::byte{0x80}});
    Append(bytes, "Benteveo1x", 10);
    bytes.insert(bytes.end(), {std::byte{9}, std::byte{0x05}, std::byte{0x00}});
    ASSERT_EQ(bytes.size(), 0x32u);

    const auto list = ReadAs<Guild::MemberList>(bytes);
    EXPECT_EQ(list.count, 2);
    EXPECT_EQ(list.score, -5);
    EXPECT_EQ(TextOf(list.name), "Horneros");
    ASSERT_EQ(list.members.size(), 2u);
    EXPECT_EQ(list.members[0].number, 3);
    EXPECT_EQ(list.members[0].server, 0x84);
    EXPECT_EQ(list.members[0].status, 0x80);
    EXPECT_EQ(list.members[1].name[9], std::byte{'x'});
    EXPECT_EQ(list.members[1].server, 0x05);

    std::vector<std::byte> shorter(bytes.begin(), bytes.end() - 1);
    shorter[2] = std::byte{0x31};
    EXPECT_EQ(ErrorOf(Guild::ReadGuildPacket(PacketOf(shorter))), ProtocolError::TooShort);
}

// 0x4d8260 y 0x4e4382: el nombre de pkt[3..10], el tipo y (el 62) el equipo; 0x4e4400: los dos puntajes.
TEST(GuildProtocolTest, TheWarPacketsReadTheRivalAndTheScores)
{
    std::vector<std::byte> start = Bytes({0xc1, 0x0d, 0x62});
    Append(start, "Caranchos", 8);
    start.insert(start.end(), {std::byte{1}, std::byte{0}});
    const auto war = ReadAs<Guild::WarStart>(start);
    // Ocho bytes sin su cero: el aplicador lo pone (0x4d8282).
    EXPECT_EQ(TextOf(war.name), "Carancho");
    EXPECT_EQ(war.type, 1);
    EXPECT_EQ(war.team, 0);

    std::vector<std::byte> request = Bytes({0xc1, 0x0c, 0x61});
    Append(request, "Teros", 8);
    request.push_back(std::byte{1});
    const auto asked = ReadAs<Guild::WarRequest>(request);
    EXPECT_EQ(TextOf(asked.name), "Teros");
    EXPECT_EQ(asked.type, 1);

    const auto score = ReadAs<Guild::WarScore>(Bytes({0xc1, 0x06, 0x64, 0x03, 0x01, 0x00}));
    EXPECT_EQ(score.ours, 3);
    EXPECT_EQ(score.theirs, 1);
}

// 0x4e42c7: la clave en pkt[4], los nombres en pkt[9] y pkt[0x11] y la marca en pkt[0x19].
TEST(GuildProtocolTest, TheGuildInfoReadsTheKeyTheNamesAndTheMark)
{
    std::vector<std::byte> bytes = Bytes({0xc1, 0x39, 0x66, 0xee, 0x2a, 0x00, 0x00, 0x00, 0xee});
    Append(bytes, "Ceibos", 8);
    Append(bytes, "Horneros", 8);
    for (int i = 0; i < 32; ++i)
        bytes.push_back(static_cast<std::byte>(i * 7));
    ASSERT_EQ(bytes.size(), 0x39u);
    const auto info = ReadAs<Guild::GuildInfo>(bytes);
    EXPECT_EQ(info.key, 42);
    EXPECT_EQ(TextOf(info.unionName), "Ceibos");
    EXPECT_EQ(TextOf(info.guildName), "Horneros");
    EXPECT_EQ(info.mark[0], 0);
    EXPECT_EQ(info.mark[31], 31 * 7);
}

// E1, E5 y E6: los bytes desde pkt[3]; la clave del E6 en big-endian (0x4d9ae9).
TEST(GuildProtocolTest, TheAlliancePacketsReadTheirBytesInOrder)
{
    const auto assigned = ReadAs<Guild::AssignResult>(Bytes({0xc1, 0x05, 0xe1, 0x02, 0x01}));
    EXPECT_EQ(assigned.type, 2);
    EXPECT_EQ(assigned.result, 1);

    const auto request = ReadAs<Guild::RelationshipRequest>(Bytes({0xc1, 0x07, 0xe5, 0x01, 0x02, 0x12, 0x34}));
    EXPECT_EQ(request.relation, 1);
    EXPECT_EQ(request.request, 2);
    EXPECT_EQ(request.keyHigh, 0x12);
    EXPECT_EQ(request.keyLow, 0x34);

    const auto result = ReadAs<Guild::RelationshipResult>(Bytes({0xc1, 0x08, 0xe6, 0x01, 0x02, 0x01, 0x12, 0x34}));
    EXPECT_EQ(result.relation, 1);
    EXPECT_EQ(result.request, 2);
    EXPECT_EQ(result.result, 1);
    EXPECT_EQ(result.key, 0x1234);
}

// 0x4d9e50: un C2 con la cuenta en pkt[4] y el resultado en pkt[5]; con 1, las entradas de 0x29 bytes desde pkt[8].
TEST(GuildProtocolTest, TheUnionListReadsItsEntriesOnlyWithResultOne)
{
    std::vector<std::byte> bytes = Bytes({0xc2, 0x00, 0x31, 0xe9, 0x01, 0x01, 0xee, 0xee, 0x07});
    for (int i = 0; i < 32; ++i)
        bytes.push_back(static_cast<std::byte>(0x10 + i));
    Append(bytes, "Ceibos", 8);
    ASSERT_EQ(bytes.size(), 0x31u);
    const auto list = ReadAs<Guild::UnionList>(bytes);
    EXPECT_EQ(list.count, 1);
    ASSERT_EQ(list.entries.size(), 1u);
    EXPECT_EQ(list.entries[0].byte0, 7);
    EXPECT_EQ(list.entries[0].mark[0], 0x10);
    EXPECT_EQ(list.entries[0].mark[31], 0x2f);
    EXPECT_EQ(TextOf(list.entries[0].name), "Ceibos");

    const auto refused = ReadAs<Guild::UnionList>(Bytes({0xc2, 0x00, 0x08, 0xe9, 0x05, 0x00, 0x00, 0x00}));
    EXPECT_EQ(refused.count, 5);
    EXPECT_TRUE(refused.entries.empty());
}

// 0x4e4486: el EB lee pkt[4] solo con pkt[3] en 1.
TEST(GuildProtocolTest, TheUnionRefreshReadsTheResultOnlyForKindOne)
{
    const auto refresh = ReadAs<Guild::UnionListRefresh>(Bytes({0xc1, 0x05, 0xeb, 0x01, 0x01}));
    EXPECT_EQ(refresh.kind, 1);
    EXPECT_EQ(refresh.result, 1);
    const auto other = ReadAs<Guild::UnionListRefresh>(Bytes({0xc1, 0x04, 0xeb, 0x02}));
    EXPECT_EQ(other.kind, 2);
    EXPECT_EQ(other.result, 0);
    EXPECT_EQ(ErrorOf(Guild::ReadGuildPacket(PacketOf(Bytes({0xc1, 0x04, 0xeb, 0x01})))), ProtocolError::TooShort);
}

// El 52 y el E9 solo en C2; los demás solo en C1; el 55 y el 5C caen en el default de 0x4e2b10.
TEST(GuildProtocolTest, TheWrongTypeOrOpcodeIsNotAGuildPacket)
{
    EXPECT_EQ(ErrorOf(Guild::ReadGuildPacket(PacketOf(Bytes({0xc1, 0x06, 0x52, 0x00, 0x00, 0x00})))),
              ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(Guild::ReadGuildPacket(PacketOf(Bytes({0xc2, 0x00, 0x06, 0x50, 0x00, 0x00})))),
              ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(Guild::ReadGuildPacket(PacketOf(Bytes({0xc1, 0x04, 0x55, 0x00})))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(Guild::ReadGuildPacket(PacketOf(Bytes({0xc1, 0x04, 0x5c, 0x00})))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(Guild::ReadGuildPacket(PacketOf(Bytes({0xc1, 0x03, 0x51})))), ProtocolError::TooShort);
}

// 0x4d6b60: C1 06 51 [sí] [clave], con el XOR32 y en claro.
TEST(GuildProtocolTest, TheJoinAnswerCarriesTheKey)
{
    const auto yes = Guild::WriteJoinAnswer(true, 0x1234);
    EXPECT_EQ(yes.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(PlainOf(yes), Bytes({0xc1, 0x06, 0x51, 0x01, 0x12, 0x34})));
    EXPECT_NE(yes.packet.Bytes()[3], std::byte{0x01});
    EXPECT_TRUE(SameBytes(PlainOf(Guild::WriteJoinAnswer(false, 0x8001)), Bytes({0xc1, 0x06, 0x51, 0x00, 0x80, 0x01})));
}

// Los pedidos de tres bytes van tal cual (C1 03 52 y C1 03 E9); los demás con el XOR32.
TEST(GuildProtocolTest, TheRequestsAndAnswersHaveTheirBytes)
{
    EXPECT_TRUE(SameBytes(RawOf(Guild::WriteMemberListRequest()), Bytes({0xc1, 0x03, 0x52})));
    EXPECT_TRUE(SameBytes(RawOf(Guild::WriteUnionListRequest()), Bytes({0xc1, 0x03, 0xe9})));
    EXPECT_TRUE(SameBytes(PlainOf(Guild::WriteMasterAnswer(0)), Bytes({0xc1, 0x04, 0x54, 0x00})));
    EXPECT_TRUE(SameBytes(PlainOf(Guild::WriteRelationshipAnswer(1, 2, 1, 0x12, 0x34)),
                          Bytes({0xc1, 0x08, 0xe6, 0x01, 0x02, 0x01, 0x12, 0x34})));
}

// 0x6552b0: C1 2B 55, el nombre y la marca de 32 bytes.
TEST(GuildProtocolTest, TheCreateCarriesTheNameAndTheMark)
{
    Guild::GuildName name{};
    const std::string_view invented = "Horneros";
    for (std::size_t i = 0; i < name.size(); ++i)
        name[i] = static_cast<std::byte>(invented[i]);
    Guild::GuildMark mark{};
    for (std::size_t i = 0; i < mark.size(); ++i)
        mark[i] = static_cast<std::uint8_t>(0x11 * (i % 16));
    std::vector<std::byte> expected = Bytes({0xc1, 0x2b, 0x55});
    Append(expected, invented, 8);
    for (const std::uint8_t value : mark)
        expected.push_back(std::byte{value});
    EXPECT_TRUE(SameBytes(PlainOf(Guild::WriteCreate(name, mark)), expected));
}
// 0x4d9260: el 65 es un C2 con la cuenta en pkt[4] y entradas de 0xc bytes desde pkt[5]: el DWORD del gremio, el
// puesto, +0x369, la relación y la clave sin el bit 7. Los tres bytes del final no se leen.
TEST(GuildProtocolTest, TheViewportGuildReadsEntriesOfTwelveBytes)
{
    const auto read = ReadAs<Guild::ViewportGuild>(Bytes({0xc2, 0x00, 0x1a, 0x65, 0x02,                         //
                                                          0x78, 0x56, 0x34, 0x12, 0x80, 0x01, 0x02, 0x81, 0x23, //
                                                          0xaa, 0xbb, 0xcc,                                     //
                                                          0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05}));
    ASSERT_EQ(read.entries.size(), 2u);
    EXPECT_EQ(read.entries[0].guildKey, 0x12345678);
    EXPECT_EQ(read.entries[0].status, 0x80);
    EXPECT_EQ(read.entries[0].byte369, 0x01);
    EXPECT_EQ(read.entries[0].relation, 0x02);
    EXPECT_EQ(read.entries[0].key, 0x0123);
    EXPECT_EQ(read.entries[1].guildKey, 1);
    EXPECT_EQ(read.entries[1].key, 0x0005);
    // La última entrada sin su clave baja, y el 65 como C1: no se leen.
    EXPECT_EQ(ErrorOf(Guild::ReadGuildPacket(
                  PacketOf(Bytes({0xc2, 0x00, 0x0d, 0x65, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})))),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Guild::ReadGuildPacket(PacketOf(Bytes({0xc1, 0x05, 0x65, 0x00, 0x00})))),
              ProtocolError::WrongPacket);
}

// 0x4e44aa: el 67, un C2 con entradas de 0x14 bytes: la clave entera (sin sacar el bit 7), el DWORD del gremio en +4,
// la relación en +8 y el nombre de la alianza en +9.
TEST(GuildProtocolTest, TheViewportUnionReadsEntriesOfTwentyBytes)
{
    std::vector<std::byte> bytes =
        Bytes({0xc2, 0x00, 0x19, 0x67, 0x01, 0x81, 0x23, 0xee, 0xee, 0x02, 0x00, 0x00, 0x00, 0x01});
    Append(bytes, "Teros", 8);
    AppendFilled(bytes, 3, 0xee);
    const auto read = ReadAs<Guild::ViewportUnion>(bytes);
    ASSERT_EQ(read.entries.size(), 1u);
    EXPECT_EQ(read.entries[0].key, 0x8123);
    EXPECT_EQ(read.entries[0].guildKey, 2);
    EXPECT_EQ(read.entries[0].relation, 1);
    EXPECT_EQ(TextOf(read.entries[0].unionName), "Teros");
    // Sin el último byte del nombre (+0x10), no se lee.
    bytes.resize(bytes.size() - 4);
    bytes[2] = static_cast<std::byte>(bytes.size());
    EXPECT_EQ(ErrorOf(Guild::ReadGuildPacket(PacketOf(bytes))), ProtocolError::TooShort);
}

// 0x4d930f..0x4d9555: C1 08 66 [0] [clave], con el XOR32 desde pkt[3].
TEST(GuildProtocolTest, TheGuildInfoRequestPadsBeforeTheKey)
{
    const auto sent = Guild::WriteGuildInfoRequest(0x12345678);
    EXPECT_EQ(sent.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(PlainOf(sent), Bytes({0xc1, 0x08, 0x66, 0x00, 0x78, 0x56, 0x34, 0x12})));
    EXPECT_NE(sent.packet.Bytes()[3], std::byte{0x00});
}
} // namespace
