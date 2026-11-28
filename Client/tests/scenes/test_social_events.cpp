#include "Network/Xor32Packet.h"
#include "Social/SocialEvents.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Party/Party.h"
#include "Protocol/Social/Trade.h"
#include "Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Movement/GameMovementState.h"
#include "Hud/GamePanels.h"
#include "FrontEnd/LoginModels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Quests/QuestState.h"
#include "FrontEnd/SelectionState.h"
#include "Social/SocialState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Session/WorldTransitionSupport.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"
#include "Camera/GameCamera.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"

#include "support/ItemFiles.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"
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

// Social/SocialEvents (CL9 milestone 7): what the incoming trade (36..3D), party (40..44) and duel (AA..AD) do,
// against the ASM of 0x4d4af0, 0x4d5570, 0x4d57f0, 0x5bc300, 0x5bc470, 0x5bc5e0, 0x4d67e0, 0x4e409b, 0x4dac90 and the
// cases of 0x4e2b10. The names and texts are made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;

constexpr std::uint32_t Seed = 1;
// Synthetic Item.bmd types, 1 by 2 and 1 by 1.
constexpr std::uint16_t TallType = 0x001;
constexpr std::uint16_t SmallType = 0x003;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

int DrawsSince(const CrtRandom& random)
{
    CrtRandom reference(Seed);
    for (int draws = 0; draws <= 20'000; ++draws)
    {
        if (reference.State() == random.State())
            return draws;
        static_cast<void>(reference.Next());
    }
    return -1;
}

template <std::size_t Size> std::array<std::byte, Size> NameOf(std::string_view text)
{
    std::array<std::byte, Size> name{};
    for (std::size_t at = 0; at < text.size() && at < Size; ++at)
        name[at] = static_cast<std::byte>(text[at]);
    return name;
}

template <std::size_t Size> std::string TextOf(const std::array<std::byte, Size>& name)
{
    std::string text;
    for (const std::byte b : name)
    {
        if (b == std::byte{0})
            break;
        text.push_back(static_cast<char>(b));
    }
    return text;
}

// The seven bytes of a 39 item (0x5bc740): the type in byte 0, bit 8 in the 0x80 of byte 3 and the upper ones in byte
// 5.
Client::Protocol::Items::ItemData ItemBytes(std::uint16_t type, std::uint8_t level, std::uint8_t durability = 20)
{
    return {static_cast<std::byte>(type & 0xff),
            static_cast<std::byte>(level << 3),
            std::byte{durability},
            static_cast<std::byte>((type >> 1) & 0x80),
            std::byte{0},
            static_cast<std::byte>((type >> 5) & 0xf0),
            std::byte{0}};
}

std::vector<std::uint8_t> PlainBytes(const Client::Protocol::Outgoing& sent)
{
    const auto plain = Client::Network::Xor32Decode(sent.packet);
    std::vector<std::uint8_t> bytes;
    for (const std::byte b : plain.Bytes())
        bytes.push_back(std::to_integer<std::uint8_t>(b));
    return bytes;
}

struct Stage
{
    // As after entering selection (0x5bbe10 from 0x633f20): the grids and the copy empty.
    Stage()
    {
        world->Runtime().hero = 0;
        Client::Social::ClearTheTradeForSelection(world->Runtime().social.trade);
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = *world,
                .selection = selection,
                .dataRoot = {},
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .items = &items,
                .log = log,
                .textMeasure = &letters,
                .texts = &texts};
    }
    Client::Kernel::WorldRuntime& Runtime()
    {
        return world->Runtime();
    }
    Client::Social::TradeState& Trade()
    {
        return world->Runtime().social.trade;
    }
    const Client::Ui::ChatEntry& LastMessage()
    {
        return world->Runtime().chat.log.entries.front();
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    Client::FrontEnd::SelectionState selection{*world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth;
    Client::FrontEnd::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Tables::ItemTable items = Client::Tests::ItemTableOf({{TallType, 1, 2, 0}, {SmallType, 1, 1, 0}});
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{492, "rechazado"},
                                                                    {493, "ocupado"},
                                                                    {494, "cancelado"},
                                                                    {495, "sin lugar"},
                                                                    {496, "sin tiempo"},
                                                                    {497, "party 0"},
                                                                    {498, "party 1"},
                                                                    {499, "party 2"},
                                                                    {500, "party 3"},
                                                                    {501, "party 4"},
                                                                    {502, "dejaste la party"},
                                                                    {535, "party 5"},
                                                                    {912, "duelo con %s"},
                                                                    {913, "no quiso"},
                                                                    {914, "fin del duelo"}});
    LetterMeasure letters;
};

constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;

class SocialEventsTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    std::unique_ptr<Stage> MakeStage()
    {
        auto stage = std::make_unique<Stage>();
        stage->Runtime().map = GetParam();
        return stage;
    }
};

// What a discard checks: each cell of the other's grid and of the copy, and 0x78abaa5.
struct TradeGrids
{
    std::vector<std::uint16_t> ids;
    std::vector<std::uint32_t> occupied;
    std::vector<std::uint8_t> warnings;
    std::uint8_t changed = 0;

    bool operator==(const TradeGrids&) const = default;
};

