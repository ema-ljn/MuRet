#include "Network/Xor32Packet.h"
#include "Kernel/MouseState.h"
#include "PersonalShop/PersonalShopWindow.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Network/Delivery.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Player/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Hud/GamePanels.h"
#include "Hud/GameUi.h"
#include "Items/InventoryMoves.h"
#include "Items/ItemPrice.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "FrontEnd/SelectionInventory.h"
#include "Ui/ZenText.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameUiState.h"
#include "Ui/MessageArea.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"

#include "support/ItemFiles.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// The personal shop's window (CL9 milestone 7, phase 5, 08 P-C208 b) in Lorencia (0) and Devias (2), against the
// ASM: the buttons of 0x5ea290 (the 3F 02, the 3F 03, warning 1119, the 0x9c box and the title dialog), the 0x9c box
// (0x4716e0, 0x471930 and 0x623750), the click and the right click on the grids with a shop open (0x5c5110 and
// 0x5c5242), dropping on the shop's grid (0x5ca250 with 0x78ab0b8 and 4) and from it to the inventory (0x5cc2cf), and
// the answers to the warnings that 0x5d8d50 checks (the price, the purchase and the trade OK). The titles, names and
// items are made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::EmptyOwnItemId;
using Client::Kernel::ItemState;

constexpr std::uint32_t Seed = 0x5a0e;
// A 1 × 3 staff, a 1 × 1 potion and one that cannot be traded (0x1c17), 1 × 1.
constexpr std::uint16_t StaffType = 0x0a05;
constexpr std::uint16_t PotionType = 0x1c00;
constexpr std::uint16_t UntradableType = 0x1c17;
// The inventory window at (0x1c2, 0), with its grid at (x + 15, y + 200), and the shop's at (0x104, 0), with its grid
// at (x + 15, y + 0x6e).
constexpr std::int32_t InventoryX = 0x1c2;
constexpr std::int32_t InventoryGridLeft = InventoryX + 15;
constexpr std::int32_t InventoryGridTop = 200;
constexpr std::int32_t ShopX = 0x104;
constexpr std::int32_t ShopGridLeft = ShopX + 15;
constexpr std::int32_t ShopGridTop = 0x6e;
constexpr std::int32_t Cell = 20;
// The staff in inventory cell (1, 2) (slot 12 + 17) and the potion in shop cell (3, 0) (slot 0x4f).
constexpr std::size_t StaffCell = 17;
constexpr std::size_t PotionCell = 3;
// The window's foot buttons: open (0x122..0x15a) and close (x + 0x69..x + 0xa1), at y 0x18c..0x1a4; the title at
// (x + 0x14..x + 0xaa, y + 0x41..y + 0x59).
constexpr std::int32_t OpenButtonX = 0x122;
constexpr std::int32_t CloseButtonX = ShopX + 0x69;
constexpr std::int32_t FootButtonY = 0x18c;
constexpr std::int32_t TitleBoxX = ShopX + 0x14;
constexpr std::int32_t TitleBoxY = 0x41;
// A place with nothing: bottom left, outside both windows.
constexpr std::int32_t NowhereX = 10;
constexpr std::int32_t NowhereY = 300;
// The Text.bmd texts it uses: the purchase question (1100), the missing-price warning (1119), the open question (1131,
// of 47 bytes: two rows) and the low price (1132, with the price's %s, and 1133).
constexpr std::size_t BuyText = 1100;
constexpr std::size_t MissingPriceText = 1119;
constexpr std::size_t OpenShopText = 1131;
constexpr std::size_t LowPriceText = 1132;
constexpr std::size_t LowPriceQuestionText = 1133;
// The title that 0x5ebbd0 rejects (0x6261cc).
constexpr std::size_t BadTitleText = 1130;
// The 0x91's OK and cancel (0x626112..0x626227): x from 0xff to 0x133 and from 0x14a to 0x17e, y from 0xa0 to 0xb0.
constexpr std::int32_t ShopOkLeft = 0xff;
constexpr std::int32_t ShopOkRight = 0x133;
constexpr std::int32_t ShopCancelLeft = 0x14a;
constexpr std::int32_t ShopCancelRight = 0x17e;
constexpr std::int32_t ShopButtonTop = 0xa0;
constexpr std::int32_t ShopButtonBottom = 0xb0;

// The step with the inventory and a shop open, the own record open (without the 3848 of its closes) and nothing under
// the mouse: the 12 of test_game_inventory_step.cpp with the window alone minus one, because the search for any open
// panel (0x5a5511, from 0x1 upward) stops at 0x800 (both shops share their state) after 0x20 and before 0x10000.
// Measured: the shop's window, its grid and the answers draw no more.
constexpr int QuietStepRandoms = 11;
// IsOpen(0x20) of the cursor item's step (0x5deb11), with an item on the cursor.
constexpr int CursorQueryRandoms = 1;
// The padding and the serial of a C3.
constexpr int C3Randoms = 2;
// The dialogs' step without Esc (0x623e70): the friends window's query (0x623e9b) draws none.
constexpr int DialogStepRandoms = 0;
// 0x657a90 queries each panel: 0x20 and 0x10000 draw one each.
constexpr int CloseAllRandoms = 2;

constexpr std::uint8_t U8(std::size_t value)
{
    return static_cast<std::uint8_t>(value);
}

std::map<std::size_t, std::string> ShopTexts()
{
    return {{BuyText, "aviso compra"},
            {MissingPriceText, "aviso sin precio"},
            {OpenShopText, "abrir la tienda con estos precios y este titulo"},
            {LowPriceText, "precio %s zen"},
            {LowPriceQuestionText, "vender a ese precio"},
            {BadTitleText, "aviso titulo malo"}};
}

struct Sent
{
    bool encrypted = false;
    std::vector<std::uint8_t> bytes;
};

Sent PlainOf(const Client::Protocol::Outgoing& outgoing)
{
    const auto plain = Client::Network::Xor32Decode(outgoing.packet);
    Sent sent{.encrypted = outgoing.delivery == Client::Network::Delivery::Encrypted, .bytes = {}};
    for (const std::byte b : plain.Bytes())
        sent.bytes.push_back(std::to_integer<std::uint8_t>(b));
    return sent;
}

// What goes out through the UI's wire (IGameUiRequests::SendGamePacket), in the same list as InventoryServices'.
class RecordingRequests final : public Client::Ui::IGameUiRequests
{
public:
    explicit RecordingRequests(std::vector<Sent>& sent) : m_sent(sent) {}
    void SendOptionData(const Client::Protocol::Player::OptionBody& /*body*/) override {}
    void RequestLogout(std::uint8_t /*kind*/) override {}
    void RequestPartyList() override {}
    void SendGamePacket(Client::Protocol::Outgoing outgoing) override
    {
        m_sent.push_back(PlainOf(outgoing));
    }
    void SendWindowClose() override {}
    void SendSetQuest(std::uint8_t /*quest*/, std::uint8_t /*state*/) override {}
    void SendGuildPacket(Client::Protocol::Outgoing /*packet*/) override {}
    void SendFriendPacket(Client::Protocol::Outgoing /*packet*/) override {}
    Client::Core::Expected<void, std::string> SaveMacros(std::string_view /*content*/) override
    {
        return {};
    }
    void Log(std::string_view /*line*/) override {}

private:
    std::vector<Sent>& m_sent;
};

