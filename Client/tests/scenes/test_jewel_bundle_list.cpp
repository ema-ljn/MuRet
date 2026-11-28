#include "ChaosMix/JewelBundleList.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Hud/GameHud.h"
#include "Hud/GameUi.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatLogWindow.h"
#include "Ui/GameUiState.h"
#include "Ui/Widget.h"
#include "Audio/SoundId.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// 0x6e5660's list (CL9 milestone 6, phase 5): adding (0x46e940), emptying (0x482f80) and the chosen one (0x483aa0); its
// step (0x402af0 → 0x45aa80 → 0x483bc0) with the keyboard arrows and its queue, the wheel, the bar's arrows and track,
// the click and the double click on a row; and its drawing (0x4832e0 with 0x46ebe0 and 0x46f020). None takes rand().
// Made-up texts; the world in Lorencia (0), Devias (2) and Noria (3).
namespace
{
using Client::Crt::CrtRandom;
using Client::Input::InputEvent;
using Client::Input::Key;
using Client::Kernel::WorldState;
using Client::Npc::JewelBundleList;

constexpr std::array<std::int32_t, 3> Maps{0, 2, 3};
// 0x62f734: the corner the 0xa4's drawing sets, (0xe4, 0x6d + 0xf0).
constexpr std::int32_t ListX = 0xe4;
constexpr std::int32_t ListY = 0x6d + 0xf0;
// 0x46f2c0 with 8 visible: row i at y = (8 − i) · 13 + 230, and the mouse takes it from 3 higher, at x from 0xe4 to
// 0xe4 + 168.
constexpr std::int32_t RowX = 300;
constexpr std::int32_t OutsideX = 100;
constexpr std::int32_t OutsideY = 100;

std::vector<std::pair<std::int32_t, std::uint8_t>> RowsOf(const JewelBundleList& list)
{
    std::vector<std::pair<std::int32_t, std::uint8_t>> rows;
    for (const JewelBundleList::Row& row : list.rows)
        rows.emplace_back(row.cell, row.level);
    return rows;
}

class SteppingTheList
{
public:
    explicit SteppingTheList(std::int32_t map)
    {
        world->Runtime().map = map;
        JewelBundleList& list = List();
        list.x = ListX;
        list.y = ListY;
    }

    // `count` rows from cell 1, with level cell % 16, and the bar as the drawing leaves it.
    void Fill(std::int32_t count)
    {
        JewelBundleList& list = List();
        Client::ChaosMix::ClearTheBundles(list);
        for (std::int32_t cell = 1; cell <= count; ++cell)
            Client::ChaosMix::AddTheBundle(list, cell, static_cast<std::uint8_t>(cell % 16));
        PlaceTheBar();
    }

    void PlaceTheBar()
    {
        JewelBundleList& list = List();
        const Client::Chat::OldListBar bar =
            Client::Chat::PlaceTheListBar(list.y, list.height, list.barMode, list.shownRows,
                                          static_cast<std::int32_t>(list.rows.size()), list.scroll);
        list.barTop = bar.barTop;
        list.barBottom = bar.barBottom;
        list.thumbY = bar.thumbY;
        list.thumbHeight = bar.thumbHeight;
    }

    // Without the click.
    Client::ChaosMix::BundleListStep Step(std::int32_t x, std::int32_t y)
    {
        world->Runtime().mouse.pendingClick = false;
        return Run(x, y);
    }

    Client::ChaosMix::BundleListStep Click(std::int32_t x, std::int32_t y)
    {
        world->Runtime().mouse.pendingClick = true;
        return Run(x, y);
    }

    Client::ChaosMix::BundleListStep Run(std::int32_t x, std::int32_t y)
    {
        return Client::ChaosMix::StepTheBundleList(
            Client::Hud::GameUiContext{.world = *world, .random = random, .input = input, .mouseX = x, .mouseY = y});
    }

    void Press(Key key)
    {
        input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = key});
    }

    void Release(Key key)
    {
        input.Apply(InputEvent{.kind = InputEvent::Kind::KeyUp, .key = key});
    }

    [[nodiscard]] JewelBundleList& List() const
    {
        return world->Runtime().npcWindows.jewels.bundles;
    }

    [[nodiscard]] Client::Ui::GameUiState& Ui() const
    {
        return world->Runtime().ui;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x1357u};
    Client::Input::InputState input;
};

