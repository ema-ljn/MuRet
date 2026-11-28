#include "Events/EventNpcWindows.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Protocol/Session/Logout.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Outgoing.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Kernel/ActorState.h"
#include "Chat/GameChat.h"
#include "Ui/GameHudContext.h"
#include "Hud/GamePanels.h"
#include "Ui/GameUiContext.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "FrontEnd/SelectionInventory.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameText.h"
#include "Ui/Widget.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"

#include "support/RecordingTextDrawer.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "Events/EventGlobals.h"
#include "Ui/GameUiState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// CL9 milestone 11, phase 2: the event NPC windows (0x5f58f0 and its step 0x5e5920) against the ASM of c32bbd5f, with
// no GL trace (*static*). Text.bmd is made up, with the specifiers of the original's texts.
namespace
{
using Client::Crt::CrtRandom;
using Client::Ui::GameHudContext;
using Client::Ui::GameUiContext;
using Client::Kernel::WorldState;
using Client::Models::OwnRecordCloseRandoms;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Text::FontKind;

constexpr std::uint32_t Seed = 0x2468ace1u;
constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};

std::map<std::size_t, std::string> NpcTexts()
{
    std::map<std::size_t, std::string> texts{
        {39, "el cuadrado"},
        {645, "cuadrado %d (%d-%d)"},
        {646, "cuadrado %d (desde %d)"},
        {677, "sin invitacion"},
        {686, "muy fuerte"},
        {687, "muy debil"},
        {832, "el castillo espera a los que traen la capa"},
        {846, "el castillo"},
        {847, "puente %d (%d-%d)"},
        {848, "puente %d (desde %d)"},
        {854, "sin capa"},
    };
    for (std::size_t line = 0; line < 6; ++line)
        texts[670 + line] = "historia " + std::to_string(line);
    return texts;
}

struct DrawnQuad
{
    std::uint16_t slot = 0;
    Color color{};
    float x = 0.0f;
    float top = 0.0f;
};

// The batch's textured quads, without the text marks.
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

void ExpectColor(const Color& drawn, const Color& expected)
{
    EXPECT_FLOAT_EQ(drawn.red, expected.red);
    EXPECT_FLOAT_EQ(drawn.green, expected.green);
    EXPECT_FLOAT_EQ(drawn.blue, expected.blue);
}

CrtRandom Advanced(CrtRandom random, std::uint32_t count)
{
    random.Discard(count);
    return random;
}

class EventNpcWindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sprites.Begin(Screen640);
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return world->Runtime();
    }

    Client::Core::Expected<void, std::string> Draw(Client::Ui::Point mouse, bool projecting = false)
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
                               .mouse = mouse};
        Client::Hud::GamePanels panels(*world, random);
        return Client::Events::DrawTheEventNpcWindow(context, panels.Registry());
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
        panels.LendTheWindowClose([this]() { sent.push_back(Client::Protocol::Session::WriteWindowClose()); });
        return Client::Events::StepTheEventNpcWindow(context, panels.Registry());
    }

    void Open(std::uint8_t kind)
    {
        // Each synthetic world has already gone through the selection (0x5bbe10), like the real game.
        Client::FrontEnd::ClearSelectionInventory(*world, random);
        Runtime().panels.eventNpc = 1;
        Runtime().panels.eventNpcKind = kind;
        Runtime().panels.inventory = 1;
    }

    void PutItem(std::size_t cell, std::uint16_t type, std::uint32_t level)
    {
        Client::Kernel::ItemState& item = world->Inventory().cells[cell];
        item.id = type;
        item.scalar1Low = level << 3;
        item.occupied = 1;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{Seed};
    std::optional<Client::Audio::SoundId> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Input::InputState input;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable(NpcTexts());
    std::vector<Client::Protocol::Outgoing> sent;
};

