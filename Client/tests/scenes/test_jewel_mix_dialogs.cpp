#include "Network/Xor32Packet.h"
#include "Kernel/MouseState.h"
#include "Hud/Dialogs/GameDialogSteps.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Input/InputState.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Hud/GameUi.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/MessageArea.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"
#include "Social/SocialState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// The chaos box mode (0x8e) and the jewel mix (0xa1, 0xa2, 0xa3 and 0xa6) in the step of 0x623e70, with the BC and the
// 31 byte by byte (CL9 milestone 6, phase 3b). None draws rand(). Made-up texts; the world in Lorencia (0), Devias (2)
// and Noria (3), the map of the jewels NPC.
namespace
{
using Client::Crt::CrtRandom;
using Client::Ui::GameUiContext;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
using Client::Network::Xor32Decode;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

constexpr std::array<std::int32_t, 3> Maps{0, 2, 3};
constexpr std::uint16_t BlessJewel = 0x1c0d;
constexpr std::uint16_t SoulJewel = 0x1c0e;
constexpr std::uint16_t SoulBundle = 0x181f;
constexpr std::uint16_t EmptyItem = 0xffff;
// Inside the buttons (with the original's edges).
constexpr std::int32_t MixButtonX = 300;
constexpr std::int32_t MixY = 190;
constexpr std::int32_t UnmixY = 255;
constexpr std::int32_t MenuCloseX = 279;
constexpr std::int32_t MenuCloseY = 345;
constexpr std::int32_t BlessX = 245;
constexpr std::int32_t SoulX = 405;
constexpr std::int32_t KindY = 185;
constexpr std::int32_t TenY = 225;
constexpr std::int32_t TwentyY = 270;
constexpr std::int32_t ThirtyY = 350;
constexpr std::int32_t ChoiceCloseY = 390;
// 0x6282dc with the 0xa3: the buttons (0x15 and 0x78, 0x5a) shifted (0xd5, 0xd2).
constexpr std::int32_t YesX = 0x15 + 0xd5;
constexpr std::int32_t NoX = 0x78 + 0xd5;
constexpr std::int32_t AnswerY = 0x5a + 0xd2;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

class JewelMixDialogs
{
public:
    explicit JewelMixDialogs(std::int32_t map)
    {
        world->Runtime().map = map;
        for (ItemState& cell : world->Inventory().cells)
            cell.id = EmptyItem;
    }

    Client::Core::Expected<void, std::string> Click(std::int32_t x, std::int32_t y)
    {
        Client::Kernel::LatchTheLeftButton(world->Runtime().mouse, true, true);
        return Client::Hud::StepTheGameDialogs(GameUiContext{.world = *world,
                                                             .random = random,
                                                             .input = input,
                                                             .mouseX = x,
                                                             .mouseY = y,
                                                             .measure = &measure,
                                                             .texts = &texts,
                                                             .sendNpcWindow = [this](Client::Protocol::Outgoing packet)
                                                             {
                                                                 sent.push_back(std::move(packet));
                                                                 return Client::Core::Expected<void, std::string>{};
                                                             }});
    }

    void Fill(std::uint16_t id, std::size_t from, std::size_t count)
    {
        for (std::size_t cell = from; cell < from + count; ++cell)
            world->Inventory().cells[cell].id = id;
    }

    [[nodiscard]] std::vector<std::string> Errors() const
    {
        std::vector<std::string> errors;
        for (const Client::Ui::MessageAreaRow& row : world->Runtime().chat.area.rows)
        {
            if (row.type == 2 && !row.text.empty())
                errors.push_back(row.text);
        }
        return errors;
    }

    [[nodiscard]] Client::Kernel::WorldRuntime& Runtime()
    {
        return world->Runtime();
    }
    [[nodiscard]] Client::Npc::JewelMixState& Jewels()
    {
        return world->Runtime().npcWindows.jewels;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x1357u};
    Client::Input::InputState input;
    LetterMeasure measure;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{1806, "Joya Uno"},
                                                                    {1807, "Joya Dos"},
                                                                    {1809, "Juntar %s y %d?"},
                                                                    {1810, "Cuesta %d zen"},
                                                                    {1813, "Deshacer %s %d?"},
                                                                    {1814, "Deshacer cuesta %d"},
                                                                    {1817, "faltan joyas"},
                                                                    {1818, "no se puede deshacer"}});
    std::vector<Client::Protocol::Outgoing> sent;
};

