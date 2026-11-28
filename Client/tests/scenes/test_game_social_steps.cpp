#include "Network/Xor32Packet.h"
#include "Social/GameSocialSteps.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Player/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Hud/GamePanels.h"
#include "Ui/GameUiContext.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Ui/PanelRegistry.h"
#include "Audio/SoundId.h"
#include "support/SceneDataFiles.h"
#include "Ui/GameUiState.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Social/GameSocialSteps (CL9 milestone 7): the trade window's buttons (0x5ecde0) and the duel request (0x5ebca0),
// against their ASM. The names are made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Ui::GameUiContext;

constexpr std::uint32_t Seed = 1;

struct Sent
{
    bool encrypted = false;
    std::vector<std::uint8_t> bytes;
};

class RecordingRequests final : public Client::Ui::IGameUiRequests
{
public:
    void SendOptionData(const Client::Protocol::Player::OptionBody& /*body*/) override {}
    void RequestLogout(std::uint8_t /*kind*/) override {}
    void RequestPartyList() override {}
    void SendGuildPacket(Client::Protocol::Outgoing packet) override
    {
        guild.push_back(Plain(packet));
    }
    void SendGamePacket(Client::Protocol::Outgoing outgoing) override
    {
        sent.push_back(Plain(outgoing));
    }
    void SendWindowClose() override {}
    void SendSetQuest(std::uint8_t /*quest*/, std::uint8_t /*state*/) override {}
    void SendFriendPacket(Client::Protocol::Outgoing /*packet*/) override {}
    Client::Core::Expected<void, std::string> SaveMacros(std::string_view /*content*/) override
    {
        return {};
    }
    void Log(std::string_view /*line*/) override {}

    static Sent Plain(const Client::Protocol::Outgoing& outgoing)
    {
        const Client::Network::Packet plain = Client::Network::Xor32Decode(outgoing.packet);
        Sent packet{.encrypted = outgoing.delivery == Client::Network::Delivery::Encrypted, .bytes = {}};
        for (const std::byte b : plain.Bytes())
            packet.bytes.push_back(std::to_integer<std::uint8_t>(b));
        return packet;
    }

    std::vector<Sent> sent;
    std::vector<Sent> guild;
};

constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;

class GameSocialStepsTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    GameSocialStepsTest()
    {
        Runtime().map = GetParam();
        Runtime().panels.trade = 1;
        Runtime().hud.tradeX = 0x104;
        Runtime().hud.tradeY = 0;
    }

    Client::Kernel::WorldRuntime& Runtime()
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
        Runtime().mouse.pendingClick = click;
        Client::Hud::GamePanels panels(*m_world, m_random);
        return Client::Social::StepTheTradeWindow(Context(x, y), panels.Registry());
    }

    Client::Core::Expected<void, std::string> StepTheDuel(std::int32_t x, std::int32_t y, bool click = true)
    {
        Runtime().mouse.pendingClick = click;
        return Client::Social::StepTheDuelRequest(Context(x, y));
    }

    // The rand() calls since Seed.
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
    // 0x173..0x176, the OK warning with the other's grid changed (made up).
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable(
        {{0x173, "el otro cambio"}, {0x174, "sus objetos"}, {0x175, "mira bien"}, {0x176, "antes del OK"}});
};

// Only its IsOpen (the protected byte's rand()).
TEST_P(GameSocialStepsTest, AClosedTradeOnlyDrawsItsQuery)
{
    Runtime().panels.trade = 0;
    ASSERT_TRUE(StepTheTrade(0x104 + 0x61, 0x186).HasValue());
    EXPECT_EQ(Draws(), 1);
    EXPECT_TRUE(m_requests.sent.empty());
    EXPECT_TRUE(Runtime().mouse.pendingClick);
}

