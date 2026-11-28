#include "Core/Expected.h"
#include "Kernel/MouseState.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Network/Xor32Packet.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Models/LogicalAccess.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Hud/Dialogs/GameDialogSteps.h"
#include "Hud/GamePanels.h"
#include "Hud/GameUi.h"
#include "Items/HudItems.h"
#include "Items/InventoryMoves.h"
#include "Items/ItemPrice.h"
#include "Npc/NpcWindowState.h"
#include "Npc/NpcWindowSteps.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "PersonalShop/PersonalShopWindow.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameUiState.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"
#include "Chat/ChatRuntime.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The steps of the NPC windows (CL9 milestone 6, phase 2; 04 § Las ventanas de los NPC): buy, sell, repair, store and
// take from the warehouse, the warehouse zen, the mix and the yes/no dialogs, byte by byte and with the step's rand().
// The NPC window's corner (0x78aba28) is set by its drawing (Npc/NpcWindowDrawing), which does not run in these tests:
// they leave it at (0x104, 0), where the original puts it. The items and the texts are made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Kernel::EmptyOwnItemId;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
using Client::Network::Delivery;
using Client::Network::Xor32Decode;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
using Client::Ui::GameUiContext;

constexpr int Own = Client::Models::OwnRecordCloseRandoms;
// Those of the step with the inventory open and nothing under the mouse (test_game_inventory_step.cpp): the
// inventory's and its siblings', before 0x5d8d50, and the skill bar's, the Q, W and E chain and the IsOpen(0) tail,
// after.
constexpr int InventoryWindowRandoms = 8 + 13 * Own;
constexpr int QuietStepRandoms = InventoryWindowRandoms + Own + 2 + 2;
// With the warehouse open, the Q, W and E chain (0x5a343e) and IsOpen(0) (0x5a5511) stop at 0x40, before the
// IsOpen(0x10000): one less each.
constexpr int WarehouseStepRandoms = QuietStepRandoms - 2;
// 0x5c3d20 with the mouse above the inventory grid.
constexpr int EquipmentStepRandoms = 12 * Own;
// The rand() of the padding and of the serial number of a C3.
constexpr int C3 = 2;
// 0x5d8d50 with an item in the cursor: IsOpen(0x20) for dropping it in the trade.
constexpr int CursorTradeQuery = 1;
// 0x5c5361 on picking up and 0x6264a6 in the OK of the 0x73: IsOpen(0x20).
constexpr int TradeQuery = 1;
// 0x5cb3af and 0x5e7aa0: twenty swaps of two keys.
constexpr int KeypadRandoms = 40;
// With the chaos box open, IsOpen(0) (0x5a5511) stops at 0x100, before the IsOpen(0x10000): one less. The Q, W and E
// chain goes through the 0x10000 before the 0x100 and draws the same.
constexpr int ChaosBoxStepRandoms = QuietStepRandoms - 1;
constexpr std::int32_t InventoryX = 0x1c2;
constexpr std::int32_t NpcX = 0x104;
constexpr std::int32_t NpcGridLeft = NpcX + 0xf;
constexpr std::int32_t NpcGridTop = 0x32;
// The mix button (0x5fe690, 0x2c × 0x20 at the corner plus (0x4b, 300)) and the price of the 3 that 0x5febf0 leaves.
constexpr std::int32_t MixButtonX = NpcX + 0x4b + 5;
constexpr std::int32_t MixButtonY = 300 + 5;
constexpr std::uint32_t UpgradePrice = 2'000'000;

// A 1 by 3 weapon and a 2 by 2 shield with a fixed price (+0x40) and durability (+0x2d), a 1 by 1 jewel and the fruit.
// For the chaos box, the jewel of Soul, the chaos one and the Devil Square eye and key, 1 by 1 (the last two with a
// fixed price).
constexpr std::uint16_t Sword = 0x0001;
constexpr std::uint16_t Shield = 0x0c00;
constexpr std::uint16_t Jewel = 0x1c0d;
constexpr std::uint16_t Fruit = 0x1a0f;
constexpr std::uint16_t Soul = 0x1c0e;
constexpr std::uint16_t ChaosJewel = 0x180f;
constexpr std::uint16_t Eye = 0x1c11;
constexpr std::uint16_t Key = 0x1c12;
constexpr std::uint32_t EyePrice = 1000;
constexpr std::uint32_t SwordPrice = 15000;
constexpr std::uint32_t ShieldPrice = 9000;
constexpr std::uint8_t SwordDurability = 30;
constexpr std::uint8_t ShieldDurability = 20;

void PutDword(std::vector<std::byte>& plain, std::size_t at, std::uint32_t value)
{
    for (std::size_t index = 0; index < 4; ++index)
        plain[at + index] = static_cast<std::byte>((value >> (8 * index)) & 0xffu);
}