TradeGrids GridsOf(const Client::Social::TradeState& trade)
{
    TradeGrids grids;
    for (std::size_t slot = 0; slot < Client::Social::TradeGridCells; ++slot)
    {
        grids.ids.push_back(trade.otherGrid[slot].id);
        grids.ids.push_back(trade.backup[slot].id);
        grids.occupied.push_back(trade.otherGrid[slot].occupied);
        grids.warnings.push_back(trade.otherGrid[slot].warning);
    }
    grids.changed = trade.changed;
    return grids;
}

bool WarnedWith(const Stage& stage, std::string_view text)
{
    return !stage.sink.lines.empty() && stage.sink.levels.back() == Client::Core::LogLevel::Warning &&
           stage.sink.lines.back().find(text) != std::string::npos;
}

// 0x4d4af0 and 0x4d5570 with the trade open: the request (no shops, no rand()), answer 1 (Open(0x20)).
void OpenTheTrade(Stage& stage)
{
    const Client::Protocol::Social::TradeRequest request{NameOf<10>("Arbusto")};
    const auto asked = Client::Social::ApplyTradeRequest(request, stage.Resources());
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    ASSERT_FALSE(asked.Value().has_value());
    // Dialog 0x78's yes closes it (0x62c4ac).
    stage.Runtime().dialogs.current = 0;
    const auto opened = Client::Social::ApplyTradeResponse({1, NameOf<10>("Arbusto"), 50, 7}, stage.Resources());
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
}

// On the asking side (no 36): the 37 brings a new name and 0x5bbdf0 empties the copy.
void OpenTheAskedTrade(Stage& stage)
{
    const auto opened = Client::Social::ApplyTradeResponse({1, NameOf<10>("Brezal"), 40, 2}, stage.Resources());
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
}

TEST_P(SocialEventsTest, TheTradeRequestOpensTheDialogWithoutRandoms)
{
    auto stage = MakeStage();
    const auto asked = Client::Social::ApplyTradeRequest({NameOf<10>("Arbusto")}, stage->Resources());
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    EXPECT_FALSE(asked.Value().has_value());
    EXPECT_EQ(stage->Runtime().dialogs.current, 0x78);
    EXPECT_EQ(TextOf(stage->Trade().otherName), "Arbusto");
    EXPECT_EQ(DrawsSince(stage->cloth.random), 0);
}

TEST_P(SocialEventsTest, AnOpenPersonalShopRefusesTheTradeInClear)
{
    auto stage = MakeStage();
    stage->Runtime().panels.personalShop = 1;
    const auto asked = Client::Social::ApplyTradeRequest({NameOf<10>("Arbusto")}, stage->Resources());
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    ASSERT_TRUE(asked.Value().has_value());
    EXPECT_EQ(asked.Value()->delivery, Client::Network::Delivery::Clear);
    EXPECT_EQ(PlainBytes(*asked.Value()), (std::vector<std::uint8_t>{0xc1, 0x04, 0x37, 0x00}));
    EXPECT_EQ(stage->Runtime().dialogs.current, 0);
    EXPECT_TRUE(TextOf(stage->Trade().otherName).empty());
}

TEST_P(SocialEventsTest, AClearTradeRequestOnlyReportsItWithTwoRandoms)
{
    auto stage = MakeStage();
    const Client::Protocol::Outgoing report = Client::Social::ApplyClearTradeRequest(stage->cloth.random);
    EXPECT_EQ(report.delivery, Client::Network::Delivery::Encrypted);
    EXPECT_EQ(PlainBytes(report), (std::vector<std::uint8_t>{0xc1, 0x06, 0xf1, 0x03, 0x00, 0x36}));
    EXPECT_EQ(DrawsSince(stage->cloth.random), 2);
}

// 0x659e46: Open(0x20) with nothing open draws 9: its IsOpen, the 0x10000 and 0x20 of CanOpen, the 0x10000 of its
// CloseByMask, the four of Open(inventory) and the protected byte's write.
TEST_P(SocialEventsTest, TheAcceptedTradeOpensTheWindowAndTheInventory)
{
    auto stage = MakeStage();
    stage->Trade().otherMoney = 5;
    stage->Trade().otherOk = 1;
    stage->Trade().okWait = 3;
    ASSERT_TRUE(Client::Social::ApplyTradeResponse({1, NameOf<10>("Brezal"), 50, 7}, stage->Resources()).HasValue());
    const Client::Ui::PanelState& panels = stage->Runtime().panels;
    EXPECT_EQ(panels.trade, 1);
    EXPECT_EQ(panels.inventory, 1);
    EXPECT_EQ(DrawsSince(stage->cloth.random), 9);
    const Client::Social::TradeState& trade = stage->Trade();
    EXPECT_EQ(TextOf(trade.otherName), "Brezal");
    EXPECT_EQ(trade.otherLevel, 50u);
    EXPECT_EQ(trade.otherGuild, 7);
    EXPECT_EQ(trade.otherMoney, 0u);
    EXPECT_EQ(trade.otherOk, 0);
    EXPECT_EQ(trade.okWait, 0);
    EXPECT_EQ(trade.cursorMoveRequested, 1);
}