// The rand() drawn from `from` to `to` (up to 100000).
int Drawn(CrtRandom from, const CrtRandom& to)
{
    for (int count = 0; count <= 100'000; ++count)
    {
        if (from.State() == to.State())
            return count;
        static_cast<void>(from.Next());
    }
    return -1;
}

// GetTextExtentPointA: 6 columns per letter and 13 high.
class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

// The game's world, on the heap.
struct ShopWorld
{
    Client::Kernel::WorldState world;
    Client::Tables::ItemTable items =
        Client::Tests::ItemTableOf({{StaffType, 1, 3, 0}, {PotionType, 1, 1, 0xff}, {UntradableType, 1, 1, 0xff}});
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable(ShopTexts());
};

class PersonalShopWindowTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    void SetUp() override
    {
        shop = std::make_unique<ShopWorld>();
        Client::Kernel::WorldState& world = shop->world;
        Client::FrontEnd::ClearSelectionInventory(world, random);
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        runtime.map = GetParam();
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        runtime.panels.inventory = 1;
        runtime.hud.inventoryX = InventoryX;
        runtime.hud.inventoryY = 0;
        // The chat log's corner outside the windows, as in test_trade_grids.cpp.
        runtime.hud.chatFrame.x = -10;
        runtime.hud.chatFrame.y = 0x65;
        runtime.ownRecordDepth = 1;
        // The window's corner, which its draw (0x5f306e) leaves before the first step.
        runtime.social.personalShop.windowX = ShopX;
        runtime.social.personalShop.windowY = 0;
        services.items = &shop->items;
        services.dressTheHero = [] { return Client::Core::Expected<void, std::string>{}; };
        services.systemMessage = [this](std::size_t index, std::int32_t type)
        {
            messages.emplace_back(index, type);
            return Client::Core::Expected<void, std::string>{};
        };
        services.sendItemMove = [this](const Client::Protocol::Items::ItemMove& move)
        {
            sent.push_back(PlainOf(Client::Protocol::Items::WriteItemMove(move)));
            return Client::Core::Expected<void, std::string>{};
        };
        services.sendGamePacket = [this](Client::Protocol::Outgoing packet) { sent.push_back(PlainOf(packet)); };
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return shop->world.Runtime();
    }

    Client::Social::PersonalShopState& Shops()
    {
        return Runtime().social.personalShop;
    }

    std::array<ItemState, Client::Kernel::SecondGridCellCount>& ShopCells()
    {
        return shop->world.Inventory().secondCells;
    }

    std::array<ItemState, Client::Kernel::InventoryCellCount>& InventoryCells()
    {
        return shop->world.Inventory().cells;
    }

    // Open(0x800) or Open(0x20000), like the foot button or the 3F 05.
    void OpenTheShop(Client::Ui::PanelId id)
    {
        Client::Hud::GamePanels panels(shop->world, random);
        panels.LendThePersonalShops(shop->world, random,
                                    [this](Client::Protocol::Outgoing packet) { sent.push_back(PlainOf(packet)); });
        const auto opened = panels.Registry().Open(id);
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        sent.clear();
    }

    // The item `type` of level `level` and `height` cells tall in cell `index` of an 8-column grid: the copy in each
    // cell of its footprint, with its column and row, and +0x3c at 1 only in the top one.
    static void Place(std::span<ItemState> cells, std::size_t index, std::uint16_t type, std::uint32_t level,
                      std::size_t height)
    {
        for (std::size_t row = 0; row < height; ++row)
        {
            ItemState& item = cells[index + row * 8];
            item = ItemState{};
            item.id = type;
            item.scalar1Low = level << 3;
            item.durability = 30;
            item.gridColumn = static_cast<std::uint8_t>(index % 8);
            item.gridRow = static_cast<std::uint8_t>(index / 8);
            item.occupied = row == 0 ? 1U : 0U;
        }
    }

    // A UI step with the mouse at (x, y) and, if `click`, the click's edge; with `rightClick`, the right one's.
    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y, bool click, bool rightClick = false)
    {
        if (click)
            Client::Kernel::LatchTheLeftButton(Runtime().mouse, true, true);
        if (rightClick)
            Runtime().ui.rightClick = true;
        mark = random;
        return Client::Hud::StepTheGameUi(Context(x, y));
    }

    // The dialogs' step (0x623e70) with the mouse at (x, y) and the click.
    Client::Core::Expected<void, std::string> Dialogs(std::int32_t x, std::int32_t y, bool click)
    {
        if (click)
            Client::Kernel::LatchTheLeftButton(Runtime().mouse, true, true);
        mark = random;
        return Client::Hud::StepTheGameDialogs(Context(x, y));
    }

    Client::Ui::GameUiContext Context(std::int32_t x, std::int32_t y)
    {
        return Client::Ui::GameUiContext{.world = shop->world,
                                         .random = random,
                                         .input = input,
                                         .mouseX = x,
                                         .mouseY = y,
                                         .texts = &shop->texts,
                                         .requests = &requests,
                                         .inventory = &services,
                                         .sendNpcWindow = [this](Client::Protocol::Outgoing packet)
                                         {
                                             sent.push_back(PlainOf(packet));
                                             return Client::Core::Expected<void, std::string>{};
                                         }};
    }

    [[nodiscard]] int DrawnByTheStep() const
    {
        return Drawn(mark, random);
    }

    // The x and the y of the center of cell (column, row) of a grid from (left, top).
    static std::int32_t CenterOf(std::int32_t corner, std::size_t place)
    {
        return corner + static_cast<std::int32_t>(place) * Cell + Cell / 2;
    }

    // What the 0x91 dialog's step (0x6260f6) leaves with the price typed and the OK: the dialog closed, 0x77f3b6c with
    // the price and 0x6c932e at 1.
    void ThePriceIsTyped(std::int32_t price)
    {
        Runtime().dialogs.current = 0;
        Runtime().social.typedMoney = price;
        Runtime().social.trade.warningByte = 1;
    }

    // The item's value (0x54d0f0 with mode 2).
    [[nodiscard]] std::uint32_t ValueOf(const ItemState& item) const
    {
        const auto value = Client::Items::ItemPrice(item, shop->items, 2, std::nullopt);
        EXPECT_TRUE(value.HasValue());
        return value.HasValue() ? value.Value() : 0;
    }

    std::unique_ptr<ShopWorld> shop;
    CrtRandom random{Seed};
    CrtRandom mark{Seed};
    Client::Input::InputState input;
    std::vector<Sent> sent;
    RecordingRequests requests{sent};
    Client::Items::InventoryServices services;
    std::vector<std::pair<std::size_t, std::int32_t>> messages;
};

