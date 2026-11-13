#include "Scenes/GameSocialSteps.h"

#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/PanelRegistry.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Scenes/GameSocialSteps (CL9 hito 7): los botones de la ventana del comercio (0x5ecde0) y el pedido de duelo
// (0x5ebca0), contra su ASM. Los nombres son inventados.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 1;

struct Sent
{
    bool encrypted = false;
    std::vector<std::uint8_t> bytes;
};

class RecordingRequests final : public Client::Scenes::IGameUiRequests
{
public:
    void SendOptionData(const Client::Protocol::OptionData::OptionBody& /*body*/) override {}
    void RequestLogout(std::uint8_t /*kind*/) override {}
    void RequestPartyList() override {}
    void SendGuildPacket(Client::Protocol::Outgoing /*packet*/) override {}
    void SendFriendPacket(Client::Protocol::Outgoing /*packet*/) override {}
    void SendGamePacket(Client::Protocol::Outgoing outgoing) override
    {
        const Client::Network::Packet plain = Client::Network::Crypto::Xor32Decode(outgoing.packet);
        Sent packet{.encrypted = outgoing.delivery == Client::Network::Delivery::Encrypted, .bytes = {}};
        for (const std::byte b : plain.Bytes())
            packet.bytes.push_back(std::to_integer<std::uint8_t>(b));
        sent.push_back(std::move(packet));
    }
    Client::Core::Expected<void, std::string> SaveMacros(std::string_view /*content*/) override
    {
        return {};
    }
    void Log(std::string_view /*line*/) override {}

    std::vector<Sent> sent;
};

class GameSocialStepsTest : public ::testing::Test
{
protected:
    GameSocialStepsTest()
    {
        Runtime().panels.trade = 1;
        Runtime().hud.tradeX = 0x104;
        Runtime().hud.tradeY = 0;
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    GameUiContext Context(std::int32_t x, std::int32_t y)
    {
        return GameUiContext{.world = *m_world,
                             .random = m_random,
                             .input = m_input,
                             .mouseX = x,
                             .mouseY = y,
                             .texts = &m_texts,
                             .requests = &m_requests};
    }

    Client::Core::Expected<void, std::string> StepTheTrade(std::int32_t x, std::int32_t y, bool click = true)
    {
        Runtime().movement.pendingClick = click;
        Client::Scenes::GamePanels panels(*m_world, m_random);
        return Client::Scenes::StepTheTradeWindow(Context(x, y), panels.Registry());
    }

    Client::Core::Expected<void, std::string> StepTheDuel(std::int32_t x, std::int32_t y, bool click = true)
    {
        Runtime().movement.pendingClick = click;
        return Client::Scenes::StepTheDuelRequest(Context(x, y));
    }

    // Los rand() desde Seed.
    int Draws() const
    {
        CrtRandom reference(Seed);
        for (int draws = 0; draws <= 100; ++draws)
        {
            if (reference.State() == m_random.State())
                return draws;
            static_cast<void>(reference.Next());
        }
        return -1;
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    CrtRandom m_random{Seed};
    Client::Input::InputState m_input;
    RecordingRequests m_requests;
    // 0x173..0x176, el aviso del OK con la grilla del otro cambiada (inventados).
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(
        {{0x173, "el otro cambio"}, {0x174, "sus objetos"}, {0x175, "mira bien"}, {0x176, "antes del OK"}});
};

// Con el comercio cerrado, solo su IsOpen (el rand() del byte protegido).
TEST_F(GameSocialStepsTest, AClosedTradeOnlyDrawsItsQuery)
{
    Runtime().panels.trade = 0;
    ASSERT_TRUE(StepTheTrade(0x104 + 0x61, 0x186).HasValue());
    EXPECT_EQ(Draws(), 1);
    EXPECT_TRUE(m_requests.sent.empty());
    EXPECT_TRUE(Runtime().movement.pendingClick);
}

// 0x5ecf2d..0x5ed476: el OK da vuelta el propio, prende 0x78abaa5 y manda el C1 04 3C con el nuevo en C3.
TEST_F(GameSocialStepsTest, TheOkButtonTogglesTheOwnOk)
{
    ASSERT_TRUE(StepTheTrade(0x104 + 0x61, 0x186).HasValue());
    const Client::Scenes::TradeState& trade = Runtime().social.trade;
    EXPECT_EQ(trade.ownOk, 1);
    EXPECT_EQ(trade.changed, 1);
    ASSERT_EQ(m_requests.sent.size(), 1u);
    EXPECT_TRUE(m_requests.sent[0].encrypted);
    EXPECT_EQ(m_requests.sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x3c, 0x01}));
    EXPECT_EQ(Draws(), 1 + 2);
    EXPECT_FALSE(Runtime().movement.pendingClick);
    EXPECT_EQ(Runtime().lastSoundId, 0x19);

    // Con 0x78abaa5 prendido y el OK propio en 1, lo apaga.
    ASSERT_TRUE(StepTheTrade(0x104 + 0x61 + 0x17, 0x186 + 0x17).HasValue());
    EXPECT_EQ(trade.ownOk, 0);
    ASSERT_EQ(m_requests.sent.size(), 2u);
    EXPECT_EQ(m_requests.sent[1].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x3c, 0x00}));
}