// 0x4d55b8 → 0x6599a0(0x20): opening the trade closes the open quest list (the 2 of +0x21383; with the dialog, the 1,
// CanOpen opens nothing), with the C1 03 31 (0x419900) through the session. Without the cable, the close cuts before
// opening.
TEST_P(SocialEventsTest, TheAcceptedTradeClosesTheQuestsWithTheWindowClose)
{
    auto stage = MakeStage();
    stage->Runtime().panels.delegated.quest = Client::Quests::QuestListWindow;
    const auto cut = Client::Social::ApplyTradeResponse({1, NameOf<10>("Brezal"), 50, 7}, stage->Resources());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x419900"), std::string::npos) << cut.Error();

    auto bound = MakeStage();
    bound->Runtime().panels.delegated.quest = Client::Quests::QuestListWindow;
    int windowCloses = 0;
    Client::Scenes::WorldEntryResources resources = bound->Resources();
    resources.sendWindowClose = [&windowCloses]() { ++windowCloses; };
    const auto opened = Client::Social::ApplyTradeResponse({1, NameOf<10>("Brezal"), 50, 7}, resources);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_EQ(windowCloses, 1);
    EXPECT_EQ(bound->Runtime().panels.delegated.quest, 0);
    EXPECT_EQ(bound->Runtime().panels.trade, 1);
    EXPECT_EQ(bound->Runtime().panels.inventory, 1);
}

// 0x657a90 from the teleport, the regen, the 1C and the F3 04 (CloseTransitionWindows): with the trade open, the
// C1 03 3D in C3 through the session (0x657fe1), and the grids empty.
TEST_P(SocialEventsTest, TheTransitionClosesTheTradeWithTheCancel)
{
    auto stage = MakeStage();
    OpenTheTrade(*stage);
    std::vector<Client::Protocol::Outgoing> sent;
    Client::Scenes::WorldEntryResources resources = stage->Resources();
    resources.sendGamePacket = [&sent](Client::Protocol::Outgoing outgoing) { sent.push_back(std::move(outgoing)); };
    const auto closed = Client::Scenes::CloseTransitionWindows(resources);
    ASSERT_TRUE(closed.HasValue()) << closed.Error();
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].delivery, Client::Network::Delivery::Encrypted);
    EXPECT_EQ(PlainBytes(sent[0]), (std::vector<std::uint8_t>{0xc1, 0x03, 0x3d}));
    EXPECT_EQ(stage->Runtime().panels.trade, 0);
    EXPECT_EQ(stage->Runtime().panels.inventory, 0);
}

TEST_P(SocialEventsTest, RefusedAndBusyTradesOnlySayIt)
{
    auto stage = MakeStage();
    ASSERT_TRUE(Client::Social::ApplyTradeResponse({0, {}, 0, 0}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "rechazado");
    EXPECT_EQ(stage->LastMessage().type, Client::Ui::ErrorMessage);
    ASSERT_TRUE(Client::Social::ApplyTradeResponse({2, {}, 0, 0}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "ocupado");
    EXPECT_EQ(stage->Runtime().panels.trade, 0);
    EXPECT_EQ(DrawsSince(stage->cloth.random), 0);
}

TEST_P(SocialEventsTest, AWholeTradeRound)
{
    auto stage = MakeStage();
    OpenTheTrade(*stage);
    ASSERT_EQ(stage->Runtime().panels.trade, 1);

    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({9, ItemBytes(TallType, 3)}, stage->Resources()).HasValue());
    const Client::Social::TradeState& trade = stage->Trade();
    EXPECT_EQ(trade.otherGrid[9].id, TallType);
    EXPECT_EQ(trade.otherGrid[9].occupied, 1u);
    EXPECT_EQ(trade.otherGrid[17].id, TallType);
    EXPECT_EQ(trade.otherGrid[17].occupied, 0u);
    EXPECT_EQ(trade.otherGrid[9].gridColumn, 1);
    EXPECT_EQ(trade.otherGrid[9].gridRow, 1);
    EXPECT_EQ((trade.otherGrid[9].scalar1Low >> 3) & 0xf, 3u);
    EXPECT_EQ(stage->Runtime().lastSoundId, Client::Audio::SoundId{0x1d});
    EXPECT_EQ(trade.changed, 0);

    stage->Trade().sentMoney = 1000;
    Client::Social::ApplyTradeMoneyResult({1}, *stage->world);
    EXPECT_EQ(trade.ownMoney, 1000u);
    Client::Social::ApplyTradeOtherMoney({2500}, *stage->world);
    EXPECT_EQ(trade.otherMoney, 2500u);
    Client::Social::ApplyTradeOkState({1}, *stage->world);
    EXPECT_EQ(trade.otherOk, 1);
    EXPECT_EQ(stage->Runtime().lastSoundId, Client::Audio::SoundId{0x19});
    stage->Trade().ownOk = 1;
    Client::Social::ApplyTradeOkState({2}, *stage->world);
    EXPECT_EQ(trade.ownOk, 0);
    EXPECT_EQ(trade.otherOk, 1);
    Client::Social::ApplyTradeMoneyResult({0}, *stage->world);
    EXPECT_EQ(trade.ownMoney, 0u);

    // 0x4d57f0 with 1: no text, the cursor count at 0 and Close(0x20, 1): its IsOpen, the CloseByMask's and the write,
    // without the 3D.
    const CrtRandom before = stage->cloth.random;
    ASSERT_TRUE(Client::Social::ApplyTradeResult({1, {0xc1, 0x04, 0x3d, 0x01}}, stage->Resources()).HasValue());
    CrtRandom expected = before;
    expected.Discard(3);
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    EXPECT_EQ(stage->Runtime().panels.trade, 0);
    EXPECT_EQ(stage->Runtime().panels.inventory, 0);
    EXPECT_EQ(trade.otherGrid[9].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(trade.otherGrid[17].id, Client::Kernel::EmptyOwnItemId);
}

// 0x5bd750 stores the copy of what the other removes (0x5bc5e0) and 0x5bc470 marks with 99 what comes back worse.
TEST_P(SocialEventsTest, TheOtherGridMarksAWorseItemThanItsCopy)
{
    auto stage = MakeStage();
    OpenTheAskedTrade(*stage);
    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({0, ItemBytes(TallType, 5)}, stage->Resources()).HasValue());
    ASSERT_TRUE(Client::Social::ApplyTradeItemRemoved({0}, stage->Resources()).HasValue());
    const Client::Social::TradeState& trade = stage->Trade();
    EXPECT_EQ(trade.backup[0].id, TallType);
    EXPECT_EQ((trade.backup[0].scalar1Low >> 3) & 0xf, 5u);
    EXPECT_EQ(trade.otherGrid[0].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(trade.otherGrid[8].id, Client::Kernel::EmptyOwnItemId);

    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({0, ItemBytes(TallType, 4)}, stage->Resources()).HasValue());
    EXPECT_EQ(trade.changed, 1);
    EXPECT_EQ(trade.otherGrid[0].warning, 99);
    EXPECT_EQ(trade.otherGrid[8].warning, 99);
    EXPECT_EQ(trade.otherGrid[1].warning, 1);

    // One equal to the copy sets 0x5bc470's list: 0x78abaa5 at 0 and the two of the type with 1.
    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({2, ItemBytes(TallType, 5)}, stage->Resources()).HasValue());
    EXPECT_EQ(trade.changed, 0);
    EXPECT_EQ(trade.otherGrid[0].warning, 1);
    EXPECT_EQ(trade.otherGrid[2].warning, 1);
}

