#include "Core/Expected.h"
#include "Kernel/MouseState.h"
#include "Crt/CrtRandom.h"
#include "Tables/SkillTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Input/MouseMessageState.h"
#include "Network/Xor32Packet.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Chat/GameChat.h"
#include "Hud/GameUi.h"
#include "CastleSiege/SiegeBattleUi.h"
#include "Items/InventoryMoves.h"
#include "Player/ItemSetState.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Ui/Dialogs.h"
#include "Messenger/FriendWindows.h"
#include "Ui/GameUiState.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelRegistry.h"
#include "Ui/PanelDialog.h"
#include "Audio/SoundId.h"

#include "support/Bytes.h"
#include "support/ChatTexts.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace
{
using Client::Crt::CrtRandom;
using Client::Hud::FramePickingContext;
using Client::Hud::GameUiContext;
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Input::MouseButton;
using Client::Kernel::WorldState;
using Client::Network::Xor32Decode;
using Client::Protocol::Items::UseItem;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

// 0x5a05e5..0x5a10e2: the skill bar's own record, opened and closed in each step.
constexpr int SkillBarRandoms = 3848;
// 0x5a343e..0x5a3587: the trade (0x20) and NPC shop (0x10000) queries of the Q, W and E chain.
constexpr int SlotGateRandoms = 2;
// 0x5a5511: IsOpen(0), with everything closed or with a panel above 0x10000.
constexpr int AnyPanelRandoms = 2;
constexpr int BaseStepRandoms = SkillBarRandoms + SlotGateRandoms + AnyPanelRandoms;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

void Press(InputState& input, Key key)
{
    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = key});
}

void Release(InputState& input, Key key)
{
    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyUp, .key = key});
}

class SteppingUi
{
public:
    Client::Core::Expected<void, std::string> Step(std::int32_t x = 0, std::int32_t y = 0)
    {
        GameUiContext context{
            .world = world, .random = random, .input = input, .mouseX = x, .mouseY = y, .inventory = inventory};
        context.skills = skills;
        return Client::Hud::StepTheGameUi(context);
    }
    Client::Core::Expected<void, std::string> Dialogs(std::int32_t x = 0, std::int32_t y = 0)
    {
        return Client::Hud::StepTheGameDialogs(
            GameUiContext{.world = world, .random = random, .input = input, .mouseX = x, .mouseY = y});
    }
    void Click()
    {
        Client::Kernel::LatchTheLeftButton(world.Runtime().mouse, true, true);
    }
    Client::Ui::GameUiState& Ui()
    {
        return world.Runtime().ui;
    }
    Client::Ui::PanelState& Panels()
    {
        return world.Runtime().panels;
    }

    WorldState world;
    CrtRandom random;
    InputState input;
    const Client::Tables::SkillTable* skills = nullptr;
    const Client::Items::InventoryServices* inventory = nullptr;
};

// The C1 06 26 the steps ask for (CL9 milestone 5).
struct UseSink
{
    UseSink()
    {
        services.sendUseItem = [this](const UseItem& use)
        {
            sent.push_back(use);
            return Client::Core::Expected<void, std::string>{};
        };
    }

    Client::Items::InventoryServices services;
    std::vector<UseItem> sent;
};

TEST(GameUiTest, TheBaseStepDrawsTheSkillBarTheSlotGatesAndTheAnyPanelTail)
{
    SteppingUi tested;
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms);
    const auto stepped = tested.Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.Ui().mouseOnUi, 0);
    EXPECT_EQ(tested.Ui().skillTooltip, -1);
}

TEST(GameUiTest, TheMouseOverTheBarBelongsToTheUi)
{
    SteppingUi tested;
    tested.Ui().mouseOnUi = 1;
    ASSERT_TRUE(tested.Step(320, 0x1af).HasValue());
    EXPECT_EQ(tested.Ui().mouseOnUi, 0);
    ASSERT_TRUE(tested.Step(320, 0x1b0).HasValue());
    EXPECT_EQ(tested.Ui().mouseOnUi, 1);
}

TEST(GameUiTest, EscapeOpensTheMenuWithTheTwoQueriesOfThePanelKey)
{
    SteppingUi tested;
    Press(tested.input, Key::Escape);
    const CrtRandom expected = Advanced(tested.random, 2);
    const auto stepped = tested.Dialogs();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.world.Runtime().dialogs.current, Client::Ui::EscapeMenuDialog);
    EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x19});
    // The latch does not let the same Esc be read twice.
    tested.world.Runtime().lastSoundId.reset();
    ASSERT_TRUE(tested.Dialogs().HasValue());
    EXPECT_EQ(tested.world.Runtime().dialogs.current, Client::Ui::EscapeMenuDialog);
    EXPECT_FALSE(tested.world.Runtime().lastSoundId.has_value());
}

TEST(GameUiTest, EscapeClosesTheMenuWithoutQueries)
{
    SteppingUi tested;
    tested.world.Runtime().dialogs.current = Client::Ui::EscapeMenuDialog;
    Press(tested.input, Key::Escape);
    const auto before = tested.random.State();
    ASSERT_TRUE(tested.Dialogs().HasValue());
    EXPECT_EQ(tested.random.State(), before);
    EXPECT_EQ(tested.world.Runtime().dialogs.current, 0);
    EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x19});
}

TEST(GameUiTest, TheMenuStepsWithTheUiBlockAndKeepsTheMouse)
{
    SteppingUi tested;
    tested.world.Runtime().dialogs.current = Client::Ui::EscapeMenuDialog;
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms);
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Ui().mouseOnUi, 1);
}

TEST(GameUiTest, TheLastMenuButtonGoesBackToTheGame)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::Ui::EscapeMenuDialog;
    runtime.mouse.updateTime = 3;
    tested.Click();
    ASSERT_TRUE(tested.Dialogs(0x110, 185).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_FALSE(runtime.mouse.pendingClick);
    EXPECT_EQ(runtime.mouse.updateTime, 0);
    EXPECT_EQ(runtime.mouse.updateTimeMax, 6);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
}

TEST(GameUiTest, TheLetterIOpensTheInventory)
{
    SteppingUi tested;
    Press(tested.input, Key::I);
    // Open(0x4000000) draws 4; with the inventory above 0x10000, IsOpen(0) draws the 2 before finding it.
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + 4);
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Panels().inventory, 1);
    // With the inventory open, 0x5ee140 runs its step (0x5e4ce0) and its six sisters (test_game_inventory_step).
    const auto again = tested.Step(320, 240);
    ASSERT_TRUE(again.HasValue()) << again.Error();
    EXPECT_EQ(tested.Panels().inventory, 1);
}

// The chat log, outside its window at (−10, 0x65) with 0x10c × 0xfa from its bottom edge, stays under the mouse at
// the top left (0x45aa80): 0x5ee140 does not run the panels (0x5ee174).
TEST(GameUiTest, TheChatLogUnderTheMouseKeepsThePanelsStill)
{
    SteppingUi tested;
    Press(tested.input, Key::I);
    ASSERT_TRUE(tested.Step(320, 240).HasValue());
    ASSERT_EQ(tested.Panels().inventory, 1);
    ASSERT_TRUE(tested.Step(0, 0).HasValue());
    EXPECT_EQ(tested.Ui().hovered, Client::Chat::ChatLogId);
    EXPECT_EQ(tested.world.Runtime().hud.chatFrame.x, -10);
    EXPECT_EQ(tested.world.Runtime().hud.chatFrame.y, 0x65);
}