// 0x5ecf57..0x5ecf80: con la grilla del otro cambiada (0x78abaa5) y el OK propio apagado, el aviso 0x633300(4, 0x173,
// 0x94) y nada sale.
TEST_F(GameSocialStepsTest, AChangedGridWarnsInsteadOfOk)
{
    Runtime().social.trade.changed = 1;
    ASSERT_TRUE(StepTheTrade(0x104 + 0x61, 0x186).HasValue());
    EXPECT_TRUE(m_requests.sent.empty());
    EXPECT_EQ(Runtime().social.trade.ownOk, 0);
    EXPECT_EQ(Runtime().social.trade.warningKind, 3);
    EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);
    EXPECT_EQ(Runtime().dialogs.current, 0x94);
}

// 0x5ece9a..0x5ecf1b: la espera del OK baja de a uno y, mientras no llega a 0, el OK no se mira; un ítem en el cursor
// tampoco lo deja.
TEST_F(GameSocialStepsTest, TheOkWaitsAndSkipsTheCursorItem)
{
    Runtime().social.trade.okWait = 2;
    ASSERT_TRUE(StepTheTrade(0x104 + 0x61, 0x186).HasValue());
    EXPECT_EQ(Runtime().social.trade.okWait, 1);
    EXPECT_TRUE(m_requests.sent.empty());
    EXPECT_TRUE(Runtime().movement.pendingClick);
    ASSERT_TRUE(StepTheTrade(0x104 + 0x61, 0x186).HasValue());
    EXPECT_EQ(Runtime().social.trade.okWait, 0);
    EXPECT_EQ(m_requests.sent.size(), 1u);

    Runtime().cursorItemCount = 1;
    ASSERT_TRUE(StepTheTrade(0x104 + 0x61, 0x186).HasValue());
    EXPECT_EQ(m_requests.sent.size(), 1u);
}

// 0x5ece4c..0x5eceae: el zen abre el diálogo 0x73 con su campo.
TEST_F(GameSocialStepsTest, TheMoneyButtonOpensTheMoneyDialog)
{
    Runtime().ui.chatOpen = true;
    ASSERT_TRUE(StepTheTrade(0x104 + 0x1a, 0x186).HasValue());
    EXPECT_EQ(Runtime().dialogs.current, 0x73);
    EXPECT_EQ(Runtime().social.dialogFieldLimit, 8);
    EXPECT_EQ(Runtime().chat.input.fieldCount, 1);
    EXPECT_FALSE(Runtime().ui.chatOpen);
    EXPECT_EQ(Runtime().social.moneyField, 1);
    EXPECT_EQ(Runtime().social.typedMoney, 0);
    EXPECT_EQ(Runtime().social.trade.field78abacc, 2);
    EXPECT_EQ(Runtime().lastSoundId, 0x19);
    EXPECT_FALSE(Runtime().movement.pendingClick);
    EXPECT_TRUE(m_requests.sent.empty());
}

// 0x5ed4cc..0x5ed936: cancelar apaga 0x78abaa5 y manda el C1 03 3D en C3; la ventana la cierra el 3D del GS.
TEST_F(GameSocialStepsTest, TheCancelButtonSendsTheCancel)
{
    Runtime().social.trade.changed = 1;
    ASSERT_TRUE(StepTheTrade(0x104 + 0x89, 0x186).HasValue());
    EXPECT_EQ(Runtime().social.trade.changed, 0);
    ASSERT_EQ(m_requests.sent.size(), 1u);
    EXPECT_TRUE(m_requests.sent[0].encrypted);
    EXPECT_EQ(m_requests.sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x03, 0x3d}));
    EXPECT_EQ(Draws(), 1 + 2);
    EXPECT_EQ(Runtime().movement.mouseUpdateTimeMax, 6);
    EXPECT_EQ(Runtime().panels.trade, 1);
}

// 0x5ebca0: sin el pedido nada; con él, el mouse sobre la caja es de la UI, y sí o no mandan el AC con la clave y el
// nombre del rival en C3 y bajan el pedido, sin gastar el clic.
TEST_F(GameSocialStepsTest, TheDuelRequestAnswersYesOrNo)
{
    ASSERT_TRUE(StepTheDuel(0xed, 0xfa).HasValue());
    EXPECT_TRUE(m_requests.sent.empty());

    Client::Scenes::DuelState& duel = Runtime().social.duel;
    duel.rivalKey = 0x0102;
    const std::string_view rival = "Brezal";
    for (std::size_t at = 0; at < rival.size(); ++at)
        duel.rivalName[at] = static_cast<std::byte>(rival[at]);
    Runtime().ui.field78abad9 = 1;
    ASSERT_TRUE(StepTheDuel(0x100, 0x80, false).HasValue());
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
    EXPECT_TRUE(m_requests.sent.empty());

    ASSERT_TRUE(StepTheDuel(0xed, 0xfa).HasValue());
    ASSERT_EQ(m_requests.sent.size(), 1u);
    EXPECT_TRUE(m_requests.sent[0].encrypted);
    EXPECT_EQ(m_requests.sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x10, 0xac, 0x01, 0x01, 0x02, 'B', 'r', 'e',
                                                                   'z', 'a', 'l', 0, 0, 0, 0}));
    EXPECT_EQ(Runtime().ui.field78abad9, 0);
    EXPECT_TRUE(Runtime().movement.pendingClick);
    EXPECT_EQ(Draws(), 2);

    Runtime().ui.field78abad9 = 1;
    ASSERT_TRUE(StepTheDuel(0x191, 0x10e).HasValue());
    ASSERT_EQ(m_requests.sent.size(), 2u);
    EXPECT_EQ(m_requests.sent[1].bytes[3], 0x00);
    EXPECT_EQ(Runtime().ui.field78abad9, 0);
    EXPECT_EQ(Draws(), 4);
}
} // namespace