// 0x5bc5e0 does not copy a plain item (level 4 or less, not excellent).
TEST_P(SocialEventsTest, APlainItemIsNotWorthACopy)
{
    auto stage = MakeStage();
    OpenTheTrade(*stage);
    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({0, ItemBytes(SmallType, 4)}, stage->Resources()).HasValue());
    ASSERT_TRUE(Client::Social::ApplyTradeItemRemoved({0}, stage->Resources()).HasValue());
    // The copy stays as 0x5bbe10 left it: the 36 had already set the name and the 37 did not run 0x5bbdf0.
    EXPECT_EQ(stage->Trade().backup[0].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(stage->Trade().backup[1].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(stage->Trade().otherGrid[0].id, Client::Kernel::EmptyOwnItemId);
}

// 0x5bc52d..0x5bc54a: 0x5bc470's stack list holds 10; the eleventh type equal to its copy falls on the list's pointer
// and is lost, and the second pass compares in its place what is left of the count, 1 (Q-C961 b): the staff (type 1),
// worse than its copy (99), goes back to 1. The item goes in and sounds.
TEST_P(SocialEventsTest, ElevenItemsLikeTheirCopiesLoseTheEleventh)
{
    auto stage = MakeStage();
    OpenTheAskedTrade(*stage);
    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({16, ItemBytes(TallType, 5)}, stage->Resources()).HasValue());
    ASSERT_TRUE(Client::Social::ApplyTradeItemRemoved({16}, stage->Resources()).HasValue());
    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({0, ItemBytes(SmallType, 5)}, stage->Resources()).HasValue());
    ASSERT_TRUE(Client::Social::ApplyTradeItemRemoved({0}, stage->Resources()).HasValue());
    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({16, ItemBytes(TallType, 4)}, stage->Resources()).HasValue());
    const Client::Social::TradeState& trade = stage->Trade();
    for (std::uint8_t slot = 0; slot < 10; ++slot)
    {
        const auto added = Client::Social::ApplyTradeItemAdded({slot, ItemBytes(SmallType, 5)}, stage->Resources());
        ASSERT_TRUE(added.HasValue()) << added.Error();
    }
    EXPECT_EQ(trade.otherGrid[16].warning, 99);
    EXPECT_EQ(trade.otherGrid[24].warning, 99);

    stage->Runtime().lastSoundId = Client::Audio::SoundId{0};
    const auto eleventh = Client::Social::ApplyTradeItemAdded({10, ItemBytes(SmallType, 5)}, stage->Resources());
    ASSERT_TRUE(eleventh.HasValue()) << eleventh.Error();
    EXPECT_EQ(trade.otherGrid[10].id, SmallType);
    EXPECT_EQ(trade.otherGrid[10].warning, 1);
    EXPECT_EQ(trade.otherGrid[16].warning, 1);
    EXPECT_EQ(trade.otherGrid[24].warning, 1);
    EXPECT_EQ(trade.changed, 0);
    EXPECT_EQ(stage->Runtime().lastSoundId, Client::Audio::SoundId{0x1d});
    EXPECT_FALSE(WarnedWith(*stage, "0x5bc545"));
}

