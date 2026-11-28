#include "Network/Xor32Packet.h"
#include "Events/EventChipWindow.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Outgoing.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Kernel/ActorState.h"
#include "Events/EventGlobals.h"
#include "Chat/GameChat.h"
#include "Hud/GameHud.h"
#include "Hud/GamePanels.h"
#include "Hud/GameUi.h"
#include "Monsters/NpcNames.h"
#include "Kernel/OwnInventory.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "FrontEnd/SelectionInventory.h"
#include "Chat/ChatInput.h"
#include "Ui/GameDialogs.h"
#include "Ui/OldControls.h"
#include "Ui/PanelRegistry.h"
#include "Ui/Widget.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"

#include "support/RecordingTextDrawer.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// CL9 milestone 11, phase 4: the window of 0x78abb00 (0x5f6550, its step 0x5e6520 and the serial number entry) against
// the ASM of c32bbd5f, with no GL trace (*static*). Text.bmd is made up; so is the name of NPC 236.
namespace
{
using Client::Crt::CrtRandom;
using Client::Hud::GameHudContext;
using Client::Hud::GameUiContext;
using Client::Kernel::WorldState;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Text::FontKind;

constexpr std::uint32_t Seed = 0x13579bdfu;
constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr std::string_view InventedNpc = "arquero inventado";

std::map<std::size_t, std::string> ChipTexts()
{
    std::map<std::size_t, std::string> texts{
        {243, "registrar rena"},   {245, "renas"},     {246, "registradas"}, {706, "muto b"},
        {818, "registrar piedra"}, {819, "piedras"},   {820, "entregadas"},  {893, "serie a"},
        {894, "serie b"},          {895, "confirmar"}, {896, "sin lugar"},   {903, "pedir numero"},
        {916, "serie c"},          {917, "serie d"},   {1002, "cerrar"},
    };
    for (std::size_t line = 0; line < 4; ++line)
    {
        texts[700 + line] = "rena " + std::to_string(line);
        texts[882 + line] = "piedra " + std::to_string(line);
    }
    texts[891] = "historia 0";
    texts[892] = "historia 1";
    texts[897] = "historia 2";
    texts[898] = "historia 3";
    texts[900] = "muto a";
    texts[901] = "muto c";
    texts[902] = "muto d";
    return texts;
}

struct DrawnQuad
{
    std::uint16_t slot = 0;
    Color color{};
    float x = 0.0f;
    float top = 0.0f;
};

std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture || command.texture == Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const Client::Render::SpriteVertex& first = vertices[command.firstVertex + quad * 6];
            quads.push_back(
                DrawnQuad{.slot = command.texture->value, .color = first.color, .x = first.x, .top = 480.0f - first.y});
        }
    }
    return quads;
}

// How many rand() there are from `from` to `to` (up to 100000).
std::uint32_t DrawsBetween(CrtRandom from, std::uint32_t to)
{
    for (std::uint32_t draws = 0; draws < 100000; ++draws)
    {
        if (from.State() == to)
            return draws;
        from.Discard(1);
    }
    ADD_FAILURE() << "más de 100.000 rand()";
    return 0;
}

// The x of 0x623690 with the fake measure (6 columns per letter) in 640 × 480.
int Centered(int x, std::string_view text)
{
    return x - static_cast<int>(text.size()) * Client::Tests::RecordingTextDrawer::LetterWidth / 2;
}

std::vector<std::byte> Decoded(const Client::Protocol::Outgoing& packet)
{
    const auto plain = Client::Network::Xor32Decode(packet.packet);
    const auto bytes = plain.Bytes();
    return {bytes.begin(), bytes.end()};
}

std::vector<std::byte> Bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    for (const int value : values)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

class EventChipWindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Reset(0);
    }

    // A new world in `map`, with the inventory empty (0x5bc740) and open.
    void Reset(std::int32_t map)
    {
        world = std::make_unique<WorldState>();
        // The game arrives after 0x5bbe10: the chaos box has types 0xffff, not the BSS zero.
        Client::FrontEnd::ClearSelectionInventory(*world, random);
        world->Inventory().ClearForInventoryReply();
        Runtime().map = map;
        Runtime().panels.inventory = 1;
        sprites.Begin(Screen640);
        text.texts.clear();
        sent.clear();
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return world->Runtime();
    }

    Client::Core::Expected<void, std::string> Draw(Client::Ui::Point mouse, bool projecting = false,
                                                   bool withNames = true)
    {
        Client::Kernel::WorldRuntime& runtime = Runtime();
        Client::Kernel::ActorState& hero = world->Hero();
        GameHudContext context{.world = *world,
                               .random = random,
                               .ownRecordDepth = runtime.ownRecordDepth,
                               .cellXDepth = hero.cellXAccesses,
                               .cellYDepth = hero.cellYAccesses,
                               .hud = runtime.hud,
                               .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                               .sound = sound,
                               .projecting = projecting,
                               .sprites = sprites,
                               .text = text,
                               .texts = &texts,
                               .worldClock = worldClock,
                               .mouse = mouse};
        // 0x7681eb8 (CL9 milestone 2): NPC 236 in record 7 of the table, with a made-up name.
        Client::Monsters::NpcName& record = runtime.npcNames[7];
        record = {};
        if (withNames)
        {
            record.type = 236;
            std::ranges::copy(InventedNpc, record.name.begin());
        }
        return Client::Events::DrawTheEventChipWindow(context);
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y)
    {
        const Client::Chat::GameChatContext chat{
            .world = *world, .random = random, .send = [this](Client::Protocol::Outgoing packet) {
                sent.push_back(std::move(packet));
            }};
        const GameUiContext context{.world = *world,
                                    .random = random,
                                    .input = input,
                                    .mouseX = x,
                                    .mouseY = y,
                                    .chat = &chat,
                                    .texts = &texts};
        Client::Hud::GamePanels panels(*world, random);
        return Client::Events::StepTheEventChipWindow(context, panels.Registry());
    }

    // The rand() state after 0x65cae0(0x4000000, 0) with the current panels.
    std::uint32_t AfterClosingTheInventory()
    {
        auto other = std::make_unique<WorldState>();
        other->Runtime().panels = Runtime().panels;
        other->Runtime().cursorItemCount = Runtime().cursorItemCount;
        other->Runtime().npcWindows.chaosBox = Runtime().npcWindows.chaosBox;
        CrtRandom expected = random;
        Client::Hud::GamePanels panels(*other, expected);
        EXPECT_TRUE(panels.Registry().Close(Client::Ui::InventoryPanel).HasValue());
        return expected.State();
    }

    void PutItem(std::size_t cell, std::uint16_t type, std::uint32_t level)
    {
        Client::Kernel::ItemState& item = world->Inventory().cells[cell];
        item.id = type;
        item.scalar1Low = level << 3;
        item.occupied = 1;
    }

    std::unique_ptr<WorldState> world;
    CrtRandom random{Seed};
    std::optional<Client::Audio::SoundId> sound;
    float worldClock = 0.0f;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Input::InputState input;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable(ChipTexts());
    std::vector<Client::Protocol::Outgoing> sent;
};

// ----- The step (0x5e6520).

// With 4: releasing the click (0x7d366ec) over the button at 0x1e5..0x25d × 0xdc..0xf4 sends C1 04 98 01 and clears
// the release; the mouse in [0x1c2, 0x280) × [0, 0x1b1) belongs to the UI. Without a release, nothing. No rand(). In
// Lorencia and Devias.
TEST_F(EventChipWindowTest, TheMutoButtonAsksForTheNumber)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        Runtime().panels.field78abb00 = Client::Events::MutoNumberWindow;
        const std::uint32_t before = random.State();
        ASSERT_TRUE(Step(0x1e5, 0xdc).HasValue());
        EXPECT_TRUE(sent.empty());
        EXPECT_EQ(Runtime().ui.mouseOnUi, 1);

        Runtime().mouse.releasedClick = true;
        const auto stepped = Step(0x25c, 0xf3);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(), Bytes({0xC1, 0x04, 0x98, 0x10})));
        EXPECT_TRUE(Client::Tests::SameBytes(Decoded(sent[0]), Bytes({0xC1, 0x04, 0x98, 0x01})));
        EXPECT_FALSE(Runtime().mouse.releasedClick);
        EXPECT_EQ(random.State(), before);

        // Outside the button (0xf4), the release is cleared anyway at the end (0x5e74cc).
        sent.clear();
        Runtime().mouse.releasedClick = true;
        Runtime().ui.mouseOnUi = 0;
        ASSERT_TRUE(Step(0x1e5, 0xf4).HasValue());
        EXPECT_TRUE(sent.empty());
        EXPECT_FALSE(Runtime().mouse.releasedClick);
        // 0x1b1 no longer belongs to the UI.
        Runtime().ui.mouseOnUi = 0;
        ASSERT_TRUE(Step(0x1e5, 0x1b1).HasValue());
        EXPECT_EQ(Runtime().ui.mouseOnUi, 0);
    }
}