// 0x5ecf2d..0x5ed476: the OK flips the own one, sets 0x78abaa5 and sends the C1 04 3C with the new one in C3.
TEST_P(GameSocialStepsTest, TheOkButtonTogglesTheOwnOk)
{
    ASSERT_TRUE(StepTheTrade(0x104 + 0x61, 0x186).HasValue());
    const Client::Social::TradeState& trade = Runtime().social.trade;
    EXPECT_EQ(trade.ownOk, 1);
    EXPECT_EQ(trade.changed, 1);
    ASSERT_EQ(m_requests.sent.size(), 1u);
    EXPECT_TRUE(m_requests.sent[0].encrypted);
    EXPECT_EQ(m_requests.sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x3c, 0x01}));
    EXPECT_EQ(Draws(), 1 + 2);
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x19});

    ASSERT_TRUE(StepTheTrade(0x104 + 0x61 + 0x17, 0x186 + 0x17).HasValue());
    EXPECT_EQ(trade.ownOk, 0);
    ASSERT_EQ(m_requests.sent.size(), 2u);
    EXPECT_EQ(m_requests.sent[1].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x3c, 0x00}));
}

// 0x5ecf57..0x5ecf80: with the other's grid changed (0x78abaa5) and the own OK off, the warning 0x633300(4, 0x173,
// 0x94) and nothing goes out.
TEST_P(GameSocialStepsTest, AChangedGridWarnsInsteadOfOk)
{
    Runtime().social.trade.changed = 1;
    ASSERT_TRUE(StepTheTrade(0x104 + 0x61, 0x186).HasValue());
    EXPECT_TRUE(m_requests.sent.empty());
    EXPECT_EQ(Runtime().social.trade.ownOk, 0);
    EXPECT_EQ(Runtime().social.trade.warningKind, 3);
    EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);
    EXPECT_EQ(Runtime().dialogs.current, 0x94);
}

// 0x5ece9a..0x5ecf1b: the OK wait drops by one and, until it reaches 0, the OK is not checked; an item on the cursor
// blocks it too.
TEST_P(GameSocialStepsTest, TheOkWaitsAndSkipsTheCursorItem)
{
    Runtime().social.trade.okWait = 2;
    ASSERT_TRUE(StepTheTrade(0x104 + 0x61, 0x186).HasValue());
    EXPECT_EQ(Runtime().social.trade.okWait, 1);
    EXPECT_TRUE(m_requests.sent.empty());
    EXPECT_TRUE(Runtime().mouse.pendingClick);
    ASSERT_TRUE(StepTheTrade(0x104 + 0x61, 0x186).HasValue());
    EXPECT_EQ(Runtime().social.trade.okWait, 0);
    EXPECT_EQ(m_requests.sent.size(), 1u);

    Runtime().cursorItemCount = 1;
    ASSERT_TRUE(StepTheTrade(0x104 + 0x61, 0x186).HasValue());
    EXPECT_EQ(m_requests.sent.size(), 1u);
}

// 0x5ece4c..0x5eceae: dialog 0x73 opens with its field.
TEST_P(GameSocialStepsTest, TheMoneyButtonOpensTheMoneyDialog)
{
    Runtime().ui.chatOpen = true;
    ASSERT_TRUE(StepTheTrade(0x104 + 0x1a, 0x186).HasValue());
    EXPECT_EQ(Runtime().dialogs.current, 0x73);
    EXPECT_EQ(Runtime().social.dialogFieldLimit, 8);
    EXPECT_EQ(Runtime().chatInput.fieldCount, 1);
    EXPECT_FALSE(Runtime().ui.chatOpen);
    EXPECT_EQ(Runtime().social.moneyField, 1);
    EXPECT_EQ(Runtime().social.typedMoney, 0);
    EXPECT_EQ(Runtime().social.trade.field78abacc, 2);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x19});
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_TRUE(m_requests.sent.empty());
}

// 0x5ed4cc..0x5ed936: cancel clears 0x78abaa5 and sends the C1 03 3D in C3; the GS's 3D closes the window.
TEST_P(GameSocialStepsTest, TheCancelButtonSendsTheCancel)
{
    Runtime().social.trade.changed = 1;
    ASSERT_TRUE(StepTheTrade(0x104 + 0x89, 0x186).HasValue());
    EXPECT_EQ(Runtime().social.trade.changed, 0);
    ASSERT_EQ(m_requests.sent.size(), 1u);
    EXPECT_TRUE(m_requests.sent[0].encrypted);
    EXPECT_EQ(m_requests.sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x03, 0x3d}));
    EXPECT_EQ(Draws(), 1 + 2);
    EXPECT_EQ(Runtime().mouse.updateTimeMax, 6);
    EXPECT_EQ(Runtime().panels.trade, 1);
}