// In its window (0x6c7a88 at 1), the corner goes to (0xba, 0x1a4), and to x = 0 with the inventory and the character
// open (0x636f29).
TEST(GameUiTest, InItsWindowTheChatLogMovesLeftOfTheInventoryAndTheCharacter)
{
    SteppingUi tested;
    tested.Ui().chatModes.placement = 1;
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(tested.world.Runtime().hud.chatFrame.x, 0xba);
    EXPECT_EQ(tested.world.Runtime().hud.chatFrame.y, 0x1a4);
}

// The unowned click with the chat closed sets the arrows' list back to 0 (0x636ead).
TEST(GameUiTest, TheUnownedClickTakesTheArrowsFromTheChatLog)
{
    SteppingUi tested;
    tested.Ui().focusedList = Client::Chat::ChatLogId;
    tested.Click();
    ASSERT_TRUE(tested.Step(320, 240).HasValue());
    EXPECT_EQ(tested.Ui().focusedList, 0);
}

// 0x636e8c..0x636ea5: with the panels' field (0x77f3b69), the guild sign-up one (0x77f3b68) or the zen mode of the
// 0x73 and of the 0x91's price (0x77f3b6a), the unowned click leaves the arrows' list and the EDIT's focus.
TEST(GameUiTest, TheUnownedClickLeavesAnOpenDialogField)
{
    for (const int field : {0, 1, 2})
    {
        SCOPED_TRACE(field);
        const auto owned = std::make_unique<SteppingUi>();
        SteppingUi& tested = *owned;
        Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
        runtime.chat.input.otherInput = field == 0;
        runtime.chat.input.dialogFieldOn = field == 1;
        runtime.social.moneyField = field == 2 ? 1 : 0;
        runtime.chat.input.focus = Client::Chat::ChatFocus::Dialog;
        tested.Ui().focusedList = Client::Chat::DialogFieldId;
        tested.Click();
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_EQ(tested.Ui().focusedList, Client::Chat::DialogFieldId);
        EXPECT_EQ(runtime.chat.input.focus, Client::Chat::ChatFocus::Dialog);
    }
}

// 0x435bd0, only in the 34: with the Crywolf results (0x723bf9 at 1), the click on the button (300, 354) × (300, 330),
// without the edges, leaves 0x723bdc at 5, and any click stays off (the button down and the edge). In Lorencia and in
// Devias the step does nothing and the results do not close (Q-C1342). The same with the contract window.
TEST(GameUiTest, TheCrywolfResultsTakeEveryClickOnlyInCrywolf)
{
    constexpr std::int32_t CrywolfMap = 34;
    for (const std::int32_t map : {0, 2, CrywolfMap})
    {
        SCOPED_TRACE(map);
        for (const auto& [x, y, pushed] :
             {std::tuple{301, 301, true}, std::tuple{353, 329, true}, std::tuple{300, 310, false},
              std::tuple{320, 330, false}, std::tuple{100, 100, false}})
        {
            SCOPED_TRACE(x);
            SCOPED_TRACE(y);
            // On the heap: with the one below there would be two WorldState on Windows' 1 MB stack.
            const auto owned = std::make_unique<SteppingUi>();
            SteppingUi& tested = *owned;
            Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
            runtime.map = map;
            runtime.crywolf.ui.byte723bf9 = 1;
            tested.Click();
            ASSERT_TRUE(tested.Step(x, y).HasValue());
            const bool crywolf = map == CrywolfMap;
            EXPECT_EQ(runtime.crywolf.ui.dwords723bdc[0], crywolf && pushed ? 5 : 0);
            if (crywolf)
            {
                EXPECT_FALSE(runtime.mouse.leftHeld);
                EXPECT_FALSE(runtime.mouse.pendingClick);
            }
            EXPECT_EQ(runtime.crywolf.ui.byte723bf9, 1);
        }
    }
    // Without the results, the click goes on.
    SteppingUi tested;
    tested.world.Runtime().map = CrywolfMap;
    tested.Click();
    ASSERT_TRUE(tested.Step(320, 310).HasValue());
    EXPECT_TRUE(tested.world.Runtime().mouse.leftHeld);
    EXPECT_EQ(tested.world.Runtime().crywolf.ui.dwords723bdc[0], 0);
    // The contract's window 1 (0x435c5c, Events/Crywolf/CrywolfContract) also keeps the click, only in the 34.
    tested.world.Runtime().crywolf.ui.byte723a44 = 1;
    tested.Click();
    ASSERT_TRUE(tested.Step(320, 310).HasValue());
    EXPECT_FALSE(tested.world.Runtime().mouse.leftHeld);
    EXPECT_EQ(tested.world.Runtime().crywolf.ui.byte723a44, 1);
    // Another click needs the button released first (0x4b3ce3).
    tested.world.Runtime().map = 2;
    Client::Kernel::LatchTheLeftButton(tested.world.Runtime().mouse, false, false);
    tested.Click();
    ASSERT_TRUE(tested.Step(320, 310).HasValue());
    EXPECT_TRUE(tested.world.Runtime().mouse.leftHeld);
}

TEST(GameUiTest, TheInventoryButtonOfTheHudTakesTheClick)
{
    SteppingUi tested;
    tested.Click();
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + 4);
    ASSERT_TRUE(tested.Step(0x1a0, 0x1d0).HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Panels().inventory, 1);
    EXPECT_FALSE(tested.world.Runtime().mouse.pendingClick);
    EXPECT_EQ(tested.Ui().mouseOnUi, 1);
}

TEST(GameUiTest, RWithNothingOpenOpensTheOwnRecordAndAsksThreeTimes)
{
    SteppingUi tested;
    Press(tested.input, Key::R);
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + SkillBarRandoms + 3);
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Panels().repairMode, 0);
}

TEST(GameUiTest, BWithNothingOpenAsksTheNpcShop)
{
    SteppingUi tested;
    Press(tested.input, Key::B);
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + 1);
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
}

// 0x5a35ca: without the slot's item in the inventory, 0x558110 gives −1 and the key does nothing more.
TEST(GameUiTest, TheQuickSlotKeysWithoutTheirItemDoNothing)
{
    for (const Key key : {Key::Q, Key::W, Key::E})
    {
        SteppingUi tested;
        Press(tested.input, key);
        const CrtRandom expected = Advanced(tested.random, BaseStepRandoms);
        const auto stepped = tested.Step();
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.random.State(), expected.State());
    }
}

// 0x5a35e0.., 0x5a3d64.. and 0x5a44eb.. (CL9 milestone 5): with the item, 0x5af520 (the rand() of IsOpen(0x20)),
// 0x5767954 at 10, the C1 06 26 in C3 with the cell + 12, 0 and [0x78abb26], and the sound: 0x21 the apple, 0x20 the
// other potions. Until 0x5767954 goes back to 0 (the replies), the key does not send another.
TEST(GameUiTest, TheQuickSlotKeysUseTheirPotions)
{
    struct Slot
    {
        Key key;
        std::uint16_t type;
        std::int32_t sound;
    };
    constexpr std::array<Slot, 3> Slots{{{Key::Q, 0x1c00, 0x21}, {Key::W, 0x1c04, 0x20}, {Key::E, 0x1c08, 0x20}}};
    for (const Slot& slot : Slots)
    {
        SteppingUi tested;
        UseSink sink;
        tested.inventory = &sink.services;
        tested.world.Inventory().cells[9].id = slot.type;
        tested.world.Inventory().cells[9].occupied = 1;
        Press(tested.input, slot.key);
        const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + 1 + 2);
        const auto stepped = tested.Step();
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.random.State(), expected.State());
        ASSERT_EQ(sink.sent.size(), 1u);
        const auto packet = Client::Protocol::Items::WriteUseItem(sink.sent.front());
        EXPECT_TRUE(SameBytes(Xor32Decode(packet.packet).Bytes(), Bytes({0xc1, 0x06, 0x26, 9 + 12, 0, 0})));
        EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{slot.sound});
        EXPECT_EQ(tested.Ui().field5767954, 10);

        Release(tested.input, slot.key);
        ASSERT_TRUE(tested.Step().HasValue());
        Press(tested.input, slot.key);
        ASSERT_TRUE(tested.Step().HasValue());
        EXPECT_EQ(sink.sent.size(), 1u);
    }
}