// With 1, 2 or another that is neither 3 nor 4: the release over 0x1e5..0x25d × 0xe7..0xfd is cleared and, if 0x5583a0
// finds 0x1c15 of level 1 (with 2) or 0 (with the others), C1 05 95 goes out with 0x78abb00 − 1 and the raw place.
// Without the item, nothing. No rand(). In Lorencia and Devias.
TEST_F(EventChipWindowTest, TheChipButtonRegistersTheRenaOrTheStone)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        PutItem(31, 0x1c15, 0);
        PutItem(5, 0x1c15, 1);
        const std::uint32_t before = random.State();

        Runtime().panels.field78abb00 = Client::Events::RenaChipWindow;
        Runtime().mouse.releasedClick = true;
        ASSERT_TRUE(Step(0x1e5, 0xe7).HasValue());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(), Bytes({0xC1, 0x05, 0x95, 0x1C, 0xBF})));
        EXPECT_TRUE(Client::Tests::SameBytes(Decoded(sent[0]), Bytes({0xC1, 0x05, 0x95, 0x00, 0x1F})));
        EXPECT_FALSE(Runtime().mouse.releasedClick);

        Runtime().panels.field78abb00 = Client::Events::StoneChipWindow;
        Runtime().mouse.releasedClick = true;
        ASSERT_TRUE(Step(0x25c, 0xfc).HasValue());
        ASSERT_EQ(sent.size(), 2u);
        EXPECT_TRUE(Client::Tests::SameBytes(sent[1].packet.Bytes(), Bytes({0xC1, 0x05, 0x95, 0x1D, 0xA4})));
        EXPECT_TRUE(Client::Tests::SameBytes(Decoded(sent[1]), Bytes({0xC1, 0x05, 0x95, 0x01, 0x05})));

        // With 5 it looks for the level 0 Rena and sends type 4.
        Runtime().panels.field78abb00 = 5;
        Runtime().mouse.releasedClick = true;
        ASSERT_TRUE(Step(0x1e5, 0xe7).HasValue());
        ASSERT_EQ(sent.size(), 3u);
        EXPECT_TRUE(Client::Tests::SameBytes(Decoded(sent[2]), Bytes({0xC1, 0x05, 0x95, 0x04, 0x1F})));
        EXPECT_EQ(random.State(), before);

        // Without the Rena, the release is spent without sending.
        world->Inventory().cells[31].occupied = 0;
        Runtime().panels.field78abb00 = Client::Events::RenaChipWindow;
        Runtime().mouse.releasedClick = true;
        ASSERT_TRUE(Step(0x1e5, 0xe7).HasValue());
        EXPECT_EQ(sent.size(), 3u);
        EXPECT_FALSE(Runtime().mouse.releasedClick);
    }
}

// With 3, the confirm button at (ftol(0x1c2 + 35), ftol(corner + 290)), 120 × 22: with some free slot
// (0x558740(−1, −1)) C1 12 9D with the 12 bytes of the EDIT of [0x5763504] in three pieces of 4 and a 0; a shorter
// text leaves zeros where the original leaves the stack (P-C247). Then the left button released (0x7d36874).
TEST_F(EventChipWindowTest, TheSerialButtonSendsTheNumber)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        Runtime().panels.field78abb00 = Client::Events::SerialNumberWindow;
        Runtime().chat.input.dialogEdit.text.SetText("AB12CD34EF56");
        Runtime().mouse.releasedClick = true;
        Runtime().mouse.leftHeld = true;
        const std::uint32_t before = random.State();
        const auto stepped = Step(485, 290);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(),
                                             Bytes({0xC1, 0x12, 0x9D, 0x55, 0xAB, 0x28, 0x85, 0xF6, 0x96, 0x7A, 0xB7,
                                                    0x35, 0x7C, 0x64, 0x1B, 0x73, 0xCF, 0x04})));
        EXPECT_FALSE(Runtime().mouse.leftHeld);
        EXPECT_FALSE(Runtime().mouse.releasedClick);
        EXPECT_EQ(random.State(), before);

        Runtime().chat.input.dialogEdit.text.SetText("Q7X");
        Runtime().mouse.releasedClick = true;
        ASSERT_TRUE(Step(485 + 119, 290 + 21).HasValue());
        ASSERT_EQ(sent.size(), 2u);
        EXPECT_TRUE(Client::Tests::SameBytes(
            Decoded(sent[1]), Bytes({0xC1, 0x12, 0x9D, 'Q', '7', 'X', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})));

        // Outside the button (y + 22), nothing, and the left button stays.
        Runtime().mouse.releasedClick = true;
        Runtime().mouse.leftHeld = true;
        ASSERT_TRUE(Step(485, 290 + 22).HasValue());
        EXPECT_EQ(sent.size(), 2u);
        EXPECT_TRUE(Runtime().mouse.leftHeld);
    }
}