Client::Tables::ItemTable SyntheticItems()
{
    using Client::Tables::ItemRecordSize;
    struct Shape
    {
        std::uint16_t type;
        std::uint8_t width;
        std::uint8_t height;
        std::uint32_t price;
        std::uint8_t durability;
    };
    constexpr std::array<Shape, 8> Shapes{{{Sword, 1, 3, SwordPrice, SwordDurability},
                                           {Shield, 2, 2, ShieldPrice, ShieldDurability},
                                           {Jewel, 1, 1, 0, 0},
                                           {Fruit, 1, 1, 0, 0},
                                           {Soul, 1, 1, 0, 0},
                                           {ChaosJewel, 1, 1, 0, 0},
                                           {Eye, 1, 1, EyePrice, 0},
                                           {Key, 1, 1, EyePrice, 0}}};
    std::vector<std::byte> plain(Client::Tables::ItemTableSize);
    for (const Shape& shape : Shapes)
    {
        const std::size_t base = std::size_t{shape.type} * ItemRecordSize;
        plain[base + 0x24] = std::byte{shape.width};
        plain[base + 0x25] = std::byte{shape.height};
        plain[base + 0x2d] = std::byte{shape.durability};
        PutDword(plain, base + 0x40, shape.price);
    }
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

CrtRandom Advanced(CrtRandom random, int count)
{
    random.Discard(static_cast<std::uint32_t>(count));
    return random;
}

// `item` in the 8-column grid `cells` from (column, row), with the copy over its whole footprint and +0x3c only in the
// corner, as 0x5bc740 leaves it.
void Place(std::span<ItemState> cells, ItemState item, std::int32_t column, std::int32_t row, std::int32_t width,
           std::int32_t height)
{
    item.gridColumn = static_cast<std::uint8_t>(column);
    item.gridRow = static_cast<std::uint8_t>(row);
    for (std::int32_t y = row; y < row + height; ++y)
    {
        for (std::int32_t x = column; x < column + width; ++x)
        {
            ItemState& cell = cells[static_cast<std::size_t>(y * 8 + x)];
            cell = item;
            cell.occupied = x == column && y == row ? 1u : 0u;
        }
    }
}

// Measures like the fake rasterizer: 6 columns per letter and 13 rows.
class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

class NpcWindowStepsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        auto& runtime = world.Runtime();
        runtime.panels.inventory = 1;
        runtime.hud.inventoryX = InventoryX;
        runtime.ui.npcWindowX = NpcX;
        // The chat log where the previous step leaves it (0x636f04, outside its window): at (0xba, 0x1a4), the first
        // step's one, it covers the NPC window and takes the mouse (0x5ee15a).
        runtime.hud.chatFrame.x = -10;
        runtime.hud.chatFrame.y = 0x65;
        for (ItemState& cell : runtime.npcWindows.grid)
            cell.id = EmptyOwnItemId;
        for (ItemState& cell : runtime.npcWindows.chaosBox)
            cell.id = EmptyOwnItemId;
        services.items = &items;
        services.dressTheHero = [] { return Client::Core::Expected<void, std::string>{}; };
        services.systemMessage = [this](std::size_t index, std::int32_t)
        {
            messages.push_back(index);
            return Client::Core::Expected<void, std::string>{};
        };
        services.sendItemMove = [this](const Client::Protocol::Items::ItemMove& move)
        {
            sent.push_back(Client::Protocol::Items::WriteItemMove(move));
            return Client::Core::Expected<void, std::string>{};
        };
    }

    GameUiContext Context(std::int32_t x, std::int32_t y)
    {
        return GameUiContext{.world = world,
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
                             }};
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y)
    {
        return Client::Hud::StepTheGameUi(Context(x, y));
    }

    Client::Core::Expected<void, std::string> Dialogs(std::int32_t x, std::int32_t y)
    {
        return Client::Hud::StepTheGameDialogs(Context(x, y));
    }

    void Click()
    {
        Client::Kernel::LatchTheLeftButton(world.Runtime().mouse, true, true);
    }

    // The C1 of packet `index` without the XOR32, to compare it byte by byte.
    [[nodiscard]] Client::Network::Packet Plain(std::size_t index) const
    {
        return Xor32Decode(sent.at(index).packet);
    }

    [[nodiscard]] Client::Npc::NpcWindowState& Npc()
    {
        return world.Runtime().npcWindows;
    }

    // 0x78abb14 and 0x6c932e, the cursor item's warning and its answer.
    [[nodiscard]] Client::Social::TradeState& Trade()
    {
        return world.Runtime().social.trade;
    }

    // 0x5d8dee..0x5dc798: the answers to the warnings, without the cursor item. False: the step ends there.
    [[nodiscard]] Client::Core::Expected<bool, std::string> AnswerTheWarnings()
    {
        return Client::PersonalShop::StepTheWarningAnswers(
            Context(0, 0), Client::Items::InventoryMoveContext{.world = world, .random = random, .services = services});
    }

    // What 0x555190 warned with type 2, in order.
    [[nodiscard]] std::vector<std::string> Errors() const
    {
        std::vector<std::string> errors;
        for (const Client::Ui::MessageAreaRow& row : world.Runtime().chat.area.rows)
        {
            if (row.type == 2 && !row.text.empty())
                errors.push_back(row.text);
        }
        return errors;
    }

    // The box of the 3: a +9 sword with luck (0x54), a jewel of Bless, one of Soul and a chaos one.
    void FillTheUpgrade()
    {
        ItemState sword{.id = Sword, .scalar1Low = 9u << 3, .optionCount = 1};
        sword.optionBytes[0] = 0x54;
        Place(Npc().chaosBox, sword, 0, 0, 1, 3);
        Place(Npc().chaosBox, ItemState{.id = Jewel}, 2, 0, 1, 1);
        Place(Npc().chaosBox, ItemState{.id = Soul}, 3, 0, 1, 1);
        Place(Npc().chaosBox, ItemState{.id = ChaosJewel}, 4, 0, 1, 1);
    }

    WorldState world;
    CrtRandom random{0x2468aceu};
    InputState input;
    Client::Tables::ItemTable items = SyntheticItems();
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{166, "luna"},
                                                                    {167, "sol"},
                                                                    {168, "cielo"},
                                                                    {169, "mar"},
                                                                    {0x1c0, "aceptar?"},
                                                                    {0x178, "comer la fruta?"},
                                                                    {0x218, "vender esto"},
                                                                    {0x219, "que vale"},
                                                                    {0x21a, "mucho?"},
                                                                    {0x21b, "mezclar ya?"},
                                                                    {580, "no hay %s"},
                                                                    {581, "sin lugar"},
                                                                    {591, "mezcla"},
                                                                    {596, "sin zen"},
                                                                    {600, "no son iguales"},
                                                                    {1171, "desde el nivel %d"},
                                                                    {1900, "rio"},
                                                                    {1901, " y suerte"},
                                                                    {0x76e, "elegir"}});
    LetterMeasure measure;
    Client::Items::InventoryServices services;
    std::vector<Client::Protocol::Outgoing> sent;
    std::vector<std::size_t> messages;
};

// 0x5c4b64..0x5c510f, in Lorencia, Devias and Noria: the click on the third cell of a shop weapon buys it from its
// corner: the C1 04 32 with slot 82 (row 10, column 2), with the XOR32 and in C3, and the price of 0x54d0f0 in mode 0
// at 0x5767950. With the purchase pending, the click is spent without sending anything. The marks, the banner's item
// (0x78aba27 at 1) and 0x78abb28 at 6 with the cell without the 12.
TEST_F(NpcWindowStepsTest, TheShopBuysTheItemUnderTheMouseByteByByte)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        SetUp();
        sent.clear();
        world.Runtime().map = map;
        world.Runtime().panels.npcShop = 1;
        Npc().buyLock = 0;
        Place(Npc().grid, ItemState{.id = Sword}, 2, 10, 1, 3);
        const std::int32_t x = NpcGridLeft + 2 * 20 + 5;
        const std::int32_t y = NpcGridTop + 11 * 20 + 5;
        const CrtRandom expected = Advanced(random, QuietStepRandoms + C3);
        Click();
        const auto stepped = Step(x, y);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(random.State(), expected.State());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_EQ(sent[0].delivery, Delivery::Encrypted);
        EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x04, 0x32, 82})));
        EXPECT_EQ(Npc().buyLock, static_cast<std::int32_t>(SwordPrice));
        EXPECT_FALSE(world.Runtime().mouse.pendingClick);
        for (const std::size_t cell : {82u, 90u, 98u})
            EXPECT_EQ(Npc().grid[cell].warning, 2) << cell;
        const auto& ui = world.Runtime().ui;
        EXPECT_EQ(ui.hoveredItem.place, Client::Ui::HoveredItem::Place::NpcGridCell);
        EXPECT_EQ(ui.hoveredItem.index, 90u);
        EXPECT_EQ(ui.field78aba27, 1);
        EXPECT_EQ(ui.field78abb28, 6);
        EXPECT_EQ(ui.field78abb2c, 90);
        EXPECT_EQ(ui.skillTooltipX, NpcGridLeft + 10 + 2 * 20);
        EXPECT_EQ(ui.skillTooltipY, NpcGridTop + 10 * 20);
        EXPECT_EQ(ui.mouseOnUi, 1);

        Click();
        ASSERT_TRUE(Step(x, y).HasValue());
        EXPECT_EQ(sent.size(), 1u);
        EXPECT_FALSE(world.Runtime().mouse.pendingClick);
    }
}

// 0x5dee7f..0x5df416: the click with a shield in the cursor over the shop's grid sells it, C1 04 33 with its slot
// (0x78a6758), with the XOR32 and in C3, after the IsOpen(0x10000) of 0x5decc1. In Lorencia, Devias and Noria.
TEST_F(NpcWindowStepsTest, SellingAPlainItemSendsItsPlace)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        SetUp();
        sent.clear();
        auto& runtime = world.Runtime();
        runtime.map = map;
        runtime.panels.npcShop = 1;
        runtime.cursorItem = ItemState{.id = Shield};
        runtime.cursorItemCount = 1;
        runtime.cursorGrid = Client::Kernel::CursorGrid::Inventory;
        runtime.slot78a6758 = 12 + 20;
        runtime.mouse.updateTimeMax = 0;
        const CrtRandom expected = Advanced(random, QuietStepRandoms + CursorTradeQuery + 1 + C3);
        Click();
        const auto stepped = Step(NpcX + 0x20, 300);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(random.State(), expected.State());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_EQ(sent[0].delivery, Delivery::Encrypted);
        EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x04, 0x33, 32})));
        EXPECT_EQ(Trade().warningKind, 0);
        EXPECT_EQ(Trade().warningByte, 0xff);
        EXPECT_EQ(runtime.mouse.updateTimeMax, 6);
    }
}