// 0x46e940: only cells 0 to 0x40 not there yet; each leaves the chosen one at 0, the last in view and the rows by
// cell. 0x482f80 empties them with the chosen one and the first visible one at 0; 0x483aa0 gives the chosen row, or
// none without a chosen one or without rows (and then the chosen one at 0).
TEST(JewelBundleListTest, AddingSortsByCellAndKeepsTheLastInView)
{
    JewelBundleList list;
    list.selection = 2;
    Client::ChaosMix::AddTheBundle(list, 5, 3);
    EXPECT_EQ(list.selection, 0);
    Client::ChaosMix::AddTheBundle(list, 2, 1);
    Client::ChaosMix::AddTheBundle(list, 5, 9);
    Client::ChaosMix::AddTheBundle(list, -1, 0);
    Client::ChaosMix::AddTheBundle(list, 0x41, 0);
    Client::ChaosMix::AddTheBundle(list, 0x40, 0xff);
    Client::ChaosMix::AddTheBundle(list, 0, 7);
    const std::vector<std::pair<std::int32_t, std::uint8_t>> expected{
        {0, std::uint8_t{7}}, {2, std::uint8_t{1}}, {5, std::uint8_t{3}}, {0x40, std::uint8_t{0xff}}};
    EXPECT_EQ(RowsOf(list), expected);
    EXPECT_EQ(list.scroll, 0);

    for (std::int32_t cell = 10; cell < 16; ++cell)
        Client::ChaosMix::AddTheBundle(list, cell, 0);
    EXPECT_EQ(list.rows.size(), 10u);
    EXPECT_EQ(list.scroll, 2);
    EXPECT_EQ(list.rows.back().flag, 0);

    EXPECT_FALSE(Client::ChaosMix::ChosenBundle(list).has_value());
    list.selection = 2;
    const auto chosen = Client::ChaosMix::ChosenBundle(list);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->cell, 2);
    EXPECT_EQ(chosen->level, 1);

    Client::ChaosMix::ClearTheBundles(list);
    EXPECT_TRUE(list.rows.empty());
    EXPECT_EQ(list.selection, 0);
    EXPECT_EQ(list.scroll, 0);
    list.selection = 3;
    EXPECT_FALSE(Client::ChaosMix::ChosenBundle(list).has_value());
    EXPECT_EQ(list.selection, 0);
}

// 0x45aa80 and 0x483bc0, on the three maps: with the mouse over the list it is the UI's bottom one (with no other), and
// the wheel scrolls 3 at a time up to the edges; 0x402af0 leaves 0x5762eb4 at 0. Outside it, none of that.
TEST(JewelBundleListTest, TheWheelScrollsOverTheList)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SteppingTheList tested(map);
        tested.Fill(12);
        JewelBundleList& list = tested.List();
        auto& ui = tested.Ui();
        ASSERT_EQ(list.scroll, 4);
        const CrtRandom before = tested.random;

        ui.wheel = 1;
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_EQ(ui.wheel, 1);
        EXPECT_EQ(ui.hovered, 0);
        EXPECT_EQ(ui.mouseOnUi, 0);

        ui.wheel = -1;
        static_cast<void>(tested.Step(RowX, 300));
        EXPECT_EQ(list.scroll, 1);
        EXPECT_EQ(ui.wheel, 0);
        EXPECT_EQ(ui.hovered, list.id);
        EXPECT_EQ(ui.mouseOnUi, 1);
        EXPECT_EQ(ui.capture, 0);
        ui.wheel = -1;
        static_cast<void>(tested.Step(RowX, 300));
        EXPECT_EQ(list.scroll, 0);

        ui.hovered = 9;
        ui.wheel = 2;
        static_cast<void>(tested.Step(RowX, 300));
        EXPECT_EQ(list.scroll, 4);
        EXPECT_EQ(ui.hovered, 9);
        EXPECT_FALSE(tested.world->Runtime().lastSoundId.has_value());
        EXPECT_EQ(tested.random.State(), before.State());
    }
}

