#include "Scenes/CastleSeniorAnswers.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/Outgoing.h"
#include "UI/CastleSiegeState.h"

#include "support/Bytes.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Las respuestas del diálogo del Castle Senior (CL9 hito 12; 04 § Castle Siege, las ventanas del castillo): lo que
// mandan 0x6623e0, 0x662d50, 0x663440, 0x664370, 0x664ce0, 0x6653d0, 0x665ab0, 0x666b30 y 0x667a40, byte a byte. No
// miran el mapa ni sacan rand().
namespace
{
using Client::Network::Crypto::Xor32Decode;
using Client::Scenes::CastleAnswerContext;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
using Client::UI::CastleData;

class CastleSeniorAnswersTest : public ::testing::Test
{
protected:
    [[nodiscard]] CastleAnswerContext Context()
    {
        return CastleAnswerContext{.data = m_data, .log = m_log, .send = [this](Client::Protocol::Outgoing packet) {
                                       m_sent.push_back(std::move(packet));
                                   }};
    }

    // Un registro de la zona: el tipo, el índice, la defensa, la recuperación, la vida máxima y +0x1a.
    void Record(std::uint32_t at, std::uint32_t type, std::uint32_t index, std::uint32_t defense,
                std::uint32_t recovery, std::uint32_t maxLife, std::uint8_t alive)
    {
        m_data.SetU32(at, type);
        m_data.SetU32(at + 4, index);
        m_data.SetU32(at + 8, defense);
        m_data.SetU32(at + 0xc, recovery);
        m_data.SetU32(at + 0x10, maxLife);
        m_data.bytes.at(at + 0x1a - Client::UI::CastleDataBase) = alive;
    }

    // Los paquetes que salieron, en claro.
    [[nodiscard]] std::vector<std::vector<std::byte>> Sent()
    {
        std::vector<std::vector<std::byte>> clear;
        for (Client::Protocol::Outgoing& packet : m_sent)
        {
            const auto decoded = Xor32Decode(std::move(packet.packet));
            const auto bytes = decoded.Bytes();
            clear.emplace_back(bytes.begin(), bytes.end());
        }
        m_sent.clear();
        return clear;
    }

    [[nodiscard]] bool Warned(std::string_view part) const
    {
        for (const std::string& line : m_sink.lines)
        {
            if (line.find(part) != std::string::npos)
                return true;
        }
        return false;
    }