// 0x5bc545: the twelfth type overwrites the list's count (ebp − 0x10) and what follows reads the stack: the 39 is
// discarded whole, without the item, without the sound and with a warning.
TEST_P(SocialEventsTest, TwelveItemsLikeTheirCopiesDiscardTheTwelfth)
{
    auto stage = MakeStage();
    OpenTheAskedTrade(*stage);
    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({0, ItemBytes(SmallType, 5)}, stage->Resources()).HasValue());
    ASSERT_TRUE(Client::Social::ApplyTradeItemRemoved({0}, stage->Resources()).HasValue());
    constexpr std::uint8_t Slots = 12;
    for (std::uint8_t slot = 0; slot + 1 < Slots; ++slot)
    {
        const auto added = Client::Social::ApplyTradeItemAdded({slot, ItemBytes(SmallType, 5)}, stage->Resources());
        ASSERT_TRUE(added.HasValue()) << added.Error();
    }
    EXPECT_FALSE(WarnedWith(*stage, "0x5bc545"));
    stage->Runtime().lastSoundId = Client::Audio::SoundId{0};
    const TradeGrids before = GridsOf(stage->Trade());
    const CrtRandom random = stage->cloth.random;
    const auto twelfth = Client::Social::ApplyTradeItemAdded({11, ItemBytes(SmallType, 5)}, stage->Resources());
    ASSERT_TRUE(twelfth.HasValue()) << twelfth.Error();
    EXPECT_EQ(stage->Trade().otherGrid[11].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(GridsOf(stage->Trade()), before);
    EXPECT_EQ(stage->cloth.random.State(), random.State());
    EXPECT_EQ(stage->Runtime().lastSoundId, Client::Audio::SoundId{0});
    EXPECT_TRUE(WarnedWith(*stage, "0x5bc545"));
}

// The 38 that leaves twelve items equal to the new copy (0x5bc5e0 stores it before 0x5bc470) is also discarded: the
// item and the copy stay as they were. With eleven, it passes.
TEST_P(SocialEventsTest, ARemovalThatLeavesTwelveLikeTheirCopyIsDiscarded)
{
    auto stage = MakeStage();
    OpenTheAskedTrade(*stage);
    constexpr std::uint8_t Slots = 13;
    for (std::uint8_t slot = 0; slot < Slots; ++slot)
    {
        const auto added = Client::Social::ApplyTradeItemAdded({slot, ItemBytes(SmallType, 5)}, stage->Resources());
        ASSERT_TRUE(added.HasValue()) << added.Error();
    }
    stage->Runtime().lastSoundId = Client::Audio::SoundId{0};
    const TradeGrids before = GridsOf(stage->Trade());
    const auto removed = Client::Social::ApplyTradeItemRemoved({12}, stage->Resources());
    ASSERT_TRUE(removed.HasValue()) << removed.Error();
    EXPECT_EQ(stage->Trade().otherGrid[12].id, SmallType);
    EXPECT_EQ(stage->Trade().backup[0].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(GridsOf(stage->Trade()), before);
    EXPECT_EQ(stage->Runtime().lastSoundId, Client::Audio::SoundId{0});
    EXPECT_TRUE(WarnedWith(*stage, "0x5bc545"));

    auto other = MakeStage();
    OpenTheAskedTrade(*other);
    for (std::uint8_t slot = 0; slot < Slots - 1; ++slot)
        ASSERT_TRUE(
            Client::Social::ApplyTradeItemAdded({slot, ItemBytes(SmallType, 5)}, other->Resources()).HasValue());
    ASSERT_TRUE(Client::Social::ApplyTradeItemRemoved({11}, other->Resources()).HasValue());
    EXPECT_EQ(other->Trade().otherGrid[11].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(other->Trade().backup[0].id, SmallType);
    EXPECT_FALSE(WarnedWith(*other, "0x5bc545"));
}

// 0x5bd750 with a slot from 32 (other than 0xff) reads outside the grid in 0x5bc5e0: the 38 is discarded, without the
// sound; 0xff returns without touching anything and sounds the same (0x4e3d7f).
TEST_P(SocialEventsTest, ARemovalPastTheGridIsDiscarded)
{
    auto stage = MakeStage();
    OpenTheAskedTrade(*stage);
    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({31, ItemBytes(SmallType, 5)}, stage->Resources()).HasValue());
    stage->Runtime().lastSoundId = Client::Audio::SoundId{0};
    const TradeGrids before = GridsOf(stage->Trade());
    ASSERT_TRUE(Client::Social::ApplyTradeItemRemoved({32}, stage->Resources()).HasValue());
    EXPECT_EQ(GridsOf(stage->Trade()), before);
    EXPECT_EQ(stage->Runtime().lastSoundId, Client::Audio::SoundId{0});
    EXPECT_TRUE(WarnedWith(*stage, "0x5bd750"));

    const std::size_t lines = stage->sink.lines.size();
    ASSERT_TRUE(Client::Social::ApplyTradeItemRemoved({0xff}, stage->Resources()).HasValue());
    EXPECT_EQ(GridsOf(stage->Trade()), before);
    EXPECT_EQ(stage->Runtime().lastSoundId, Client::Audio::SoundId{0x1d});
    EXPECT_EQ(stage->sink.lines.size(), lines);
}

// 0x5bd307..0x5bd3ec: a 1 by 2 item on the last row would write cell 36 (Q-C200): the 39 is discarded before writing
// and before 0x54bf40, without the sound. From slot 23 it just fits (cells 23 and 31).
TEST_P(SocialEventsTest, AnItemPastTheGridIsDiscarded)
{
    auto stage = MakeStage();
    OpenTheAskedTrade(*stage);
    stage->Runtime().lastSoundId = Client::Audio::SoundId{0};
    const TradeGrids before = GridsOf(stage->Trade());
    const CrtRandom random = stage->cloth.random;
    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({28, ItemBytes(TallType, 3)}, stage->Resources()).HasValue());
    EXPECT_EQ(GridsOf(stage->Trade()), before);
    EXPECT_EQ(stage->cloth.random.State(), random.State());
    EXPECT_EQ(stage->Runtime().lastSoundId, Client::Audio::SoundId{0});
    EXPECT_TRUE(WarnedWith(*stage, "casilla 36"));
    EXPECT_TRUE(WarnedWith(*stage, "Q-C200"));

    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({23, ItemBytes(TallType, 3)}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->Trade().otherGrid[23].id, TallType);
    EXPECT_EQ(stage->Trade().otherGrid[31].id, TallType);
    EXPECT_EQ(stage->Runtime().lastSoundId, Client::Audio::SoundId{0x1d});
}