std::vector<std::uint8_t> OpenShopBytes(std::string_view title)
{
    std::vector<std::uint8_t> bytes{0xc1, 0x28, 0x3f, 0x02};
    for (std::size_t at = 0; at < Client::Protocol::PersonalShop::TitleSize; ++at)
        bytes.push_back(at < title.size() ? static_cast<std::uint8_t>(title[at]) : std::uint8_t{0});
    return bytes;
}

const std::vector<std::uint8_t> CloseShopBytes{0xc1, 0x04, 0x3f, 0x03};

// 0x5ea290: the corner on each step; the open button without the prices, without items or without a title gives
// warning 1119 of type 2 and spends the click; with everything, the 0x9c box with the 1000, the two rows of text 1131
// and two buttons. The yes (0x471930 → 0x623750) sends the C1 28 3F 02 with the title and the zeros behind, in C3, and
// closes the panels (0x657a90).
TEST_P(PersonalShopWindowTest, TheOpenButtonAsksAndOpensTheShop)
{
    OpenTheShop(Client::Ui::PersonalShopPanel);
    Shops().windowX = 0;
    auto stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_EQ(Shops().windowX, ShopX);
    EXPECT_EQ(Shops().windowY, 0);

    // Without items, with an item without a price, with a price of 0 and without a title: the warning.
    const auto press = [this] { return Step(OpenButtonX, FootButtonY, true); };
    stepped = press();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    Place(ShopCells(), PotionCell, PotionType, 0, 1);
    stepped = press();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    Shops().ownPrices[0x4c + static_cast<std::int32_t>(PotionCell)] = 0;
    stepped = press();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    Shops().ownPrices[0x4c + static_cast<std::int32_t>(PotionCell)] = 500;
    stepped = press();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    ASSERT_EQ(messages.size(), 4U);
    for (const auto& [text, type] : messages)
    {
        EXPECT_EQ(text, MissingPriceText);
        EXPECT_EQ(type, 2);
    }
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_TRUE(sent.empty());

    // With the title: the box.
    Shops().ownTitle = "Puesto Sur";
    stepped = press();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_EQ(messages.size(), 4U);
    EXPECT_EQ(Runtime().dialogs.current, 0x9c);
    const Client::Social::ConfirmBoxState& box = Runtime().social.confirmBox;
    EXPECT_EQ(box.id, Client::PersonalShop::OpenShopConfirm);
    EXPECT_EQ(box.type, 2);
    EXPECT_EQ(box.lines, (std::vector<std::string>{"abrir la tienda con estos", " precios y este titulo"}));
    EXPECT_TRUE(sent.empty());

    // Outside the buttons, without the click, nothing.
    stepped = Dialogs(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), DialogStepRandoms);
    EXPECT_EQ(Runtime().dialogs.current, 0x9c);

    // The yes: y from h + 140 to h + 156 with h = 2 · (the letter step + 2), x from 0xff to 0x133.
    const std::int32_t height = 2 * (Client::Render::Text::FontStepFor(640) + 2);
    stepped = Dialogs(0xff, height + 140, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), DialogStepRandoms + C3Randoms + CloseAllRandoms);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, OpenShopBytes("Puesto Sur"));
    EXPECT_TRUE(sent[0].encrypted);
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().panels.inventory, 0);
    EXPECT_EQ(Runtime().panels.personalShop, 0);
    EXPECT_EQ(Shops().mode, Client::Social::NoShopView);
    EXPECT_FALSE(Runtime().mouse.pendingClick);
}

// With the shop open (0x78aba50): close sends the C1 04 3F 03 in C3; open sends the 3F 02 again without the box and
// closes the inventory (0x65cae0(0x4000000)), which takes the shop with it (0x11d9e0).
TEST_P(PersonalShopWindowTest, WithTheShopOpenTheButtonsSendAtOnce)
{
    OpenTheShop(Client::Ui::PersonalShopPanel);
    Place(ShopCells(), PotionCell, PotionType, 0, 1);
    Shops().ownPrices[0x4c + static_cast<std::int32_t>(PotionCell)] = 500;
    Shops().ownTitle = "Puesto Sur";
    Shops().ownOpen = 1;

    auto stepped = Step(CloseButtonX, FootButtonY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + C3Randoms);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, CloseShopBytes);
    EXPECT_TRUE(sent[0].encrypted);
    EXPECT_FALSE(Runtime().mouse.pendingClick);

    // Closed, the close button only spends the click.
    sent.clear();
    Shops().ownOpen = 0;
    stepped = Step(CloseButtonX + 0x37, FootButtonY + 0x17, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(sent.empty());
    EXPECT_FALSE(Runtime().mouse.pendingClick);

    Shops().ownOpen = 1;
    stepped = Step(OpenButtonX + 0x37, FootButtonY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    // The C3, the inventory close (0x11d9e0 queries 0x20 and 0x10000, like 0x657a90) and 0x5a5511's search, which
    // without the shop reaches 0x10000.
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + C3Randoms + CloseAllRandoms + 1);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, OpenShopBytes("Puesto Sur"));
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().panels.inventory, 0);
    EXPECT_EQ(Runtime().panels.personalShop, 0);
    EXPECT_EQ(Shops().mode, Client::Social::NoShopView);
}

// 0x5eafc8..0x5eb00b: the title (without spending the click) opens dialog 0x91 in mode 1, with the 0x1c field, without
// the zen and without the chat; answered (0x6c932e at 1 or 2), the next step clears it (0x5ea2f1).
TEST_P(PersonalShopWindowTest, TheTitleBoxOpensTheTitleDialog)
{
    OpenTheShop(Client::Ui::PersonalShopPanel);
    Runtime().ui.chatOpen = true;
    Runtime().social.moneyField = 1;
    auto stepped = Step(TitleBoxX, TitleBoxY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_EQ(Runtime().dialogs.current, 0x91);
    EXPECT_EQ(Shops().dialogMode, Client::PersonalShop::ShopTitleMode);
    EXPECT_EQ(Runtime().social.dialogFieldLimit, 0x1c);
    EXPECT_EQ(Runtime().social.moneyField, 0);
    EXPECT_FALSE(Runtime().ui.chatOpen);
    EXPECT_TRUE(sent.empty());

    // The 0x91's cancel leaves the answer at 2 and closes it.
    stepped = Dialogs(ShopCancelLeft, ShopButtonTop, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().social.trade.warningByte, 2);
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Shops().dialogMode, 0);
    EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);
}