// Without free slots, 0x5e6d22 opens notice 896 and sends nothing; the left button is released anyway. More than 12
// bytes in the EDIT (which only a SetText the game does not do would put) would overwrite the step's stack: it stops.
TEST_F(EventChipWindowTest, TheSerialButtonWarnsWithAFullInventory)
{
    for (Client::Kernel::ItemState& item : world->Inventory().cells)
    {
        item.id = 0x0001;
        item.occupied = 1;
    }
    Runtime().panels.field78abb00 = Client::Events::SerialNumberWindow;
    Runtime().chat.input.dialogEdit.text.SetText("AB12");
    Runtime().mouse.releasedClick = true;
    Runtime().mouse.leftHeld = true;
    ASSERT_TRUE(Step(500, 300).HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(Runtime().dialogs.current, Client::Ui::MessageBoxDialog);
    EXPECT_EQ(std::string_view(Runtime().dialogBox.rows.data()), "sin lugar");
    EXPECT_FALSE(Runtime().mouse.leftHeld);

    Reset(0);
    Runtime().panels.field78abb00 = Client::Events::SerialNumberWindow;
    Runtime().chat.input.dialogEdit.text.SetText("ABCDEFGHIJKLM");
    Runtime().mouse.releasedClick = true;
    const auto cut = Step(500, 300);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5e6d68"), std::string::npos) << cut.Error();
}

// 0x5e7255..0x5e74c6: the click (0x7d366d4) on the close button (0x19, 0x18b from the corner, 24 × 24) clears the
// click, sets 0x77f3b10 to 0 and 0x6c8dcc to 6, sends C1 03 97 in the clear, 0x78abb00 at 0, closes the inventory
// (with the rand() of 0x65cae0), empties the chat buffers, closes it, zeroes the zen mode, the typed zen, 0x78abacc and
// 0x77f3b6b and gives the focus to the game window; 0x553570 (with 0x6c7b8c at 1) also empties the text of the EDIT of
// [0x5763504]. In Lorencia and Devias, with each type.
TEST_F(EventChipWindowTest, TheCloseButtonLeavesTheWindow)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::int32_t window : {1, 2, 3, 4})
        {
            SCOPED_TRACE(map * 10 + window);
            Reset(map);
            Runtime().panels.field78abb00 = window;
            Runtime().panels.field78abb04 = 9;
            Runtime().hud.inventoryX = 0x1c2;
            Runtime().hud.inventoryY = 0;
            Runtime().events.serialEntry = window == Client::Events::SerialNumberWindow;
            Client::Chat::ChatInputState& chat = Runtime().chat.input;
            chat.focus = Client::Chat::ChatFocus::Dialog;
            chat.dialogEdit.text.SetText("AB12");
            chat.messageBuffer[0] = 'm';
            chat.messageLength = 1;
            Runtime().ui.chatOpen = true;
            Runtime().social.moneyField = 1;
            Runtime().social.typedMoney = 500;
            Runtime().social.trade.field78abacc = 2;
            Runtime().mouse.pendingClick = true;
            Runtime().mouse.updateTime = 3;
            const std::uint32_t closed = AfterClosingTheInventory();

            const auto stepped = Step(0x1c2 + 0x19 + 23, 0x18b + 23);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            ASSERT_EQ(sent.size(), 1u);
            EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(),
                                                 Client::Protocol::Events::WriteEventWindowLeave().packet.Bytes()));
            EXPECT_EQ(random.State(), closed);
            EXPECT_EQ(Runtime().panels.field78abb00, 0);
            EXPECT_EQ(Runtime().panels.field78abb04, 0);
            EXPECT_EQ(Runtime().panels.inventory, 0);
            EXPECT_FALSE(Runtime().mouse.pendingClick);
            EXPECT_EQ(Runtime().mouse.updateTime, 0);
            EXPECT_EQ(Runtime().mouse.updateTimeMax, 6);
            EXPECT_FALSE(Runtime().ui.chatOpen);
            EXPECT_FALSE(Runtime().events.serialEntry);
            EXPECT_EQ(Runtime().social.moneyField, 0);
            EXPECT_EQ(Runtime().social.typedMoney, 0);
            EXPECT_EQ(Runtime().social.trade.field78abacc, 0);
            EXPECT_EQ(chat.focus, Client::Chat::ChatFocus::Game);
            EXPECT_EQ(chat.messageBuffer[0], '\0');
            EXPECT_EQ(chat.messageLength, 0);
            EXPECT_EQ(chat.dialogEdit.text.Value(), "");
        }
    }
}

