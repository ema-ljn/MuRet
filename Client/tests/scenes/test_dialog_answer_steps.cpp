#include "Network/Xor32Packet.h"
#include "Kernel/MouseState.h"
#include "Hud/Dialogs/GameDialogSteps.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Hud/GameUi.h"
#include "Items/InventoryMoves.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"
#include "Audio/SoundId.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// The steps of the 0x9a (the AF 01), the 0xa8 (with its third button and Esc) and the 0xaa (the D0 00) in 0x623e70
// (CL9 milestone 6, phase 4a), with the packets byte by byte and each one's rand() against a step with nothing chosen.
// The 0x91's (the personal shop's title or price) is CL9 milestone 7's (test_personal_shop_window.cpp). Invented texts
// and numbers; the world, in Lorencia (0), Devias (2) and Noria (3).
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Network::Delivery;
using Client::Network::Xor32Decode;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
using Client::Ui::GameUiContext;

constexpr std::array<std::int32_t, 3> Maps{0, 2, 3};
// 0x6282dc: 0x633300's buttons shifted (0xd5, 0x64): yes, no and, in the 0xa8, the third.
constexpr std::int32_t YesX = 0x15 + 0xd5;
constexpr std::int32_t NoX = 0x78 + 0xd5;
constexpr std::int32_t ButtonsY = 0x5a + 0x64;
constexpr std::int32_t ChaosCastleY = 0x78 + 0x64;
constexpr std::int32_t LuckYesX = 0x16 + 0xd5;
constexpr std::int32_t LuckNoX = 0x52 + 0xd5;
constexpr std::int32_t LuckThirdX = 0x8e + 0xd5;
constexpr std::int32_t LuckY = 0x5c + 0x64;
constexpr std::int32_t Silent = -1;
// 0x5af520: the warehouse and window 0x80 without rand(); the trade, a protected byte, with one (and only if the first
// two are closed).
constexpr int TradeQuery = 1;
// 0x628c11..0x629367 (and the yes): the C3 send's two rand().
constexpr int SendRandoms = 2;
constexpr std::uint8_t LuckItemCell = 0x27;
constexpr std::uint8_t LuckItemSlot = LuckItemCell + 12;
constexpr std::int32_t UseWait = 10;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

class AnsweringDialogs
{
public:
    explicit AnsweringDialogs(std::int32_t map, std::int32_t id)
    {
        auto& runtime = world->Runtime();
        runtime.map = map;
        runtime.dialogs.current = id;
        runtime.dialogs.pending = 0x8a;
        runtime.lastSoundId = Client::Audio::SoundId{Silent};
        runtime.mouse.updateTime = 3;
        runtime.mouse.updateTimeMax = 1;
        services.systemMessage = [this](std::size_t index, std::int32_t type)
        {
            messages.emplace_back(index, type);
            return Client::Core::Expected<void, std::string>{};
        };
    }

    // Opens the dialog with 0x633300's buttons (those of the 0xa8 and the y of the 0x9a).
    void OpenWithTheButtons()
    {
        auto& box = world->Runtime().dialogBox;
        const std::int32_t id = world->Runtime().dialogs.current;
        box.buttons[0] = {.state = 1, .x = 0x15, .y = 0x5a, .width = 0x46, .height = 0x15};
        box.buttons[1] = {.state = 3, .x = 0x78, .y = 0x5a, .width = 0x46, .height = 0x15};
        if (id == Client::Ui::ChaosCastleDialog)
        {
            box.buttons[0].y = 0x78;
            box.buttons[1].y = 0x78;
        }
        if (id == Client::Ui::ItemNameWithLuckDialog)
        {
            box.buttons[0] = {.state = 1, .x = 0x16, .y = 0x5c, .width = 0x31, .height = 0x10};
            box.buttons[1] = {.state = 3, .x = 0x52, .y = 0x5c, .width = 0x31, .height = 0x10};
            box.thirdButton = {.state = 5, .x = 0x8e, .y = 0x5c, .width = 0x31, .height = 0x10};
        }
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y)
    {
        return Client::Hud::StepTheGameDialogs(GameUiContext{.world = *world,
                                                             .random = random,
                                                             .input = input,
                                                             .mouseX = x,
                                                             .mouseY = y,
                                                             .measure = &measure,
                                                             .texts = &texts,
                                                             .inventory = &services,
                                                             .sendNpcWindow = [this](Client::Protocol::Outgoing packet)
                                                             {
                                                                 sent.push_back(std::move(packet));
                                                                 return Client::Core::Expected<void, std::string>{};
                                                             }});
    }

    Client::Core::Expected<void, std::string> Click(std::int32_t x, std::int32_t y)
    {
        Client::Kernel::LatchTheLeftButton(world->Runtime().mouse, true, true);
        return Step(x, y);
    }