// 0x5df417: a jewel asks for confirmation with the 0x98 (texts 0x218 to 0x21a) and 0x78abb14 at 1. The yes of the 0x98
// (0x62a779, the button at (0x15, 0x5a) shifted to (0xd5, 0x64)) leaves 1 at 0x6c932e and, on the next step, 0x5d8d50
// sends the C1 04 33 and sets everything back to 0 and 0xff. With Esc it is the no: the jewel goes back to its slot.
TEST_F(NpcWindowStepsTest, AValuableItemIsSoldAfterTheConfirmation)
{
    auto& runtime = world.Runtime();
    runtime.panels.npcShop = 1;
    runtime.cursorItem = ItemState{.id = Jewel};
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Kernel::CursorGrid::Inventory;
    runtime.slot78a6758 = 12 + 3;
    const CrtRandom asked = Advanced(random, QuietStepRandoms + CursorTradeQuery + 1);
    Click();
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    EXPECT_EQ(random.State(), asked.State());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(runtime.dialogs.current, 0x98);
    EXPECT_EQ(Trade().warningKind, 1);
    EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 0), "vender esto");
    EXPECT_EQ(runtime.dialogBox.lineCount, 3);

    Click();
    ASSERT_TRUE(Dialogs(0x15 + 0xd5, 0x5a + 0x64).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(Trade().warningByte, 1);
    EXPECT_EQ(runtime.dialogBox.buttons[0].state, 2);
    EXPECT_FALSE(runtime.mouse.pendingClick);
    EXPECT_FALSE(runtime.mouse.leftHeld);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});

    const CrtRandom sold = Advanced(random, QuietStepRandoms + C3);
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    EXPECT_EQ(random.State(), sold.State());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x04, 0x33, 15})));
    EXPECT_EQ(Trade().warningKind, 0);
    EXPECT_EQ(Trade().warningByte, 0xff);

    // The no, with Esc.
    Click();
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    ASSERT_EQ(runtime.dialogs.current, 0x98);
    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = Key::Escape});
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(Trade().warningByte, 2);
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    EXPECT_EQ(sent.size(), 1u);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(world.Inventory().cells[3].id, Jewel);
    EXPECT_EQ(Trade().warningKind, 0);
}

// 0x5ed960 with 0x78abb09: the repair-all button sends C1 05 34 FF 00 in C3 without spending the click; 0x5b1570 adds
// up, with the own record open, the cost of repairing the worn equipment weapon and inventory shield (0x5b13e0 over
// 0x54d0f0 in mode 2). The button at x + 0x19 turns on the repair cursor.
TEST_F(NpcWindowStepsTest, RepairingEverythingSendsFfAndPricesTheWornItems)
{
    auto& runtime = world.Runtime();
    runtime.panels.npcShop = 1;
    runtime.panels.field78abb09 = 1;
    ItemState& sword = world.OwnCharacter().equipment[0];
    sword = ItemState{.id = Sword, .durability = 10};
    sword.occupied = 1;
    Place(world.Inventory().cells, ItemState{.id = Shield, .durability = 5}, 4, 1, 2, 2);
    const auto priceOf = [this](const ItemState& item, std::uint8_t durability)
    {
        const std::uint16_t maximum = Client::Items::MaxDurability(item, *items.Record(item.id), 0);
        EXPECT_EQ(maximum, durability);
        const auto price = Client::Items::ItemPrice(item, items, Client::Items::RepairPriceMode, std::nullopt);
        return Client::Items::RepairPriceOf(price.Value(), item.durability, maximum, 0).gold;
    };
    const std::int32_t total = priceOf(sword, SwordDurability) + priceOf(world.Inventory().cells[12], ShieldDurability);
    ASSERT_GT(total, 0);

    const CrtRandom expected = Advanced(random, QuietStepRandoms + C3 + Own);
    Click();
    const auto stepped = Step(NpcX + 0x73 + 5, 0x16d + 5);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x05, 0x34, 0xff, 0x00})));
    EXPECT_TRUE(runtime.mouse.pendingClick);
    EXPECT_EQ(Npc().repairAllPrice, total);

    runtime.mouse.pendingClick = true;
    ASSERT_TRUE(Step(NpcX + 0x19, 0x16d).HasValue());
    EXPECT_EQ(runtime.panels.repairCursor, 2);
    EXPECT_EQ(runtime.panels.repairMode, 1);
    EXPECT_FALSE(runtime.mouse.pendingClick);
}

// 0x5c2dd5..0x5c3594: with repair mode, the click on the weapon at equipment slot 0 sends C1 05 34 00 and 0x78abb10,
// in C3 and with the record of 0x5bec50 open.
TEST_F(NpcWindowStepsTest, RepairModeRepairsAnEquipmentPlace)
{
    auto& runtime = world.Runtime();
    world.OwnCharacter().equipment[0] = ItemState{.id = Sword};
    runtime.panels.repairMode = 1;
    runtime.field78abb10 = 1;
    const CrtRandom expected = Advanced(random, QuietStepRandoms + EquipmentStepRandoms + C3);
    Click();
    const auto stepped = Step(InventoryX + 15 + 5, 89 + 5);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x05, 0x34, 0x00, 0x01})));
    EXPECT_FALSE(runtime.mouse.pendingClick);
    EXPECT_EQ(world.OwnCharacter().equipment[0].id, Sword);
}

// Store (0x5ca250 with type 2): the weapon picked up from the inventory (slot 12) over row 8 and column 3 of the
// warehouse sends 0x449d80(0, 12, 2, 67), the C1 0E 24 in C3 with the item's seven bytes, and turns on the 24's lock.
// In Lorencia, Devias and Noria, with the rand() of the open warehouse.
TEST_F(NpcWindowStepsTest, StoringAnItemInTheWarehouseSendsTheMove)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        SetUp();
        sent.clear();
        auto& runtime = world.Runtime();
        runtime.map = map;
        runtime.cursorItemCount = 0;
        runtime.ui.field78abb39 = 0;
        runtime.itemMoveTarget = 0;
        runtime.panels.warehouse = 1;
        Place(world.Inventory().cells, ItemState{.id = Sword, .scalar1Low = 3u << 3, .durability = 25, .flags1b = 0x10},
              0, 0, 1, 3);
        Click();
        ASSERT_TRUE(Step(InventoryX + 15 + 5, 200 + 5).HasValue());
        ASSERT_EQ(runtime.cursorItemCount, 1);
        ASSERT_EQ(runtime.slot78a6758, 12);

        const CrtRandom expected = Advanced(random, WarehouseStepRandoms + CursorTradeQuery + C3);
        Click();
        const auto stepped = Step(NpcGridLeft + 3 * 20 + 10, NpcGridTop + 9 * 20 + 5);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(random.State(), expected.State());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_EQ(sent[0].delivery, Delivery::Encrypted);
        EXPECT_TRUE(SameBytes(Plain(0).Bytes(),
                              Bytes({0xc1, 0x0e, 0x24, 0x00, 12, 0x01, 0x18, 25, 0x10, 0x00, 0x00, 0x00, 0x02, 67})));
        EXPECT_EQ(runtime.itemMoveTarget, 67);
        EXPECT_EQ(runtime.ui.field78abb39, 1);
        EXPECT_FALSE(runtime.mouse.pendingClick);
    }
}