// 0x5eba70(1): the EDIT of [0x5763504] in view with +0x4c at 0xc, the background at 0, the cap of 0x1c, 0x85 × 10 at
// (0xfd, 0x8a), and empty; with the price, +0x4c at 9 (digits only), the cap of 8 and 0x3c × 10 at (0x125, 0x8a).
TEST_P(PersonalShopWindowTest, TheShopDialogSetsUpItsEdit)
{
    Client::Ui::OldEdit& edit = Runtime().chatInput.dialogEdit;
    edit.text.SetText("viejo");
    edit.base.state = Client::Ui::HiddenWindow;
    Client::PersonalShop::OpenTheShopDialog(Runtime(), Client::PersonalShop::ShopTitleMode);
    EXPECT_EQ(Runtime().dialogs.current, 0x91);
    EXPECT_EQ(edit.base.state, Client::Ui::ShownWindow);
    EXPECT_EQ(edit.filter, 0xc);
    EXPECT_EQ(edit.backgroundColor, 0u);
    EXPECT_EQ(edit.base.width, 0x85);
    EXPECT_EQ(edit.base.height, 0xa);
    EXPECT_EQ(edit.base.x, 0xfd);
    EXPECT_EQ(edit.base.y, 0x8a);
    EXPECT_EQ(edit.text.Value(), "");
    Client::Ui::TypeInTheEdit(edit, std::string(0x20, 'a'));
    EXPECT_EQ(edit.text.Value().size(), 0x1cu);

    Runtime().dialogs.current = 0;
    Client::PersonalShop::OpenTheShopDialog(Runtime(), Client::PersonalShop::ShopPriceMode);
    EXPECT_EQ(edit.filter, 9);
    EXPECT_EQ(edit.base.width, 0x3c);
    EXPECT_EQ(edit.base.x, 0x125);
    EXPECT_EQ(edit.text.Value(), "");
    Client::Ui::TypeInTheEdit(edit, "12a3456789");
    EXPECT_EQ(edit.text.Value(), "12345678");
}

// 0x6260f6: the OK with the click (on the bottom-right edge) spends the click, reads the EDIT and keeps the title; the
// answer is 1, the text fields stay empty (the EDIT too), the focus goes back to the game and the dialog closes. No
// rand().
TEST_P(PersonalShopWindowTest, TheShopDialogOkKeepsTheTitle)
{
    Client::PersonalShop::OpenTheShopDialog(Runtime(), Client::PersonalShop::ShopTitleMode);
    Client::Ui::ChatInputState& chat = Runtime().chatInput;
    Client::Ui::TypeInTheEdit(chat.dialogEdit, "Mi puesto");
    chat.focus = Client::Ui::ChatFocus::Dialog;
    const auto stepped = Dialogs(ShopOkRight, ShopButtonBottom, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), DialogStepRandoms);
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_EQ(Shops().ownTitle, "Mi puesto");
    EXPECT_EQ(Runtime().social.trade.warningByte, 1);
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(chat.dialogEdit.text.Value(), "");
    EXPECT_EQ(chat.focus, Client::Ui::ChatFocus::Game);
    EXPECT_FALSE(Runtime().ui.chatOpen);
    EXPECT_TRUE(sent.empty());
}

// With the price, the OK keeps the EDIT's atoi in 0x77f3b6c: above 0 answers 1, and 0 answers 2.
TEST_P(PersonalShopWindowTest, TheShopDialogOkReadsThePrice)
{
    for (const auto& [typed, price, answer] :
         {std::tuple<std::string_view, std::int32_t, std::uint8_t>{"1250", 1250, std::uint8_t{1}},
          std::tuple<std::string_view, std::int32_t, std::uint8_t>{"0", 0, std::uint8_t{2}}})
    {
        Client::PersonalShop::OpenTheShopDialog(Runtime(), Client::PersonalShop::ShopPriceMode);
        Client::Ui::TypeInTheEdit(Runtime().chatInput.dialogEdit, typed);
        const auto stepped = Dialogs(ShopOkLeft, ShopButtonTop, true);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(Runtime().social.typedMoney, price) << typed;
        EXPECT_EQ(Runtime().social.trade.warningByte, answer) << typed;
        EXPECT_EQ(Runtime().dialogs.current, 0) << typed;
    }
}

// The cancel spends the click and answers 2 without reading the EDIT; outside both buttons, neither the click nor the
// dialog change.
TEST_P(PersonalShopWindowTest, TheShopDialogCancelAnswersNo)
{
    Client::PersonalShop::OpenTheShopDialog(Runtime(), Client::PersonalShop::ShopTitleMode);
    Client::Ui::TypeInTheEdit(Runtime().chatInput.dialogEdit, "Otro");
    Runtime().social.trade.warningByte = 0xff;
    auto stepped = Dialogs(ShopCancelRight + 1, ShopButtonTop, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(Runtime().mouse.pendingClick);
    EXPECT_EQ(Runtime().dialogs.current, 0x91);
    EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);

    Runtime().mouse.pendingClick = false;
    stepped = Dialogs(ShopCancelLeft, ShopButtonBottom, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().social.trade.warningByte, 2);
    EXPECT_EQ(Shops().ownTitle, "");
    EXPECT_EQ(Runtime().chatInput.dialogEdit.text.Value(), "");
}

// Enter (0x5763588) counts as the OK without reading the EDIT: the WndProc (0x4b55db) kept the title. No title, 2.
TEST_P(PersonalShopWindowTest, EnterAnswersTheShopDialog)
{
    for (const auto& [title, answer] : {std::pair<std::string_view, std::uint8_t>{"Del Enter", std::uint8_t{1}},
                                        std::pair<std::string_view, std::uint8_t>{"", std::uint8_t{2}}})
    {
        Client::PersonalShop::OpenTheShopDialog(Runtime(), Client::PersonalShop::ShopTitleMode);
        Client::Ui::TypeInTheEdit(Runtime().chatInput.dialogEdit, "no se lee");
        Shops().ownTitle = std::string(title);
        Runtime().ui.enterPressed = true;
        const auto stepped = Dialogs(NowhereX, NowhereY, false);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_FALSE(Runtime().ui.enterPressed);
        EXPECT_EQ(Shops().ownTitle, title);
        EXPECT_EQ(Runtime().social.trade.warningByte, answer) << title;
        EXPECT_EQ(Runtime().dialogs.current, 0);
    }
}