    void PressEscape()
    {
        input.Apply(Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown,
                                              .key = Client::Input::Key::Escape});
    }

    // The rand() of a step with nothing chosen, from the current state (on a copy of the world).
    [[nodiscard]] CrtRandom QuietStep() const
    {
        AnsweringDialogs quiet(world->Runtime().map, world->Runtime().dialogs.current);
        quiet.world->Runtime().panels = world->Runtime().panels;
        quiet.OpenWithTheButtons();
        quiet.random = random;
        static_cast<void>(quiet.Step(0, 0));
        return quiet.random;
    }

    [[nodiscard]] Client::Kernel::WorldRuntime& Runtime() const
    {
        return world->Runtime();
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x13579bdu};
    Client::Input::InputState input;
    LetterMeasure measure;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{474, "ahora no"}});
    Client::Items::InventoryServices services;
    std::vector<Client::Protocol::Outgoing> sent;
    std::vector<std::pair<std::size_t, std::int32_t>> messages;
};

// What CloseTheDialog leaves: the waiting one open, MouseUpdateTime at 0 with its maximum at 6 and sound 0x19.
void ExpectClosedWithTheSound(const AnsweringDialogs& tested, std::int32_t sound = 0x19)
{
    auto& runtime = tested.Runtime();
    EXPECT_EQ(runtime.dialogs.current, 0x8a);
    EXPECT_EQ(runtime.dialogs.pending, 0);
    EXPECT_EQ(runtime.mouse.updateTime, 0);
    EXPECT_EQ(runtime.mouse.updateTimeMax, 6);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{sound});
}

// 0x628582..0x628aaf: the 0x9a's yes with the dialogs' item (+8 at 0x78a59cc: level 5) sends C1 06 AF 01 with the
// level and the low byte of 0x78a6758, with the XOR32 and in the clear, without rand(), and closes with the sound.
TEST(DialogAnswerStepsTest, TheChaosCastleYesSendsTheEntryWithTheItemLevelAndSlot)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        AnsweringDialogs tested(map, Client::Ui::ChaosCastleDialog);
        tested.OpenWithTheButtons();
        tested.Runtime().dialogItem.scalar1Low = (5u << 3) | 2u;
        tested.Runtime().slot78a6758 = 0x114;
        const CrtRandom quiet = tested.QuietStep();

        ASSERT_TRUE(tested.Click(YesX, ChaosCastleY).HasValue());
        EXPECT_EQ(tested.random.State(), quiet.State());
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_EQ(tested.sent[0].delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(Xor32Decode(tested.sent[0].packet).Bytes(), Bytes({0xc1, 0x06, 0xaf, 0x01, 0x05, 0x14})));
        ExpectClosedWithTheSound(tested);
    }
}

// 0x628582..0x62858e: without the item (0x78a59cc at 0xff) the yes does nothing and the dialog stays; 0x62854f: the no
// closes with the sound and sends nothing.
TEST(DialogAnswerStepsTest, TheChaosCastleWithoutItemStaysAndItsNoCloses)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        AnsweringDialogs tested(map, Client::Ui::ChaosCastleDialog);
        tested.OpenWithTheButtons();
        tested.Runtime().dialogItem.scalar1Low = 0xff;
        ASSERT_TRUE(tested.Click(YesX, ChaosCastleY).HasValue());
        EXPECT_TRUE(tested.sent.empty());
        EXPECT_EQ(tested.Runtime().dialogs.current, Client::Ui::ChaosCastleDialog);
        EXPECT_EQ(tested.Runtime().lastSoundId, Client::Audio::SoundId{Silent});
        EXPECT_FALSE(tested.Runtime().mouse.pendingClick);

        ASSERT_TRUE(tested.Click(NoX, ChaosCastleY).HasValue());
        EXPECT_TRUE(tested.sent.empty());
        ExpectClosedWithTheSound(tested);
    }
}

// 0x62a480..0x62a752: the 0xaa's yes sends C1 04 D0 00 with the XOR32, in the clear, and closes; the no (and Esc) only
// closes.
TEST(DialogAnswerStepsTest, TheNoticeYesSendsD0AndTheNoOnlyCloses)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        AnsweringDialogs yes(map, Client::Ui::NoticeQuestionDialog);
        yes.OpenWithTheButtons();
        const CrtRandom quiet = yes.QuietStep();
        ASSERT_TRUE(yes.Click(YesX, ButtonsY).HasValue());
        EXPECT_EQ(yes.random.State(), quiet.State());
        ASSERT_EQ(yes.sent.size(), 1u);
        EXPECT_EQ(yes.sent[0].delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(Xor32Decode(yes.sent[0].packet).Bytes(), Bytes({0xc1, 0x04, 0xd0, 0x00})));
        ExpectClosedWithTheSound(yes);

        AnsweringDialogs no(map, Client::Ui::NoticeQuestionDialog);
        no.OpenWithTheButtons();
        ASSERT_TRUE(no.Click(NoX, ButtonsY).HasValue());
        EXPECT_TRUE(no.sent.empty());
        ExpectClosedWithTheSound(no);

        AnsweringDialogs escape(map, Client::Ui::NoticeQuestionDialog);
        escape.OpenWithTheButtons();
        escape.PressEscape();
        ASSERT_TRUE(escape.Step(0, 0).HasValue());
        EXPECT_TRUE(escape.sent.empty());
        ExpectClosedWithTheSound(escape);
    }
}

