#include "Session/OpcodeTable.h"

#include "test_server_session_support.h"

#include "Network/IdentityObfuscation.h"
#include "Network/Packet.h"
#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/SessionPackets.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace
{
using Client::Network::IdentityObfuscation;
using Client::Network::Opcode;
using Client::Network::Subcode;
using Client::Session::ConnectServerOpcodes;
using Client::Session::EachCodeHasOneRow;
using Client::Session::GameServerOpcodes;
using Client::Session::OpcodeRow;
using Client::Session::Row;
using Client::Session::RowHandler;
using Client::Session::SentOnly;
using Client::Session::Unported;
using Client::Tests::Bytes;
using Client::Tests::Join;
using Client::Tests::SameBytes;
using Client::Tests::ServerSessionSupport::Harness;

const OpcodeRow& RowOf(std::span<const OpcodeRow> rows, std::uint8_t opcode)
{
    const auto row = std::ranges::find_if(rows, [opcode](const OpcodeRow& candidate)
                                          { return Client::Network::ByteOf(candidate.opcode) == opcode; });
    EXPECT_NE(row, rows.end()) << static_cast<int>(opcode);
    return *row;
}

TEST(OpcodeTablesTest, NoTwoRowsShareACode)
{
    EXPECT_TRUE(EachCodeHasOneRow(GameServerOpcodes()));
    EXPECT_TRUE(EachCodeHasOneRow(ConnectServerOpcodes()));
}

TEST(OpcodeTablesTest, TwoRowsOfOneCodeAreCaught)
{
    constexpr std::array Rows{Row(Opcode{0x29}, std::nullopt, 0, "First", Unported{}),
                              Row(Opcode{0x29}, std::nullopt, 0, "Second", SentOnly{})};
    EXPECT_FALSE(EachCodeHasOneRow(Rows));
}

TEST(OpcodeTablesTest, AnOpcodeOfItsOwnAndBySubcodeIsCaught)
{
    constexpr std::array Rows{Row(Opcode{0xf1}, std::nullopt, 0, "Whole", Unported{}),
                              Row(Opcode{0xf1}, Subcode{0x04}, 0, "Sub", Unported{})};
    EXPECT_FALSE(EachCodeHasOneRow(Rows));
}

TEST(OpcodeTablesTest, TheRowsGoInTheOrderOfTheCases)
{
    for (const std::span<const OpcodeRow> rows : {GameServerOpcodes(), ConnectServerOpcodes()})
    {
        EXPECT_TRUE(std::ranges::is_sorted(rows, {},
                                           [](const OpcodeRow& row) {
                                               return std::pair{Client::Network::ByteOf(row.opcode),
                                                                row.subcode ? Client::Network::ByteOf(*row.subcode)
                                                                            : 0};
                                           }));
    }
}

TEST(OpcodeTablesTest, OnlyTheFiveRingCodesCopy)
{
    std::vector<std::uint8_t> copied;
    for (const OpcodeRow& row : GameServerOpcodes())
    {
        if (row.copiesToRing)
            copied.push_back(Client::Network::ByteOf(row.opcode));
    }
    EXPECT_EQ(copied, (std::vector<std::uint8_t>{0x0b, 0x12, 0x1c, 0x24, 0x2a}));
    EXPECT_TRUE(std::ranges::none_of(ConnectServerOpcodes(), &OpcodeRow::copiesToRing));
}

// The original tells the two 00 apart by the scene (0x4e33e4); MuRet, by a table per server.
TEST(OpcodeTablesTest, TheConnectServersHelloIsNotTheGameServersChat)
{
    const OpcodeRow& hello = RowOf(ConnectServerOpcodes(), 0x00);
    const OpcodeRow& chat = RowOf(GameServerOpcodes(), 0x00);
    EXPECT_EQ(hello.name, "ConnectHello");
    EXPECT_EQ(chat.name, "Chat");
    ASSERT_TRUE(std::holds_alternative<RowHandler>(hello.action));
    ASSERT_TRUE(std::holds_alternative<RowHandler>(chat.action));
    EXPECT_NE(std::get<RowHandler>(hello.action), std::get<RowHandler>(chat.action));
}

// 0x4e38ca: the copy comes before the flag and the reader, so an unencrypted and short 24 is copied too.
TEST(OpcodeTablesTest, AShortItemMoveIsStillCopiedToTheRing)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto itemMove = Bytes({0xc1, 3, 0x24});
    tested.socket.reads = {Join({itemMove})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    ASSERT_EQ(tested.eventSink.packetCopies.size(), 1u);
    EXPECT_TRUE(SameBytes(tested.eventSink.packetCopies[0], itemMove));
    EXPECT_TRUE(tested.eventSink.world.empty());
}
} // namespace