// 0x5af520: with the warehouse open (Q, W and E do not even get there: 0x5a343e), the right click on the slot
// notifies with the 474 of type 2 and sends nothing.
TEST(GameUiTest, TheRightClickOnAQuickSlotDoesNotUseWithTheWarehouseOpen)
{
    SteppingUi tested;
    UseSink sink;
    std::vector<std::size_t> messages;
    sink.services.systemMessage = [&messages](std::size_t text, std::int32_t type)
    {
        EXPECT_EQ(type, 2);
        messages.push_back(text);
        return Client::Core::Expected<void, std::string>{};
    };
    tested.inventory = &sink.services;
    tested.world.Inventory().cells[9].id = 0x1c00;
    tested.world.Inventory().cells[9].occupied = 1;
    tested.Panels().warehouse = 1;
    tested.Ui().rightClick = true;
    const auto stepped = tested.Step(0xd0, 0x1c4);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(sink.sent.empty());
    EXPECT_EQ(messages, std::vector<std::size_t>{474});
    EXPECT_EQ(tested.Ui().field5767954, 0);
}

// 0x5a2b93: the right click on a slot is spent even if it is empty.
TEST(GameUiTest, TheRightClickOnAnEmptyQuickSlotIsSpent)
{
    SteppingUi tested;
    tested.Ui().rightClick = true;
    const auto stepped = tested.Step(0xd0 + 0x1f, 0x1c4);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(tested.Ui().rightClick);
}

// 0x5a2bc8..0x5a32cc (CL9 milestone 5): the right click on the slot uses its item, like Q, W and E.
TEST(GameUiTest, TheRightClickOnAQuickSlotUsesItsItem)
{
    SteppingUi tested;
    UseSink sink;
    tested.inventory = &sink.services;
    tested.world.Inventory().cells[0].id = 0x1c05;
    tested.world.Inventory().cells[0].occupied = 1;
    tested.Ui().rightClick = true;
    const auto stepped = tested.Step(0xd0 + 0x1f, 0x1c4);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(sink.sent.size(), 1u);
    EXPECT_EQ(sink.sent.front().slot, 12);
    EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x20});
    EXPECT_FALSE(tested.Ui().rightClick);
}

TEST(GameUiTest, ADialogClosesTheSlotGateAfterItsTwoQueries)
{
    SteppingUi tested;
    tested.world.Runtime().dialogs.current = Client::Ui::EscapeMenuDialog;
    Press(tested.input, Key::Q);
    Press(tested.input, Key::I);
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms);
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Panels().inventory, 0);
}

TEST(GameUiTest, TheSkillBarButtonTogglesTheList)
{
    SteppingUi tested;
    tested.world.OwnCharacter().byte6a = 1;
    tested.Click();
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms);
    const auto stepped = tested.Step(0x140, 0x1c0);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    // Without skills in the record, the list does not read Skill.bmd.
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Ui().skillListOpen, 1);
    EXPECT_EQ(tested.Ui().skillTooltipX, 0x140);
    EXPECT_EQ(tested.Ui().skillTooltipY, 0x1bc);
    EXPECT_FALSE(tested.world.Runtime().mouse.pendingClick);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
}

// A made-up Skill.bmd: the skills of `hidden` with +0x31 at 1 (the ones the list does not show).
Client::Tables::SkillTable SkillsHiding(std::initializer_list<std::uint8_t> hidden)
{
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    for (const std::uint8_t skill : hidden)
        plain[std::size_t{skill} * Client::Tables::SkillRecordSize + 0x31] = std::byte{1};
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::SkillRecordSize);
    return Client::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

// The Skill.bmd entry that the list opens and closes at 0 (0x40f600).
constexpr int SkillEntryRandoms = 60;
// The own record that 0x59f810 opens and closes outside the bar.
constexpr int ChooseByKeyRandoms = 3848;

// 0x5a070e..0x5a1098: the open list reads the +0x31 of each skill there is (except 0x43 to 0x48), and the ones that
// do not have it at 1 go in a centered row: with +0x6a 3 and +0x6b 1, of two, from x 288.
TEST(GameUiTest, TheOpenSkillListReadsEachSkillAndCentersTheShownOnes)
{
    auto tested = std::make_unique<SteppingUi>();
    const auto skills = SkillsHiding({9});
    tested->skills = &skills;
    auto& own = tested->world.OwnCharacter();
    own.byte6a = 3;
    own.byte6b = 1;
    own.SetSkillAt(0, 5);
    own.SetSkillAt(1, 0x44); // 0x43 to 0x48: it does not even open it
    own.SetSkillAt(2, 9);    // escondida
    own.SetSkillAt(3, 0x11);
    tested->Ui().skillListOpen = 1;
    const CrtRandom expected = Advanced(tested->random, BaseStepRandoms + 3 * SkillEntryRandoms);
    // The second visible one, the 0x11 of slot 3: x 288 + 32 = 320.
    const auto stepped = tested->Step(320 + 5, 0x172 + 5);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->Ui().skillTooltip, 3);
    EXPECT_EQ(tested->Ui().skillTooltipX, 320 + 0x10);
    EXPECT_EQ(tested->Ui().skillTooltipY, 0x172);
    EXPECT_EQ(tested->Ui().mouseOnUi, 1);
    EXPECT_EQ(tested->Ui().skillListOpen, 1);
    EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
}

TEST(GameUiTest, TheOpenSkillListWithoutSkillBmdIsACut)
{
    auto tested = std::make_unique<SteppingUi>();
    tested->world.OwnCharacter().byte6a = 1;
    tested->world.OwnCharacter().SetSkillAt(0, 5);
    tested->Ui().skillListOpen = 1;
    const auto stepped = tested->Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x5a07a3"), std::string::npos) << stepped.Error();
}

// 0x5a08c2..0x5a08f3: the click on a skill of the list gives it to the hero and closes the list with the 0x19.
TEST(GameUiTest, TheClickOnTheListChoosesTheSkillAndClosesIt)
{
    auto tested = std::make_unique<SteppingUi>();
    const auto skills = SkillsHiding({});
    tested->skills = &skills;
    auto& own = tested->world.OwnCharacter();
    own.byte6a = 1;
    own.SetSkillAt(4, 0x11);
    tested->Ui().skillListOpen = 1;
    tested->Click();
    // A single one: x 320 − 16 = 304.
    const auto stepped = tested->Step(304, 0x172);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested->world.Actors()[0].skillIndex, 4);
    EXPECT_EQ(tested->Ui().skillListOpen, 0);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x19});
    EXPECT_FALSE(tested->world.Runtime().mouse.pendingClick);
    EXPECT_EQ(tested->world.Runtime().mouse.updateTimeMax, 6);
}