// 0x6261c5..0x6261f2: a title that 0x5ebbd0 rejects (two blanks at the end) is not kept: warning 1130 of type 1
// without a name, and answer 2 with the title empty.
TEST_P(PersonalShopWindowTest, TheShopDialogRefusesABadTitle)
{
    Client::PersonalShop::OpenTheShopDialog(Runtime(), Client::PersonalShop::ShopTitleMode);
    Client::Ui::TypeInTheEdit(Runtime().chatInput.dialogEdit, "Mal  ");
    LetterMeasure letters;
    Client::Ui::GameUiContext context = Context(ShopOkLeft, ShopButtonTop);
    context.measure = &letters;
    Client::Kernel::LatchTheLeftButton(Runtime().mouse, true, true);
    const auto stepped = Client::Hud::StepTheGameDialogs(context);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Shops().ownTitle, "");
    EXPECT_EQ(Runtime().social.trade.warningByte, 2);
    const auto& entries = Runtime().chat.log.entries;
    EXPECT_TRUE(std::ranges::any_of(entries,
                                    [](const Client::Ui::ChatEntry& entry) {
                                        return entry.text == "aviso titulo malo" &&
                                               entry.type == Client::Ui::SystemMessage && entry.name.empty();
                                    }));
}

// 0x5c5110 with the own shop: the click picks up the item from either grid (the slot + 12 or + 0x4c) without the sound
// or the trade, and clears the item under the mouse; 0x78abaa4 marks the shop's grid.
TEST_P(PersonalShopWindowTest, TheClickPicksUpFromBothGridsWithTheOwnShop)
{
    OpenTheShop(Client::Ui::PersonalShopPanel);
    Place(InventoryCells(), StaffCell, StaffType, 2, 3);
    Place(ShopCells(), PotionCell, PotionType, 0, 1);
    Runtime().lastSoundId = Client::Audio::SoundId{0};

    // The mouse over the shop's potion, without the click: 0x78abaa4 at 1 and 0x5aed40 with 0 and the slot.
    const std::int32_t potionX = CenterOf(ShopGridLeft, PotionCell);
    const std::int32_t potionY = CenterOf(ShopGridTop, 0);
    auto stepped = Step(potionX, potionY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_EQ(Runtime().ui.shopGridHovered, 1);
    EXPECT_EQ(Runtime().ui.hoveredItem.place, Client::Ui::HoveredItem::Place::ShopCell);
    EXPECT_EQ(Runtime().ui.hoveredItem.index, PotionCell);
    EXPECT_EQ(Runtime().ui.field78abb28, 0);
    EXPECT_EQ(Runtime().ui.field78abb2c, 0x4c + static_cast<std::int32_t>(PotionCell));

    // The staff from the bottom cell of its footprint: the top slot (12 + 17).
    const std::int32_t staffX = CenterOf(InventoryGridLeft, 1);
    const std::int32_t staffY = CenterOf(InventoryGridTop, 4);
    stepped = Step(staffX, staffY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + CursorQueryRandoms);
    EXPECT_EQ(Runtime().ui.shopGridHovered, 0);
    EXPECT_EQ(Runtime().cursorItemCount, 1);
    EXPECT_EQ(Runtime().cursorItem.id, StaffType);
    EXPECT_EQ(Runtime().cursorGrid, Client::Kernel::CursorGrid::Inventory);
    EXPECT_EQ(Runtime().slot78a6758, 12 + static_cast<std::int32_t>(StaffCell));
    for (const std::size_t cell : {StaffCell, StaffCell + 8, StaffCell + 16})
        EXPECT_EQ(InventoryCells()[cell].id, EmptyOwnItemId);
    EXPECT_EQ(Runtime().ui.hoveredItem.place, Client::Ui::HoveredItem::Place::None);
    EXPECT_EQ(Runtime().hoveredItemCopy.id, StaffType);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0});
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_TRUE(sent.empty());

    // The shop's potion, with the cursor empty again.
    Runtime().cursorItemCount = 0;
    stepped = Step(potionX, potionY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().cursorItem.id, PotionType);
    EXPECT_EQ(Runtime().cursorGrid, Client::Kernel::CursorGrid::PersonalShop);
    EXPECT_EQ(Runtime().slot78a6758, 0x4c + static_cast<std::int32_t>(PotionCell));
    EXPECT_EQ(ShopCells()[PotionCell].id, EmptyOwnItemId);
    EXPECT_TRUE(sent.empty());
}

// 0x5c5110 with another's: the click on their item copies it to 0x78a59c8 with its slot and opens the 0x94 with text
// 1100, which waits for warning 5; the yes (0x5d9c82) sends the C1 11 3F 06 with the owner's key, their name and the
// slot in C3. The no and the yes without the key only clear it. One from the inventory is picked up.
TEST_P(PersonalShopWindowTest, TheOtherShopAsksBeforeBuying)
{
    OpenTheShop(Client::Ui::OtherShopPanel);
    ASSERT_EQ(Shops().mode, Client::Social::OtherShopView);
    Place(ShopCells(), PotionCell + 2, PotionType, 0, 1);
    const std::int32_t potionX = CenterOf(ShopGridLeft, PotionCell + 2);
    const std::int32_t potionY = CenterOf(ShopGridTop, 0);
    auto stepped = Step(potionX, potionY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_EQ(Runtime().dialogs.current, 0x94);
    EXPECT_EQ(std::string_view(Runtime().dialogBox.rows.data()), "aviso compra");
    EXPECT_EQ(Runtime().dialogItem.id, PotionType);
    EXPECT_EQ(Runtime().slot78a6758, 0x4c + static_cast<std::int32_t>(PotionCell) + 2);
    EXPECT_EQ(Runtime().social.trade.warningKind, 5);
    EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);
    EXPECT_EQ(Runtime().cursorItemCount, 0);
    EXPECT_EQ(ShopCells()[PotionCell + 2].id, PotionType);
    EXPECT_EQ(Runtime().ui.hoveredItem.place, Client::Ui::HoveredItem::Place::None);
    EXPECT_FALSE(Runtime().mouse.pendingClick);

    // The 0x94 (0x6282dc, CL9.3) leaves the answer and closes. With no answer yet, the warning keeps waiting.
    Runtime().dialogs.current = 0;
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().social.trade.warningKind, 5);
    Shops().ownerKey = 0x0456;
    Shops().ownerName = {std::byte{'V'}, std::byte{'e'}, std::byte{'n'}, std::byte{'d'}, std::byte{'e'},
                         std::byte{'d'}, std::byte{'o'}, std::byte{'r'}, std::byte{0},   std::byte{0}};
    Runtime().social.trade.warningByte = 1;
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + C3Randoms);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x11, 0x3f, 0x06, 0x04, 0x56, 'V', 'e', 'n', 'd', 'e',
                                                        'd', 'o', 'r', 0, 0, U8(0x4c + PotionCell + 2)}));
    EXPECT_TRUE(sent[0].encrypted);
    EXPECT_EQ(Runtime().social.trade.warningKind, 0);
    EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);

    // The no, and the yes without the key: nothing.
    sent.clear();
    for (const auto& [answer, key] : {std::pair<std::uint8_t, std::uint16_t>{std::uint8_t{2}, std::uint16_t{0x0456}},
                                      std::pair<std::uint8_t, std::uint16_t>{std::uint8_t{1}, std::uint16_t{0}}})
    {
        Shops().ownerKey = key;
        Runtime().social.trade.warningKind = 5;
        Runtime().social.trade.warningByte = answer;
        stepped = Step(NowhereX, NowhereY, false);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
        EXPECT_EQ(Runtime().social.trade.warningKind, 0);
        EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);
    }
    EXPECT_TRUE(sent.empty());

    // From the inventory, it is picked up.
    Place(InventoryCells(), StaffCell, StaffType, 2, 3);
    stepped = Step(CenterOf(InventoryGridLeft, 1), CenterOf(InventoryGridTop, 2), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().cursorItem.id, StaffType);
    EXPECT_EQ(Runtime().slot78a6758, 12 + static_cast<std::int32_t>(StaffCell));
    EXPECT_EQ(Runtime().dialogs.current, 0);
}