// 0x5583a0: from column 7 to 0 and, in each, from row 7 to 0; with +0x3c at 0 or with another level it does not count.
TEST_F(EventNpcWindowTest, FindsTheInventoryItemFromTheLastColumn)
{
    const Client::Kernel::OwnInventory& inventory = world->Inventory();
    EXPECT_EQ(Client::Events::FindInventoryItem(inventory, 0x1c13, 0), -1);
    PutItem(9, 0x1c13, 0);  // row 1, column 1
    PutItem(17, 0x1c13, 0); // row 2, column 1
    PutItem(2, 0x1c13, 3);  // row 0, column 2
    EXPECT_EQ(Client::Events::FindInventoryItem(inventory, 0x1c13, 0), 17);
    EXPECT_EQ(Client::Events::FindInventoryItem(inventory, 0x1c13, -1), 2);
    EXPECT_EQ(Client::Events::FindInventoryItem(inventory, 0x1c13, 3), 2);
    world->Inventory().cells[2].occupied = 0;
    EXPECT_EQ(Client::Events::FindInventoryItem(inventory, 0x1c13, 3), -1);
    EXPECT_EQ(Client::Events::FindInventoryItem(inventory, 0x1a12, -1), -1);
}

// Devil Square (0x1000): the background, the six 0xf0 buttons from (285, 187) in steps of 40 (white with the mouse,
// otherwise (0.9, 0.8, 0.7)), the lines with the table of the own record's class (opened and closed per row: 6 × 3848
// rand()), the title and the six bottom texts. Writes the corner and the levels the step reads. In Lorencia and Devias.
TEST_F(EventNpcWindowTest, TheDevilSquareWindowDrawsItsSquares)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t characterClass;
        std::array<std::string_view, 6> rows;
    };
    const std::array<Case, 2> cases{{{0,
                                      0,
                                      {"cuadrado 1 (15-130)", "cuadrado 2 (131-180)", "cuadrado 3 (181-230)",
                                       "cuadrado 4 (231-400)", "cuadrado 5 (281-330)", "cuadrado 6 (331-400)"}},
                                     {2,
                                      3,
                                      {"cuadrado 1 (15-110)", "cuadrado 2 (111-160)", "cuadrado 3 (161-210)",
                                       "cuadrado 4 (211-400)", "cuadrado 5 (261-310)", "cuadrado 6 (311-400)"}}}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.map);
        world = std::make_unique<WorldState>();
        sprites.Begin(Screen640);
        text.texts.clear();
        Runtime().map = probe.map;
        world->OwnCharacter().characterClass = probe.characterClass;
        Open(0);
        const CrtRandom expected = Advanced(random, 6u * OwnRecordCloseRandoms);
        const auto drawn = Draw({300, 232});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 8u);
        EXPECT_EQ(quads[0].slot, 0x104);
        EXPECT_EQ(quads[1].slot, 0x105);
        for (std::size_t row = 0; row < 6; ++row)
        {
            SCOPED_TRACE(row);
            const DrawnQuad& button = quads[2 + row];
            EXPECT_EQ(button.slot, 0xf0);
            EXPECT_FLOAT_EQ(button.x, 285.0f);
            EXPECT_FLOAT_EQ(button.top, 187.0f + 40.0f * static_cast<float>(row));
            ExpectColor(button.color, row == 1 ? White : Color{0.9f, 0.8f, 0.7f, 1.0f});
        }
        ASSERT_EQ(text.texts.size(), 13u);
        for (std::size_t row = 0; row < 6; ++row)
        {
            SCOPED_TRACE(row);
            const Client::Tests::RecordedText& line = text.texts[row];
            EXPECT_EQ(line.text, probe.rows[row]);
            EXPECT_EQ(line.x, 297);
            EXPECT_EQ(line.y, 199 + 40 * static_cast<int>(row));
            EXPECT_TRUE(line.windowed);
            EXPECT_EQ(line.style.font, FontKind::Bold);
            EXPECT_EQ(line.style.colors.text, 0xc41432ffu);
            EXPECT_EQ(line.style.colors.background, 0x101000u);
        }
        EXPECT_EQ(text.texts[6].text, "el cuadrado");
        EXPECT_EQ(text.texts[6].x, 0x104 + 0x23);
        EXPECT_EQ(text.texts[6].y, 0xc);
        EXPECT_EQ(text.texts[6].style.font, FontKind::Bold);
        EXPECT_EQ(text.texts[6].style.colors.text, 0xffdcdcdcu);
        EXPECT_EQ(text.texts[6].style.colors.background, 0xff141414u);
        for (std::size_t line = 0; line < 6; ++line)
        {
            SCOPED_TRACE(line);
            EXPECT_EQ(text.texts[7 + line].text, "historia " + std::to_string(line));
            EXPECT_EQ(text.texts[7 + line].x, 0x104 + 0x1e);
            EXPECT_EQ(text.texts[7 + line].y, 0x41 + 20 * static_cast<int>(line));
            EXPECT_EQ(text.texts[7 + line].style.font, FontKind::Normal);
            EXPECT_EQ(text.texts[7 + line].style.colors.text, 0xffffffffu);
        }
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(Runtime().ui.npcWindowX, 0x104);
        EXPECT_EQ(Runtime().events.devilSquareLevels[3][0], probe.characterClass == 3 ? 211 : 231);
        EXPECT_EQ(Runtime().events.devilSquareLevels[3][1], 400);
    }
}

