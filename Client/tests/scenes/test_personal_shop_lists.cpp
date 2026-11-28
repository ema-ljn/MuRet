#include "Network/Xor32Packet.h"
#include "PersonalShop/PersonalShopLists.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Network/Delivery.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Hud/GamePanels.h"
#include "Items/InventoryMoves.h"
#include "FrontEnd/LoginModels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Items/OwnItemPlacement.h"
#include "FrontEnd/SelectionState.h"
#include "Social/SocialEvents.h"
#include "Social/SocialState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "FrontEnd/SelectionInventory.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"
#include "Camera/GameCamera.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"

#include "support/ItemFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

// PersonalShop/PersonalShopLists (CL9 milestone 7, phase 4): opening and closing the personal shops (0x6599a0 with
// 0x800 and 0x20000, 0x5eb020) and the 3F lists (0x4db120, 0x4db890, 0x4dbcb0 and 0x4dbb40), against the ASM.
// GameServer 0.97 has no 3F: the packets come from the client. Names, titles and texts made up; each case in Lorencia
// and in Devias.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
namespace Shop = Client::Protocol::PersonalShop;

constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::size_t Hero = 0;
constexpr std::size_t Seller = 7;
constexpr std::uint16_t HeroKey = 0x0101;
constexpr std::uint16_t SellerKey = 0x0234;
// An Item.bmd type of 1 by 2 and another of 1 by 1 (synthetic).
constexpr std::uint16_t TallType = 0x001;
constexpr std::uint16_t SmallType = 0x003;
constexpr std::uint8_t FirstShopSlot = 0x4c;
// The queries of the protected bytes of 0x10000 and 0x20 (one rand() each) and the padding and serial of a C3.
constexpr std::uint32_t C3Randoms = 2;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

Client::Protocol::Items::ItemData ItemBytes(std::uint16_t type, std::uint8_t level = 0)
{
    return {static_cast<std::byte>(type & 0xff),
            static_cast<std::byte>(level << 3),
            std::byte{20},
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

std::array<std::byte, Shop::TitleSize> TitleOf(std::string_view text)
{
    std::array<std::byte, Shop::TitleSize> title{};
    for (std::size_t at = 0; at < text.size() && at < title.size(); ++at)
        title[at] = static_cast<std::byte>(text[at]);
    return title;
}

class PersonalShopListsTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    PersonalShopListsTest()
    {
        WorldState& world = *m_world;
        Client::FrontEnd::ClearSelectionInventory(world, m_cloth.random);
        world.Runtime().map = GetParam();
        world.Runtime().hero = Hero;
        Place(Hero, HeroKey, "Tejon");
        Place(Seller, SellerKey, "Brezal");
        // 0x5be150 dresses the hero at the end (0x512a30), which reads Player.bmd's actions.
        m_models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.OwnCharacter().ClearEquipmentForEntry();
        // The own record open around everything: 0x5bc740 with the last argument at 0 opens and closes it on each item
        // (3848 rand() if left without accesses), and here only the panels and the sends count.
        world.Runtime().ownRecordDepth = 1;
        // The inventory open (0x7d53717), as Open(0x800) requires (0x6598a0).
        world.Runtime().panels.inventory = 1;
        for (ItemState& cell : world.Inventory().cells)
            cell.id = Client::Kernel::EmptyOwnItemId;
        for (ItemState& cell : world.Inventory().secondCells)
            cell.id = Client::Kernel::EmptyOwnItemId;
        // 0x78a6758 outside the equipment: 0x5bc740 does not open the record for the speeds.
        world.Runtime().slot78a6758 = FirstShopSlot;
    }

    void Place(std::size_t index, std::uint16_t key, std::string_view name)
    {
        Client::Kernel::ActorState& actor = m_world->Actors()[index];
        actor.alive = true;
        actor.seen = true;
        actor.key = key;
        actor.byte88 = 1;
        for (std::size_t at = 0; at < name.size(); ++at)
            actor.name[at] = static_cast<std::byte>(name[at]);
    }

    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = *m_world,
                .selection = m_selection,
                .dataRoot = {},
                .cloth = m_cloth,
                .camera = m_camera,
                .cameraInput = m_input,
                .models = m_models,
                .items = &m_items,
                .log = m_log,
                .textMeasure = &m_letters,
                .texts = &m_texts};
    }

    Client::Social::GamePacketSender Sender()
    {
        return [this](Client::Protocol::Outgoing outgoing) { m_sent.push_back(std::move(outgoing)); };
    }

    Client::Social::PersonalShopState& Shops()
    {
        return m_world->Runtime().social.personalShop;
    }

    // Open or Close of a panel as the original calls it (0x6599a0 and 0x65cae0), with the wire.
    void OpenPanel(Client::Ui::PanelId id)
    {
        Client::Hud::GamePanels panels(*m_world, m_cloth.random);
        panels.LendThePersonalShops(*m_world, m_cloth.random, Sender());
        const auto opened = panels.Registry().Open(id);
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        EXPECT_TRUE(opened.Value());
    }
    void ClosePanel(Client::Ui::PanelId id)
    {
        Client::Hud::GamePanels panels(*m_world, m_cloth.random);
        panels.LendThePersonalShops(*m_world, m_cloth.random, Sender());
        const auto closed = panels.Registry().Close(id);
        ASSERT_TRUE(closed.HasValue()) << closed.Error();
    }

    void ExpectDraws(const CrtRandom& before, std::uint32_t draws)
    {
        CrtRandom expected = before;
        expected.Discard(draws);
        EXPECT_EQ(m_cloth.random.State(), expected.State()) << "se esperaban " << draws << " rand()";
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    Client::FrontEnd::SelectionState m_selection{*m_world};
    Client::Camera::GameCamera m_camera;
    Client::Camera::GameCameraInput m_input;
    Client::Models::ClothWorld m_cloth;
    Client::FrontEnd::SingleModelLibrary m_models;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Tables::ItemTable m_items = Client::Tests::ItemTableOf({{TallType, 1, 2, 0}, {SmallType, 1, 1, 0}});
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable({{375, "no alcanza el zen"},
                                                                      {423, "no hay lugar"},
                                                                      {1120, "la tienda no se puede ver"},
                                                                      {1166, "la tienda ya no esta"}});
    LetterMeasure m_letters;
    std::vector<Client::Protocol::Outgoing> m_sent;
};

