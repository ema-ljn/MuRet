#include "Hud/Dialogs/GameDialogSteps.h"
#include "Kernel/MouseState.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Input/InputState.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Hud/GameUi.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameDialogs.h"
#include "Ui/MessageArea.h"
#include "Kernel/ItemState.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"
#include "Ui/GameUiState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// The step of the 0xa4, unmixing a jewel bundle (0x625cf4, CL9 milestone 6, phases 4a and 5): the two jewels with the
// list of 0x6e5660 (0x402a10 and 0x402a20), the notice with no bundles, the cancel with no jewel and, with one, the
// list's step, its OK with the chosen one and its cancel (0x625dbe..0x625f37). None draws rand(). Made-up texts; the
// world in Lorencia (0), Devias (2) and Noria (3).
namespace
{
using Client::Crt::CrtRandom;
using Client::Ui::GameUiContext;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
using Client::Npc::JewelBundleList;
using Client::Npc::JewelMixState;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

constexpr std::array<std::int32_t, 3> Maps{0, 2, 3};
constexpr std::uint16_t BlessBundle = 0x181e;
constexpr std::uint16_t SoulBundle = 0x181f;
constexpr std::uint16_t EmptyItem = 0xffff;
constexpr std::int32_t UnmixDialog = 0xa4;
// 0x625d00..0x625d6f: the jewels at y from 160 to 185, the first at x from 235 to 315 and the second from 315 to 395.
constexpr std::int32_t KindY = 170;
constexpr std::int32_t BlessX = 250;
constexpr std::int32_t SoulX = 330;
// 0x625dc3..0x625f37: the OK and the cancel, at y from 0x6d + 0xf0 + 10 = 359 to 384.
constexpr std::int32_t ButtonsY = 370;
constexpr std::int32_t OkX = 270;
constexpr std::int32_t CancelX = 360;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

class UnmixingJewels
{
public:
    explicit UnmixingJewels(std::int32_t map)
    {
        auto& runtime = world->Runtime();
        runtime.map = map;
        runtime.dialogs.current = UnmixDialog;
        runtime.npcWindows.jewels.mode = 1;
        for (ItemState& cell : world->Inventory().cells)
            cell.id = EmptyItem;
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

    void Put(std::uint16_t id, std::size_t cell, std::uint32_t level)
    {
        ItemState& item = world->Inventory().cells[cell];
        item.id = id;
        item.scalar1Low = (level << 3) | 2u;
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

    [[nodiscard]] Client::Kernel::WorldRuntime& Runtime() const
    {
        return world->Runtime();
    }
    [[nodiscard]] JewelMixState& Jewels() const
    {
        return world->Runtime().npcWindows.jewels;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x2468u};
    Client::Input::InputState input;
    LetterMeasure measure;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{1806, "bendicion"},
                                                                    {1807, "alma"},
                                                                    {1813, "deshacer %s %d?"},
                                                                    {1814, "cuesta %d"},
                                                                    {1818, "nada para deshacer"}});
    std::vector<Client::Protocol::Outgoing> sent;
};

std::vector<std::pair<std::int32_t, std::uint8_t>> RowsOf(const JewelMixState& jewels)
{
    std::vector<std::pair<std::int32_t, std::uint8_t>> rows;
    for (const JewelBundleList::Row& row : jewels.bundles.rows)
        rows.emplace_back(row.cell, row.level);
    return rows;
}

// 0x625d73..0x625db1: with no bundles of the jewel, the list stays empty, the error notice 1818 and 0x402e50 leaves it
// unchosen and opens the 0xa4 again; the click turns off. The other jewel's bundles do not count.
TEST(JewelUnmixTest, AKindWithoutBundlesSaysWhyAndGoesBack)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        UnmixingJewels tested(map);
        tested.Put(SoulBundle, 4, 1);
        tested.Jewels().bundles.rows.push_back({.cell = 9, .level = 9});
        const CrtRandom before = tested.random;
        ASSERT_TRUE(tested.Click(BlessX, KindY).HasValue());
        EXPECT_TRUE(tested.Jewels().bundles.rows.empty());
        EXPECT_EQ(tested.Errors(), std::vector<std::string>{"nada para deshacer"});
        EXPECT_EQ(tested.Jewels().kind, 0xff);
        EXPECT_EQ(tested.Runtime().dialogs.current, UnmixDialog);
        EXPECT_EQ(tested.Runtime().dialogs.pending, 0);
        EXPECT_FALSE(tested.Runtime().mouse.pendingClick);
        EXPECT_TRUE(tested.sent.empty());
        EXPECT_EQ(tested.random.State(), before.State());
    }
}