// The close draws the rand() of the protected reads of 0x65cae0 (the trade and the NPC shop, 0x7d531f8 and
// 0x7d53710): two with the inventory open.
TEST_F(EventChipWindowTest, TheCloseButtonDrawsTheInventoryCloseRandoms)
{
    Runtime().panels.field78abb00 = Client::Events::RenaChipWindow;
    Runtime().mouse.pendingClick = true;
    const CrtRandom before = random;
    ASSERT_TRUE(Step(0x19, 0x18b).HasValue());
    EXPECT_EQ(DrawsBetween(before, random.State()), 2u);
}

// ----- The serial number entry (0x4dd8ce..0x4dd920 and 0x56b6a9..0x56b72a).

// 0x4dd8ce..0x4dd920: the serial number zeroed, 0x553570(0) (the chosen field and the message buffer, not the
// whisper's), the chat closed, the zen mode, the typed zen and 0x78abacc at 0, the field length at 0xc, a single field
// for Tab and 0x77f3b6b at 1. 0x56b6a9..0x56b72a: the chosen field at 0, the message buffer empty (not the whisper's),
// the EDIT empty, the chat closed, the zen mode, the typed zen and 0x78abacc at 0 and 0x77f3b6b at 0, without touching
// the focus.
TEST_F(EventChipWindowTest, TheSerialEntryOpensAndCloses)
{
    Client::Chat::ChatInputState& chat = Runtime().chat.input;
    Runtime().events.serial[0] = std::byte{0x41};
    Runtime().ui.chatOpen = true;
    chat.messageBuffer[0] = 'm';
    chat.messageLength = 1;
    chat.whisperBuffer[0] = 'w';
    chat.fieldCount = 2;
    Client::Social::SocialState& social = Runtime().social;
    social.moneyField = 1;
    social.typedMoney = 500;
    social.trade.field78abacc = 2;
    Client::Events::OpenTheSerialEntry(Runtime());
    EXPECT_EQ(Runtime().events.serial[0], std::byte{0});
    EXPECT_FALSE(Runtime().ui.chatOpen);
    EXPECT_EQ(chat.messageBuffer[0], '\0');
    EXPECT_EQ(chat.whisperBuffer[0], 'w');
    EXPECT_EQ(chat.fieldCount, 1);
    EXPECT_EQ(social.moneyField, 0);
    EXPECT_EQ(social.typedMoney, 0);
    EXPECT_EQ(social.trade.field78abacc, 0);
    EXPECT_EQ(social.dialogFieldLimit, 0xc);
    EXPECT_TRUE(Runtime().events.serialEntry);

    chat.dialogEdit.text.SetText("AB12");
    chat.numberField.text.SetText("1234");
    chat.focus = Client::Chat::ChatFocus::Dialog;
    chat.field = 1;
    chat.messageBuffer[0] = 'm';
    chat.whisperBuffer[0] = 'w';
    Runtime().ui.chatOpen = true;
    social.moneyField = 1;
    social.typedMoney = 500;
    social.trade.field78abacc = 2;
    Client::Events::CloseTheSerialEntry(Runtime());
    EXPECT_EQ(chat.field, Client::Chat::MessageField);
    EXPECT_EQ(chat.messageBuffer[0], '\0');
    EXPECT_EQ(chat.whisperBuffer[0], 'w');
    EXPECT_EQ(chat.dialogEdit.text.Value(), "");
    EXPECT_EQ(chat.numberField.text.Value(), "");
    EXPECT_EQ(chat.focus, Client::Chat::ChatFocus::Dialog);
    EXPECT_FALSE(Runtime().ui.chatOpen);
    EXPECT_EQ(social.moneyField, 0);
    EXPECT_EQ(social.typedMoney, 0);
    EXPECT_EQ(social.trade.field78abacc, 0);
    EXPECT_FALSE(Runtime().events.serialEntry);
}