// 0x5c5242: the right click copies the item to 0x78a59c8 and puts its slot + 0x4c in 0x78a6758 with any grid; with
// the own shop and its grid it opens the price dialog (0x5eba70(2): the 8 field and the zen). The item under the mouse
// stays.
TEST_P(PersonalShopWindowTest, TheRightClickOpensThePriceDialog)
{
    OpenTheShop(Client::Ui::PersonalShopPanel);
    Place(InventoryCells(), StaffCell, StaffType, 2, 3);
    Place(ShopCells(), PotionCell, PotionType, 0, 1);
    auto stepped = Step(CenterOf(ShopGridLeft, PotionCell), CenterOf(ShopGridTop, 0), false, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_FALSE(Runtime().ui.rightClick);
    EXPECT_EQ(Runtime().dialogs.current, 0x91);
    EXPECT_EQ(Shops().dialogMode, Client::PersonalShop::ShopPriceMode);
    EXPECT_EQ(Runtime().social.dialogFieldLimit, 8);
    EXPECT_EQ(Runtime().social.moneyField, 1);
    EXPECT_EQ(Runtime().dialogItem.id, PotionType);
    EXPECT_EQ(Runtime().slot78a6758, 0x4c + static_cast<std::int32_t>(PotionCell));
    EXPECT_EQ(Runtime().cursorGrid, Client::Kernel::CursorGrid::PersonalShop);
    EXPECT_EQ(Runtime().ui.hoveredItem.place, Client::Ui::HoveredItem::Place::ShopCell);
    EXPECT_EQ(ShopCells()[PotionCell].id, PotionType);

    // Over the inventory's staff: the slot with 0x4c all the same (0x5c526a), without a dialog.
    Runtime().dialogs.current = 0;
    Shops().dialogMode = 0;
    stepped = Step(CenterOf(InventoryGridLeft, 1), CenterOf(InventoryGridTop, 3), false, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().dialogItem.id, StaffType);
    EXPECT_EQ(Runtime().slot78a6758, 0x4c + static_cast<std::int32_t>(StaffCell));
    EXPECT_EQ(Runtime().cursorGrid, Client::Kernel::CursorGrid::Inventory);
    EXPECT_EQ(InventoryCells()[StaffCell].id, StaffType);
    EXPECT_TRUE(sent.empty());
}

// 0x5d5046 from the inventory: dropping the staff on the own shop copies it to 0x78a59c8 and opens the price dialog
// with the target in 0x77f3cc8. The typed price (0x5dc79e) not below its value: the C1 09 3F 01 with the source slot
// and the price, the 24 from grid 0 to 4 and the price in the map at the target; below, warning 0x99 with the price,
// and its no goes back to the price dialog. The dialog's no returns the staff to its slot.
TEST_P(PersonalShopWindowTest, DroppingOnTheShopAsksForThePrice)
{
    OpenTheShop(Client::Ui::PersonalShopPanel);
    Place(InventoryCells(), StaffCell, StaffType, 2, 3);
    auto stepped = Step(CenterOf(InventoryGridLeft, 1), CenterOf(InventoryGridTop, 2), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(Runtime().cursorItem.id, StaffType);

    // Without the click, the footprint marks on the shop's grid.
    const std::int32_t dropX = CenterOf(ShopGridLeft, 2);
    const std::int32_t dropY = CenterOf(ShopGridTop, 1);
    stepped = Step(dropX, dropY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + CursorQueryRandoms);
    for (const std::size_t cell : {2U, 10U, 18U})
        EXPECT_EQ(ShopCells()[cell].warning, 2) << cell;

    stepped = Step(dropX, dropY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + CursorQueryRandoms);
    EXPECT_EQ(Runtime().dialogs.current, 0x91);
    EXPECT_EQ(Shops().dialogMode, Client::PersonalShop::ShopPriceMode);
    EXPECT_EQ(Runtime().dialogItem.id, StaffType);
    EXPECT_EQ(Runtime().itemMoveTarget, 0x4c + 2);
    EXPECT_EQ(Runtime().cursorItemCount, 1);
    EXPECT_TRUE(sent.empty());

    // The price, below the value: warning 0x99 (0x5dc7d5) with the price written by 0x5b1130 in the first row and the
    // 1133 in the second, and warning 6 waiting for the answer.
    const std::uint32_t value = ValueOf(Runtime().dialogItem);
    ASSERT_GT(value, 0U);
    ThePriceIsTyped(static_cast<std::int32_t>(value - 1));
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().dialogs.current, Client::Ui::MoneyTextDialog);
    EXPECT_EQ(Runtime().dialogBox.lineCount, 2);
    EXPECT_EQ(Client::Ui::DialogRow(Runtime().dialogBox, 0), "precio " + Client::Ui::FormatZen(value - 1) + " zen");
    EXPECT_EQ(Client::Ui::DialogRow(Runtime().dialogBox, 1), "vender a ese precio");
    EXPECT_EQ(Runtime().social.trade.warningKind, 6);
    EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);
    EXPECT_EQ(Shops().dialogMode, 0);
    EXPECT_TRUE(sent.empty());

    // The 0x99's no (its step closes it and leaves 2): the price dialog again.
    Runtime().dialogs.current = 0;
    Runtime().social.trade.warningByte = 2;
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().dialogs.current, 0x91);
    EXPECT_EQ(Shops().dialogMode, Client::PersonalShop::ShopPriceMode);
    EXPECT_TRUE(sent.empty());

    // The price equal to the value, with the shop open: the 3F 03, the 3F 01 and the 24.
    Shops().dialogMode = Client::PersonalShop::ShopPriceMode;
    Shops().ownOpen = 1;
    ThePriceIsTyped(static_cast<std::int32_t>(value));
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + 3 * C3Randoms);
    ASSERT_EQ(sent.size(), 3U);
    EXPECT_EQ(sent[0].bytes, CloseShopBytes);
    const auto byte = [](std::uint32_t number, unsigned shift) { return static_cast<std::uint8_t>(number >> shift); };
    EXPECT_EQ(sent[1].bytes, (std::vector<std::uint8_t>{0xc1, 0x09, 0x3f, 0x01, U8(12 + StaffCell), byte(value, 0),
                                                        byte(value, 8), byte(value, 16), byte(value, 24)}));
    EXPECT_TRUE(sent[1].encrypted);
    EXPECT_EQ(sent[2].bytes, (std::vector<std::uint8_t>{0xc1, 0x0e, 0x24, 0x00, U8(12 + StaffCell), 0x05, 2 << 3, 30,
                                                        0x00, 0x00, 0x50, 0x00, 0x04, U8(0x4c + 2)}));
    EXPECT_EQ(Shops().ownPrices.at(0x4c + 2), static_cast<std::int32_t>(value));
    EXPECT_EQ(Runtime().social.typedMoney, 0);
    EXPECT_EQ(Runtime().ui.field78abb39, 1);
    EXPECT_EQ(Shops().dialogMode, 0);
    EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);

    // The dialog's no: 0x5be150 returns the staff.
    sent.clear();
    Runtime().ui.field78abb39 = 0;
    Shops().dialogMode = Client::PersonalShop::ShopPriceMode;
    Runtime().social.trade.warningByte = 2;
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().cursorItemCount, 0);
    EXPECT_EQ(InventoryCells()[StaffCell].id, StaffType);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1d});
    EXPECT_EQ(Shops().dialogMode, 0);
    EXPECT_TRUE(sent.empty());
}