// 0x5c8e8f → 0x5ca250 in the same pass: the right click finds the last free footprint and the 24 goes out without a
// left click.
TEST_F(NpcWindowStepsTest, TheWarehouseRightClickMovesThroughTheWholeUiStep)
{
    auto& runtime = world.Runtime();
    runtime.panels.warehouse = 1;
    Place(world.Inventory().cells, ItemState{.id = Sword, .scalar1Low = 0x18, .durability = 25}, 1, 1, 1, 3);
    runtime.ui.rightClick = true;
    const auto stepped = Step(InventoryX + 15 + 20 + 5, 200 + 20 + 5);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(),
                          Bytes({0xc1, 0x0e, 0x24, 0x00, 21, 0x01, 0x18, 25, 0x00, 0x00, 0x00, 0x00, 0x02, 103})));
    EXPECT_EQ(world.Inventory().cells[9].id, EmptyOwnItemId);
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.ui.field78abb39, 1);
    EXPECT_FALSE(runtime.mouse.pendingClick);
    EXPECT_EQ(runtime.itemMoveTarget, 103);
}

// Take out: the click on the warehouse weapon picks it up from its corner (slot 73, without the 12) with the
// IsOpen(0x20) of 0x5c5361, and dropping it at column 4 and row 2 of the inventory sends 0x449d80(2, 73, 0, 32). With
// the warehouse lock (0x6c932f) and without 0x78abb1c, instead, it leaves it waiting for the password ({2, 73, 0, 32}),
// shuffles the keypad and sets 0x78abb20 to 1 (0x5cb3af..0x5cb455), without sending anything.
TEST_F(NpcWindowStepsTest, TakingAnItemFromTheWarehouseSendsTheMoveOrWaitsForThePassword)
{
    for (const bool locked : {false, true})
    {
        SCOPED_TRACE(locked);
        SetUp();
        sent.clear();
        auto& runtime = world.Runtime();
        runtime.ui.field78abb39 = 0;
        runtime.ui.field78abb20 = 0;
        runtime.cursorItemCount = 0;
        runtime.panels.warehouse = 1;
        Npc().warehouseLocked = locked ? 1 : 0;
        Npc().field78abb1c = 0;
        Place(Npc().grid, ItemState{.id = Sword, .durability = 7}, 1, 9, 1, 3);
        const CrtRandom picked = Advanced(random, WarehouseStepRandoms + TradeQuery + CursorTradeQuery);
        Click();
        ASSERT_TRUE(Step(NpcGridLeft + 20 + 5, NpcGridTop + 10 * 20 + 5).HasValue());
        EXPECT_EQ(random.State(), picked.State());
        ASSERT_EQ(runtime.cursorItemCount, 1);
        EXPECT_EQ(runtime.cursorGrid, Client::Kernel::CursorGrid::NpcGrid);
        EXPECT_EQ(runtime.slot78a6758, 73);
        for (const std::size_t cell : {73u, 81u, 89u})
            EXPECT_EQ(Npc().grid[cell].id, EmptyOwnItemId) << cell;
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1d});

        const CrtRandom beforeTheDrop = random;
        CrtRandom keys = Advanced(random, InventoryWindowRandoms);
        std::array<std::uint16_t, Client::Npc::WarehouseKeyCount> keypad{};
        Client::Items::ShuffleTheKeypad(keys, keypad);
        Click();
        const auto dropped = Step(InventoryX + 15 + 4 * 20 + 5, 200 + 3 * 20 + 5);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        if (!locked)
        {
            EXPECT_EQ(random.State(), Advanced(beforeTheDrop, WarehouseStepRandoms + CursorTradeQuery + C3).State());
            ASSERT_EQ(sent.size(), 1u);
            EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x0e, 0x24, 0x02, 73, 0x01, 0x00, 7, 0x00, 0x00, 0x00,
                                                           0x00, 0x00, 32})));
            EXPECT_EQ(runtime.ui.field78abb20, 0);
            continue;
        }
        EXPECT_TRUE(sent.empty());
        EXPECT_EQ(random.State(),
                  Advanced(beforeTheDrop, WarehouseStepRandoms + KeypadRandoms + CursorTradeQuery).State());
        EXPECT_EQ(Npc().keypad, keypad);
        EXPECT_EQ(runtime.ui.field78abb20, 1);
        EXPECT_EQ(Npc().pendingMove.slot, 2);
        EXPECT_EQ(Npc().pendingMove.second, 73);
        EXPECT_EQ(Npc().pendingMove.third, 0);
        EXPECT_EQ(Npc().pendingMove.fourth, 32);
        // 0x5e7bf0: the password box keeps the click (0x5ee140 does not go on): the bottom left key (0x5e7b50) adds its
        // digit and the warehouse does not see the click.
        Click();
        const auto keypadStep = Step(0xdf + 5, 0x96 + 5);
        ASSERT_TRUE(keypadStep.HasValue()) << keypadStep.Error();
        EXPECT_EQ(Npc().typedNumber[0], '0' + keypad[0]);
        EXPECT_EQ(Npc().typedNumber[1], 0);
        EXPECT_EQ(Npc().keypadPressed, 1);
        EXPECT_EQ(runtime.ui.field78abb20, 1);
        EXPECT_FALSE(runtime.mouse.pendingClick);
        EXPECT_TRUE(sent.empty());
    }
}

// The trainer's box with 0x724580 at 3 and the inventory open: the mouse over the jewel (0x44ad10, 0x5c4110 at (0x113,
// 0x6e)) notes it as the item under the mouse with the help at slot 4 (0x5aed40 with 0x449c30); the click picks it up
// (slot 17, without the 12) and dropping it at column 4 and row 3 of the inventory sends 0x449d80(5, 17, 0, 40). The
// rand() of each move are in test_cursor_item.cpp. In Lorencia and Devias.
TEST_F(NpcWindowStepsTest, TheTrainerBoxGridLiftsAndSendsToTheInventory)
{
    constexpr std::int32_t TrainerLeft = 0x113;
    constexpr std::int32_t TrainerTop = 0x6e;
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        SetUp();
        sent.clear();
        auto& runtime = world.Runtime();
        runtime.map = map;
        runtime.ui.field78abb39 = 0;
        runtime.cursorItemCount = 0;
        runtime.panels.delegated.trainer = 3;
        Npc().trainerBox = Client::Npc::EmptyTrainerBox();
        Place(Npc().trainerBox, ItemState{.id = Jewel, .durability = 7}, 1, 2, 1, 1);
        const std::int32_t x = TrainerLeft + 20 + 5;
        const std::int32_t y = TrainerTop + 2 * 20 + 5;
        const auto hovered = Step(x, y);
        ASSERT_TRUE(hovered.HasValue()) << hovered.Error();
        EXPECT_EQ(runtime.ui.hoveredItem.place, Client::Ui::HoveredItem::Place::TrainerBoxCell);
        EXPECT_EQ(runtime.ui.hoveredItem.index, 17u);
        EXPECT_EQ(runtime.ui.field78abb28, 4);
        EXPECT_EQ(runtime.ui.field78abb2c, 17);

        Click();
        const auto picked = Step(x, y);
        ASSERT_TRUE(picked.HasValue()) << picked.Error();
        ASSERT_EQ(runtime.cursorItemCount, 1);
        EXPECT_EQ(runtime.cursorGrid, Client::Kernel::CursorGrid::TrainerBox);
        EXPECT_EQ(runtime.slot78a6758, 17);
        EXPECT_EQ(Npc().trainerBox[17].id, EmptyOwnItemId);
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1d});

        Click();
        const auto dropped = Step(InventoryX + 15 + 4 * 20 + 5, 200 + 3 * 20 + 5);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(SameBytes(Plain(0).Bytes(),
                              Bytes({0xc1, 0x0e, 0x24, 0x05, 17, 0x0d, 0x00, 7, 0x00, 0x00, 0xe0, 0x00, 0x00, 40})));
        EXPECT_EQ(runtime.ui.field78abb39, 1);
    }
}