// Subclass 0x467b10 with +0x4c at 2: digits and letters, lowercase to uppercase; nothing else.
TEST(EventChipEditTest, TheEditFilterTakesDigitsAndLetters)
{
    using Client::Ui::FilteredByTheEdit;
    EXPECT_EQ(FilteredByTheEdit(2, 'a'), std::optional<char>('A'));
    EXPECT_EQ(FilteredByTheEdit(2, 'Z'), std::optional<char>('Z'));
    EXPECT_EQ(FilteredByTheEdit(2, '7'), std::optional<char>('7'));
    EXPECT_EQ(FilteredByTheEdit(2, '-'), std::nullopt);
    EXPECT_EQ(FilteredByTheEdit(2, static_cast<char>(0xb0)), std::nullopt);
    EXPECT_EQ(FilteredByTheEdit(1, 'a'), std::nullopt);
    EXPECT_EQ(FilteredByTheEdit(1, '3'), std::optional<char>('3'));
    EXPECT_EQ(FilteredByTheEdit(0, '-'), std::optional<char>('-'));
    EXPECT_EQ(FilteredByTheEdit(0, static_cast<char>(0x1b)), std::nullopt);
    EXPECT_EQ(FilteredByTheEdit(2, static_cast<char>(0x1a)), std::nullopt);

    Client::Ui::OldEdit edit;
    edit.filter = Client::Ui::UppercaseLettersAndDigitsEdit;
    edit.text.SetLimit(12);
    Client::Ui::TypeInTheEdit(edit, "ab-12 cd34ef56gh");
    EXPECT_EQ(edit.text.Value(), "AB12CD34EF56");
}

// ----- The drawing (0x5f6550).

// 0x5f65df: the title is NPC 236 of the NpcName(%s).txt table (0x54ebb0). Without a record of that type, 0x54ebb0
// gives NULL and 0x554180 does not draw the title, like the quests' (0x41be9e): the window goes on the same, without
// stopping. In Lorencia and Devias.
TEST_F(EventChipWindowTest, WithoutTheNpcInTheTableTheTitleIsNotDrawn)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        Runtime().panels.field78abb00 = Client::Events::RenaChipWindow;
        const auto drawn = Draw({0, 0}, false, false);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        const auto& lines = text.texts;
        ASSERT_FALSE(lines.empty());
        EXPECT_EQ(lines[0].text, "registrar rena");
        for (const auto& line : lines)
            EXPECT_NE(line.text, InventedNpc);
    }
}

// With 1: the corner at (0x1c2, 0), the background, the title, button 0xf0 at (485, 231) in white with text 243 in
// bold (there are Renas), the close button, the four texts 700..703 from y 100 in steps of 15, label 245 and "x2" of
// the two level 0 Renas, 246 and "x7" of 0x78abb04. No rand(). In Lorencia and Devias.
TEST_F(EventChipWindowTest, TheRenaWindowDrawsItsCounts)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        PutItem(3, 0x1c15, 0);
        PutItem(4, 0x1c15, 0);
        PutItem(5, 0x1c15, 1);
        Runtime().panels.field78abb00 = Client::Events::RenaChipWindow;
        Runtime().panels.field78abb04 = 7;
        const std::uint32_t before = random.State();
        const auto drawn = Draw({0, 0});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), before);
        EXPECT_EQ(Runtime().hud.inventoryX, 0x1c2);
        EXPECT_EQ(Runtime().hud.inventoryY, 0);

        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 4u);
        EXPECT_EQ(quads[0].slot, 0x104);
        EXPECT_FLOAT_EQ(quads[0].x, 450.0f);
        EXPECT_EQ(quads[1].slot, 0x105);
        EXPECT_EQ(quads[2].slot, 0xf0);
        EXPECT_FLOAT_EQ(quads[2].x, 485.0f);
        EXPECT_FLOAT_EQ(quads[2].top, 231.0f);
        EXPECT_FLOAT_EQ(quads[2].color.red, 1.0f);
        EXPECT_EQ(quads[3].slot, 0x118);
        EXPECT_FLOAT_EQ(quads[3].x, 475.0f);
        EXPECT_FLOAT_EQ(quads[3].top, 395.0f);

        const auto& lines = text.texts;
        ASSERT_EQ(lines.size(), 10u);
        EXPECT_EQ(lines[0].text, InventedNpc);
        EXPECT_EQ(lines[0].x, 0x1c2 + 0x23);
        EXPECT_EQ(lines[0].y, 0xc);
        EXPECT_EQ(lines[1].text, "registrar rena");
        EXPECT_EQ(lines[1].x, Centered(0x221, "registrar rena"));
        EXPECT_EQ(lines[1].y, 0xec);
        EXPECT_EQ(lines[1].style.font, FontKind::Bold);
        EXPECT_EQ(lines[1].style.colors.text, 0xffd2e6ffu);
        for (std::size_t line = 0; line < 4; ++line)
        {
            SCOPED_TRACE(line);
            const std::string expected = "rena " + std::to_string(line);
            EXPECT_EQ(lines[2 + line].text, expected);
            EXPECT_EQ(lines[2 + line].x, Centered(0x1c2 + 95, expected));
            EXPECT_EQ(lines[2 + line].y, 100 + 15 * static_cast<int>(line));
            EXPECT_EQ(lines[2 + line].style.font, FontKind::Normal);
        }
        EXPECT_EQ(lines[6].text, "renas");
        EXPECT_EQ(lines[6].x, 0x1e5);
        EXPECT_EQ(lines[6].y, 0x122);
        EXPECT_EQ(lines[6].style.font, FontKind::Bold);
        EXPECT_EQ(lines[7].text, "x2");
        EXPECT_EQ(lines[7].x, 0x230);
        EXPECT_EQ(lines[7].y, 300);
        EXPECT_EQ(lines[7].style.font, FontKind::BigBold);
        EXPECT_EQ(lines[7].style.colors.text, 0xff0096d4u);
        EXPECT_EQ(lines[8].text, "registradas");
        EXPECT_EQ(lines[8].y, 0x145);
        EXPECT_EQ(lines[8].style.colors.text, 0xffd2e6ffu);
        EXPECT_EQ(lines[9].text, "x7");
        EXPECT_EQ(lines[9].y, 335);
    }
}