// 0x5a08fd..0x5a106e: Ctrl and a digit over a skill of the list give it that key, which leaves the one that had it.
// With the record already opened by the bar, its access does not close; the digit also chooses the skill (0x59f810).
TEST(GameUiTest, CtrlAndADigitOverTheListGiveTheSkillThatKey)
{
    auto tested = std::make_unique<SteppingUi>();
    const auto skills = SkillsHiding({});
    tested->skills = &skills;
    auto& own = tested->world.OwnCharacter();
    own.byte6a = 1;
    own.SetSkillAt(2, 0x11);
    auto& keys = own.skillKeys[0];
    keys.fill(0xff);
    keys[7] = 3;
    tested->Ui().skillListOpen = 1;
    Press(tested->input, Key::CtrlLeft);
    Press(tested->input, Key::Digit3);
    const CrtRandom expected = Advanced(tested->random, BaseStepRandoms + SkillEntryRandoms + ChooseByKeyRandoms);
    const auto stepped = tested->Step(304, 0x172);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(keys[2], 3);
    EXPECT_EQ(keys[7], 0xff);
    EXPECT_EQ(tested->world.Actors()[0].skillIndex, 2);
}

// 0x420e90 (from 0x5a1084, with the list open and some skill): with the raven, the four buttons of its orders.
TEST(GameUiTest, TheRavenOrderButtonsSitUnderTheList)
{
    auto tested = std::make_unique<SteppingUi>();
    tested->world.Actors()[0].darkSpirit.emplace();
    tested->world.OwnCharacter().byte6a = 1;
    tested->Ui().skillListOpen = 1;
    tested->Click();
    ASSERT_TRUE(tested->Step(0x120 + 3, 0x14a + 3).HasValue());
    EXPECT_EQ(tested->Ui().skillTooltip, 0x79);
    EXPECT_EQ(tested->Ui().skillTooltipX, 0x130);
    EXPECT_EQ(tested->Ui().skillTooltipY, 0x14a);
    EXPECT_EQ(tested->world.Actors()[0].skillIndex, 0x79);
    EXPECT_EQ(tested->Ui().skillListOpen, 0);
}

TEST(GameUiTest, WithoutTheRavenTheOrderButtonsAreNotThere)
{
    auto tested = std::make_unique<SteppingUi>();
    tested->world.OwnCharacter().byte6a = 1;
    tested->Ui().skillListOpen = 1;
    ASSERT_TRUE(tested->Step(0x120 + 3, 0x14a + 3).HasValue());
    EXPECT_EQ(tested->Ui().skillTooltip, -1);
}

// 0x59f810: the digit chooses the first of the 64 skills with that key in the selected one's slot, with the record
// opened and closed.
TEST(GameUiTest, TheDigitsChooseTheSkillWithThatKey)
{
    auto tested = std::make_unique<SteppingUi>();
    auto& own = tested->world.OwnCharacter();
    own.SetSkillAt(1, 0x45); // from 0x43 to 0x48: no
    own.SetSkillAt(3, 0x11);
    auto& keys = own.skillKeys[2];
    keys.fill(0xff);
    keys[1] = 2;
    keys[3] = 2;
    keys[5] = 2; // no skill
    Press(tested->input, Key::Digit2);
    const CrtRandom expected = Advanced(tested->random, BaseStepRandoms + ChooseByKeyRandoms);
    GameUiContext context{
        .world = tested->world, .random = tested->random, .input = tested->input, .mouseX = 320, .mouseY = 100};
    context.chosenSlot = std::uint8_t{2};
    const auto stepped = Client::Hud::StepTheGameUi(context);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->world.Actors()[0].skillIndex, 3);
}

// 0x59f9c3: without a skill with the key, the first raven order (0x78 to 0x7b) that has it.
TEST(GameUiTest, WithoutASkillTheDigitChoosesTheRavenOrder)
{
    auto tested = std::make_unique<SteppingUi>();
    auto& keys = tested->world.OwnCharacter().skillKeys[0];
    keys.fill(0xff);
    keys[0x7a] = 0;
    Press(tested->input, Key::Digit0);
    ASSERT_TRUE(tested->Step(320, 100).HasValue());
    EXPECT_EQ(tested->world.Actors()[0].skillIndex, 0x7a);
}

// 0x59f91e..0x59f958: in each slot not chosen, with the automatic attack and the hero's skill 6 or 0xf, the targets
// 0x6c8e30 and 0x6c8e38 go back to −1, except in maps 6 and 18 to 23.
TEST(GameUiTest, TheDigitsDropTheAutoTargetsOutsideTheArenaMaps)
{
    for (const auto& [map, dropped] : {std::pair{0, true}, std::pair{2, true}, std::pair{6, false},
                                       std::pair{0x12, false}, std::pair{0x17, false}, std::pair{0x18, true}})
    {
        auto tested = std::make_unique<SteppingUi>();
        auto& runtime = tested->world.Runtime();
        runtime.map = map;
        tested->Ui().autoAttack = true;
        tested->world.OwnCharacter().SetSkillAt(0, 6);
        tested->world.OwnCharacter().skillKeys[0].fill(0xff);
        runtime.selectedTargets = {1, 2, 3, 4, 5};
        Press(tested->input, Key::Digit4);
        ASSERT_TRUE(tested->Step(320, 100).HasValue()) << map;
        EXPECT_EQ(runtime.selectedTargets[2], dropped ? -1 : 3) << map;
        EXPECT_EQ(runtime.selectedTargets[4], dropped ? -1 : 5) << map;
        EXPECT_EQ(runtime.selectedTargets[0], 1) << map;
    }
}

// 0x420e30: the Dark Lord with Shift and 1 to 4 chooses the raven order, and then the digits choose no skill.
TEST(GameUiTest, TheDarkLordShiftDigitsChooseTheRavenOrder)
{
    auto tested = std::make_unique<SteppingUi>();
    tested->world.Actors()[0].characterClass = 4;
    Press(tested->input, Key::ShiftLeft);
    Press(tested->input, Key::Digit3);
    const CrtRandom expected = Advanced(tested->random, BaseStepRandoms);
    ASSERT_TRUE(tested->Step(320, 100).HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->world.Actors()[0].skillIndex, 0x7a);

    // Another class: the Shift does not count and the 3 goes to 0x59f810.
    auto other = std::make_unique<SteppingUi>();
    other->world.Actors()[0].characterClass = 1;
    Press(other->input, Key::ShiftLeft);
    Press(other->input, Key::Digit3);
    const CrtRandom chosen = Advanced(other->random, BaseStepRandoms + ChooseByKeyRandoms);
    ASSERT_TRUE(other->Step(320, 100).HasValue());
    EXPECT_EQ(other->random.State(), chosen.State());
}

// 0x5a1115..0x5a111c: with a skill charging (0x7d368a4 other than 0), neither the digits (0x59f810, without opening
// the own record) nor the Dark Lord's Shift (0x420e30) change the skill.
TEST(GameUiTest, TheDigitsDoNotChooseWhileASkillCharges)
{
    for (const std::uint8_t heroClass : {std::uint8_t{1}, std::uint8_t{4}})
    {
        SCOPED_TRACE(int{heroClass});
        auto tested = std::make_unique<SteppingUi>();
        tested->world.Actors()[0].characterClass = heroClass;
        tested->world.Actors()[0].skillIndex = 1;
        auto& own = tested->world.OwnCharacter();
        own.SetSkillAt(3, 0x11);
        auto& keys = own.skillKeys[0];
        keys.fill(0xff);
        keys[3] = 3;
        tested->Ui().rightHoldFrames = 5;
        Press(tested->input, Key::ShiftLeft);
        Press(tested->input, Key::Digit3);
        const CrtRandom expected = Advanced(tested->random, BaseStepRandoms);
        ASSERT_TRUE(tested->Step(320, 100).HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->world.Actors()[0].skillIndex, 1);
        EXPECT_EQ(tested->Ui().rightHoldFrames, 5);
    }
}