// Q-C921, in Lorencia, Devias and Noria: after a mix (0x7d2dc80 at 2, 0x4da2d1), the item it left in the chaos box is
// picked up and goes to the inventory. With the item in the cursor, 0x5ca250 with the chaos box sets [ebp − 0x2d] to 0
// (0x5ca333) each frame: over the box, the footprint stays marked as refused (the click there goes on to the floor,
// 0x5deca7, of milestone 5), and the click in the inventory sends 0x449d80(3, 0, 0, 32).
TEST_F(NpcWindowStepsTest, TheMixResultLeavesTheChaosBoxForTheInventory)
{
    constexpr std::int32_t ChaosBoxTop = 0x6e;
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        SetUp();
        sent.clear();
        auto& runtime = world.Runtime();
        runtime.map = map;
        runtime.ui.field78abb39 = 0;
        runtime.cursorItemCount = 0;
        runtime.panels.chaosBox = 1;
        runtime.dialogs.current = 0;
        Npc().chaosMixState = 2;
        Place(Npc().chaosBox, ItemState{.id = Sword, .durability = 7}, 0, 0, 1, 3);
        Click();
        const auto picked = Step(NpcGridLeft + 5, ChaosBoxTop + 5);
        ASSERT_TRUE(picked.HasValue()) << picked.Error();
        ASSERT_EQ(runtime.cursorItemCount, 1);
        EXPECT_EQ(runtime.cursorGrid, Client::Kernel::CursorGrid::ChaosBox);
        EXPECT_EQ(runtime.slot78a6758, 0);
        for (const std::size_t cell : {0u, 8u, 16u})
            EXPECT_EQ(Npc().chaosBox[cell].id, EmptyOwnItemId) << cell;

        // Over the box and without the click: the footprint stays refused (0x5ca385..0x5ca453), without the 24.
        const auto refused = Step(NpcGridLeft + 2 * 20 + 10, ChaosBoxTop + 30);
        ASSERT_TRUE(refused.HasValue()) << refused.Error();
        EXPECT_TRUE(sent.empty());
        EXPECT_EQ(runtime.cursorItemCount, 1);
        for (const std::size_t cell : {2u, 10u, 18u})
            EXPECT_EQ(Npc().chaosBox[cell].warning, 3) << cell;
        EXPECT_EQ(Npc().chaosMixState, 2);

        const CrtRandom beforeTheDrop = random;
        Click();
        const auto dropped = Step(InventoryX + 15 + 4 * 20 + 5, 200 + 3 * 20 + 5);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(random.State(), Advanced(beforeTheDrop, ChaosBoxStepRandoms + CursorTradeQuery + C3).State());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(SameBytes(Plain(0).Bytes(),
                              Bytes({0xc1, 0x0e, 0x24, 0x03, 0, 0x01, 0x00, 7, 0x00, 0x00, 0x00, 0x00, 0x00, 32})));
        EXPECT_EQ(runtime.ui.field78abb39, 1);
    }
}

// 0x5ea060: deposit (the button at x + 0x1a) opens the 0x73 with the number input; its OK with Enter (0x6270ed) sends
// C1 08 81 00 and the zen, with the XOR32 and in clear, with the own record open. More zen than the inventory's opens
// the 0x74; more than 100000000, the 0x75.
TEST_F(NpcWindowStepsTest, DepositingZenSends81FromTheMoneyDialog)
{
    auto& runtime = world.Runtime();
    runtime.panels.warehouse = 1;
    world.OwnCharacter().zen = 5000;
    const CrtRandom opened = Advanced(random, WarehouseStepRandoms);
    Click();
    ASSERT_TRUE(Step(NpcX + 0x1a + 5, 0x186 + 5).HasValue());
    EXPECT_EQ(random.State(), opened.State());
    EXPECT_EQ(runtime.dialogs.current, 0x73);
    EXPECT_EQ(Trade().field78abacc, 0);
    EXPECT_EQ(world.Runtime().social.dialogFieldLimit, 8);
    EXPECT_EQ(world.Runtime().social.moneyField, 1);
    EXPECT_EQ(runtime.chat.input.fieldCount, 1);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
    EXPECT_FALSE(runtime.mouse.pendingClick);

    world.Runtime().social.typedMoney = 1000;
    runtime.ui.enterPressed = true;
    const CrtRandom confirmed = Advanced(random, TradeQuery + Own);
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(random.State(), confirmed.State());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x08, 0x81, 0x00, 0xe8, 0x03, 0x00, 0x00})));
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(world.Runtime().social.dialogFieldLimit, 0x2a);
    EXPECT_EQ(world.Runtime().social.moneyField, 0);
    EXPECT_EQ(runtime.chat.input.fieldCount, 2);

    runtime.dialogs.current = 0x73;
    world.Runtime().social.typedMoney = 6000;
    runtime.ui.enterPressed = true;
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(sent.size(), 1u);
    EXPECT_EQ(runtime.dialogs.current, 0x74);

    runtime.dialogs.current = 0x73;
    world.Runtime().social.typedMoney = 100'000'001;
    runtime.ui.enterPressed = true;
    const CrtRandom refused = Advanced(random, 0);
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(random.State(), refused.State());
    EXPECT_EQ(runtime.dialogs.current, 0x75);
}

// 0x5e74f0: there is no protected query for 0x2000. Accepting enables the notice, which goes on to the keypad in mode
// 6.
TEST_F(NpcWindowStepsTest, ThePanel2000CheckboxAndConfirmationReachTheKeypad)
{
    auto& runtime = world.Runtime();
    runtime.panels.panel2000 = 1;
    runtime.hud.inventoryX = 450;
    Client::Hud::GamePanels ownedPanels(world, random);
    auto& panels = ownedPanels.Registry();
    Click();
    auto context = Context(475, 240);
    auto stepped = Client::Npc::StepThePanel2000(context, panels);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(runtime.panels.field78abb08, 1);
    EXPECT_FALSE(runtime.mouse.pendingClick);
    EXPECT_EQ(runtime.ui.mouseOnUi, 1);
    const auto before = random.State();
    Click();
    context.mouseX = 485;
    context.mouseY = 320;
    stepped = Client::Npc::StepThePanel2000(context, panels);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(runtime.social.trade.warningKind, 4);
    EXPECT_EQ(runtime.social.trade.warningByte, 0xff);
    EXPECT_EQ(runtime.dialogs.current, 0x98);
    EXPECT_EQ(random.State(), before);
    runtime.social.trade.warningByte = 1;
    ASSERT_TRUE(Client::Npc::AnswerTheNpcWarning(context, 4).HasValue());
    EXPECT_EQ(runtime.ui.field78abb20, 6);
}

// 0x5ee1f1: the main step must reach panel 2000 with the services lent to its own record.
TEST_F(NpcWindowStepsTest, TheMainUiStepUsesThePanel2000Checkbox)
{
    auto& runtime = world.Runtime();
    runtime.panels.panel2000 = 1;
    runtime.hud.inventoryX = 450;
    Click();
    const auto stepped = Step(475, 240);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(runtime.panels.field78abb08, 1);
    EXPECT_FALSE(runtime.mouse.pendingClick);
    EXPECT_EQ(runtime.ui.mouseOnUi, 1);
    EXPECT_TRUE(sent.empty());
}