// 0x483bc0, on the three maps: the click over the list takes the keyboard focus; the bar's arrows (x in [395, 408), y
// in [240, 253) and [336, 349)) scroll one at a time with sound 0x19, count up to 15 with the click and scroll on
// every step from 16; the track above and below the thumb scrolls 8, without sound. The click stays.
TEST(JewelBundleListTest, TheBarArrowsAndTheTrackScroll)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SteppingTheList tested(map);
        tested.Fill(20);
        JewelBundleList& list = tested.List();
        auto& runtime = *tested.world;
        list.scroll = 5;
        tested.PlaceTheBar();

        static_cast<void>(tested.Click(400, 241));
        EXPECT_EQ(list.scroll, 6);
        EXPECT_EQ(list.arrowRepeat, 1);
        EXPECT_EQ(runtime.Runtime().lastSoundId, Client::Audio::SoundId{0x19});
        EXPECT_EQ(runtime.Runtime().ui.focusedList, list.id);
        EXPECT_TRUE(runtime.Runtime().mouse.pendingClick);
        runtime.Runtime().lastSoundId.reset();
        for (int step = 0; step < 14; ++step)
            static_cast<void>(tested.Click(400, 241));
        EXPECT_EQ(list.scroll, 6);
        EXPECT_EQ(list.arrowRepeat, 15);
        static_cast<void>(tested.Click(400, 241));
        static_cast<void>(tested.Click(400, 241));
        EXPECT_EQ(list.scroll, 7);
        EXPECT_FALSE(runtime.Runtime().lastSoundId.has_value());

        static_cast<void>(tested.Step(400, 241));
        EXPECT_EQ(list.arrowRepeat, 0);
        static_cast<void>(tested.Click(400, 340));
        EXPECT_EQ(list.scroll, 6);
        EXPECT_EQ(runtime.Runtime().lastSoundId, Client::Audio::SoundId{0x19});
        runtime.Runtime().lastSoundId.reset();

        tested.PlaceTheBar();
        const std::int32_t aboveThumb = static_cast<std::int32_t>(list.thumbY) - 2;
        static_cast<void>(tested.Step(400, aboveThumb));
        static_cast<void>(tested.Click(400, aboveThumb));
        EXPECT_EQ(list.scroll, 12);
        EXPECT_EQ(list.trackRepeat, 1);
        tested.PlaceTheBar();
        static_cast<void>(tested.Step(400, 300));
        const std::int32_t belowThumb = static_cast<std::int32_t>(list.thumbY + list.thumbHeight) + 2;
        static_cast<void>(tested.Click(400, belowThumb));
        EXPECT_EQ(list.scroll, 4);
        EXPECT_FALSE(runtime.Runtime().lastSoundId.has_value());

        // The thumb is not grabbed (07 Q-C1151): the click on it scrolls nothing.
        tested.PlaceTheBar();
        static_cast<void>(tested.Step(400, 300));
        static_cast<void>(tested.Click(400, static_cast<std::int32_t>(list.thumbY) + 1));
        EXPECT_EQ(list.scroll, 4);
        EXPECT_EQ(runtime.Runtime().ui.capture, 0);
    }
}