// 0x6283a1..0x628409 and 0x628bb9..0x628bd7: the 0xa8's third button (stateless) with the click, or Esc (0x62841e),
// choose 2: closes with MouseUpdateTime, without sound and without 0x5af520. With Esc the 0x19 of 0x623f6a sounds
// anyway, which belongs to any dialog.
TEST(DialogAnswerStepsTest, TheLuckThirdButtonAndEscapeSkipTheItem)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        AnsweringDialogs third(map, Client::Ui::ItemNameWithLuckDialog);
        third.OpenWithTheButtons();
        const CrtRandom quiet = third.QuietStep();
        ASSERT_TRUE(third.Click(LuckThirdX + 0x31, LuckY + 0x10).HasValue());
        EXPECT_EQ(third.random.State(), quiet.State());
        ExpectClosedWithTheSound(third, Silent);
        EXPECT_EQ(third.Runtime().dialogBox.thirdButton.state, 5);
        EXPECT_FALSE(third.Runtime().mouse.pendingClick);

        AnsweringDialogs escape(map, Client::Ui::ItemNameWithLuckDialog);
        escape.OpenWithTheButtons();
        const CrtRandom escapeQuiet = escape.QuietStep();
        escape.PressEscape();
        ASSERT_TRUE(escape.Step(0, 0).HasValue());
        EXPECT_EQ(escape.random.State(), escapeQuiet.State());
        ExpectClosedWithTheSound(escape);
    }
}

// 0x628bdc..0x628c00 and 0x6293b5..0x6293d9: the 0xa8's yes and no leave 0 or 1 in 0x78abb26; with the warehouse open
// (0x5af520 false at its first IsOpen, without rand()), the error warning 474 and the close with the sound. With the
// wait of 0x5767954 (the three IsOpen, with the trade's rand()) it closes without sending; without it, it waits 10 and
// sends the C1 06 26 in C3 with the dialog's cell + 12, destination 0 and the choice, with two more rand(). Both close.
TEST(DialogAnswerStepsTest, TheLuckAnswersUseTheItemOrSayWhyNot)
{
    for (const std::int32_t map : Maps)
    {
        for (const auto& [x, choice] : std::to_array<std::pair<std::int32_t, std::uint8_t>>(
                 {{LuckYesX, std::uint8_t{0}}, {LuckNoX, std::uint8_t{1}}}))
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", botón " << int{choice});
            AnsweringDialogs refused(map, Client::Ui::ItemNameWithLuckDialog);
            refused.OpenWithTheButtons();
            refused.Runtime().ui.field78abb26 = 7;
            refused.Runtime().panels.warehouse = 1;
            const CrtRandom quiet = refused.QuietStep();
            ASSERT_TRUE(refused.Click(x, LuckY).HasValue());
            EXPECT_EQ(refused.random.State(), quiet.State());
            EXPECT_EQ(refused.Runtime().ui.field78abb26, choice);
            EXPECT_EQ(refused.messages, (std::vector<std::pair<std::size_t, std::int32_t>>{{474, 2}}));
            EXPECT_TRUE(refused.sent.empty());
            EXPECT_EQ(refused.Runtime().ui.field5767954, 0);
            ExpectClosedWithTheSound(refused);

            AnsweringDialogs waiting(map, Client::Ui::ItemNameWithLuckDialog);
            waiting.OpenWithTheButtons();
            waiting.Runtime().ui.field5767954 = 4;
            const CrtRandom waitingQuiet = waiting.QuietStep();
            ASSERT_TRUE(waiting.Click(x, LuckY).HasValue());
            EXPECT_EQ(waiting.random.State(), Advanced(waitingQuiet, TradeQuery).State());
            EXPECT_EQ(waiting.Runtime().ui.field78abb26, choice);
            EXPECT_EQ(waiting.Runtime().ui.field5767954, 4);
            EXPECT_TRUE(waiting.sent.empty());
            EXPECT_TRUE(waiting.messages.empty());
            ExpectClosedWithTheSound(waiting);

            AnsweringDialogs used(map, Client::Ui::ItemNameWithLuckDialog);
            used.OpenWithTheButtons();
            used.Runtime().dialogItemCell = LuckItemCell;
            used.Runtime().slot78a6758 = 0x73;
            used.Runtime().ui.field78abb26 = 7;
            const CrtRandom usedQuiet = used.QuietStep();
            ASSERT_TRUE(used.Click(x, LuckY).HasValue());
            EXPECT_EQ(used.random.State(), Advanced(usedQuiet, TradeQuery + SendRandoms).State());
            EXPECT_EQ(used.Runtime().ui.field78abb26, choice);
            EXPECT_EQ(used.Runtime().ui.field5767954, UseWait);
            EXPECT_TRUE(used.messages.empty());
            ASSERT_EQ(used.sent.size(), 1u);
            EXPECT_EQ(used.sent[0].delivery, Delivery::Encrypted);
            EXPECT_TRUE(SameBytes(Xor32Decode(used.sent[0].packet).Bytes(),
                                  Bytes({0xc1, 0x06, 0x26, LuckItemSlot, 0x00, choice})));
            ExpectClosedWithTheSound(used);
        }
    }
}
} // namespace