// 0x5e7678..0x5e7840 and 0x5e78a2..0x5e7a79: exit sends C1 03 31 and closes everything; only the cross clears
// 0x78abb00.
TEST_F(NpcWindowStepsTest, BothPanel2000ExitButtonsSend31BeforeClosing)
{
    for (const bool cross : {false, true})
    {
        auto& runtime = world.Runtime();
        runtime.panels.panel2000 = 1;
        runtime.panels.field78abb08 = 1;
        runtime.panels.field78abb00 = 7;
        runtime.panels.inventory = 0;
        runtime.hud.inventoryX = 450;
        Click();
        Client::Hud::GamePanels ownedPanels(world, random);
        auto context = Context(cross ? 475 : 485, cross ? 395 : 350);
        const auto before = random;
        const auto stepped = Client::Npc::StepThePanel2000(context, ownedPanels.Registry());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(runtime.panels.panel2000, 0);
        EXPECT_EQ(runtime.panels.field78abb08, 0);
        EXPECT_EQ(runtime.panels.field78abb00, cross ? 0 : 7);
        EXPECT_EQ(runtime.mouse.updateTime, 0);
        EXPECT_EQ(runtime.mouse.updateTimeMax, 6);
        EXPECT_FALSE(runtime.mouse.pendingClick);
        EXPECT_TRUE(SameBytes(Plain(sent.size() - 1).Bytes(), Bytes({0xc1, 3, 0x31})));
        EXPECT_EQ(random.State(), Advanced(before, 2).State());
    }
}

// Withdraw (the button at x + 0x4c): C1 08 81 01 and the zen. With the warehouse lock it waits for the password: {−1,
// 1, zen, 0x77f3cc8} and 0x5e7aa0(1), which shuffles the keypad with the record still open (the 40 rand() before the
// close's 3848). More than the warehouse zen opens the 0x74.
TEST_F(NpcWindowStepsTest, WithdrawingZenSends81OrWaitsForThePassword)
{
    auto& runtime = world.Runtime();
    runtime.panels.warehouse = 1;
    world.OwnCharacter().zen = 100;
    world.OwnCharacter().warehouseZen = 3000;
    Click();
    ASSERT_TRUE(Step(NpcX + 0x4c + 5, 0x186 + 5).HasValue());
    ASSERT_EQ(runtime.dialogs.current, 0x73);
    EXPECT_EQ(Trade().field78abacc, 1);
    world.Runtime().social.typedMoney = 2000;
    runtime.ui.enterPressed = true;
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x08, 0x81, 0x01, 0xd0, 0x07, 0x00, 0x00})));

    runtime.dialogs.current = 0x73;
    world.Runtime().social.typedMoney = 3001;
    runtime.ui.enterPressed = true;
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0x74);

    runtime.dialogs.current = 0x73;
    world.Runtime().social.typedMoney = 2000;
    Npc().warehouseLocked = 1;
    runtime.itemMoveTarget = 9;
    runtime.ui.enterPressed = true;
    CrtRandom keys = Advanced(random, TradeQuery);
    std::array<std::uint16_t, Client::Npc::WarehouseKeyCount> keypad{};
    Client::Items::ShuffleTheKeypad(keys, keypad);
    const CrtRandom expected = Advanced(random, TradeQuery + KeypadRandoms + Own);
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(sent.size(), 1u);
    EXPECT_EQ(Npc().keypad, keypad);
    EXPECT_EQ(runtime.ui.field78abb20, 1);
    EXPECT_EQ(Npc().pendingMove.slot, -1);
    EXPECT_EQ(Npc().pendingMove.second, 1);
    EXPECT_EQ(Npc().pendingMove.third, 2000);
    EXPECT_EQ(Npc().pendingMove.fourth, 9);
    EXPECT_EQ(runtime.dialogs.current, 0);
}

// The warehouse's button 2 (0x5ea1aa) opens the keypad in 4, or in 3 with the lock; button 3 (0x5ea24d) closes the
// warehouse with the C1 03 82 in clear (0x658567). The OK of the 0x8f (0x628276) opens the keypad in 6.
TEST_F(NpcWindowStepsTest, TheWarehouseButtonsOpenTheKeypadAndClose)
{
    auto& runtime = world.Runtime();
    runtime.panels.warehouse = 1;
    Npc().warehouseLocked = 1;
    const CrtRandom expected = Advanced(random, WarehouseStepRandoms + KeypadRandoms);
    Click();
    ASSERT_TRUE(Step(NpcX + 0x7e + 5, 0x186 + 5).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(runtime.ui.field78abb20, 3);

    runtime.ui.field78abb20 = 0;
    runtime.dialogs.current = 0x8f;
    runtime.ui.enterPressed = true;
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(runtime.ui.field78abb20, 6);

    runtime.ui.field78abb20 = 0;
    Click();
    const auto closed = Step(NpcX + 0xd7 + 5, 0x18b + 5);
    ASSERT_TRUE(closed.HasValue()) << closed.Error();
    EXPECT_EQ(runtime.panels.warehouse, 0);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x03, 0x82})));
    EXPECT_FALSE(runtime.mouse.pendingClick);
    EXPECT_EQ(runtime.mouse.updateTimeMax, 6);
}

// 0x5d934f..0x5d9633: with 0x78abb14 at 2 and the yes, the mix requested (0x7d2dc80 at 1) and the C1 04 86 with the
// type, with the XOR32 and in clear, without rand(); with the no, nothing.
TEST_F(NpcWindowStepsTest, TheMixConfirmationSends86)
{
    auto& runtime = world.Runtime();
    Trade().warningKind = 2;
    Trade().warningByte = 1;
    Npc().mixKind = 3;
    const CrtRandom expected = random;
    const auto chose = AnswerTheWarnings();
    ASSERT_TRUE(chose.HasValue()) << chose.Error();
    EXPECT_FALSE(chose.Value());
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x04, 0x86, 0x03})));
    EXPECT_EQ(Npc().chaosMixState, 1);
    EXPECT_EQ(Trade().warningKind, 0);
    EXPECT_EQ(Trade().warningByte, 0xff);

    Trade().warningKind = 2;
    Trade().warningByte = 2;
    ASSERT_TRUE(AnswerTheWarnings().HasValue());
    EXPECT_EQ(sent.size(), 1u);
    EXPECT_EQ(Trade().warningKind, 0);
    EXPECT_EQ(runtime.dialogs.current, 0);
}