// Warning 6 (0x5dc7e9, the low price one) with its answer: the no opens the price dialog again; the yes sets the price
// (without an item on the cursor, the 3F 01 and the price at 0x78a6758's slot).
TEST_P(PersonalShopWindowTest, TheLowPriceWarningAnswers)
{
    OpenTheShop(Client::Ui::PersonalShopPanel);
    Runtime().social.trade.warningKind = 6;
    Runtime().social.trade.warningByte = 2;
    auto stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().dialogs.current, 0x91);
    EXPECT_EQ(Shops().dialogMode, Client::PersonalShop::ShopPriceMode);
    EXPECT_EQ(Runtime().social.trade.warningKind, 0);
    EXPECT_TRUE(sent.empty());

    Runtime().dialogs.current = 0;
    Runtime().slot78a6758 = 0x4c + 5;
    Runtime().social.typedMoney = 7000;
    Runtime().social.trade.warningKind = 6;
    Runtime().social.trade.warningByte = 1;
    Shops().dialogMode = 0;
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + C3Randoms);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x09, 0x3f, 0x01, 0x51, 0x58, 0x1b, 0x00, 0x00}));
    EXPECT_EQ(Shops().ownPrices.at(0x4c + 5), 7000);
    EXPECT_EQ(Runtime().social.typedMoney, 0);
}

// From the shop's grid (0x5d5098 and 0x5cc2cf): to the same slot, 0x5be150; to another of the shop, with the shop
// open, the 3F 03 and the 24 from 4 to 4; to the inventory, the 3F 03 and the 24 from 4 to 0 with the slot + 12.
TEST_P(PersonalShopWindowTest, MovingOutOfTheShopGridClosesTheShop)
{
    OpenTheShop(Client::Ui::PersonalShopPanel);
    Place(ShopCells(), PotionCell, PotionType, 0, 1);
    Shops().ownOpen = 1;
    const std::int32_t potionX = CenterOf(ShopGridLeft, PotionCell);
    const std::int32_t potionY = CenterOf(ShopGridTop, 0);
    auto stepped = Step(potionX, potionY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(Runtime().cursorGrid, Client::Kernel::CursorGrid::PersonalShop);

    // To the same slot.
    stepped = Step(potionX, potionY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + CursorQueryRandoms);
    EXPECT_EQ(Runtime().cursorItemCount, 0);
    EXPECT_EQ(ShopCells()[PotionCell].id, PotionType);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1d});
    EXPECT_TRUE(sent.empty());

    // To the shop's cell (6, 1).
    stepped = Step(potionX, potionY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    stepped = Step(CenterOf(ShopGridLeft, 6), CenterOf(ShopGridTop, 1), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + CursorQueryRandoms + 2 * C3Randoms);
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent[0].bytes, CloseShopBytes);
    EXPECT_EQ(sent[1].bytes, (std::vector<std::uint8_t>{0xc1, 0x0e, 0x24, 0x04, U8(0x4c + PotionCell), 0x00, 0x00, 30,
                                                        0x00, 0x00, 0xe0, 0x00, 0x04, U8(0x4c + 14)}));
    EXPECT_EQ(Runtime().itemMoveTarget, 0x4c + 14);

    // To the inventory, to cell (0, 0).
    sent.clear();
    Runtime().ui.field78abb39 = 0;
    stepped = Step(CenterOf(InventoryGridLeft, 0), CenterOf(InventoryGridTop, 0), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + CursorQueryRandoms + 2 * C3Randoms);
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent[0].bytes, CloseShopBytes);
    EXPECT_EQ(sent[1].bytes, (std::vector<std::uint8_t>{0xc1, 0x0e, 0x24, 0x04, U8(0x4c + PotionCell), 0x00, 0x00, 30,
                                                        0x00, 0x00, 0xe0, 0x00, 0x00, 12}));
    EXPECT_EQ(Runtime().itemMoveTarget, 12);
}