// 0x5a10e7..0x5a10f9: with the zen dialog (0x73) open, what is typed belongs to the EDIT of [0x5763504] and the
// digits choose no skill, without looking at the focus.
TEST(GameUiTest, TheDigitsDoNotChooseWithTheMoneyDialog)
{
    auto tested = std::make_unique<SteppingUi>();
    auto& own = tested->world.OwnCharacter();
    own.SetSkillAt(3, 0x11);
    auto& keys = own.skillKeys[0];
    keys.fill(0xff);
    keys[3] = 3;
    tested->world.Runtime().dialogs.current = 0x73;
    Press(tested->input, Key::Digit3);
    const CrtRandom expected = Advanced(tested->random, BaseStepRandoms);
    const auto stepped = tested->Step(320, 100);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->world.Actors()[0].skillIndex, 0);
}

TEST(GameUiTest, TheSiegePanelTogglesWithTabAndTakesTheMouse)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{0};
    ASSERT_TRUE(tested.Step(0x1ea, 0x104).HasValue());
    EXPECT_EQ(tested.Ui().mouseOnUi, 1);
    Press(tested.input, Key::Tab);
    ASSERT_TRUE(tested.Step(0x1ea, 0x104).HasValue());
    EXPECT_EQ(runtime.castleSiege.siegePanel.visible, 0);
    EXPECT_EQ(tested.Ui().mouseOnUi, 0);
    // With Tab down, the latch does not read it again.
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(runtime.castleSiege.siegePanel.visible, 0);
    Release(tested.input, Key::Tab);
    ASSERT_TRUE(tested.Step().HasValue());
    Press(tested.input, Key::Tab);
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(runtime.castleSiege.siegePanel.visible, 1);
}

// 0x40f6df → 0x6578e0: an active field or any focus outside the game prevents even querying 0x553520(Tab).
TEST(GameUiTest, TheSiegeTabWaitsForTheCanonicalTextInputGateWithoutConsumingItsLatch)
{
    for (int blocked = 0; blocked < 8; ++blocked)
    {
        auto tested = std::make_unique<SteppingUi>();
        auto& runtime = tested->world.Runtime();
        runtime.castleSiege.siegePanelKind = std::uint8_t{0};
        auto& field = runtime.panels.delegated.panelDialog;
        if (blocked == 0)
            runtime.ui.chatOpen = true;
        else if (blocked == 1)
            runtime.chat.input.dialogFieldOn = true;
        else if (blocked == 2)
        {
            field.opened = 1;
            field.flags = Client::Ui::PanelDialogTextField;
        }
        else
        {
            constexpr std::array Focuses{Client::Chat::ChatFocus::Message, Client::Chat::ChatFocus::Whisper,
                                         Client::Chat::ChatFocus::OldWindow, Client::Chat::ChatFocus::Dialog,
                                         Client::Chat::ChatFocus::NumberField};
            runtime.chat.input.focus = Focuses[static_cast<std::size_t>(blocked - 3)];
        }
        Press(tested->input, Key::Tab);
        GameUiContext context{.world = tested->world, .random = tested->random, .input = tested->input};
        EXPECT_TRUE(Client::Hud::GameTextInputBusy(runtime));
        ASSERT_TRUE(Client::CastleSiege::StepTheBattlePanel(context).HasValue());
        EXPECT_EQ(runtime.castleSiege.siegePanel.visible, 1);
        EXPECT_EQ(runtime.ui.keyLatches[9], 0);

        runtime.ui.chatOpen = false;
        runtime.chat.input.dialogFieldOn = false;
        runtime.chat.input.focus = Client::Chat::ChatFocus::Game;
        field.opened = 0;
        EXPECT_FALSE(Client::Hud::GameTextInputBusy(runtime));
        ASSERT_TRUE(Client::CastleSiege::StepTheBattlePanel(context).HasValue());
        EXPECT_EQ(runtime.castleSiege.siegePanel.visible, 0);
        EXPECT_EQ(runtime.ui.keyLatches[9], 1);
        ASSERT_TRUE(Client::CastleSiege::StepTheBattlePanel(context).HasValue());
        EXPECT_EQ(runtime.castleSiege.siegePanel.visible, 0);
    }
}

TEST(GameUiTest, APanelDialogWithoutTextStillAllowsTheSiegeTab)
{
    auto tested = std::make_unique<SteppingUi>();
    auto& runtime = tested->world.Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{0};
    runtime.panels.delegated.panelDialog.opened = 1;
    runtime.panels.delegated.panelDialog.flags = Client::Ui::PanelDialogTimed;
    Press(tested->input, Key::Tab);
    EXPECT_FALSE(Client::Hud::GameTextInputBusy(runtime));
    ASSERT_TRUE(Client::CastleSiege::StepTheBattlePanel(
                    {.world = tested->world, .random = tested->random, .input = tested->input})
                    .HasValue());
    EXPECT_EQ(runtime.castleSiege.siegePanel.visible, 0);
}

TEST(GameUiTest, TheSiegePanelPagesAndFades)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{0};
    const auto& panel = runtime.castleSiege.siegePanel;
    tested.Click();
    ASSERT_TRUE(tested.Step(panel.x + 130, panel.y + 20).HasValue());
    EXPECT_EQ(panel.page, 2);
    EXPECT_FALSE(runtime.mouse.pendingClick);
    tested.Click();
    ASSERT_TRUE(tested.Step(panel.x + 130, panel.y + 20).HasValue());
    EXPECT_EQ(panel.page, 1);
    // The alpha: 1 + 0.1f stays below 1.1f on the x87 (1.1000000015 against 1.1000000238) and is stored as 1.1f; the
    // next sum passes and goes back to 0.5.
    tested.Click();
    ASSERT_TRUE(tested.Step(panel.x + 10, panel.y + 30).HasValue());
    EXPECT_EQ(panel.alpha, 1.1f);
    tested.Click();
    ASSERT_TRUE(tested.Step(panel.x + 10, panel.y + 30).HasValue());
    EXPECT_EQ(panel.alpha, 0.5f);
    tested.Click();
    ASSERT_TRUE(tested.Step(panel.x + 10, panel.y + 30).HasValue());
    EXPECT_EQ(panel.alpha, 0.6f);
    // Outside both, the click goes on.
    tested.Click();
    ASSERT_TRUE(tested.Step(panel.x + 60, panel.y + 100).HasValue());
    EXPECT_TRUE(runtime.mouse.pendingClick);
}

TEST(GameUiTest, TheClickLatchKeepsTheEdgeUntilSomeoneUsesIt)
{
    Client::Kernel::MouseState state;
    Client::Kernel::LatchTheLeftButton(state, true, true);
    EXPECT_TRUE(state.pendingClick);
    EXPECT_TRUE(state.leftHeld);
    state.pendingClick = false;
    state.leftHeld = false;
    // The same button down, without another WM_LBUTTONDOWN: nothing comes back.
    Client::Kernel::LatchTheLeftButton(state, false, true);
    EXPECT_FALSE(state.pendingClick);
    EXPECT_FALSE(state.leftHeld);
    Client::Kernel::LatchTheLeftButton(state, false, false);
    EXPECT_FALSE(state.leftHeld);
}