// 0x625910..0x6259c5, in Lorencia, Devias and Noria: the click on a button leaves its number at 0x7d3a114 and closes,
// without spending the click or sounding; between the buttons, nothing. The step looks at y from 180 (the drawing,
// from 190: Q-C945).
TEST(JewelMixDialogsTest, TheChaosMixModeButtonsLeaveTheModeAndClose)
{
    for (const std::int32_t map : Maps)
    {
        JewelMixDialogs tested(map);
        auto& runtime = tested.Runtime();
        const CrtRandom before = tested.random;
        runtime.dialogs.current = 0x8e;
        ASSERT_TRUE(tested.Click(MixButtonX, 240).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0x8e);
        ASSERT_TRUE(tested.Click(MixButtonX, 180 + 85).HasValue());
        EXPECT_EQ(runtime.npcWindows.chaosMixMode, 1);
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_TRUE(runtime.mouse.pendingClick);
        EXPECT_FALSE(runtime.lastSoundId.has_value());
        runtime.dialogs.current = 0x8e;
        runtime.dialogs.pending = 0x94;
        ASSERT_TRUE(tested.Click(395, 180).HasValue());
        EXPECT_EQ(runtime.npcWindows.chaosMixMode, 0);
        EXPECT_EQ(runtime.dialogs.current, 0x94);
        EXPECT_EQ(runtime.dialogs.pending, 0);
        EXPECT_EQ(tested.random.State(), before.State()) << map;
        EXPECT_TRUE(tested.sent.empty());
    }
}

// 0x6259c6..0x625b22: the first opens the 0xa2 with 0x6e5794 at 0 and the second the 0xa4 with 1, with the click off
// and MouseUpdateTime at 0 and its maximum at 6; the close (0x402eb0) puts everything back to its start and sends the
// C1 03 31 in clear.
TEST(JewelMixDialogsTest, TheMenuChoosesMixingOrUnmixingAndTheCloseSends31)
{
    for (const std::int32_t map : Maps)
    {
        JewelMixDialogs tested(map);
        auto& runtime = tested.Runtime();
        const CrtRandom before = tested.random;
        runtime.dialogs.current = 0xa1;
        runtime.mouse.updateTime = 3;
        ASSERT_TRUE(tested.Click(MixButtonX, MixY).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0xa2);
        EXPECT_EQ(tested.Jewels().mode, 0);
        EXPECT_FALSE(runtime.mouse.pendingClick);
        EXPECT_EQ(runtime.mouse.updateTime, 0);
        EXPECT_EQ(runtime.mouse.updateTimeMax, 6);
        runtime.dialogs.current = 0xa1;
        ASSERT_TRUE(tested.Click(395, UnmixY + 35).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0xa4);
        EXPECT_EQ(tested.Jewels().mode, 1);

        runtime.dialogs.current = 0xa1;
        tested.Jewels().kind = 1;
        tested.Jewels().cell = 7;
        runtime.npcWindows.field6e5798 = 1;
        ASSERT_TRUE(tested.Click(MenuCloseX, MenuCloseY).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_EQ(tested.Jewels().mode, 0);
        EXPECT_EQ(tested.Jewels().kind, 0xff);
        EXPECT_EQ(tested.Jewels().count, 0xff);
        EXPECT_EQ(tested.Jewels().cell, -1);
        EXPECT_EQ(tested.Jewels().price, -1);
        EXPECT_EQ(runtime.npcWindows.field6e5798, 0);
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_TRUE(SameBytes(tested.sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
        EXPECT_EQ(tested.random.State(), before.State()) << map;
    }
}

// 0x625b23..0x625cf3 and 0x402bc0: with no jewel chosen the bundles are not looked at; with the soul and 20 in the
// inventory, the 20 one opens the 0xa3 with the question (0x63340d) and the price (20 · 50000); the yes (0x62846f)
// opens the 0xa5 and sends C1 06 BC 00 01 01 in clear, only once.
TEST(JewelMixDialogsTest, ChoosingTheSoulAndTwentyAsksAndTheYesSendsBc00)
{
    for (const std::int32_t map : Maps)
    {
        JewelMixDialogs tested(map);
        auto& runtime = tested.Runtime();
        const CrtRandom before = tested.random;
        tested.Fill(BlessJewel, 0, 5);
        tested.Fill(SoulJewel, 10, 20);
        runtime.dialogs.current = 0xa2;
        ASSERT_TRUE(tested.Click(MixButtonX, TwentyY).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0xa2);
        EXPECT_EQ(tested.Jewels().count, 0xff);
        ASSERT_TRUE(tested.Click(SoulX, KindY + 25).HasValue());
        EXPECT_EQ(tested.Jewels().kind, 1);
        EXPECT_EQ(runtime.dialogs.current, 0xa2);
        ASSERT_TRUE(tested.Click(MixButtonX, TwentyY).HasValue());
        EXPECT_EQ(tested.Jewels().count, 20);
        EXPECT_EQ(tested.Jewels().counted, 20);
        EXPECT_EQ(tested.Jewels().price, 1000000);
        EXPECT_EQ(tested.Jewels().field6e57a0, 100);
        EXPECT_EQ(runtime.dialogs.current, Client::Ui::ServerTimeDialog);
        EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 0), "Juntar Joya Dos y 20?");
        EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 1), "Cuesta 1000000 zen");
        EXPECT_EQ(runtime.dialogBox.lineCount, 2);

        ASSERT_TRUE(tested.Click(YesX, AnswerY).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0xa5);
        EXPECT_EQ(runtime.social.trade.warningByte, 1);
        EXPECT_EQ(runtime.npcWindows.field6e5798, 1);
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_EQ(tested.sent[0].delivery, Client::Network::Delivery::Clear);
        EXPECT_TRUE(SameBytes(Xor32Decode(tested.sent[0].packet).Bytes(), Bytes({0xc1, 0x06, 0xbc, 0x00, 0x01, 0x01})));
        EXPECT_EQ(tested.random.State(), before.State()) << map;
    }
}