// With 2 and no level 1 stones: the button in gray (0.4) and its text 818 in the normal font, and "x0".
TEST_F(EventChipWindowTest, TheStoneWindowWithoutStonesIsGrey)
{
    PutItem(3, 0x1c15, 0);
    Runtime().panels.field78abb00 = Client::Events::StoneChipWindow;
    ASSERT_TRUE(Draw({0, 0}).HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    ASSERT_GE(quads.size(), 3u);
    EXPECT_FLOAT_EQ(quads[2].color.red, 0.4f);
    ASSERT_GE(text.texts.size(), 8u);
    EXPECT_EQ(text.texts[1].text, "registrar piedra");
    EXPECT_EQ(text.texts[1].style.font, FontKind::Normal);
    EXPECT_EQ(text.texts[2].text, "piedra 0");
    EXPECT_EQ(text.texts[6].text, "piedras");
    EXPECT_EQ(text.texts[7].text, "x0");
}

// With the button pressed (0x7d366d4) under the mouse and the release (0x7d366ec), the drawing clears the click and the
// left button; one that projects does not.
TEST_F(EventChipWindowTest, ThePressedButtonTakesTheClick)
{
    Runtime().panels.field78abb00 = Client::Events::MutoNumberWindow;
    Runtime().mouse.pendingClick = true;
    Runtime().mouse.releasedClick = true;
    Runtime().mouse.leftHeld = true;
    ASSERT_TRUE(Draw({0x1e5, 0xdc}, true).HasValue());
    EXPECT_TRUE(Runtime().mouse.pendingClick);
    EXPECT_TRUE(Runtime().mouse.leftHeld);
    ASSERT_TRUE(Draw({0x1e5, 0xdc}).HasValue());
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_FALSE(Runtime().mouse.leftHeld);
    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    bool pressed = false;
    for (const DrawnQuad& quad : quads)
        pressed = pressed || (quad.slot == 0xf0 && quad.top == 220.0f && quad.color.red == 0.4f);
    EXPECT_TRUE(pressed);
}

// With 3: texts 893, 916, 917 (in 0xff67bfdf) and 894 (in bold) above the entry, frame 0x10f, the EDIT of [0x5763504]
// reconfigured (filter 2, background 0, limit 12, 150 × 14 at (495, 265)) with the focus, button 895 40 lower, texts
// 891, 892, 897 and 898 from y 70 and, with the 9D result, its shadow and itself in bold 0x104 lower. No counts. One
// that projects does not touch the EDIT or the focus.
TEST_F(EventChipWindowTest, TheSerialWindowDrawsTheEntry)
{
    Runtime().panels.field78abb00 = Client::Events::SerialNumberWindow;
    const std::string_view result = "R3SULT0";
    for (std::size_t at = 0; at < result.size(); ++at)
        Runtime().events.serial[at] = static_cast<std::byte>(result[at]);
    ASSERT_TRUE(Draw({0, 0}, true).HasValue());
    EXPECT_EQ(Runtime().chat.input.focus, Client::Chat::ChatFocus::Game);
    EXPECT_EQ(Runtime().chat.input.dialogEdit.filter, 0);

    sprites.Begin(Screen640);
    text.texts.clear();
    const std::uint32_t before = random.State();
    const auto drawn = Draw({0, 0});
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), before);
    const Client::Ui::OldEdit& edit = Runtime().chat.input.dialogEdit;
    EXPECT_EQ(Runtime().chat.input.focus, Client::Chat::ChatFocus::Dialog);
    EXPECT_EQ(Runtime().ui.focusedList, Client::Chat::DialogFieldId);
    EXPECT_EQ(edit.filter, Client::Ui::UppercaseLettersAndDigitsEdit);
    EXPECT_EQ(edit.backgroundColor, 0u);
    EXPECT_EQ(edit.base.x, 495);
    EXPECT_EQ(edit.base.y, 265);
    EXPECT_EQ(edit.base.width, 150);
    EXPECT_EQ(edit.base.height, 14);

    std::vector<std::string> said;
    for (const Client::Tests::RecordedText& line : text.texts)
        said.push_back(line.text);
    const std::vector<std::string> expected{std::string(InventedNpc),
                                            "serie a",
                                            "serie c",
                                            "serie d",
                                            "serie b",
                                            "confirmar",
                                            "historia 0",
                                            "historia 1",
                                            "historia 2",
                                            "historia 3",
                                            "R3SULT0",
                                            "R3SULT0"};
    ASSERT_EQ(said, expected);
    const auto& lines = text.texts;
    EXPECT_EQ(lines[1].x, Centered(541, "serie a"));
    EXPECT_EQ(lines[1].y, 250 - 0x28);
    EXPECT_EQ(lines[2].y, 250 - 0xf);
    EXPECT_EQ(lines[2].style.colors.text, 0xff67bfdfu);
    EXPECT_EQ(lines[3].y, 250 - 5);
    EXPECT_EQ(lines[4].y, 250 - 0x19);
    EXPECT_EQ(lines[4].style.font, FontKind::Bold);
    EXPECT_EQ(lines[5].x, Centered(545, "confirmar"));
    EXPECT_EQ(lines[5].y, 295);
    EXPECT_EQ(lines[6].x, Centered(545, "historia 0"));
    EXPECT_EQ(lines[6].y, 70);
    EXPECT_EQ(lines[9].y, 70 + 45);
    EXPECT_EQ(lines[10].x, Centered(546, "R3SULT0"));
    EXPECT_EQ(lines[10].y, 70 + 0x104 + 1);
    EXPECT_EQ(lines[10].style.font, FontKind::Bold);
    EXPECT_EQ(lines[11].y, 70 + 0x104);
    EXPECT_EQ(lines[11].style.colors.text, 0xff67d2ffu);

    bool frame = false;
    for (const DrawnQuad& quad : QuadsOf(sprites))
        frame = frame || (quad.slot == 0x10f && quad.x == 488.0f && quad.top == 260.0f);
    EXPECT_TRUE(frame);
}

// With 4: button 903 at y 220 (text at 0xe1), texts 900, 706, 901 and 902 from y 70 and the two counts without labels.
TEST_F(EventChipWindowTest, TheMutoWindowDrawsItsButton)
{
    Runtime().panels.field78abb00 = Client::Events::MutoNumberWindow;
    Runtime().panels.field78abb04 = 3;
    ASSERT_TRUE(Draw({0, 0}).HasValue());
    std::vector<std::string> said;
    for (const Client::Tests::RecordedText& line : text.texts)
        said.push_back(line.text);
    const std::vector<std::string> expected{
        std::string(InventedNpc), "pedir numero", "muto a", "muto b", "muto c", "muto d", "x0", "x3"};
    ASSERT_EQ(said, expected);
    EXPECT_EQ(text.texts[1].y, 0xe1);
    EXPECT_EQ(text.texts[2].y, 70);
    EXPECT_EQ(text.texts[6].y, 300);
    EXPECT_EQ(text.texts[7].y, 335);
}
} // namespace