// 0x5ebca0: without the request nothing; with it, the mouse over the box belongs to the UI, and yes or no send the AC
// with the rival's key and name in C3 and lower the request, without spending the click.
TEST_P(GameSocialStepsTest, TheDuelRequestAnswersYesOrNo)
{
    ASSERT_TRUE(StepTheDuel(0xed, 0xfa).HasValue());
    EXPECT_TRUE(m_requests.sent.empty());

    Client::Social::DuelState& duel = Runtime().social.duel;
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
    EXPECT_TRUE(Runtime().mouse.pendingClick);
    EXPECT_EQ(Draws(), 2);

    Runtime().ui.field78abad9 = 1;
    ASSERT_TRUE(StepTheDuel(0x191, 0x10e).HasValue());
    ASSERT_EQ(m_requests.sent.size(), 2u);
    EXPECT_EQ(m_requests.sent[1].bytes[3], 0x00);
    EXPECT_EQ(Runtime().ui.field78abad9, 0);
    EXPECT_EQ(Draws(), 4);
}

// 0x65c039..0x65c946 (CL9 milestone 7): with the lent session (LendTheSession), a Dark Lord opening the inventory, as
// the trade and the shops open it, sends the two pet A9 in clear: the left hand's raven and the horse from its slot.
TEST_P(GameSocialStepsTest, ADarkLordOpeningTheInventoryAsksForBothPets)
{
    Runtime().panels.trade = 0;
    m_world->Hero().characterClass = 4;
    Client::Hud::GamePanels panels(*m_world, m_random);
    panels.LendTheSession(*m_world, m_random, &m_requests);
    const auto opened = panels.Registry().Open(Client::Ui::InventoryPanel);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_TRUE(opened.Value());
    ASSERT_EQ(m_requests.sent.size(), 2u);
    EXPECT_FALSE(m_requests.sent[0].encrypted);
    EXPECT_EQ(m_requests.sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x06, 0xa9, 0x00, 0x00, 0x01}));
    EXPECT_FALSE(m_requests.sent[1].encrypted);
    EXPECT_EQ(m_requests.sent[1].bytes, (std::vector<std::uint8_t>{0xc1, 0x06, 0xa9, 0x01, 0x00, 0x08}));
}

// 0x657b50 and 0x657b73 (CL9 milestone 8): with the lent session (LendTheSession), closing the two guild windows calls
// their closes: the first (0x6522f0) without sends and the second (0x6570b0) with the C1 04 54 00 in clear through
// SendGuildPacket, and sound 0x1c.
TEST_P(GameSocialStepsTest, TheSessionClosesBothGuildWindows)
{
    Client::Ui::DelegatedPanels& delegated = Runtime().panels.delegated;
    delegated.guild = 1;
    delegated.secondGuild = 1;
    Client::Hud::GamePanels panels(*m_world, m_random);
    panels.LendTheSession(*m_world, m_random, &m_requests);

    const auto first = panels.Registry().Close(Client::Ui::GuildPanel);
    ASSERT_TRUE(first.HasValue()) << first.Error();
    EXPECT_EQ(delegated.guild, 0);
    EXPECT_TRUE(m_requests.guild.empty());
    const auto second = panels.Registry().Close(Client::Ui::SecondGuildPanel);
    ASSERT_TRUE(second.HasValue()) << second.Error();
    EXPECT_EQ(delegated.secondGuild, 0);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
    ASSERT_EQ(m_requests.guild.size(), 1u);
    EXPECT_FALSE(m_requests.guild[0].encrypted);
    EXPECT_EQ(m_requests.guild[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x54, 0x00}));
    EXPECT_TRUE(m_requests.sent.empty());
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, GameSocialStepsTest, ::testing::Values(Lorencia, Devias));
} // namespace