// 0x483bc0 and 0x4834a0, on the three maps: with the focus, Up sends 0xf (next) and Down 0x10 (previous, down to 1),
// handled by the next step; they repeat from the sixth step. The chosen one stays in view. Without a chosen one they do
// not choose, and with more rows than visible the first visible one goes back to 0 (07 Q-C1153). Without the focus,
// nothing.
TEST(JewelBundleListTest, TheArrowKeysMoveTheChoiceOnTheNextStep)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SteppingTheList tested(map);
        tested.Fill(12);
        JewelBundleList& list = tested.List();
        auto& ui = tested.Ui();
        tested.Press(Key::Up);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_TRUE(list.messages.empty());

        ui.focusedList = list.id;
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_EQ(list.messages, std::vector<std::int32_t>{0xf});
        EXPECT_EQ(list.keyRepeat, 1);
        tested.Release(Key::Up);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_TRUE(list.messages.empty());
        EXPECT_EQ(list.keyRepeat, 0);
        EXPECT_EQ(list.selection, 0);
        EXPECT_EQ(list.scroll, 0);

        list.selection = 3;
        tested.Press(Key::Up);
        for (int step = 0; step < 7; ++step)
            static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_EQ(list.keyRepeat, 6);
        EXPECT_EQ(list.messages, std::vector<std::int32_t>{0xf});
        EXPECT_EQ(list.selection, 4);
        tested.Release(Key::Up);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_EQ(list.selection, 5);

        list.selection = 12;
        list.scroll = 0;
        tested.Press(Key::Up);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        tested.Release(Key::Up);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_EQ(list.selection, 12);
        EXPECT_EQ(list.scroll, 4);

        list.selection = 2;
        tested.Press(Key::Down);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        tested.Release(Key::Down);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_EQ(list.selection, 1);
        EXPECT_EQ(list.scroll, 0);
    }
}

// 0x46f1a0, on the three maps: the click on a row chooses it (the first visible + 1 + the row), flips its +0 and is
// cleared; the double click chooses it, is cleared and returns the row for 0x402a90, except cell 0 (07 Q-C1152).
TEST(JewelBundleListTest, TheRowsTakeTheClickAndTheDoubleClick)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SteppingTheList tested(map);
        JewelBundleList& list = tested.List();
        for (const std::int32_t cell : {0, 7, 9})
            Client::ChaosMix::AddTheBundle(list, cell, static_cast<std::uint8_t>(cell));
        // With 3 rows, row i at y = (3 − i) · 13 + 230.
        const std::int32_t secondRowY = 2 * 13 + 230 - 2;
        auto& runtime = tested.world->Runtime();
        auto stepped = tested.Click(RowX, secondRowY);
        EXPECT_FALSE(stepped.asked.has_value());
        EXPECT_EQ(list.selection, 2);
        EXPECT_EQ(list.rows[1].flag, 1);
        EXPECT_FALSE(runtime.mouse.pendingClick);
        static_cast<void>(tested.Click(RowX, secondRowY));
        EXPECT_EQ(list.rows[1].flag, 0);
        static_cast<void>(tested.Click(OutsideX, secondRowY));
        EXPECT_TRUE(runtime.mouse.pendingClick);

        runtime.ui.doubleClick = true;
        stepped = tested.Step(RowX, 3 * 13 + 230);
        EXPECT_FALSE(stepped.asked.has_value());
        EXPECT_EQ(list.selection, 1);
        EXPECT_FALSE(runtime.ui.doubleClick);

        runtime.ui.doubleClick = true;
        stepped = tested.Step(RowX, 13 + 230 + 9);
        ASSERT_TRUE(stepped.asked.has_value());
        EXPECT_EQ(stepped.asked->cell, 9);
        EXPECT_EQ(stepped.asked->level, 9);
        EXPECT_EQ(list.selection, 3);
        EXPECT_FALSE(runtime.ui.doubleClick);
    }
}

class DrawingTheList
{
public:
    explicit DrawingTheList(std::int32_t map)
    {
        world->Runtime().map = map;
        sprites.Begin(Client::Core::PixelSize{640, 480});
    }

    Client::Core::Expected<void, std::string> Draw(bool projecting = false, bool withTexts = true)
    {
        Client::Kernel::WorldRuntime& runtime = world->Runtime();
        Client::Hud::GameHudContext context{.world = *world,
                                            .random = random,
                                            .ownRecordDepth = runtime.ownRecordDepth,
                                            .cellXDepth = cellDepth,
                                            .cellYDepth = cellDepth,
                                            .hud = runtime.hud,
                                            .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                                            .sound = sound,
                                            .projecting = projecting,
                                            .sprites = sprites,
                                            .text = text,
                                            .texts = withTexts ? &texts : nullptr,
                                            .mouse = mouse};
        return Client::ChaosMix::DrawTheBundleList(context, ListX, ListY);
    }