TEST_P(PersonalShopListsTest, OpenTheOwnShopAndViewAnother)
{
    WorldState& world = *m_world;
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    Client::Social::PersonalShopState& shops = Shops();
    // An own item in the shop's first cell (slot 0x4c) and the hero's sign (3F 02).
    ASSERT_TRUE(Client::Items::PlaceInventoryItem(FirstShopSlot, ItemBytes(SmallType), Resources()).HasValue());
    shops.titles[Hero] = Client::Social::ShopTitle{.key = HeroKey, .name = "Tejon", .title = "Mi puesto"};

    // 0x6599a0(0x800): CanOpen queries 0x10000 and 0x20 (0x6598a0) and CloseByMask, with everything but the shop, the
    // inventory and the friends, queries them again (0x657ab0). Open(0x4000000) is already open. 0x6c931c at −1: no
    // F3 10.
    CrtRandom before = m_cloth.random;
    OpenPanel(Client::Ui::PersonalShopPanel);
    ExpectDraws(before, 4);
    EXPECT_EQ(runtime.panels.personalShop, 1);
    EXPECT_EQ(runtime.panels.inventory, 1);
    EXPECT_EQ(shops.mode, Client::Social::OwnShopView);
    EXPECT_TRUE(shops.pricesBuilt);
    EXPECT_TRUE(m_sent.empty());

    // 0x4db120 (3F 01) with 1: nothing. With 0 and 0x6c931c at 1: the slot leaves the own map and the F3 10 in C3.
    shops.ownPrices[FirstShopSlot] = 500;
    before = m_cloth.random;
    ASSERT_TRUE(
        Client::PersonalShop::ApplyPriceResult({.result = 1, .slot = FirstShopSlot}, Resources(), Sender()).HasValue());
    EXPECT_EQ(shops.ownPrices.size(), 1u);
    ASSERT_TRUE(
        Client::PersonalShop::ApplyPriceResult({.result = 0, .slot = FirstShopSlot}, Resources(), Sender()).HasValue());
    ExpectDraws(before, C3Randoms);
    EXPECT_TRUE(shops.ownPrices.empty());
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(PlainBytes(m_sent[0]), (std::vector<std::uint8_t>{0xc1, 0x04, 0xf3, 0x10}));
    EXPECT_EQ(m_sent[0].delivery, Client::Network::Delivery::Encrypted);

    // 0x65cae0(0x800): 0x5eb020 with 0x6c931c at 1 only sets it back to −1; no rand().
    before = m_cloth.random;
    ClosePanel(Client::Ui::PersonalShopPanel);
    ExpectDraws(before, 0);
    EXPECT_EQ(runtime.panels.personalShop, 0);
    EXPECT_EQ(shops.mode, Client::Social::NoShopView);
    EXPECT_EQ(runtime.panels.inventory, 1);

    // 0x4db890 (3F 05) from the seller, with two items. Close(0x4000000): its companions (0x657ab0 with 0x11d9e0, which
    // queries 0x10000 and 0x20) and the inventory; Open(0x20000): CanOpen and CloseByMask (four queries) and
    // Open(0x4000000) with its own (four more). 0x6c931c at −1: no F3 10.
    Shop::ShopItemList list{};
    list.result = Shop::ListedResult;
    list.key = SellerKey;
    list.title = TitleOf("Cosas baratas");
    list.entries = {{.slot = FirstShopSlot + 9, .item = ItemBytes(TallType, 2), .price = 1500},
                    {.slot = FirstShopSlot + 3, .item = ItemBytes(SmallType), .price = 200}};
    before = m_cloth.random;
    const auto viewed = Client::PersonalShop::ApplyShopItemList(list, Resources(), Sender());
    ASSERT_TRUE(viewed.HasValue()) << viewed.Error();
    ExpectDraws(before, 10);
    EXPECT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(runtime.panels.personalShop, 1);
    EXPECT_EQ(runtime.panels.inventory, 1);
    EXPECT_EQ(shops.mode, Client::Social::OtherShopView);
    EXPECT_EQ(shops.ownTitle, "Cosas baratas");
    EXPECT_EQ(shops.ownGridBackup[0].id, SmallType);
    EXPECT_EQ(shops.ownGridBackup[0].occupied, 1u);
    const auto& grid = world.Inventory().secondCells;
    EXPECT_EQ(grid[0].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(grid[9].id, TallType);
    EXPECT_EQ(grid[9].occupied, 1u);
    EXPECT_EQ(grid[17].id, TallType);
    EXPECT_EQ(grid[17].occupied, 0u);
    EXPECT_EQ(grid[3].id, SmallType);
    EXPECT_EQ(shops.otherPrices,
              (std::map<std::int32_t, std::int32_t>{{FirstShopSlot + 3, 200}, {FirstShopSlot + 9, 1500}}));
    EXPECT_EQ(shops.ownerKey, SellerKey);
    EXPECT_EQ(shops.ownerName[0], std::byte{'B'});
    EXPECT_EQ(shops.ownerName[5], std::byte{'l'});

    // 0x4dbcb0 (3F 06) with 1 and 0x6c931c at 2: the chosen slot (0x78a6758) leaves the map and the grid and the bought
    // item goes to its inventory slot (12 + 5).
    runtime.slot78a6758 = FirstShopSlot + 3;
    before = m_cloth.random;
    const auto bought = Client::PersonalShop::ApplyShopPurchase(
        {.result = 1, .item = ItemBytes(SmallType), .slot = 12 + 5}, Resources(), Sender());
    ASSERT_TRUE(bought.HasValue()) << bought.Error();
    ExpectDraws(before, 0);
    EXPECT_EQ(grid[3].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(world.Inventory().cells[5].id, SmallType);
    EXPECT_EQ(shops.otherPrices, (std::map<std::int32_t, std::int32_t>{{FirstShopSlot + 9, 1500}}));

    // 0x4dbb40 (3F 0x13): the grid and the map again, without looking at the price.
    const Shop::ShopRefresh refresh{.result = Shop::ListedResult,
                                    .entries = {{.slot = FirstShopSlot + 1, .item = ItemBytes(SmallType), .price = 0}}};
    ASSERT_TRUE(Client::PersonalShop::ApplyShopRefresh(refresh, Resources()).HasValue());
    EXPECT_EQ(grid[9].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(grid[1].id, SmallType);
    EXPECT_EQ(shops.otherPrices, (std::map<std::int32_t, std::int32_t>{{FirstShopSlot + 1, 0}}));

    // 0x65cae0(0x20000): 0x5eb020 with 0x6c931c at 2 restores the own grid and the hero's sign's title, and sends the
    // 3F 07 with the owner's key and name in C3.
    before = m_cloth.random;
    ClosePanel(Client::Ui::OtherShopPanel);
    ExpectDraws(before, C3Randoms);
    EXPECT_EQ(runtime.panels.personalShop, 0);
    EXPECT_EQ(shops.mode, Client::Social::NoShopView);
    EXPECT_EQ(grid[0].id, SmallType);
    EXPECT_EQ(grid[1].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(shops.ownTitle, "Mi puesto");
    EXPECT_EQ(shops.ownerKey, 0);
    ASSERT_EQ(m_sent.size(), 2u);
    EXPECT_EQ(PlainBytes(m_sent[1]), (std::vector<std::uint8_t>{0xc1, 0x10, 0x3f, 0x07, 0x02, 0x34, 'B', 'r', 'e', 'z',
                                                                'a', 'l', 0x00, 0x00, 0x00, 0x00}));
    EXPECT_EQ(m_sent[1].delivery, Client::Network::Delivery::Encrypted);
}

// 0x4db890: a price of 0 or less closes both and leaves without the owner's copy; with 3, text 1120 with type 2.
TEST_P(PersonalShopListsTest, ABadListClosesTheShopOrSaysIt)
{
    // An own item in the shop's cell 5: 0x5eb020 restores the grid from the copy on closing.
    ASSERT_TRUE(Client::Items::PlaceInventoryItem(FirstShopSlot + 5, ItemBytes(SmallType), Resources()).HasValue());
    Shop::ShopItemList list{};
    list.result = Shop::ListedResult;
    list.key = SellerKey;
    list.entries = {{.slot = FirstShopSlot, .item = ItemBytes(SmallType), .price = 100},
                    {.slot = FirstShopSlot + 2, .item = ItemBytes(SmallType), .price = 0},
                    {.slot = FirstShopSlot + 4, .item = ItemBytes(SmallType), .price = 300}};
    const auto closed = Client::PersonalShop::ApplyShopItemList(list, Resources(), Sender());
    ASSERT_TRUE(closed.HasValue()) << closed.Error();
    // What was placed before the price at 0 goes away with the copy's grid.
    const auto& grid = m_world->Inventory().secondCells;
    EXPECT_EQ(grid[0].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(grid[5].id, SmallType);
    EXPECT_EQ(Shops().otherPrices, (std::map<std::int32_t, std::int32_t>{{FirstShopSlot, 100}}));
    EXPECT_EQ(m_world->Runtime().panels.personalShop, 0);
    EXPECT_EQ(Shops().mode, Client::Social::NoShopView);
    // The owner was never copied: 0x5eb020 does not send the 3F 07.
    EXPECT_TRUE(m_sent.empty());

    list.result = 3;
    ASSERT_TRUE(Client::PersonalShop::ApplyShopItemList(list, Resources(), Sender()).HasValue());
    const auto& entries = m_world->Runtime().chat.log.entries;
    ASSERT_FALSE(entries.empty());
    EXPECT_EQ(entries.front().text, "la tienda no se puede ver");
    EXPECT_EQ(entries.front().type, Client::Ui::ErrorMessage);
}

// 07 Q-C962: strncpy(0x78aba64, pkt + 0x12, 0x24) does not terminate a 36-byte title without a 0, and the original
// follows it with whatever is in the .bss from 0x78aba88. MuRet keeps the 36.
TEST_P(PersonalShopListsTest, AFullTitleKeepsItsThirtySixBytes)
{
    Shop::ShopItemList list{};
    list.result = Shop::ListedResult;
    list.key = SellerKey;
    list.title.fill(std::byte{'x'});
    list.entries = {{.slot = FirstShopSlot, .item = ItemBytes(SmallType), .price = 100}};
    const auto viewed = Client::PersonalShop::ApplyShopItemList(list, Resources(), Sender());
    ASSERT_TRUE(viewed.HasValue()) << viewed.Error();
    EXPECT_EQ(Shops().ownTitle, std::string(Shop::TitleSize, 'x'));
}

// 0x4dbcb0: 6 warns and closes; 7 and 8 warn and return the cursor item; another only returns it.
TEST_P(PersonalShopListsTest, TheFailedPurchasesSayWhy)
{
    const auto& entries = m_world->Runtime().chat.log.entries;
    ASSERT_TRUE(Client::PersonalShop::ApplyShopPurchase({.result = 7}, Resources(), Sender()).HasValue());
    EXPECT_EQ(entries.front().text, "no hay lugar");
    ASSERT_TRUE(Client::PersonalShop::ApplyShopPurchase({.result = 8}, Resources(), Sender()).HasValue());
    EXPECT_EQ(entries.front().text, "no alcanza el zen");
    const std::size_t said = entries.size();
    ASSERT_TRUE(Client::PersonalShop::ApplyShopPurchase({.result = 2}, Resources(), Sender()).HasValue());
    EXPECT_EQ(entries.size(), said);
    ASSERT_TRUE(Client::PersonalShop::ApplyShopPurchase({.result = 6}, Resources(), Sender()).HasValue());
    EXPECT_EQ(entries.front().text, "la tienda ya no esta");
    EXPECT_EQ(entries.front().type, Client::Ui::ErrorMessage);
}

// 0x4dbb40 outside 0x6c931c at 2 and 0x4db120 outside 1 touch nothing; before the first Open, the maps do not exist and
// the 3F 05 places the items without the prices (Open(0x20000) does not open with the trade open).
TEST_P(PersonalShopListsTest, WithoutTheMapsThePricesAreLost)
{
    const Shop::ShopRefresh refresh{.result = Shop::ListedResult,
                                    .entries = {{.slot = FirstShopSlot, .item = ItemBytes(SmallType), .price = 50}}};
    ASSERT_TRUE(Client::PersonalShop::ApplyShopRefresh(refresh, Resources()).HasValue());
    EXPECT_EQ(m_world->Inventory().secondCells[0].id, Client::Kernel::EmptyOwnItemId);
    ASSERT_TRUE(
        Client::PersonalShop::ApplyPriceResult({.result = 0, .slot = FirstShopSlot}, Resources(), {}).HasValue());

    m_world->Runtime().panels.trade = 1;
    Shop::ShopItemList list{};
    list.result = Shop::ListedResult;
    list.key = SellerKey;
    list.entries = {{.slot = FirstShopSlot + 6, .item = ItemBytes(SmallType), .price = 70}};
    // With the trade open (and the inventory closed), CanOpen does not let 0x20000 open.
    m_world->Runtime().panels.inventory = 0;
    const auto listed = Client::PersonalShop::ApplyShopItemList(list, Resources(), Sender());
    ASSERT_TRUE(listed.HasValue()) << listed.Error();
    EXPECT_EQ(m_world->Inventory().secondCells[6].id, SmallType);
    EXPECT_FALSE(Shops().pricesBuilt);
    EXPECT_TRUE(Shops().otherPrices.empty());
    EXPECT_EQ(Shops().mode, Client::Social::NoShopView);
    EXPECT_EQ(Shops().ownerKey, SellerKey);
}

// 0x4c5ec0 (28) from the 0x4c: with 0x6c931c at 2, the copy of the own grid; otherwise, the cursor item lifted from
// that cell goes back first (0x5be150 with 0x78ab0b8) and the cell stays empty.
TEST_P(PersonalShopListsTest, TheDeleteOfAShopSlotLooksAtTheMode)
{
    WorldState& world = *m_world;
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    Client::Social::PersonalShopState& shops = Shops();
    shops.ownGridBackup[10].id = TallType;
    shops.ownGridBackup[10].gridColumn = 2;
    shops.ownGridBackup[10].gridRow = 1;
    shops.ownGridBackup[18] = shops.ownGridBackup[10];
    shops.mode = Client::Social::OtherShopView;
    namespace Inventory = Client::Protocol::Items;
    ASSERT_TRUE(
        Client::Items::ApplyItemDelete(Inventory::ItemDelete{.slot = FirstShopSlot + 10, .endsTheWait = 0}, Resources())
            .HasValue());
    EXPECT_EQ(shops.ownGridBackup[10].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(shops.ownGridBackup[18].id, Client::Kernel::EmptyOwnItemId);

    // Lifted from the shop's cell 4: it goes back (0x5be3eb) and the 28 empties it.
    shops.mode = Client::Social::NoShopView;
    runtime.cursorItem.id = SmallType;
    runtime.cursorItem.gridColumn = 4;
    runtime.cursorItem.gridRow = 0;
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Kernel::CursorGrid::PersonalShop;
    runtime.slot78a6758 = FirstShopSlot + 4;
    const auto deleted =
        Client::Items::ApplyItemDelete(Inventory::ItemDelete{.slot = FirstShopSlot + 4, .endsTheWait = 1}, Resources());
    ASSERT_TRUE(deleted.HasValue()) << deleted.Error();
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(world.Inventory().secondCells[4].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1d});
}

// 0x5be150 with the shop's grid (0x5be3eb, from the slot − 0x4c) and the trade's own one (0x5be191).
TEST_P(PersonalShopListsTest, TheCursorItemGoesBackToItsGrid)
{
    WorldState& world = *m_world;
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    // The services keep references to the resources: they must live the whole test.
    const Client::Scenes::WorldEntryResources resources = Resources();
    const auto services = Client::Items::InventoryServicesOf(resources);
    const Client::Items::InventoryMoveContext context{.world = world, .random = m_cloth.random, .services = services};
    runtime.cursorItem.id = TallType;
    runtime.cursorItem.gridColumn = 5;
    runtime.cursorItem.gridRow = 2;
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Kernel::CursorGrid::PersonalShop;
    runtime.slot78a6758 = FirstShopSlot + 21;
    ASSERT_TRUE(Client::Items::ReturnTheCursorItem(context).HasValue());
    EXPECT_EQ(world.Inventory().secondCells[21].id, TallType);
    EXPECT_EQ(world.Inventory().secondCells[21].occupied, 1u);
    EXPECT_EQ(world.Inventory().secondCells[29].occupied, 0u);
    EXPECT_EQ(runtime.cursorItemCount, 0);

    Client::Social::TradeState& trade = runtime.social.trade;
    for (ItemState& cell : trade.ownGrid)
        cell.id = Client::Kernel::EmptyOwnItemId;
    runtime.cursorItem.gridColumn = 1;
    runtime.cursorItem.gridRow = 0;
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Kernel::CursorGrid::OwnTrade;
    runtime.slot78a6758 = 1;
    ASSERT_TRUE(Client::Items::ReturnTheCursorItem(context).HasValue());
    EXPECT_EQ(trade.ownGrid[1].id, TallType);
    EXPECT_EQ(trade.ownGrid[1].occupied, 1u);
    EXPECT_EQ(trade.ownGrid[9].id, TallType);
    EXPECT_EQ(trade.ownGrid[9].occupied, 0u);
}

// 0x5bc740 with 0x78a9bb0 writes the footprint without bound (Q-C200): a 3F 05 with a 1 by 2 item in the shop grid's
// last row is dropped whole, before closing or opening anything and without rand(); what is behind a price of 0 does
// not count. The same for a 3F 06 that passes inventory cell 63 and a 3F 0x13 that passes the shop's cell 31, or that
// starts at slot 116.
TEST_P(PersonalShopListsTest, AnEntryPastItsGridDiscardsThePacket)
{
    WorldState& world = *m_world;
    Shop::ShopItemList list{};
    list.result = Shop::ListedResult;
    list.key = SellerKey;
    list.title = TitleOf("Cosas raras");
    list.entries = {{.slot = FirstShopSlot, .item = ItemBytes(SmallType), .price = 100},
                    {.slot = FirstShopSlot + 28, .item = ItemBytes(TallType), .price = 100}};
    CrtRandom before = m_cloth.random;
    const auto listed = Client::PersonalShop::ApplyShopItemList(list, Resources(), Sender());
    ASSERT_TRUE(listed.HasValue()) << listed.Error();
    ExpectDraws(before, 0);
    EXPECT_EQ(world.Runtime().panels.personalShop, 0);
    EXPECT_EQ(world.Runtime().panels.inventory, 1);
    EXPECT_EQ(Shops().mode, Client::Social::NoShopView);
    EXPECT_TRUE(Shops().ownTitle.empty());
    EXPECT_EQ(world.Inventory().secondCells[0].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_TRUE(m_sent.empty());
    ASSERT_FALSE(m_sink.lines.empty());
    EXPECT_NE(m_sink.lines.back().find("3F 05"), std::string::npos) << m_sink.lines.back();
    EXPECT_NE(m_sink.lines.back().find("casilla 36"), std::string::npos) << m_sink.lines.back();
    EXPECT_NE(m_sink.lines.back().find("Q-C200"), std::string::npos) << m_sink.lines.back();

    // Nothing is placed behind a price of 0: the list applies (the panels' Open and Close draw rand()) and closes both.
    std::size_t warnings = m_sink.lines.size();
    list.entries[0].price = 0;
    before = m_cloth.random;
    ASSERT_TRUE(Client::PersonalShop::ApplyShopItemList(list, Resources(), Sender()).HasValue());
    EXPECT_EQ(m_sink.lines.size(), warnings);
    EXPECT_NE(m_cloth.random.State(), before.State());
    EXPECT_EQ(Shops().mode, Client::Social::NoShopView);

    // 3F 06 to inventory cell 60 (slot 72): the 68 passes the 64. Not even the map is emptied.
    Shops().pricesBuilt = true;
    Shops().otherPrices = {{FirstShopSlot + 3, 5}};
    before = m_cloth.random;
    const auto bought = Client::PersonalShop::ApplyShopPurchase(
        {.result = 1, .item = ItemBytes(TallType), .slot = 12 + 60}, Resources(), Sender());
    ASSERT_TRUE(bought.HasValue()) << bought.Error();
    ExpectDraws(before, 0);
    EXPECT_EQ(world.Inventory().cells[60].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(Shops().otherPrices, (std::map<std::int32_t, std::int32_t>{{FirstShopSlot + 3, 5}}));
    EXPECT_NE(m_sink.lines.back().find("3F 06"), std::string::npos) << m_sink.lines.back();
    EXPECT_NE(m_sink.lines.back().find("0x78a9bb0"), std::string::npos) << m_sink.lines.back();

    // 3F 0x13 with the other's in view: neither the grid nor the map is emptied.
    Shops().mode = Client::Social::OtherShopView;
    world.Inventory().secondCells[1].id = SmallType;
    for (const std::uint8_t slot : {static_cast<std::uint8_t>(FirstShopSlot + 31), std::uint8_t{116}})
    {
        warnings = m_sink.lines.size();
        const std::uint16_t type = slot == 116 ? SmallType : TallType;
        const Shop::ShopRefresh refresh{.result = Shop::ListedResult,
                                        .entries = {{.slot = slot, .item = ItemBytes(type), .price = 9}}};
        ASSERT_TRUE(Client::PersonalShop::ApplyShopRefresh(refresh, Resources()).HasValue());
        EXPECT_EQ(world.Inventory().secondCells[1].id, SmallType);
        EXPECT_EQ(Shops().otherPrices, (std::map<std::int32_t, std::int32_t>{{FirstShopSlot + 3, 5}}));
        ASSERT_EQ(m_sink.lines.size(), warnings + 1);
        EXPECT_NE(m_sink.lines.back().find("3F 0x13"), std::string::npos) << m_sink.lines.back();
        EXPECT_NE(m_sink.lines.back().find("0x78ab0b8"), std::string::npos) << m_sink.lines.back();
    }
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, PersonalShopListsTest, ::testing::Values(Lorencia, Devias));
} // namespace