// 0x625d73..0x625dbe and 0x402a20: the clicked jewel (x = 315 belongs to the first) fills the list with its bundles in
// cell order, with the level from (+4 >> 3) & 0xf, with none chosen; the click no longer reaches the OK or the cancel.
TEST(JewelUnmixTest, AKindFillsTheList)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        for (const auto& [x, y, kind, rows] :
             std::to_array<std::tuple<std::int32_t, std::int32_t, std::uint8_t,
                                      std::vector<std::pair<std::int32_t, std::uint8_t>>>>({
                 {SoulX, 185, std::uint8_t{1}, {{5, std::uint8_t{3}}, {40, std::uint8_t{0}}}},
                 {315, 160, std::uint8_t{0}, {{12, std::uint8_t{15}}}},
                 {395, KindY, std::uint8_t{1}, {{5, std::uint8_t{3}}, {40, std::uint8_t{0}}}},
             }))
        {
            UnmixingJewels tested(map);
            tested.Put(SoulBundle, 40, 0);
            tested.Put(BlessBundle, 12, 15);
            tested.Put(SoulBundle, 5, 3);
            const CrtRandom before = tested.random;
            ASSERT_TRUE(tested.Click(x, y).HasValue());
            EXPECT_EQ(tested.Jewels().kind, kind);
            EXPECT_EQ(RowsOf(tested.Jewels()), rows);
            EXPECT_EQ(tested.Jewels().bundles.selection, 0);
            EXPECT_EQ(tested.Runtime().dialogs.current, UnmixDialog);
            EXPECT_TRUE(tested.sent.empty());
            EXPECT_FALSE(tested.Runtime().mouse.pendingClick);
            EXPECT_TRUE(tested.Errors().empty());
            EXPECT_EQ(tested.random.State(), before.State());
        }
    }
}

// 0x625d00..0x625d6f, 0x625f4d..0x625f96 and 0x625f1f..0x625f37: without the click, neither the jewels nor the cancel;
// outside them, the click stays. The cancel (x from 279.5 to 359.5, y from 200 to 225) with the click closes
// everything with 0x402eb0 and the C1 03 31, without turning it off (07 Q-C952).
TEST(JewelUnmixTest, TheCancelClosesAllAndKeepsTheClick)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        UnmixingJewels tested(map);
        tested.Put(BlessBundle, 0, 2);
        auto& runtime = tested.Runtime();
        ASSERT_TRUE(tested.Step(BlessX, KindY).HasValue());
        EXPECT_EQ(tested.Jewels().kind, 0xff);
        ASSERT_TRUE(tested.Step(300, 210).HasValue());
        EXPECT_EQ(runtime.dialogs.current, UnmixDialog);
        ASSERT_TRUE(tested.Click(279, 210).HasValue());
        ASSERT_TRUE(tested.Click(BlessX, 186).HasValue());
        EXPECT_EQ(runtime.dialogs.current, UnmixDialog);
        EXPECT_TRUE(runtime.mouse.pendingClick);
        EXPECT_TRUE(tested.sent.empty());

        tested.Jewels().cell = 3;
        ASSERT_TRUE(tested.Click(359, 225).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_EQ(tested.Jewels().mode, 0);
        EXPECT_EQ(tested.Jewels().cell, -1);
        EXPECT_TRUE(runtime.mouse.pendingClick);
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_TRUE(SameBytes(tested.sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
    }
}

// 0x625dc3..0x625eb8 and 0x402a90, in the three maps: with none chosen the OK only turns off the click. With one, it
// asks for its bundle with its level: 0x402b70 finds it in its cell and opens the 0xa3 with the question; without the
// bundle, the notice 1818 and the return of 0x402e50.
TEST(JewelUnmixTest, TheOkAsksForTheChosenBundle)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        UnmixingJewels tested(map);
        tested.Put(SoulBundle, 7, 2);
        tested.Put(SoulBundle, 30, 5);
        auto& runtime = tested.Runtime();
        const CrtRandom before = tested.random;
        ASSERT_TRUE(tested.Click(SoulX, KindY).HasValue());
        ASSERT_EQ(tested.Jewels().bundles.rows.size(), 2u);

        ASSERT_TRUE(tested.Click(OkX, ButtonsY).HasValue());
        EXPECT_FALSE(runtime.mouse.pendingClick);
        EXPECT_EQ(runtime.dialogs.current, UnmixDialog);
        EXPECT_EQ(tested.Jewels().cell, -1);

        tested.Jewels().bundles.selection = 2;
        ASSERT_TRUE(tested.Click(OkX, ButtonsY).HasValue());
        EXPECT_EQ(tested.Jewels().cell, 30);
        EXPECT_EQ(tested.Jewels().level, 5);
        EXPECT_EQ(tested.Jewels().counted, 1);
        EXPECT_EQ(tested.Jewels().price, 1000000);
        EXPECT_EQ(runtime.dialogs.current, Client::Ui::ServerTimeDialog);
        EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 0), "deshacer alma 6?");
        EXPECT_EQ(runtime.mouse.updateTimeMax, 6);
        EXPECT_FALSE(runtime.mouse.pendingClick);
        EXPECT_TRUE(tested.sent.empty());

        runtime.dialogs.current = UnmixDialog;
        tested.world->Inventory().cells[30].id = EmptyItem;
        ASSERT_TRUE(tested.Click(OkX, ButtonsY).HasValue());
        EXPECT_EQ(tested.Errors(), std::vector<std::string>{"nada para deshacer"});
        EXPECT_EQ(tested.Jewels().kind, 0xff);
        EXPECT_EQ(runtime.dialogs.current, UnmixDialog);
        EXPECT_EQ(tested.random.State(), before.State());
    }
}