// The consumer turned 0x7d36874 off; WM_LBUTTONUP followed by DOWN turns it on again, even if read in the same pump.
TEST(GameUiTest, AnUpDownBatchRearmsAHeldFlagConsumedByTheUi)
{
    Client::Input::InputState input;
    Client::Kernel::MouseState state;
    input.Apply({.kind = Client::Input::InputEvent::Kind::ButtonDown, .button = Client::Input::MouseButton::Left});
    Client::Kernel::LatchTheLeftButton(state, input.MouseMessages().leftPress, input.MouseMessages().leftDown);
    ASSERT_TRUE(state.leftHeld);
    ASSERT_TRUE(state.previousLeftDown);
    state.leftHeld = false;
    state.pendingClick = false;
    input.ConsumeEdges();
    input.Apply({.kind = Client::Input::InputEvent::Kind::ButtonUp, .button = Client::Input::MouseButton::Left});
    input.Apply({.kind = Client::Input::InputEvent::Kind::ButtonDown, .button = Client::Input::MouseButton::Left});
    ASSERT_TRUE(input.MouseMessages().leftPress);
    ASSERT_TRUE(input.MouseMessages().leftDown);

    Client::Kernel::LatchTheLeftButton(state, input.MouseMessages().leftPress, input.MouseMessages().leftDown);

    EXPECT_TRUE(state.leftHeld);
    EXPECT_TRUE(state.pendingClick);
}

// WM_LBUTTONUP turns off the edge (0x4b3ce3): unused, it is lost on release, and pressed and released in the same
// step it does not stay.
TEST(GameUiTest, TheReleasedButtonDropsTheEdge)
{
    Client::Kernel::MouseState state;
    Client::Kernel::LatchTheLeftButton(state, true, true);
    Client::Kernel::LatchTheLeftButton(state, false, false);
    EXPECT_FALSE(state.pendingClick);
    Client::Kernel::LatchTheLeftButton(state, true, false);
    EXPECT_FALSE(state.pendingClick);
    EXPECT_FALSE(state.leftHeld);
}

// WM_LBUTTONDBLCLK (0x4b4444..0x4b47b6) does not set 0x7d36874, which only WM_LBUTTONDOWN (0x4b3961) and
// WM_LBUTTONUP (0x4b3cf1) write: the second press of a double click, held several steps, leaves the button up for the
// game until the next WM_LBUTTONDOWN. In the same step as the first click and its release, it does not stay either.
TEST(GameUiTest, AHeldDoubleClickLeavesTheButtonUp)
{
    using Client::Input::InputEvent;
    using Client::Input::MouseButton;
    Client::Input::InputState input;
    Client::Kernel::MouseState state;
    const auto latch = [&input](Client::Kernel::MouseState& latched)
    {
        Client::Kernel::LatchTheLeftButton(latched, input.MouseMessages().leftPress, input.MouseMessages().leftDown);
        input.ConsumeEdges();
    };
    input.Apply({.kind = InputEvent::Kind::ButtonDown, .button = MouseButton::Left});
    latch(state);
    EXPECT_TRUE(state.leftHeld);
    state.pendingClick = false;
    input.Apply({.kind = InputEvent::Kind::ButtonUp, .button = MouseButton::Left});
    latch(state);
    input.Apply({.kind = InputEvent::Kind::ButtonDoubleClick, .button = MouseButton::Left});
    latch(state);
    EXPECT_FALSE(state.leftHeld);
    EXPECT_FALSE(state.pendingClick);
    for (int step = 0; step < 3; ++step)
    {
        latch(state);
        EXPECT_FALSE(state.leftHeld);
        EXPECT_FALSE(state.pendingClick);
    }
    input.Apply({.kind = InputEvent::Kind::ButtonUp, .button = MouseButton::Left});
    latch(state);
    input.Apply({.kind = InputEvent::Kind::ButtonDown, .button = MouseButton::Left});
    latch(state);
    EXPECT_TRUE(state.leftHeld);
    EXPECT_TRUE(state.pendingClick);

    // Down, up and the double in one step: WM_LBUTTONUP turned it off and WM_LBUTTONDBLCLK does not turn it on.
    Client::Input::InputState batch;
    batch.Apply({.kind = InputEvent::Kind::ButtonDown, .button = MouseButton::Left});
    batch.Apply({.kind = InputEvent::Kind::ButtonUp, .button = MouseButton::Left});
    batch.Apply({.kind = InputEvent::Kind::ButtonDoubleClick, .button = MouseButton::Left});
    Client::Kernel::MouseState fast;
    Client::Kernel::LatchTheLeftButton(fast, batch.MouseMessages().leftPress, batch.MouseMessages().leftDown);
    EXPECT_FALSE(fast.leftHeld);
    EXPECT_FALSE(fast.pendingClick);
}

// 0x7d366ec: WM_LBUTTONUP sets it with the mouse at that moment and it lasts while the mouse does not move;
// WM_LBUTTONDOWN clears it (0x4b3cea, 0x4b344d and 0x4b3947).
TEST(GameUiTest, TheReleaseLatchLastsWhileTheMouseStaysStill)
{
    Client::Kernel::MouseState state;
    // Pressed and released in the same step: the last message is the release.
    Client::Kernel::LatchTheLeftRelease(state, true, true, false, 10, 20);
    EXPECT_TRUE(state.releasedClick);
    EXPECT_EQ(state.releaseX, 10);
    EXPECT_EQ(state.releaseY, 20);
    Client::Kernel::LatchTheLeftRelease(state, false, false, false, 10, 20);
    EXPECT_TRUE(state.releasedClick);
    Client::Kernel::LatchTheLeftRelease(state, false, false, false, 11, 20);
    EXPECT_FALSE(state.releasedClick);
    Client::Kernel::LatchTheLeftRelease(state, false, true, false, 5, 5);
    EXPECT_TRUE(state.releasedClick);
    Client::Kernel::LatchTheLeftRelease(state, true, false, true, 5, 5);
    EXPECT_FALSE(state.releasedClick);
    // Released and pressed again: the last one is the press.
    Client::Kernel::LatchTheLeftRelease(state, true, true, true, 5, 5);
    EXPECT_FALSE(state.releasedClick);
}

class PickingFrame
{
public:
    Client::Core::Expected<void, std::string> Pick(bool alt = false)
    {
        return Client::Hud::PickInTheFrame(FramePickingContext{.world = world,
                                                               .random = random,
                                                               .ownRecordDepth = world.Runtime().ownRecordDepth,
                                                               .targets = world.Runtime().selectedTargets,
                                                               .altDown = alt,
                                                               .cameraPosition = {}});
    }
    WorldState world;
    CrtRandom random;
};

TEST(GameUiTest, ThePickingOfTheWizardAndTheElfReadsTheOwnRecord)
{
    for (const std::uint8_t heroClass : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{1}, std::uint8_t{3}})
    {
        PickingFrame tested;
        tested.world.Hero().characterClass = heroClass;
        const int draws = heroClass == 0 || heroClass == 2 ? SkillBarRandoms : 0;
        const CrtRandom expected = Advanced(tested.random, draws);
        ASSERT_TRUE(tested.Pick().HasValue());
        EXPECT_EQ(tested.random.State(), expected.State()) << int{heroClass};
        EXPECT_EQ(tested.world.Runtime().selectedTargets, (std::array<std::int32_t, 5>{-1, -1, -1, -1, -1}));
    }
}

TEST(GameUiTest, ThePickingSkipsTheOwnRecordWithTheUiOrAlt)
{
    PickingFrame tested;
    tested.world.Runtime().ui.mouseOnUi = 1;
    auto before = tested.random.State();
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(tested.random.State(), before);
    tested.world.Runtime().ui.mouseOnUi = 0;
    before = tested.random.State();
    ASSERT_TRUE(tested.Pick(true).HasValue());
    EXPECT_EQ(tested.random.State(), before);
}

TEST(GameUiTest, ThePickingAsksTheWorldWidthWithThePartyClosed)
{
    PickingFrame tested;
    tested.world.Hero().characterClass = 1;
    tested.world.Runtime().panels.inventory = 1;
    const CrtRandom expected = Advanced(tested.random, 2);
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
}