// On the asking side, the 37 arrives with a new name and 0x5bbdf0 empties the copy (type −1).
TEST_P(SocialEventsTest, ANewNameClearsTheCopy)
{
    auto stage = MakeStage();
    ASSERT_TRUE(Client::Social::ApplyTradeResponse({1, NameOf<10>("Brezal"), 50, 7}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->Trade().backup[0].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(stage->Trade().backup[31].id, Client::Kernel::EmptyOwnItemId);
}

// 0x5bc107..0x5bc13f: entering selection leaves 0x78abaa5 at 0 and the two grids and the copy empty, without rand().
TEST_P(SocialEventsTest, TheSelectionEntryEmptiesTheTrade)
{
    Client::Social::TradeState trade;
    trade.changed = 1;
    trade.otherGrid[5].id = SmallType;
    trade.otherGrid[5].occupied = 1;
    trade.ownGrid[31].occupied = 1;
    Client::Social::ClearTheTradeForSelection(trade);
    EXPECT_EQ(trade.changed, 0);
    for (std::size_t slot = 0; slot < Client::Social::TradeGridCells; ++slot)
    {
        EXPECT_EQ(trade.otherGrid[slot].id, Client::Kernel::EmptyOwnItemId);
        EXPECT_EQ(trade.ownGrid[slot].id, Client::Kernel::EmptyOwnItemId);
        EXPECT_EQ(trade.backup[slot].id, Client::Kernel::EmptyOwnItemId);
        EXPECT_EQ(trade.otherGrid[slot].occupied, 0u);
        EXPECT_EQ(trade.ownGrid[slot].occupied, 0u);
    }
}

TEST_P(SocialEventsTest, TheTradeResultsSayWhatHappened)
{
    auto stage = MakeStage();
    OpenTheAskedTrade(*stage);
    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({0, ItemBytes(TallType, 6)}, stage->Resources()).HasValue());
    stage->Runtime().dialogs.current = 0x73;
    stage->Runtime().social.moneyField = 1;
    ASSERT_TRUE(Client::Social::ApplyTradeResult({0, {}}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "cancelado");
    // The cancel copies what the other had.
    EXPECT_EQ(stage->Trade().backup[0].id, TallType);
    EXPECT_EQ(stage->Runtime().dialogs.current, 0);
    EXPECT_EQ(stage->Runtime().social.dialogFieldLimit, 0x2a);
    EXPECT_EQ(stage->Runtime().social.moneyField, 0);
    EXPECT_EQ(stage->Runtime().panels.trade, 0);

    OpenTheTrade(*stage);
    ASSERT_TRUE(Client::Social::ApplyTradeResult({2, {}}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "sin lugar");
    OpenTheTrade(*stage);
    stage->Runtime().dialogs.pending = 0x6e;
    ASSERT_TRUE(Client::Social::ApplyTradeResult({3, {}}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "sin tiempo");
    EXPECT_EQ(stage->Runtime().dialogs.current, 0x6e);
}

TEST_P(SocialEventsTest, TheItemMoveLockDefersTheFirstResult)
{
    auto stage = MakeStage();
    OpenTheTrade(*stage);
    stage->Runtime().ui.field78abb39 = 1;
    ASSERT_TRUE(Client::Social::ApplyTradeResult({0, {0xc1, 0x04, 0x3d, 0x00}}, stage->Resources()).HasValue());
    ASSERT_TRUE(Client::Social::ApplyTradeResult({3, {0xc1, 0x04, 0x3d, 0x03}}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->Trade().resultDeferred, 1);
    EXPECT_EQ(stage->Trade().deferredHead, (std::array<std::uint8_t, 4>{0xc1, 0x04, 0x3d, 0x00}));
    EXPECT_EQ(stage->Runtime().panels.trade, 1);
}

// 0x657fb5 with aux at 0 (the close by hand): the 3D in C3 with its two rand(); with 1, nothing goes out. The two
// grids end up empty and, with 0x78abaa5 at 1, the copy too.
TEST_P(SocialEventsTest, ClosingTheGridsSendsTheCancelOnlyByHand)
{
    auto stage = MakeStage();
    stage->Trade().ownGrid[3].id = SmallType;
    stage->Trade().otherGrid[4].id = SmallType;
    stage->Trade().backup[0].id = SmallType;
    stage->Trade().changed = 1;
    std::vector<Client::Protocol::Outgoing> sent;
    ASSERT_TRUE(Client::Social::CloseTheTradeGrids(*stage->world, stage->cloth.random, true,
                                                   [&sent](Client::Protocol::Outgoing out)
                                                   { sent.push_back(std::move(out)); })
                    .HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(DrawsSince(stage->cloth.random), 0);
    EXPECT_EQ(stage->Trade().ownGrid[3].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(stage->Trade().otherGrid[4].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(stage->Trade().backup[0].id, Client::Kernel::EmptyOwnItemId);

    ASSERT_TRUE(Client::Social::CloseTheTradeGrids(*stage->world, stage->cloth.random, false,
                                                   [&sent](Client::Protocol::Outgoing out)
                                                   { sent.push_back(std::move(out)); })
                    .HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].delivery, Client::Network::Delivery::Encrypted);
    EXPECT_EQ(PlainBytes(sent[0]), (std::vector<std::uint8_t>{0xc1, 0x03, 0x3d}));
    EXPECT_EQ(DrawsSince(stage->cloth.random), 2);

    const auto cut = Client::Social::CloseTheTradeGrids(*stage->world, stage->cloth.random, false, {});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x657fe1"), std::string::npos);
}

// Close(0x20) by hand with the trade open and without the cable (GamePanels without LendTheSession): its two queries
// and the cut before the 3D, with the protected byte still at 1.
TEST_P(SocialEventsTest, ClosingTheOpenTradeByHandWithoutTheCableCutsBeforeTheCancel)
{
    auto stage = MakeStage();
    OpenTheTrade(*stage);
    const CrtRandom before = stage->cloth.random;
    Client::Hud::GamePanels panels(*stage->world, stage->cloth.random);
    const auto cut = panels.Registry().Close(Client::Ui::TradePanel);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x657fe1"), std::string::npos);
    CrtRandom expected = before;
    expected.Discard(2);
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    EXPECT_EQ(stage->Runtime().panels.trade, 1);
}

TEST_P(SocialEventsTest, CompareTradeItemsGoesFieldByField)
{
    ItemState a{};
    ItemState b{};
    a.id = TallType;
    b.id = SmallType;
    EXPECT_EQ(Client::Social::CompareTradeItems(a, b), 2);
    b.id = TallType;
    EXPECT_EQ(Client::Social::CompareTradeItems(a, b), 0);
    a.scalar1Low = 3 << 3;
    EXPECT_EQ(Client::Social::CompareTradeItems(a, b), 1);
    b.scalar1Low = 4 << 3;
    EXPECT_EQ(Client::Social::CompareTradeItems(a, b), -1);
    b.scalar1Low = a.scalar1Low;
    a.flags1b = 0x01;
    EXPECT_EQ(Client::Social::CompareTradeItems(a, b), 1);
    b.flags1b = 0x41;
    EXPECT_EQ(Client::Social::CompareTradeItems(a, b), 0);
    a.optionCount = 1;
    EXPECT_EQ(Client::Social::CompareTradeItems(a, b), 1);
    b.optionCount = 1;
    b.optionBytes[0] = 0x50;
    b.optionBytes[Client::Kernel::ItemOptionValueOffset] = 4;
    EXPECT_EQ(Client::Social::CompareTradeItems(a, b), -1);
    a.optionBytes[0] = 0x53;
    a.optionBytes[Client::Kernel::ItemOptionValueOffset] = 4;
    EXPECT_EQ(Client::Social::CompareTradeItems(a, b), 0);
    a.durability = 9;
    b.durability = 10;
    EXPECT_EQ(Client::Social::CompareTradeItems(a, b), -1);
    EXPECT_EQ(Client::Social::CompareTradeItems(b, a), 1);
}

// The invitation (40), the yes (in Hud/Dialogs/GameDialogSteps), the list (42), the lives (44) and the exit (43).
TEST_P(SocialEventsTest, APartyFormsAndBreaks)
{
    auto stage = MakeStage();
    Client::Social::ApplyPartyRequest({0x1234}, *stage->world);
    EXPECT_EQ(stage->Runtime().social.party.requestKey, 0x1234);
    EXPECT_EQ(stage->Runtime().dialogs.current, 0x77);

    stage->Runtime().dialogs.pending = 0;
    ASSERT_TRUE(Client::Social::ApplyPartyRequestResult({1}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "party 1");
    EXPECT_EQ(stage->Runtime().dialogs.current, 0);
    ASSERT_TRUE(Client::Social::ApplyPartyRequestResult({5}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "party 5");
    const std::size_t messages = stage->Runtime().chat.log.entries.size();
    ASSERT_TRUE(Client::Social::ApplyPartyRequestResult({6}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->Runtime().chat.log.entries.size(), messages);

    Client::Protocol::Party::PartyList list{.result = 1, .members = {}};
    list.members.push_back({NameOf<10>("Arbusto"), 0, 3, 120, 130, 900, 1000});
    list.members.push_back({NameOf<10>("Brezal"), 1, 3, 121, 131, 50, 600});
    Client::Social::ApplyPartyList(list, *stage->world, stage->log);
    const Client::Social::PartyState& party = stage->Runtime().social.party;
    EXPECT_EQ(stage->Runtime().hud.partyCount, 2);
    EXPECT_EQ(TextOf(party.slots[1].name), "Brezal");
    EXPECT_EQ(party.slots[1].number, 1);
    EXPECT_EQ(party.slots[1].x, 121);
    EXPECT_EQ(party.slots[1].maxLife, 600u);

    Client::Social::ApplyPartyLife({{0x07, 0x1f}}, *stage->world, stage->log);
    EXPECT_EQ(party.slots[0].tenths, 7);
    EXPECT_EQ(party.slots[1].tenths, 10);
    // 0x4e41bb: a slot of 5 or more would write past the array (Q-C960): the 44 is discarded whole, also what came
    // before.
    Client::Social::ApplyPartyLife({{0x03, 0x53}}, *stage->world, stage->log);
    EXPECT_EQ(party.slots[0].tenths, 7);
    EXPECT_TRUE(WarnedWith(*stage, "Q-C960"));

    stage->Runtime().movement.followTarget = 1;
    ASSERT_TRUE(Client::Social::ApplyPartyLeft(Client::Protocol::Party::PartyLeft{}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->Runtime().hud.partyCount, 0);
    EXPECT_EQ(stage->LastMessage().text, "dejaste la party");
    EXPECT_EQ(stage->Runtime().movement.followTarget, -1);
}

// 0x4d67e0 with six members would write past the array (Q-C960): the 42 is discarded whole, the count included.
TEST_P(SocialEventsTest, SixPartyMembersAreDiscarded)
{
    auto stage = MakeStage();
    Client::Protocol::Party::PartyList list{.result = 1, .members = {}};
    for (std::uint8_t at = 0; at < 6; ++at)
        list.members.push_back({NameOf<10>("Arbusto"), at, 0, 0, 0, 1, 1});
    Client::Social::ApplyPartyList(list, *stage->world, stage->log);
    EXPECT_EQ(stage->Runtime().hud.partyCount, 0);
    EXPECT_TRUE(TextOf(stage->Runtime().social.party.slots[0].name).empty());
    EXPECT_TRUE(WarnedWith(*stage, "Q-C960"));
}

// The request (AC), the start (AA), the points (AD) and the end (AB).
TEST_P(SocialEventsTest, ADuelFromRequestToEnd)
{
    auto stage = MakeStage();
    Client::Social::ApplyDuelRequest({0x0102, NameOf<10>("Brezal")}, *stage->world);
    const Client::Social::DuelState& duel = stage->Runtime().social.duel;
    EXPECT_EQ(stage->Runtime().ui.field78abad9, 1);
    EXPECT_EQ(duel.rivalKey, 0x0102);
    EXPECT_EQ(TextOf(duel.rivalName), "Brezal");
    EXPECT_EQ(stage->Runtime().lastSoundId, Client::Audio::SoundId{0x8c});

    ASSERT_TRUE(Client::Social::ApplyDuelStart({1, 0x0102, NameOf<10>("Brezal")}, stage->Resources()).HasValue());
    EXPECT_EQ(duel.active, 1);
    EXPECT_EQ(stage->LastMessage().text, "duelo con Brezal");
    EXPECT_EQ(stage->Runtime().lastSoundId, Client::Audio::SoundId{0x8d});

    Client::Social::ApplyDuelScore({0x0033, 0x0102, 2, 5}, *stage->world);
    EXPECT_EQ(duel.rivalScore, 5);
    EXPECT_EQ(duel.ownScore, 2);
    Client::Social::ApplyDuelScore({0x0102, 0x0033, 3, 4}, *stage->world);
    EXPECT_EQ(duel.rivalScore, 3);
    EXPECT_EQ(duel.ownScore, 4);
    Client::Social::ApplyDuelScore({0x0044, 0x0055, 9, 9}, *stage->world);
    EXPECT_EQ(duel.rivalScore, 3);

    stage->Runtime().events.timeKind = 2;
    stage->Runtime().events.timeValue = 7;
    ASSERT_TRUE(Client::Social::ApplyDuelEnd({0x0102, NameOf<10>("Brezal")}, stage->Resources()).HasValue());
    EXPECT_EQ(duel.active, 0);
    EXPECT_EQ(stage->Runtime().events.timeValue, 0);
    EXPECT_EQ(stage->LastMessage().text, "fin del duelo");
    EXPECT_EQ(DrawsSince(stage->cloth.random), 0);
}

TEST_P(SocialEventsTest, ARefusedDuelOnlyKeepsTheName)
{
    auto stage = MakeStage();
    ASSERT_TRUE(Client::Social::ApplyDuelStart({0, 0x0102, NameOf<10>("Brezal")}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->Runtime().social.duel.active, 0);
    EXPECT_EQ(stage->Runtime().social.duel.rivalKey, 0);
    EXPECT_EQ(TextOf(stage->Runtime().social.duel.rivalName), "Brezal");
    EXPECT_EQ(stage->LastMessage().text, "no quiso");

    // With dialog 0x9a open, the AC does nothing.
    stage->Runtime().dialogs.current = 0x9a;
    Client::Social::ApplyDuelRequest({0x0203, NameOf<10>("Arbusto")}, *stage->world);
    EXPECT_EQ(stage->Runtime().ui.field78abad9, 0);
    EXPECT_EQ(TextOf(stage->Runtime().social.duel.rivalName), "Brezal");
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, SocialEventsTest, ::testing::Values(Lorencia, Devias));
} // namespace