// 0x46f1a0 and 0x402a90, in the three maps: with the list at its corner (0xe4, 0x15d), the double click on a row
// chooses it and asks for its bundle; the one of cell 0 only chooses it (07 Q-C1152). The OK asks for it anyway.
TEST(JewelUnmixTest, TheDoubleClickOnARowAsksForItsBundle)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        UnmixingJewels tested(map);
        tested.Put(BlessBundle, 0, 1);
        tested.Put(BlessBundle, 3, 4);
        auto& runtime = tested.Runtime();
        ASSERT_TRUE(tested.Click(BlessX, KindY).HasValue());
        JewelBundleList& list = tested.Jewels().bundles;
        list.x = 0xe4;
        list.y = 0x6d + 0xf0;
        // With 2 rows, row i at y = (2 − i) · 13 + 230: row 0 in [253, 266) and row 1 in [240, 253).
        runtime.ui.doubleClick = true;
        ASSERT_TRUE(tested.Step(300, 260).HasValue());
        EXPECT_EQ(list.selection, 1);
        EXPECT_FALSE(runtime.ui.doubleClick);
        EXPECT_EQ(runtime.dialogs.current, UnmixDialog);
        EXPECT_EQ(tested.Jewels().cell, -1);
        EXPECT_EQ(runtime.ui.hovered, list.id);
        EXPECT_EQ(runtime.ui.capture, 0);

        runtime.ui.doubleClick = true;
        ASSERT_TRUE(tested.Step(300, 245).HasValue());
        EXPECT_EQ(list.selection, 2);
        EXPECT_EQ(tested.Jewels().cell, 3);
        EXPECT_EQ(tested.Jewels().level, 4);
        EXPECT_EQ(runtime.dialogs.current, Client::Ui::ServerTimeDialog);
        EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 0), "deshacer bendicion 5?");

        runtime.dialogs.current = UnmixDialog;
        list.selection = 1;
        ASSERT_TRUE(tested.Click(OkX, ButtonsY).HasValue());
        EXPECT_EQ(tested.Jewels().cell, 0);
        EXPECT_EQ(runtime.dialogs.current, Client::Ui::ServerTimeDialog);
    }
}

// 0x625ebe..0x625f37, in the three maps: with a jewel, the cancel (x from 324.5 to 404.5) with the click closes
// everything with 0x402eb0 and the C1 03 31, without turning it off; the OK turns off the click before and the cancel
// no longer sees it.
TEST(JewelUnmixTest, TheCancelWithAKindClosesAll)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        UnmixingJewels tested(map);
        tested.Put(SoulBundle, 12, 0);
        auto& runtime = tested.Runtime();
        ASSERT_TRUE(tested.Click(SoulX, KindY).HasValue());
        ASSERT_TRUE(tested.Step(CancelX, ButtonsY).HasValue());
        ASSERT_TRUE(tested.Click(320, ButtonsY).HasValue());
        ASSERT_TRUE(tested.Click(CancelX, 385).HasValue());
        EXPECT_EQ(runtime.dialogs.current, UnmixDialog);
        EXPECT_TRUE(tested.sent.empty());

        ASSERT_TRUE(tested.Click(CancelX, ButtonsY).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_EQ(tested.Jewels().kind, 0xff);
        EXPECT_TRUE(runtime.mouse.pendingClick);
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_TRUE(SameBytes(tested.sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
    }
}
} // namespace