// With an item on the cursor (0x7879fbc), no button lights up; a drawing that projects does not write the levels or
// the corner.
TEST_F(EventNpcWindowTest, TheDevilSquareWindowWithAnItemOrProjecting)
{
    Open(0);
    Runtime().cursorItemCount = 1;
    ASSERT_TRUE(Draw({300, 232}, true).HasValue());
    for (const DrawnQuad& quad : QuadsOf(sprites))
    {
        if (quad.slot == 0xf0)
            ExpectColor(quad.color, Color{0.9f, 0.8f, 0.7f, 1.0f});
    }
    EXPECT_EQ(Runtime().ui.npcWindowX, 0);
    EXPECT_EQ(Runtime().events.devilSquareLevels[0][1], 0);
}

// Blood Castle (0x4000): seven buttons from (285, 170) in steps of 35, 30 high: in gray (0.4, 0.4, 0.5) those not of
// the own record's level (100), white the one under the mouse and (0.7, 0.7, 0.9) the others; the table by the hero's
// class (+0x1d4), seven accesses to the own record, the title and 832 in lines of under 26 bytes.
TEST_F(EventNpcWindowTest, TheBloodCastleWindowDrawsItsBridges)
{
    for (const std::uint8_t characterClass : {std::uint8_t{0}, std::uint8_t{4}})
    {
        SCOPED_TRACE(static_cast<int>(characterClass));
        world = std::make_unique<WorldState>();
        sprites.Begin(Screen640);
        text.texts.clear();
        Runtime().map = 2;
        world->Hero().characterClass = characterClass;
        world->OwnCharacter().level = 100;
        Open(1);
        const CrtRandom expected = Advanced(random, 7u * OwnRecordCloseRandoms);
        // The mouse on the second bridge (170 + 35 ≤ 210 < 235).
        const auto drawn = Draw({300, 210});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 9u);
        // With class 0, 100 belongs to bridge 2 (81-130); with class 4, too (61-110).
        for (std::size_t row = 0; row < 7; ++row)
        {
            SCOPED_TRACE(row);
            const DrawnQuad& button = quads[2 + row];
            EXPECT_FLOAT_EQ(button.top, 170.0f + 35.0f * static_cast<float>(row));
            ExpectColor(button.color, row == 1 ? White : Color{0.4f, 0.4f, 0.5f, 1.0f});
        }
        ASSERT_GE(text.texts.size(), 8u);
        EXPECT_EQ(text.texts[0].text, characterClass == 0 ? "puente 1 (15-80)" : "puente 1 (10-60)");
        EXPECT_EQ(text.texts[0].x, 295);
        EXPECT_EQ(text.texts[0].y, 182);
        EXPECT_EQ(text.texts[0].style.colors.text, 0xdcffdcbeu);
        EXPECT_EQ(text.texts[6].text, characterClass == 0 ? "puente 7 (331-400)" : "puente 7 (311-400)");
        EXPECT_EQ(text.texts[7].text, "el castillo");
        const std::vector<std::string> story = Client::Ui::SplitIntoLines(NpcTexts()[832], 7, 0x1a);
        ASSERT_EQ(text.texts.size(), 8u + story.size());
        for (std::size_t line = 0; line < story.size(); ++line)
        {
            EXPECT_EQ(text.texts[8 + line].text, story[line]);
            EXPECT_EQ(text.texts[8 + line].y, 0x40 + 20 * static_cast<int>(line));
        }
        EXPECT_EQ(random.State(), expected.State());
    }
}