// 0x5d9b82..0x5d9c6b, in Lorencia, Devias and Noria: with 0x78abb14 at 4 (panel 0x2000) and the yes, the warehouse
// keypad goes back to 0..9 and is shuffled with twenty pairs of rand() % 10 (the 40 rand()), what was typed goes to 0
// and 0x78abb20 to 6; with the no, 0x78abb08 at 0 and no rand(); with no answer, nothing.
TEST_F(NpcWindowStepsTest, TheKeypadChoiceShufflesTheWarehouseKeypadInSix)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        auto& runtime = world.Runtime();
        runtime.map = map;
        Trade().warningKind = 4;
        Trade().warningByte = 0xff;
        const CrtRandom unchanged = random;
        ASSERT_FALSE(AnswerTheWarnings().Value());
        EXPECT_EQ(Trade().warningKind, 4);
        EXPECT_EQ(random.State(), unchanged.State());

        Trade().warningByte = 1;
        Npc().typedNumber.fill(7);
        Npc().keypad.fill(9);
        CrtRandom expectedRandom = random;
        std::array<std::uint16_t, 10> expectedKeys{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        for (int swap = 0; swap < 20; ++swap)
        {
            const auto first = static_cast<std::size_t>(expectedRandom.Next() % 10);
            const auto second = static_cast<std::size_t>(expectedRandom.Next() % 10);
            std::swap(expectedKeys[first], expectedKeys[second]);
        }
        const auto chose = AnswerTheWarnings();
        ASSERT_TRUE(chose.HasValue()) << chose.Error();
        EXPECT_FALSE(chose.Value());
        EXPECT_EQ(random.State(), expectedRandom.State());
        EXPECT_EQ(Npc().keypad, expectedKeys);
        EXPECT_EQ(Npc().typedNumber, (std::array<std::uint8_t, 11>{}));
        EXPECT_EQ(runtime.ui.field78abb20, 6);
        EXPECT_EQ(Trade().warningKind, 0);
        EXPECT_EQ(Trade().warningByte, 0xff);
        EXPECT_TRUE(sent.empty());

        Trade().warningKind = 4;
        Trade().warningByte = 2;
        runtime.panels.field78abb08 = 1;
        runtime.ui.field78abb20 = 0;
        const CrtRandom refused = random;
        ASSERT_FALSE(AnswerTheWarnings().Value());
        EXPECT_EQ(runtime.panels.field78abb08, 0);
        EXPECT_EQ(runtime.ui.field78abb20, 0);
        EXPECT_EQ(Trade().warningKind, 0);
        EXPECT_EQ(Trade().warningByte, 0xff);
        EXPECT_EQ(random.State(), refused.State());
    }
}

// 0x5d8db6..0x5d8dda and 0x5fe690..0x5fea82, in Lorencia, Devias and Noria (the chaos goblin's): with the box open,
// each step counts what it is worth (0x5fd120: 15000 for the sword and the jewels' fixed ones) and the type
// (0x5fd4f0): the +9 sword with luck, Bless, Soul and the chaos jewel are the 3, with luck. The click on the mix
// button, with more zen than the drawing's price, opens and closes the own record, sets 0x78abb14 to 2 and opens the
// 0x98 with text 0x21b, and sounds. The yes and the next step send the C1 04 86 03 in clear, and the requested mix
// turns the button off.
TEST_F(NpcWindowStepsTest, TheMixButtonAsksAndTheYesSends86WithTheKind)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        SetUp();
        sent.clear();
        auto& runtime = world.Runtime();
        runtime.map = map;
        runtime.panels.chaosBox = 1;
        runtime.dialogs.current = 0;
        runtime.lastSoundId = Client::Audio::SoundId{0};
        Npc().chaosMixMode = 0;
        Npc().chaosMixState = 0;
        Npc().mixLuck = 0;
        FillTheUpgrade();
        world.OwnCharacter().zen = UpgradePrice + 1;

        const CrtRandom scanned = Advanced(random, ChaosBoxStepRandoms);
        const auto quiet = Step(NpcX + 0x20, 300);
        ASSERT_TRUE(quiet.HasValue()) << quiet.Error();
        EXPECT_EQ(random.State(), scanned.State());
        EXPECT_EQ(Npc().chaosBoxValue, 15'000 + 100'000 + 70'000 + 40'000);
        EXPECT_EQ(Npc().mixKind, 3);
        EXPECT_EQ(Npc().mixLuck, 1);
        EXPECT_EQ(Npc().mixWidth, 1);
        EXPECT_EQ(Npc().mixHeight, 3);

        // The price the drawing of the 3 leaves (0x5feec8).
        Npc().mixPrice = UpgradePrice;
        const CrtRandom asked = Advanced(random, ChaosBoxStepRandoms + Own);
        Click();
        const auto pressed = Step(MixButtonX, MixButtonY);
        ASSERT_TRUE(pressed.HasValue()) << pressed.Error();
        EXPECT_EQ(random.State(), asked.State());
        EXPECT_TRUE(sent.empty());
        EXPECT_EQ(runtime.dialogs.current, 0x98);
        EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 0), "mezclar ya?");
        EXPECT_EQ(Trade().warningKind, 2);
        EXPECT_EQ(Trade().warningByte, 0xff);
        EXPECT_FALSE(runtime.mouse.pendingClick);
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
        EXPECT_TRUE(Errors().empty());

        Click();
        ASSERT_TRUE(Dialogs(0x15 + 0xd5, 0x5a + 0x64).HasValue());
        EXPECT_EQ(Trade().warningByte, 1);
        const CrtRandom mixed = Advanced(random, ChaosBoxStepRandoms);
        ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
        EXPECT_EQ(random.State(), mixed.State());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_EQ(sent[0].delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x04, 0x86, 0x03})));
        EXPECT_EQ(Npc().chaosMixState, 1);

        // With the mix requested, the button does nothing and the click stays.
        Click();
        ASSERT_TRUE(Step(MixButtonX, MixButtonY).HasValue());
        EXPECT_EQ(sent.size(), 1u);
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_TRUE(runtime.mouse.pendingClick);
    }
}

// 0x5fe982..0x5feb0e: the mix button's warnings (0x555190 with type 2). With the zen equal to the price, the 596 and no
// sound; with the eye and the key of different levels (the −2), the 600; with nothing in the box (the 0), the 580 with
// the 591; with the −0x18, the 1171 with 10 (the wsprintfA of 0x5feab6); with the inventory full, the 581. Each one
// spends the click and draws the 3848 rand() of the own record.
TEST_F(NpcWindowStepsTest, TheMixButtonWarnsWithoutZenRoomOrAMix)
{
    auto& runtime = world.Runtime();
    runtime.panels.chaosBox = 1;
    world.OwnCharacter().zen = UpgradePrice;
    FillTheUpgrade();
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    ASSERT_EQ(Npc().mixKind, 3);
    Npc().mixPrice = UpgradePrice;
    runtime.lastSoundId = Client::Audio::SoundId{0};
    const CrtRandom poor = Advanced(random, ChaosBoxStepRandoms + Own);
    Click();
    ASSERT_TRUE(Step(MixButtonX, MixButtonY).HasValue());
    EXPECT_EQ(random.State(), poor.State());
    EXPECT_EQ(Errors(), std::vector<std::string>{"sin zen"});
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0});
    EXPECT_FALSE(runtime.mouse.pendingClick);
    EXPECT_EQ(runtime.dialogs.current, 0);

    // The +1 eye and the +2 key with the chaos jewel.
    for (ItemState& cell : Npc().chaosBox)
        cell = ItemState{.id = EmptyOwnItemId};
    Place(Npc().chaosBox, ItemState{.id = Eye, .scalar1Low = 1u << 3}, 0, 0, 1, 1);
    Place(Npc().chaosBox, ItemState{.id = Key, .scalar1Low = 2u << 3}, 1, 0, 1, 1);
    Place(Npc().chaosBox, ItemState{.id = ChaosJewel}, 2, 0, 1, 1);
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    ASSERT_EQ(Npc().mixKind, -2);
    Npc().mixPrice = 0;
    Click();
    ASSERT_TRUE(Step(MixButtonX, MixButtonY).HasValue());
    EXPECT_EQ(Errors().back(), "no son iguales");
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});

    for (ItemState& cell : Npc().chaosBox)
        cell = ItemState{.id = EmptyOwnItemId};
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    ASSERT_EQ(Npc().mixKind, 0);
    Click();
    ASSERT_TRUE(Step(MixButtonX, MixButtonY).HasValue());
    EXPECT_EQ(Errors().back(), "no hay mezcla");

    // The −0x18 is left by 0x5fd4f0 with the wings and the +1 feather without level 10. The button (0x5ee1b1) runs
    // before 0x5d8d50 (0x5ee1de) and reads the previous step's type.
    Npc().mixKind = -0x18;
    Click();
    ASSERT_TRUE(Step(MixButtonX, MixButtonY).HasValue());
    EXPECT_EQ(Errors().back(), "desde el nivel 10");
    EXPECT_EQ(Npc().mixKind, 0);

    // The 0xf (Bless only) asks for 1 × 1, and the inventory is full.
    for (ItemState& cell : world.Inventory().cells)
        cell = ItemState{.id = Jewel};
    Place(Npc().chaosBox, ItemState{.id = Jewel}, 0, 0, 1, 1);
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    ASSERT_EQ(Npc().mixKind, 0xf);
    Click();
    ASSERT_TRUE(Step(MixButtonX, MixButtonY).HasValue());
    EXPECT_EQ(Errors().back(), "sin lugar");
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_TRUE(sent.empty());
}