    CastleData m_data;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    std::vector<Client::Protocol::Outgoing> m_sent;
};

// 0x6623e0: la puerta elegida desde 0x7d5c590; con +0x1a en 0 la compra (B2 05), si no la repara (B2 06).
TEST_F(CastleSeniorAnswersTest, TheGateAnswerBuysOrRepairsTheSelectedGate)
{
    m_data.SetU32(Client::UI::SelectedGateAt, 1);
    Record(0x7d5c590 + 0x1c, 0x115, 2, 0, 0, 500000, 0);
    Client::Scenes::AnswerTheGateDialog(7, Context());
    EXPECT_TRUE(m_sent.empty());
    Client::Scenes::AnswerTheGateDialog(8, Context());
    m_data.bytes.at(0x7d5c590 + 0x1c + 0x1a - Client::UI::CastleDataBase) = 1;
    Client::Scenes::AnswerTheGateDialog(8, Context());
    const auto sent = Sent();
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_TRUE(SameBytes(sent[0], Bytes({0xc1, 0x0c, 0xb2, 5, 0x15, 1, 0, 0, 2, 0, 0, 0})));
    EXPECT_TRUE(SameBytes(sent[1], Bytes({0xc1, 0x0c, 0xb2, 6, 0x15, 1, 0, 0, 2, 0, 0, 0})));
}

// 0x664370: la estatua elegida desde 0x7d5c520. Una elegida en −1 cae en el registro anterior, dentro de la zona.
TEST_F(CastleSeniorAnswersTest, TheStatueAnswerReadsFrom0x7d5c520)
{
    Record(0x7d5c520, 0x11b, 1, 0, 0, 400000, 1);
    Record(0x7d5c504, 0x11b, 0, 0, 0, 400000, 0);
    Client::Scenes::AnswerTheStatueDialog(8, Context());
    m_data.SetU32(Client::UI::SelectedStatueAt, 0xffffffff);
    Client::Scenes::AnswerTheStatueDialog(8, Context());
    const auto sent = Sent();
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_TRUE(SameBytes(sent[0], Bytes({0xc1, 0x0c, 0xb2, 6, 0x1b, 1, 0, 0, 1, 0, 0, 0})));
    EXPECT_TRUE(SameBytes(sent[1], Bytes({0xc1, 0x0c, 0xb2, 5, 0x1b, 1, 0, 0, 0, 0, 0, 0})));
}

// Una elegida cuyo registro sale de la zona no manda nada (Q-C1360).
TEST_F(CastleSeniorAnswersTest, ASelectionOutsideTheZoneSendsNothing)
{
    m_data.SetU32(Client::UI::SelectedGateAt, 8);
    Client::Scenes::AnswerTheGateDialog(8, Context());
    Client::Scenes::AnswerTheGateLifeDialog(8, Context());
    EXPECT_TRUE(m_sent.empty());
    EXPECT_TRUE(Warned("Q-C1360"));
}

// 0x660da0 y 0x660d20, las dos tablas de la vida.
TEST(CastleLifeLevelsTest, LevelsAndMaxLivesFollowTheTables)
{
    using Client::UI::LifeLevelOf;
    using Client::UI::MaxLifeOfLevel;
    EXPECT_EQ(LifeLevelOf(0x115, 500000), 0);
    EXPECT_EQ(LifeLevelOf(0x115, 500001), 1);
    EXPECT_EQ(LifeLevelOf(0x115, 800000), 1);
    EXPECT_EQ(LifeLevelOf(0x115, 1500000), 2);
    EXPECT_EQ(LifeLevelOf(0x115, 1000000), 3);
    EXPECT_EQ(LifeLevelOf(0x115, -5), 0);
    EXPECT_EQ(LifeLevelOf(0x11b, 400000), 0);
    EXPECT_EQ(LifeLevelOf(0x11b, 750000), 1);
    EXPECT_EQ(LifeLevelOf(0x11b, 1300000), 2);
    EXPECT_EQ(LifeLevelOf(0x11b, 1300001), 3);
    EXPECT_EQ(LifeLevelOf(0x120, 1), -1);
    EXPECT_EQ(MaxLifeOfLevel(0x115, 0), 500000);
    EXPECT_EQ(MaxLifeOfLevel(0x115, 1), 800000);
    EXPECT_EQ(MaxLifeOfLevel(0x115, 2), 1500000);
    EXPECT_EQ(MaxLifeOfLevel(0x115, 3), 3000000);
    EXPECT_EQ(MaxLifeOfLevel(0x115, -1), 3000000);
    EXPECT_EQ(MaxLifeOfLevel(0x11b, 0), 400000);
    EXPECT_EQ(MaxLifeOfLevel(0x11b, 1), 750000);
    EXPECT_EQ(MaxLifeOfLevel(0x11b, 2), 1300000);
    EXPECT_EQ(MaxLifeOfLevel(0x11b, 4), 2500000);
    EXPECT_EQ(MaxLifeOfLevel(0x99, 0), -1);
}

// 0x662d50 y 0x664ce0: el B2 07 con 3 y la vida máxima del nivel siguiente.
TEST_F(CastleSeniorAnswersTest, TheLifeAnswersSendTheNextLevel)
{
    Record(0x7d5c590, 0x115, 1, 0, 0, 500000, 1);
    Client::Scenes::AnswerTheGateLifeDialog(8, Context());
    Record(0x7d5c590, 0x115, 1, 0, 0, 1500000, 1);
    Client::Scenes::AnswerTheGateLifeDialog(8, Context());
    Record(0x7d5c520, 0x11b, 1, 0, 0, 750000, 1);
    Client::Scenes::AnswerTheStatueLifeDialog(8, Context());
    Client::Scenes::AnswerTheStatueLifeDialog(2, Context());
    // Otro tipo: el nivel −1 + 1 y 0x660d20(tipo, 0) da −1.
    Record(0x7d5c520, 0x120, 1, 0, 0, 750000, 1);
    Client::Scenes::AnswerTheStatueLifeDialog(8, Context());
    const auto sent = Sent();
    ASSERT_EQ(sent.size(), 4u);
    EXPECT_TRUE(SameBytes(sent[3],
                          Bytes({0xc1, 0x14, 0xb2, 7, 0x20, 1, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 0xff, 0xff, 0xff, 0xff})));
    // 800000 = 0x0c3500, 3000000 = 0x2dc6c0, 1300000 = 0x13d620.
    EXPECT_TRUE(
        SameBytes(sent[0], Bytes({0xc1, 0x14, 0xb2, 7, 0x15, 1, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 0x00, 0x35, 0x0c, 0})));
    EXPECT_TRUE(
        SameBytes(sent[1], Bytes({0xc1, 0x14, 0xb2, 7, 0x15, 1, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 0xc0, 0xc6, 0x2d, 0})));
    EXPECT_TRUE(
        SameBytes(sent[2], Bytes({0xc1, 0x14, 0xb2, 7, 0x1b, 1, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 0x20, 0xd6, 0x13, 0})));
}

// 0x663440, 0x6653d0 y 0x665ab0: la defensa + 1 (kind 1) y la recuperación + 1 (kind 2; una puerta da 0 + 1).
TEST_F(CastleSeniorAnswersTest, TheDefenseAndRecoveryAnswersSendTheNextLevel)
{
    Record(0x7d5c590, 0x115, 1, 2, 0, 0, 1);
    Client::Scenes::AnswerTheGateDefenseDialog(8, Context());
    Record(0x7d5c520, 0x11b, 3, 0, 4, 0, 1);
    Client::Scenes::AnswerTheStatueDefenseDialog(8, Context());
    Client::Scenes::AnswerTheStatueRecoveryDialog(8, Context());
    Record(0x7d5c520, 0x115, 3, 0, 4, 0, 1);
    Client::Scenes::AnswerTheStatueRecoveryDialog(8, Context());
    Record(0x7d5c520, 0x120, 3, 5, 4, 0, 1);
    Client::Scenes::AnswerTheStatueDefenseDialog(8, Context());
    const auto sent = Sent();
    ASSERT_EQ(sent.size(), 5u);
    EXPECT_TRUE(SameBytes(sent[0], Bytes({0xc1, 0x14, 0xb2, 7, 0x15, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0})));
    EXPECT_TRUE(SameBytes(sent[1], Bytes({0xc1, 0x14, 0xb2, 7, 0x1b, 1, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0})));
    EXPECT_TRUE(SameBytes(sent[2], Bytes({0xc1, 0x14, 0xb2, 7, 0x1b, 1, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 5, 0, 0, 0})));
    EXPECT_TRUE(SameBytes(sent[3], Bytes({0xc1, 0x14, 0xb2, 7, 0x15, 1, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0})));
    EXPECT_TRUE(SameBytes(sent[4], Bytes({0xc1, 0x14, 0xb2, 7, 0x20, 1, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0})));
}

// 0x666b30: los dos impuestos que muestra la ventana, en dos B2 09 con el byte de arriba primero.
TEST_F(CastleSeniorAnswersTest, TheTaxAnswerSendsBothTaxes)
{
    m_data.SetU32(Client::UI::ChaosTaxAt, 3);
    m_data.SetU32(Client::UI::StoreTaxAt, 0x01020304);
    Client::Scenes::AnswerTheTaxDialog(1, Context());
    Client::Scenes::AnswerTheTaxDialog(8, Context());
    const auto sent = Sent();
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_TRUE(SameBytes(sent[0], Bytes({0xc1, 9, 0xb2, 9, 1, 0, 0, 0, 3})));
    EXPECT_TRUE(SameBytes(sent[1], Bytes({0xc1, 9, 0xb2, 9, 2, 1, 2, 3, 4})));
}

// 0x667a40: con la respuesta 2, sscanf("%u") del campo; sin número, el original manda basura de la pila (Q-C1363).
TEST_F(CastleSeniorAnswersTest, TheMoneyAnswerScansTheField)
{
    Client::Scenes::AnswerTheMoneyDialog(8, "5", Context());
    Client::Scenes::AnswerTheMoneyDialog(2, "  1000000zen", Context());
    Client::Scenes::AnswerTheMoneyDialog(2, "-1", Context());
    Client::Scenes::AnswerTheMoneyDialog(2, "4294967297", Context());
    Client::Scenes::AnswerTheMoneyDialog(2, "zen", Context());
    Client::Scenes::AnswerTheMoneyDialog(2, "", Context());
    const auto sent = Sent();
    ASSERT_EQ(sent.size(), 3u);
    EXPECT_TRUE(SameBytes(sent[0], Bytes({0xc1, 8, 0xb2, 0x10, 0x40, 0x42, 0x0f, 0})));
    EXPECT_TRUE(SameBytes(sent[1], Bytes({0xc1, 8, 0xb2, 0x10, 0xff, 0xff, 0xff, 0xff})));
    EXPECT_TRUE(SameBytes(sent[2], Bytes({0xc1, 8, 0xb2, 0x10, 1, 0, 0, 0})));
    EXPECT_TRUE(Warned("Q-C1363"));
}
} // namespace