    // Quad `index` of the batch, without the texts' marks: its first vertex (top left) and the third.
    [[nodiscard]] std::vector<std::pair<Client::Render::SpriteVertex, Client::Render::SpriteVertex>> Quads() const
    {
        std::vector<std::pair<Client::Render::SpriteVertex, Client::Render::SpriteVertex>> quads;
        const auto vertices = sprites.Vertices();
        for (const Client::Render::DrawCommand& command : sprites.Commands())
        {
            if (command.texture == Client::Tests::TextMarkerSlot)
                continue;
            for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
                quads.emplace_back(vertices[command.firstVertex + quad * 6],
                                   vertices[command.firstVertex + quad * 6 + 2]);
        }
        return quads;
    }

    [[nodiscard]] std::vector<std::optional<std::uint16_t>> Slots() const
    {
        std::vector<std::optional<std::uint16_t>> slots;
        for (const Client::Render::DrawCommand& command : sprites.Commands())
        {
            if (command.texture == Client::Tests::TextMarkerSlot)
                continue;
            for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
                slots.push_back(command.texture ? std::optional<std::uint16_t>{command.texture->value} : std::nullopt);
        }
        return slots;
    }

    [[nodiscard]] JewelBundleList& List() const
    {
        return world->Runtime().npcWindows.jewels.bundles;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x1357u};
    std::uint8_t cellDepth = 0;
    std::optional<Client::Audio::SoundId> sound;
    Client::Render::SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Ui::Point mouse{};
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{1806, "bendicion"}, {1807, "alma"}});
};

// 0x4832e0, on the three maps: the black background of 0.4 one point bigger, the two win_scrollbar arrows (0x649), the
// two track lines with palette 2 and the thumb in three quads; with 3 rows (fewer than 8) the thumb fills the track.
// The rows from bottom to top, "%s,  %d" with the jewel's name and (level + 1) · 10 at x + 6, the chosen one with its
// 0.5 veil and the text in black, the others in 0xffc8dce6, in the normal font. Leaves the bar for the step, and
// blending off.
TEST(JewelBundleListTest, TheListDrawsItsFrameBarAndRows)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        DrawingTheList tested(map);
        JewelBundleList& list = tested.List();
        tested.world->Runtime().npcWindows.jewels.kind = 1;
        for (const std::int32_t cell : {4, 10, 20})
            Client::ChaosMix::AddTheBundle(list, cell, static_cast<std::uint8_t>(cell == 20 ? 15 : cell / 5));
        list.selection = 2;
        tested.world->Runtime().hud.dcFont = Client::Render::Text::FontKind::Bold;
        const CrtRandom before = tested.random;
        ASSERT_TRUE(tested.Draw().HasValue());
        EXPECT_EQ(list.x, ListX);
        EXPECT_EQ(list.y, ListY);
        EXPECT_FLOAT_EQ(list.barTop, 252.0f);
        EXPECT_FLOAT_EQ(list.barBottom, 337.0f);
        EXPECT_FLOAT_EQ(list.thumbHeight, 85.0f);

        constexpr std::uint16_t Bar = 0x649;
        const std::vector<std::optional<std::uint16_t>> slots{std::nullopt, Bar, Bar, std::nullopt, std::nullopt,
                                                              Bar,          Bar, Bar, std::nullopt};
        EXPECT_EQ(tested.Slots(), slots);
        const auto quads = tested.Quads();
        ASSERT_EQ(quads.size(), 9u);
        EXPECT_FLOAT_EQ(quads[0].first.x, 227.0f);
        EXPECT_FLOAT_EQ(480.0f - quads[0].first.y, 239.0f);
        EXPECT_NEAR(quads[0].first.color.alpha, 102.0f / 255.0f, 1e-6);
        EXPECT_FLOAT_EQ(quads[0].first.color.red, 0.0f);
        EXPECT_FLOAT_EQ(quads[1].first.x, 396.0f);
        EXPECT_FLOAT_EQ(480.0f - quads[1].first.y, 239.0f);
        EXPECT_FLOAT_EQ(quads[1].first.v, 0.09375f);
        EXPECT_FLOAT_EQ(480.0f - quads[2].first.y, 337.0f);
        EXPECT_FLOAT_EQ(quads[2].first.v, 0.5f);
        EXPECT_FLOAT_EQ(quads[3].first.x, 396.0f);
        EXPECT_FLOAT_EQ(quads[4].first.x, 408.0f);
        EXPECT_FLOAT_EQ(480.0f - quads[4].first.y, 252.0f);
        EXPECT_FLOAT_EQ(quads[5].first.x, 397.0f);
        EXPECT_FLOAT_EQ(480.0f - quads[5].first.y, 252.0f);
        EXPECT_FLOAT_EQ(480.0f - quads[7].first.y, 336.0f);
        EXPECT_FLOAT_EQ(quads[7].first.v, 0.0625f);
        // Row 1's veil (the chosen 2), at (0xe4, 256 − 3).
        EXPECT_FLOAT_EQ(quads[8].first.x, 228.0f);
        EXPECT_FLOAT_EQ(480.0f - quads[8].first.y, 253.0f);
        EXPECT_FLOAT_EQ(quads[8].first.color.alpha, 0.5f);
        EXPECT_FLOAT_EQ(quads[8].second.x, 396.0f);

        const auto& drawn = tested.text.texts;
        ASSERT_EQ(drawn.size(), 3u);
        EXPECT_EQ(drawn[0].text, "alma,  10");
        EXPECT_EQ(drawn[0].x, 0xe4 + 6);
        EXPECT_EQ(drawn[0].y, 269);
        EXPECT_EQ(drawn[0].style.colors.text, 0xffc8dce6u);
        EXPECT_EQ(drawn[0].style.colors.background, 0u);
        EXPECT_EQ(drawn[0].style.font, Client::Render::Text::FontKind::Normal);
        EXPECT_EQ(drawn[1].text, "alma,  30");
        EXPECT_EQ(drawn[1].y, 256);
        EXPECT_EQ(drawn[1].style.colors.text, 0xff000000u);
        EXPECT_EQ(drawn[2].text, "alma,  160");
        EXPECT_EQ(drawn[2].y, 243);
        EXPECT_EQ(tested.sprites.Blend(), Client::Render::BlendMode::Opaque);
        EXPECT_EQ(tested.random.State(), before.State());
    }
}