// Dropping on the shop's grid what cannot be traded (0x5cf3c9) or while looking at another's (0x5d506b) does nothing
// but spend the click inside the grid.
TEST_P(PersonalShopWindowTest, SomeDropsOnTheShopDoNothing)
{
    OpenTheShop(Client::Ui::PersonalShopPanel);
    Place(InventoryCells(), 0, UntradableType, 0, 1);
    auto stepped = Step(CenterOf(InventoryGridLeft, 0), CenterOf(InventoryGridTop, 0), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(Runtime().cursorItem.id, UntradableType);
    stepped = Step(CenterOf(ShopGridLeft, 0), CenterOf(ShopGridTop, 0), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().cursorItemCount, 1);
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_TRUE(sent.empty());

    Shops().mode = Client::Social::OtherShopView;
    Runtime().cursorItem.id = PotionType;
    stepped = Step(CenterOf(ShopGridLeft, 0), CenterOf(ShopGridTop, 0), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().itemMoveTarget, 0x4c);
    EXPECT_TRUE(sent.empty());
}

// The 0x78abb14 warnings go before the cursor item: the 3 (0x5d966b) with yes flips the own OK and sends the C1 04 3C
// with the new one in C3; without an answer the step ends without dropping the item; the NPC windows' ones are handled
// by Npc::AnswerTheNpcWarning (milestone 6).
TEST_P(PersonalShopWindowTest, TheWarningsAnswerBeforeTheCursorItem)
{
    OpenTheShop(Client::Ui::PersonalShopPanel);
    Client::Social::TradeState& trade = Runtime().social.trade;
    trade.warningKind = 3;
    trade.warningByte = 1;
    auto stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + C3Randoms);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x3c, 0x01}));
    EXPECT_EQ(trade.ownOk, 1);
    EXPECT_EQ(trade.changed, 1);
    EXPECT_EQ(trade.warningKind, 0);

    // Without an answer, the cursor's staff is not dropped in the inventory.
    sent.clear();
    Place(InventoryCells(), StaffCell, StaffType, 2, 3);
    stepped = Step(CenterOf(InventoryGridLeft, 1), CenterOf(InventoryGridTop, 2), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(Runtime().cursorItemCount, 1);
    trade.warningKind = 3;
    trade.warningByte = 0xff;
    stepped = Step(CenterOf(InventoryGridLeft, 5), CenterOf(InventoryGridTop, 0), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_EQ(Runtime().cursorItemCount, 1);
    EXPECT_TRUE(Runtime().mouse.pendingClick);
    EXPECT_TRUE(sent.empty());

    // The 1 with no returns the item. With yes, the 1 sends the C1 04 33 (0x5d8e0d), the 2 the C1 04 86 (0x5d934f) and
    // the 4 opens the warehouse keypad (0x5d9b82), without a packet.
    trade.warningKind = 1;
    trade.warningByte = 2;
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().cursorItemCount, 0);
    EXPECT_EQ(InventoryCells()[StaffCell].id, StaffType);
    for (const auto& [kind, opcode] : {std::pair<std::int32_t, int>{1, 0x33}, {2, 0x86}, {4, -1}})
    {
        sent.clear();
        trade.warningKind = kind;
        trade.warningByte = 1;
        stepped = Step(NowhereX, NowhereY, false);
        ASSERT_TRUE(stepped.HasValue()) << kind << ": " << stepped.Error();
        EXPECT_EQ(trade.warningKind, 0) << kind;
        if (opcode < 0)
        {
            EXPECT_TRUE(sent.empty()) << kind;
            continue;
        }
        ASSERT_EQ(sent.size(), 1U) << kind;
        ASSERT_GE(sent[0].bytes.size(), 3U) << kind;
        EXPECT_EQ(sent[0].bytes[2], opcode) << kind;
    }
}

INSTANTIATE_TEST_SUITE_P(LorenciaAndDevias, PersonalShopWindowTest, ::testing::Values(0, 2));

// 0x4716e0: the rows are cut when they pass 24 bytes (a byte of 0x80 or more counts two, without looking at the next
// one) or at the exact end; the piece that ends with a byte of 0x80 or more past the end is lost (07 Q-C966). With the
// 0x9c open, false; with a type outside 1..2, false with the dialog requested.
TEST(ConfirmBoxTest, TheTextIsCutIntoRows)
{
    Client::Social::ConfirmBoxState box;
    Client::Ui::DialogState dialogs;
    ASSERT_TRUE(Client::PersonalShop::OpenTheConfirmBox(box, dialogs, 7, std::string(30, 'a'), 1));
    EXPECT_EQ(dialogs.current, 0x9c);
    EXPECT_EQ(box.id, 7);
    EXPECT_EQ(box.type, 1);
    EXPECT_EQ(box.lines, (std::vector<std::string>{std::string(25, 'a'), std::string(5, 'a')}));
    EXPECT_FALSE(Client::PersonalShop::OpenTheConfirmBox(box, dialogs, 8, "otro", 2));
    EXPECT_EQ(box.id, 7);

    // A high byte at the 25th: the piece reaches 26, with the byte that follows it.
    dialogs = {};
    const std::string wide = std::string(24, 'b') + "\xb0\xa1" + "cc";
    ASSERT_TRUE(Client::PersonalShop::OpenTheConfirmBox(box, dialogs, 1, wide, 2));
    EXPECT_EQ(box.lines, (std::vector<std::string>{std::string(24, 'b') + "\xb0\xa1", "cc"}));

    // A high one in the last byte: the piece is lost.
    dialogs = {};
    ASSERT_TRUE(Client::PersonalShop::OpenTheConfirmBox(box, dialogs, 1, std::string("dd") + "\xb0", 2));
    EXPECT_TRUE(box.lines.empty());

    // Type 3: the dialog requested and nothing else.
    dialogs = {};
    EXPECT_FALSE(Client::PersonalShop::OpenTheConfirmBox(box, dialogs, 9, "x", 3));
    EXPECT_EQ(dialogs.current, 0x9c);
    EXPECT_EQ(box.id, 1);
}

// 0x471930 with one button (0x127..0x15b) and the no of two (0x14a..0x17e), with the edges: the 1000 with the 2 only
// goes back to the dialog that was waiting; with another id, 0x623750 does nothing and the box stays.
TEST(ConfirmBoxTest, TheButtonsAnswerTheBox)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::Kernel::WorldRuntime& runtime = world->Runtime();
    CrtRandom random{Seed};
    Client::Input::InputState input;
    std::vector<Sent> sent;
    RecordingRequests requests{sent};
    const auto step = [&](std::int32_t x, std::int32_t y)
    {
        Client::Kernel::LatchTheLeftButton(runtime.mouse, true, true);
        return Client::Hud::StepTheGameDialogs(Client::Ui::GameUiContext{
            .world = *world, .random = random, .input = input, .mouseX = x, .mouseY = y, .requests = &requests});
    };
    const std::int32_t row = Client::Render::Text::FontStepFor(640) + 2;

    // Two buttons, the no on the bottom edge: the dialog that was waiting (0x7d3a110) comes back.
    ASSERT_TRUE(Client::PersonalShop::OpenTheConfirmBox(runtime.social.confirmBox, runtime.dialogs,
                                                        Client::PersonalShop::OpenShopConfirm, "pregunta", 2));
    runtime.dialogs.pending = 0x8c;
    auto stepped = step(0x17e, row + 156);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(runtime.dialogs.current, 0x8c);
    EXPECT_EQ(runtime.dialogs.pending, 0);
    EXPECT_FALSE(runtime.mouse.pendingClick);
    EXPECT_TRUE(sent.empty());

    // One button, another id: the click is spent and the box stays.
    runtime.dialogs = {};
    ASSERT_TRUE(Client::PersonalShop::OpenTheConfirmBox(runtime.social.confirmBox, runtime.dialogs, 5, "aviso", 1));
    stepped = step(0x14a, row + 140);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(runtime.dialogs.current, 0x9c);
    EXPECT_FALSE(runtime.mouse.pendingClick);
    // Outside the button (y + 157), the click stays.
    stepped = step(0x14a, row + 157);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(runtime.mouse.pendingClick);
    EXPECT_TRUE(sent.empty());
}
} // namespace