// 0x40e6c0: the mouse over the inventory's set label turns on the set window (+0x9304) and spends the click, without
// looking at it (Q-C472).
TEST(GameUiTest, TheMouseOverTheSetLabelOpensTheSetWindow)
{
    SteppingUi tested;
    tested.Panels().inventory = 1;
    tested.world.Runtime().hud.inventoryX = 0x1c2;
    tested.Click();
    const auto stepped = tested.Step(0x1c2 + 0x20, 0x20);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.world.ItemSets().byte9304, 1);
    EXPECT_FALSE(tested.world.Runtime().mouse.pendingClick);
    EXPECT_EQ(tested.world.Runtime().mouse.updateTimeMax, 6);

    tested.world.ItemSets().byte9304 = 0;
    ASSERT_TRUE(tested.Step(0x1c2 + 0xb0, 0x20).HasValue());
    EXPECT_EQ(tested.world.ItemSets().byte9304, 0);
}

// 0x5a00a1..0x5a0124: PgUp and PgDn scroll the log as many lines as it shows.
TEST(GameUiTest, PageUpAndPageDownScrollTheChatLog)
{
    SteppingUi tested;
    auto& log = tested.world.Runtime().chat.log;
    tested.Ui().chatModes.placement = 1;
    for (int i = 0; i < 20; ++i)
        log.entries.push_back(Client::Chat::ChatEntry{.name = "", .text = "x", .type = 0});
    Press(tested.input, Key::PageUp);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(log.scroll, log.rows);
    Release(tested.input, Key::PageUp);
    Press(tested.input, Key::PageDown);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(log.scroll, 0);
}

// 0x5a0124..0x5a03c3: with the focus on the message EDIT, up brings the last thing said; with the focus on the game,
// it moves the whisper name.
TEST(GameUiTest, TheArrowsWalkTheHistoryOrTheWhisperNames)
{
    SteppingUi tested;
    auto& input = tested.world.Runtime().chat.input;
    Client::Chat::ShowTheChatFields(input, 0);
    Client::Chat::RememberTheMessage(input, "dicho");
    Press(tested.input, Key::Up);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(input.message.text.Value(), "dicho");
    EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x19});

    Release(tested.input, Key::Up);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    Client::Chat::ShowTheChatFields(input, 3);
    Press(tested.input, Key::Up);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(input.whisperNameAt, Client::Chat::FirstWhisperNameSlot - 1);
}

// With the chat open, the digits belong to the EDIT and do not choose the skill.
TEST(GameUiTest, TheDigitsWithTheChatOpenDoNotPickASkill)
{
    SteppingUi tested;
    tested.Ui().chatOpen = true;
    Client::Chat::ShowTheChatFields(tested.world.Runtime().chat.input, 0);
    Press(tested.input, Key::Digit1);
    const auto stepped = tested.Step(320, 100);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
}

// 0x5ac8d0: F2 changes the log's normal chat, F3 the whisper mode and F4 the log's size and corner, each with 0x19.
TEST(GameUiTest, TheFunctionKeysChangeTheChatModes)
{
    SteppingUi tested;
    const auto keys = [&tested]
    {
        return Client::Hud::StepTheFunctionKeys(GameUiContext{
            .world = tested.world, .random = tested.random, .input = tested.input, .mouseX = 320, .mouseY = 100});
    };
    ASSERT_TRUE(keys().HasValue());
    EXPECT_FALSE(tested.world.Runtime().lastSoundId.has_value());

    Press(tested.input, Key::F2);
    ASSERT_TRUE(keys().HasValue());
    EXPECT_FALSE(tested.Ui().chatModes.showChat);
    EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x19});

    Press(tested.input, Key::F3);
    ASSERT_TRUE(keys().HasValue());
    EXPECT_FALSE(tested.world.Runtime().chat.input.whisperMode);
    // The latch: the F2 that is still down does not change again.
    EXPECT_FALSE(tested.Ui().chatModes.showChat);

    Press(tested.input, Key::F4);
    ASSERT_TRUE(keys().HasValue());
    EXPECT_EQ(tested.Ui().chatModes.placement, 1);
    EXPECT_EQ(tested.world.Runtime().chat.log.rows, 3);
    EXPECT_EQ(tested.world.Runtime().hud.chatFrame.x, 0xba);
    EXPECT_EQ(tested.world.Runtime().hud.chatFrame.y, 0x1a4);
}

// 0x5ac9ce..0x5aca42: F1 goes from 0 to 1, to 2 and back to 0, and from 3 (the item's help) or 4 (the /move window) to
// 0; it turns off the set label (+0x9305) and asks for the 0x19. Held down it does not go again.
TEST(GameUiTest, TheF1KeyWalksTheHelps)
{
    SteppingUi tested;
    std::int32_t& mode = tested.world.Runtime().panels.overlayMode;
    const auto keys = [&tested]
    {
        return Client::Hud::StepTheFunctionKeys(GameUiContext{
            .world = tested.world, .random = tested.random, .input = tested.input, .mouseX = 320, .mouseY = 100});
    };
    const auto f1 = [&]
    {
        Press(tested.input, Key::F1);
        const bool pressed = keys().HasValue();
        Release(tested.input, Key::F1);
        return pressed && keys().HasValue();
    };
    tested.world.ItemSets().byte9305 = 5;
    ASSERT_TRUE(f1());
    EXPECT_EQ(mode, 1);
    EXPECT_EQ(tested.world.ItemSets().byte9305, 0);
    EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x19});
    ASSERT_TRUE(f1());
    EXPECT_EQ(mode, 2);
    ASSERT_TRUE(f1());
    EXPECT_EQ(mode, 0);
    mode = 3;
    ASSERT_TRUE(f1());
    EXPECT_EQ(mode, 0);
    mode = 4;
    ASSERT_TRUE(f1());
    EXPECT_EQ(mode, 0);

    Press(tested.input, Key::F1);
    ASSERT_TRUE(keys().HasValue());
    ASSERT_TRUE(keys().HasValue());
    EXPECT_EQ(mode, 1);
}

// 0x637cff: Print Screen is not one of the game's keys, but of what runs after any scene's step
// (Scenes::Screenshots): the function keys do not touch its latch.
TEST(GameUiTest, TheFunctionKeysLeaveThePrintScreenKey)
{
    SteppingUi tested;
    Press(tested.input, Key::PrintScreen);
    const auto keys = Client::Hud::StepTheFunctionKeys(GameUiContext{
        .world = tested.world, .random = tested.random, .input = tested.input, .mouseX = 320, .mouseY = 100});
    ASSERT_TRUE(keys.HasValue()) << keys.Error();
    EXPECT_EQ(tested.Ui().keyLatches[0x2c], 0);
}

class TypedLetters final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

// 0x5a116f..0x5a2b47: Alt and a digit say the macro and brake the others until 0x77f3b74 goes back to 0. Without the
// chat input, the error.
TEST(GameUiTest, AltAndADigitSayTheMacro)
{
    SteppingUi tested;
    tested.world.Runtime().chat.input.macros[0] = "uno";
    Press(tested.input, Key::AltLeft);
    Press(tested.input, Key::Digit1);
    const auto alone = tested.Step(320, 100);
    ASSERT_FALSE(alone.HasValue());
    EXPECT_NE(alone.Error().find("0x5a11cb"), std::string::npos);

    const auto texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    const TypedLetters letters;
    std::vector<Client::Protocol::Outgoing> sent;
    const Client::Chat::GameChatContext chat{.world = tested.world,
                                             .random = tested.random,
                                             .texts = &texts,
                                             .measure = &letters,
                                             .send = [&sent](Client::Protocol::Outgoing packet)
                                             { sent.push_back(std::move(packet)); }};
    const auto step = [&]
    {
        return Client::Hud::StepTheGameUi(GameUiContext{.world = tested.world,
                                                        .random = tested.random,
                                                        .input = tested.input,
                                                        .mouseX = 320,
                                                        .mouseY = 100,
                                                        .chat = &chat});
    };
    const auto said = step();
    ASSERT_TRUE(said.HasValue()) << said.Error();
    EXPECT_EQ(sent.size(), 1u);
    EXPECT_EQ(tested.world.Runtime().hud.macroTime, Client::Chat::MacroLockSteps);
    ASSERT_TRUE(step().HasValue());
    EXPECT_EQ(sent.size(), 1u);
}