// 0x46ebe0 and 0x4832e0, on the three maps: with the click on an arrow, flipped (the UVs from bottom to top); with more
// rows than visible, the thumb is the bar's and the bless says 1806. An intermediate draw writes neither the corner
// nor the bar. Without Text.bmd, it fails at the jewel's text.
TEST(JewelBundleListTest, ThePressedArrowFlipsAndAnIntermediateDrawWritesNothing)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        DrawingTheList tested(map);
        JewelBundleList& list = tested.List();
        tested.world->Runtime().npcWindows.jewels.kind = 0;
        for (std::int32_t cell = 0; cell < 12; ++cell)
            Client::ChaosMix::AddTheBundle(list, cell, 0);
        tested.world->Runtime().mouse.pendingClick = true;
        tested.mouse = {397, 345};
        ASSERT_TRUE(tested.Draw(true).HasValue());
        EXPECT_EQ(list.x, 0);
        EXPECT_FLOAT_EQ(list.barTop, 0.0f);
        const auto quads = tested.Quads();
        ASSERT_EQ(quads.size(), 8u);
        EXPECT_FLOAT_EQ(quads[1].first.u, 0.0f);
        EXPECT_FLOAT_EQ(quads[2].first.u, 0.8125f);
        EXPECT_FLOAT_EQ(quads[2].first.v, 0.5f);
        EXPECT_FLOAT_EQ(quads[2].second.u, 0.0f);
        EXPECT_FLOAT_EQ(quads[2].second.v, 0.09375f);
        // The thumb at 8/12 of the track, at the top with the first visible one at 4 (the last).
        EXPECT_NEAR(480.0f - quads[5].first.y, 252.0f, 1e-3);
        ASSERT_EQ(tested.text.texts.size(), 8u);
        EXPECT_EQ(tested.text.texts[0].text, "bendicion,  10");

        const auto cut = tested.Draw(false, false);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("1806"), std::string::npos) << cut.Error();
        EXPECT_EQ(list.x, ListX);
    }
}
} // namespace