// 0x402b70: with 30 chosen and fewer in the inventory, the error notice 1817 and the return to the 0xa2 (0x402e50) with
// the jewel unchosen; the no of the 0xa3 leaves 2 and returns the same; the close of the 0xa2 sends the 31.
TEST(JewelMixDialogsTest, MissingJewelsWarnAndTheNoReturnsToTheChoice)
{
    for (const std::int32_t map : Maps)
    {
        JewelMixDialogs tested(map);
        auto& runtime = tested.Runtime();
        tested.Fill(BlessJewel, 0, 12);
        runtime.dialogs.current = 0xa2;
        tested.Jewels().kind = 0;
        ASSERT_TRUE(tested.Click(MixButtonX, ThirtyY).HasValue());
        EXPECT_EQ(tested.Errors(), std::vector<std::string>{"faltan joyas"});
        EXPECT_EQ(tested.Jewels().refusal, 0);
        EXPECT_EQ(tested.Jewels().kind, 0xff);
        EXPECT_EQ(tested.Jewels().counted, 12);
        EXPECT_EQ(runtime.dialogs.current, 0xa2);

        tested.Jewels().kind = 0;
        ASSERT_TRUE(tested.Click(BlessX, TenY).HasValue());
        EXPECT_EQ(runtime.dialogs.current, Client::Ui::ServerTimeDialog);
        EXPECT_EQ(tested.Jewels().price, 500000);
        ASSERT_TRUE(tested.Click(NoX, AnswerY).HasValue());
        EXPECT_EQ(runtime.social.trade.warningByte, 2);
        EXPECT_EQ(runtime.dialogs.current, 0xa2);
        EXPECT_TRUE(tested.sent.empty());

        ASSERT_TRUE(tested.Click(360, ChoiceCloseY).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0);
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_TRUE(SameBytes(tested.sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
    }
}

// 0x4029e0 and 0x401f30: unmixing the level 2 soul bundle of cell 17 sends C1 07 BC 01 01 02 1D and leaves the cell as
// a slot (+12); the 0xa6 (0x625f98) returns to the 0xa4 with 0x402e50.
TEST(JewelMixDialogsTest, UnmixingSendsBc01WithTheSlotAndTheResultReturns)
{
    for (const std::int32_t map : Maps)
    {
        JewelMixDialogs tested(map);
        auto& runtime = tested.Runtime();
        tested.world->Inventory().cells[17].id = SoulBundle;
        auto& jewels = tested.Jewels();
        jewels.mode = 1;
        jewels.kind = 1;
        jewels.cell = 17;
        jewels.level = 2;
        jewels.counted = 1;
        jewels.price = 1000000;
        // 0x402a90 opens the 0xa3 with 0x633300(0, 0, 0xa3), as the 0xa4's list would.
        const Client::Ui::DialogTextArguments question{
            .jewelMode = 1, .jewelKind = 1, .jewelCount = 1, .jewelLevel = 2, .jewelPrice = 1000000};
        ASSERT_TRUE(Client::Ui::OpenTextDialog(runtime.dialogBox, runtime.dialogs, &tested.texts, 0, 0,
                                               Client::Ui::ServerTimeDialog, nullptr, &question)
                        .HasValue());
        EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 0), "Deshacer Joya Dos 3?");
        ASSERT_TRUE(tested.Click(YesX, AnswerY).HasValue());
        EXPECT_EQ(jewels.cell, 17 + 12);
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_TRUE(
            SameBytes(Xor32Decode(tested.sent[0].packet).Bytes(), Bytes({0xc1, 0x07, 0xbc, 0x01, 0x01, 0x02, 0x1d})));
        // Once sent, another yes does not send again.
        runtime.dialogs.current = Client::Ui::ServerTimeDialog;
        ASSERT_TRUE(tested.Click(YesX, AnswerY).HasValue());
        EXPECT_EQ(tested.sent.size(), 1u);

        runtime.dialogs.current = 0xa6;
        ASSERT_TRUE(tested.Click(300, 180).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0xa4);
        EXPECT_EQ(runtime.npcWindows.field6e5798, 0);
        EXPECT_EQ(jewels.kind, 0xff);
    }
}
} // namespace