// The Devil Square step: the click on a square (285 ≤ x < 425), with the own record open (3848 rand() when closing it),
// the level against the one the drawing left and the invitation of level 0 or of the square: C1 05 90 with the place +
// 0x18. Without a click, nothing.
TEST_F(EventNpcWindowTest, TheDevilSquareStepSendsTheEntry)
{
    Open(0);
    world->OwnCharacter().level = 150;
    Runtime().events.devilSquareLevels[1] = {131, 180};
    PutItem(20, 0x1c13, 2);
    ASSERT_TRUE(Step(300, 232).HasValue());
    EXPECT_TRUE(sent.empty());

    Runtime().mouse.pendingClick = true;
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms);
    const auto stepped = Step(300, 232);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    ASSERT_EQ(sent.size(), 1u);
    const Client::Protocol::Outgoing entry = Client::Protocol::Events::WriteDevilSquareEntry(1, 20 + 0x18);
    EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(), entry.packet.Bytes()));
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(Runtime().ui.npcWindowX, 0x104);
}

// Outside the level, 0x65cae0(0x4000000, 0) closes the inventory and with it panel 0x1000, which sends C1 03 31
// (0x658a04), and warns with 686 (too high) or 687 (too low). Without an invitation, 677; with an item on the cursor,
// the click is spent with nothing else.
TEST_F(EventNpcWindowTest, TheDevilSquareStepWarns)
{
    for (const auto& [level, warning] : {std::pair<std::uint16_t, std::string_view>{std::uint16_t{200}, "muy fuerte"},
                                         std::pair<std::uint16_t, std::string_view>{std::uint16_t{100}, "muy debil"}})
    {
        SCOPED_TRACE(level);
        world = std::make_unique<WorldState>();
        sent.clear();
        Open(0);
        world->OwnCharacter().level = level;
        Runtime().events.devilSquareLevels[1] = {131, 180};
        Runtime().mouse.pendingClick = true;
        const auto stepped = Step(300, 232);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(),
                                             Client::Protocol::Session::WriteWindowClose().packet.Bytes()));
        EXPECT_EQ(Runtime().panels.eventNpc, 0);
        EXPECT_EQ(Runtime().panels.inventory, 0);
        EXPECT_EQ(Runtime().dialogs.current, Client::Ui::MessageBoxDialog);
        EXPECT_EQ(std::string_view(Runtime().dialogBox.rows.data()), warning);
    }
    sent.clear();
    world = std::make_unique<WorldState>();
    Open(0);
    world->OwnCharacter().level = 150;
    Runtime().events.devilSquareLevels[1] = {131, 180};
    Runtime().mouse.pendingClick = true;
    const auto stepped = Step(300, 232);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(Runtime().dialogs.current, Client::Ui::MessageBoxDialog);
    EXPECT_EQ(std::string_view(Runtime().dialogBox.rows.data()), "sin invitacion");

    world = std::make_unique<WorldState>();
    Open(0);
    Runtime().cursorItemCount = 1;
    Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Step(300, 232).HasValue());
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_EQ(Runtime().dialogs.current, 0);
}

// The Blood Castle step: each bridge opens and closes the own record up to the chosen one; a bridge not of the level
// does not take the click. The bridge's cloak 0x1a12, and for 7 also the level 6 one: C1 05 9A with the place + 12.
// Without a cloak, 854.
TEST_F(EventNpcWindowTest, TheBloodCastleStepSendsTheEntry)
{
    Open(1);
    world->OwnCharacter().level = 100;
    PutItem(5, 0x1a12, 2);
    Runtime().mouse.pendingClick = true;
    // Bridge 1 (15-80) is not of the level: the seven accesses and the click goes on.
    CrtRandom expected = Advanced(random, 7u * OwnRecordCloseRandoms);
    ASSERT_TRUE(Step(300, 180).HasValue());
    EXPECT_TRUE(Runtime().mouse.pendingClick);
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(random.State(), expected.State());

    expected = Advanced(random, 2u * OwnRecordCloseRandoms);
    ASSERT_TRUE(Step(300, 210).HasValue());
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(),
                                         Client::Protocol::Events::WriteBloodCastleEntry(2, 5 + 12).packet.Bytes()));
    EXPECT_EQ(random.State(), expected.State());

    sent.clear();
    world->OwnCharacter().level = 350;
    PutItem(5, 0x1a12, 6);
    Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Step(300, 170 + 6 * 35 + 1).HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(),
                                         Client::Protocol::Events::WriteBloodCastleEntry(7, 5 + 12).packet.Bytes()));

    sent.clear();
    world->Inventory().cells[5].occupied = 0;
    Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Step(300, 170 + 6 * 35 + 1).HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(std::string_view(Runtime().dialogBox.rows.data()), "sin capa");
}
} // namespace