// A message box (0x48ee60 case 9) in the game's manager, opened by hand: in CL8 nobody opens it.
std::int32_t OpenABox(WorldState& world, std::int32_t x, std::int32_t y)
{
    const TypedLetters letters;
    const auto opened = Client::Ui::CreateTheOldWindow(
        world.Runtime().oldWindows,
        Client::Ui::OldWindowRequest{.kind = Client::Ui::QuestionCase,
                                     .x = x,
                                     .y = y,
                                     .data = "Seguro?",
                                     .aux = 0,
                                     .flags = Client::Ui::KeepPlaceFlag},
        Client::Ui::OldWindowTexts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No"},
        Client::Ui::OldWindowMeasure{
            .measure = letters, .dcFont = Client::Render::Text::FontKind::Normal, .screen = {640, 480}});
    EXPECT_TRUE(opened.HasValue()) << opened.Error();
    return opened.HasValue() ? opened.Value() : 0;
}

// The real friends window (0x491080 from the level, CL9 milestone 9): panel 1 open means it is there.
void OpenTheFriendsWindowIn(WorldState& world)
{
    const TypedLetters letters;
    auto& runtime = world.Runtime();
    const Client::Ui::OldWindowTexts texts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No"};
    const Client::Ui::OldWindowMeasure measure{
        .measure = letters, .dcFont = Client::Render::Text::FontKind::Normal, .screen = {640, 480}};
    const Client::Ui::OldWindowStepView view{.texts = &texts, .measure = &measure};
    const auto opened = Client::Messenger::OpenTheFriendsWindow(runtime.oldWindows, Client::Messenger::FriendsWindowX,
                                                                Client::Messenger::FriendsWindowY,
                                                                Client::Hud::OldWindowGlobalsOf(runtime), view);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_TRUE(opened.Value());
}

// 0x636e3c..0x636e5a: the window list and the old windows take the mouse before the chat log; over a box, the click
// is not the game's (0x636e5d) and the manager's focus stays.
TEST(GameUiTest, TheOldWindowsTakeTheMouseBeforeTheChatLog)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    // The first step leaves the log in its corner (0x45a9d0(−10, 0x65)).
    ASSERT_TRUE(tested.Step(320, 240).HasValue());
    ASSERT_TRUE(tested.Step(2, 30).HasValue());
    ASSERT_EQ(tested.Ui().hovered, Client::Chat::ChatLogId);

    const std::int32_t box = OpenABox(tested.world, 0, 0);
    tested.Click();
    ASSERT_TRUE(tested.Step(2, 30).HasValue());
    EXPECT_EQ(tested.Ui().hovered, box);
    EXPECT_EQ(tested.Ui().mouseOnUi, 1);
    EXPECT_EQ(runtime.oldWindows.focus, box);
}

// Outside the windows, the click takes their focus away and, with the chat closed, gives the keyboard to the game
// (0x636e87..0x636ed1).
TEST(GameUiTest, TheUnownedClickTakesTheFocusFromTheOldWindows)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    const std::int32_t box = OpenABox(tested.world, 0, 0);
    runtime.oldWindows.focus = box;
    runtime.chat.input.focus = Client::Chat::ChatFocus::Message;
    tested.Click();
    ASSERT_TRUE(tested.Step(320, 240).HasValue());
    EXPECT_EQ(runtime.oldWindows.focus, 0);
    EXPECT_EQ(runtime.chat.input.focus, Client::Chat::ChatFocus::Game);
}

// Q-C446: 0x623e70 reads Esc with the friends window (panel 1) closed, and opens the menu; with it open, it reaches
// 0x490490 and closes the top box.
TEST(GameUiTest, EscapeReachesTheOldWindowsOnlyWithTheFriendsWindowOpen)
{
    for (const bool friendsOpen : {false, true})
    {
        SCOPED_TRACE(friendsOpen);
        // On the heap: the WorldState takes about 867 KB and, with milestone 10's friends window, the step does not fit
        // in Windows' 1 MB stack.
        const auto owned = std::make_unique<SteppingUi>();
        SteppingUi& tested = *owned;
        auto& runtime = tested.world.Runtime();
        if (friendsOpen)
            OpenTheFriendsWindowIn(tested.world);
        const std::int32_t box = OpenABox(tested.world, 300, 0);
        Press(tested.input, Key::Escape);
        const auto dialogs = tested.Dialogs(639, 300);
        ASSERT_TRUE(dialogs.HasValue()) << dialogs.Error();
        // The friends one measures its state button's text in each step (0x4a3030).
        const TypedLetters letters;
        const auto stepped = Client::Hud::StepTheGameUi(GameUiContext{.world = tested.world,
                                                                      .random = tested.random,
                                                                      .input = tested.input,
                                                                      .mouseX = 639,
                                                                      .mouseY = 300,
                                                                      .measure = &letters});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(runtime.oldWindows.windows.contains(box), !friendsOpen);
        EXPECT_EQ(runtime.dialogs.current == Client::Ui::EscapeMenuDialog, !friendsOpen);
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
    }
}

// 0x490538..0x490548: F5 is only read with the keyboard in the game window.
TEST(GameUiTest, F5IsReadOnlyWithTheKeyboardInTheGame)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    Press(tested.input, Key::F5);
    runtime.chat.input.focus = Client::Chat::ChatFocus::Message;
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(tested.Ui().keyLatches[0x74], 0);
    runtime.chat.input.focus = Client::Chat::ChatFocus::Game;
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(tested.Ui().keyLatches[0x74], 1);
}

// Q-C573: in CL8's game there are no old windows, and the first F6 leaves 0x490490 without F6 forever; it does not
// hide a box opened later either.
TEST(GameUiTest, TheFirstF6LeavesF6Stuck)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms);
    Press(tested.input, Key::F6);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(runtime.oldWindows.hiddenByF6, 1);
    Release(tested.input, Key::F6);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());

    const std::int32_t box = OpenABox(tested.world, 300, 0);
    Press(tested.input, Key::F6);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(runtime.oldWindows.windows.at(box).base.state, Client::Ui::ShownWindow);
    EXPECT_EQ(tested.Ui().keyLatches[0x75], 1);
}

// 0x59ee4b..0x59ef10: the selected monster (+0x88 at 2) stays with the automatic attack; in map 6, not, and it also
// drops 0x6c8e38. The other cases, CharacterPickingTest.
TEST(GameUiTest, ThePreviousMonsterStaysOutsideTheMapSix)
{
    PickingFrame tested;
    auto& runtime = tested.world.Runtime();
    tested.world.Actors()[5].byte88 = 2;
    runtime.selectedTargets[2] = 5;
    runtime.selectedTargets[4] = 5;
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(runtime.selectedTargets[2], 5);
    EXPECT_EQ(runtime.selectedTargets[4], 5);
    runtime.map = 6;
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(runtime.selectedTargets[2], -1);
    EXPECT_EQ(runtime.selectedTargets[4], -1);
}
} // namespace