// 0x633644..0x633739: the 0x96 names the level 2 fruit (text 167) in "( %s )" and the yes (0x628b2d) opens the 0xa8
// with "( %s%s )" and the 1901, text 0x76e below and its three buttons. Another item goes without a name.
TEST_F(NpcWindowStepsTest, TheFruitDialogsNameTheItem)
{
    auto& runtime = world.Runtime();
    runtime.dialogItem = ItemState{.id = Fruit, .scalar1Low = 2u << 3};
    ASSERT_TRUE(
        Client::Ui::OpenTextDialog(runtime.dialogBox, runtime.dialogs, &texts, 1, 0x178, 0x96, &runtime.dialogItem)
            .HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0x96);
    EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 0), "( sol )");
    EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 1), "comer la fruta?");
    EXPECT_EQ(runtime.dialogBox.lineCount, 2);

    Click();
    ASSERT_TRUE(Dialogs(0x15 + 0xd5, 0x5a + 0x64).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0xa8);
    EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 0), "( sol y suerte )");
    EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 1), "elegir");
    EXPECT_EQ(runtime.dialogBox.buttons[0].x, 0x16);
    EXPECT_EQ(runtime.dialogBox.buttons[1].x, 0x52);
    EXPECT_EQ(runtime.dialogBox.thirdButton.state, 5);
    EXPECT_EQ(runtime.dialogBox.thirdButton.x, 0x8e);

    runtime.dialogs.current = 0;
    runtime.dialogItem.id = Sword;
    ASSERT_TRUE(
        Client::Ui::OpenTextDialog(runtime.dialogBox, runtime.dialogs, &texts, 1, 0x178, 0x96, &runtime.dialogItem)
            .HasValue());
    EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 0), "(  )");
    const auto unnamed = Client::Ui::OpenTextDialog(runtime.dialogBox, runtime.dialogs, &texts, 1, 0x178, 0x96);
    ASSERT_FALSE(unnamed.HasValue());
    EXPECT_NE(unnamed.Error().find("0x63330f"), std::string::npos);
}

// 0x62a779: the yes of the 0x94 leaves 1 at 0x6c932e; the mouse outside the buttons chooses nothing and the click is
// spent anyway. 0x629c41: the no of the 0xa9 releases the jewel and puts it back in its slot; the yes with the
// warehouse open warns with the 474.
TEST_F(NpcWindowStepsTest, TheYesNoDialogsLeaveTheirAnswer)
{
    auto& runtime = world.Runtime();
    runtime.dialogs.current = 0x94;
    runtime.dialogBox.buttons[0] = {.state = 1, .x = 0x15, .y = 0x5a, .width = 0x46, .height = 0x15};
    runtime.dialogBox.buttons[1] = {.state = 3, .x = 0x78, .y = 0x5a, .width = 0x46, .height = 0x15};
    Click();
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0x94);
    EXPECT_FALSE(runtime.mouse.pendingClick);
    EXPECT_EQ(Trade().warningByte, 0xff);
    Click();
    ASSERT_TRUE(Dialogs(0x15 + 0xd5 + 0x46, 0x5a + 0x64 + 0x15).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(Trade().warningByte, 1);

    runtime.dialogs.current = 0xa9;
    runtime.dialogBox.jewelCell = 4;
    runtime.dialogBox.jewelTarget = 9;
    runtime.cursorItem = ItemState{.id = Jewel};
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Kernel::CursorGrid::Inventory;
    runtime.slot78a6758 = 12 + 4;
    Click();
    ASSERT_TRUE(Dialogs(0x78 + 0xd5, 0x5a + 0x64).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(runtime.dialogBox.jewelCell, 0);
    EXPECT_EQ(runtime.dialogBox.jewelTarget, 0);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(world.Inventory().cells[4].id, Jewel);

    runtime.dialogs.current = 0xa9;
    runtime.panels.warehouse = 1;
    Click();
    ASSERT_TRUE(Dialogs(0x15 + 0xd5, 0x5a + 0x64).HasValue());
    EXPECT_EQ(messages, std::vector<std::size_t>{474});
    EXPECT_EQ(runtime.dialogs.current, 0);
}
} // namespace

TEST_F(NpcWindowStepsTest, ARightClickedFruitOpensBothNamedConfirmations)
{
    auto& runtime = world.Runtime();
    services.texts = &texts;
    world.OwnCharacter().level = 200;
    Place(world.Inventory().cells, ItemState{.id = Fruit, .scalar1Low = 2u << 3}, 1, 1, 1, 1);
    runtime.ui.rightClick = true;
    const auto used = Step(InventoryX + 15 + 20 + 5, 200 + 20 + 5);
    ASSERT_TRUE(used.HasValue()) << used.Error();
    EXPECT_EQ(runtime.dialogs.current, 0x96);
    EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 0), "( sol )");
    EXPECT_EQ(runtime.dialogItemCell, 9);
    EXPECT_EQ(runtime.dialogItem.id, Fruit);
    Click();
    ASSERT_TRUE(Dialogs(234, 190).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0xa8);
    EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 0), "( sol y suerte )");
}

TEST_F(NpcWindowStepsTest, TheFruitAndJewelConfirmationsSendTheOriginal26)
{
    auto& runtime = world.Runtime();
    for (const std::int32_t choice : {0, 1, 2})
    {
        runtime.dialogs = {};
        runtime.dialogItem = ItemState{.id = Fruit, .scalar1Low = 2u << 3};
        runtime.dialogItemCell = 5;
        runtime.dialogBox.jewelCell = 4;
        runtime.dialogBox.jewelTarget = 25;
        runtime.ui.field5767954 = 0;
        runtime.ui.field78abb26 = 9;
        const std::int32_t id = choice == 2 ? 0xa9 : 0xa8;
        ASSERT_TRUE(
            Client::Ui::OpenTextDialog(runtime.dialogBox, runtime.dialogs, &texts, 0, 0, id, &runtime.dialogItem)
                .HasValue());
        sent.clear();
        const auto before = random;
        Click();
        const auto used = Dialogs(choice == 0 ? 235 : choice == 1 ? 295 : 234, choice == 2 ? 190 : 200);
        ASSERT_TRUE(used.HasValue()) << used.Error();
        ASSERT_EQ(sent.size(), 1u);
        const int slot = choice == 2 ? 16 : 17;
        const int target = choice == 2 ? 25 : 0;
        const int kind = choice == 2 ? 9 : choice;
        EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 6, 0x26, slot, target, kind})));
        EXPECT_EQ(sent[0].delivery, Delivery::Encrypted);
        EXPECT_EQ(runtime.ui.field5767954, 10);
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
        EXPECT_EQ(random.State(), Advanced(before, 3).State());
    }
}
