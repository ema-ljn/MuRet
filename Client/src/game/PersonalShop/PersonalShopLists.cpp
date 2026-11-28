#include "PersonalShop/PersonalShopLists.h"
#include "Kernel/ActorSearch.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Kernel/ActorState.h"
#include "Hud/GamePanels.h"
#include "CastleSiege/CastleWindows.h"
#include "Items/InventoryMoves.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Items/OwnItemPlacement.h"
#include "PersonalShop/PersonalShopTitles.h"
#include "Social/SocialEvents.h"
#include "Social/SocialState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/Dialogs.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"
#include "Models/Cloth.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace Client::PersonalShop
{
namespace
{
namespace Shop = Protocol::PersonalShop;

// The padding and the serial of a C3 (0x40b0a0).
constexpr std::uint32_t C3Randoms = 2;
// 0x5eb910..0x5eb92a: the shop dialogs that 0x5eb020 closes (the 0x94 with 0x78abb14 at 5).
constexpr std::int32_t ShopPriceDialog = 0x91;
constexpr std::int32_t ShopWarningDialog = 0x94;
constexpr std::int32_t ShopWarningKind = 5;
// The Text.bmd texts ((address − 0x76bd1dc) / 300).
constexpr std::size_t ShopListRefusedText = 1120; // 0x770f25c, the 3F 05 with 3
constexpr std::size_t ShopGoneText = 1166;        // 0x7712844, the 3F 06 with 6
constexpr std::size_t PurchaseSevenText = 423;    // 0x76dc190, the 3F 06 with 7
constexpr std::size_t PurchaseEightText = 375;    // 0x76d8950, the 3F 06 with 8
// 0x4db8a1 and 0x4dbcc0
constexpr std::uint8_t ListRefused = 3;
constexpr std::uint8_t Bought = 1;
constexpr std::uint8_t ShopGone = 6;
constexpr std::uint8_t PurchaseSeven = 7;
constexpr std::uint8_t PurchaseEight = 8;
// 0x4dba89..0x4dba94: the owner's character, from 0 to 399.
constexpr std::size_t KeysSearched = 400;

// 0x5be150 with what the original reads from the world.
Core::Expected<void, std::string> ReturnTheCursor(const Scenes::WorldEntryResources& resources)
{
    const Items::InventoryServices services = Items::InventoryServicesOf(resources);
    return Items::ReturnTheCursorItem(
        Items::InventoryMoveContext{.world = resources.world, .random = resources.cloth.random, .services = services});
}

// 0x4db933..0x4db951 and 0x4dbb64..0x4dbb82: in the 32 cells of 0x78ab0b8, the type at −1, +0x3c and +0x1b at 0.
void ClearTheShopGrid(Kernel::WorldState& world)
{
    for (Kernel::ItemState& cell : world.Inventory().secondCells)
    {
        cell.id = Kernel::EmptyOwnItemId;
        cell.occupied = 0;
        cell.flags1b = 0;
    }
}

// 0x4db9e2..0x4dba5a and 0x4dbc01..0x4dbc6e: 0x5bc740(0x78a9bb0, 8, 8, slot, item, 0) and, with the map, the price in
// its slot (find, erase and 0x4e7440's insert: the last stays).
Core::Expected<void, std::string> PlaceTheEntry(const Shop::ShopListEntry& entry,
                                                const Scenes::WorldEntryResources& resources)
{
    if (auto placed = Items::PlaceMovedItem(entry.slot, entry.item, resources); !placed.HasValue())
        return placed;
    Social::PersonalShopState& shops = resources.world.Runtime().social.personalShop;
    if (shops.pricesBuilt)
        shops.otherPrices[entry.slot] = entry.price;
    return {};
}

// 0x5bc740 with 0x78a9bb0 writes the footprint without bound (Q-C200): the warning of the first of `entries` that would
// overrun its grid, checked before writing anything. Without the Item.bmd record there is no warning: the placement
// stops in its place, after what the original does before.
std::optional<std::string> AnEntryPastTheGrid(std::span<const Shop::ShopListEntry> entries,
                                              const Scenes::WorldEntryResources& resources)
{
    for (const Shop::ShopListEntry& entry : entries)
    {
        auto past = Items::InventoryPlacementPastTheGrid(entry.slot, Items::InventoryItemBytes(entry.item), resources);
        if (!past.HasValue())
            return std::nullopt;
        if (past.Value())
            return past.Value();
    }
    return std::nullopt;
}

// Close(0x800, 0) and Close(0x20000, 0), like 0x4dbad2..0x4dbaf4 and 0x4dbdfe..0x4dbe20.
Core::Expected<void, std::string> CloseBothShops(const Scenes::WorldEntryResources& resources,
                                                 const Social::GamePacketSender& send)
{
    Hud::GamePanels panels(resources.world, resources.cloth.random);
    panels.LendThePersonalShops(resources.world, resources.cloth.random, send);
    for (const Ui::PanelId id : {Ui::PersonalShopPanel, Ui::OtherShopPanel})
    {
        auto closed = panels.Registry().Close(id);
        if (!closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
    }
    return {};
}

// strncpy(0x78aba64, pkt + 0x12, 0x24): up to the first 0. With none in the 36 bytes, the original does not terminate
// the text and what follows in the .bss (0x78aba88..) goes into it (*inferred*, 07 Q-C962): MuRet keeps the 36.
std::string TitleOf(const std::array<std::byte, Shop::TitleSize>& title)
{
    std::string text;
    for (const std::byte letter : title)
    {
        if (letter == std::byte{0})
            break;
        text.push_back(static_cast<char>(std::to_integer<unsigned char>(letter)));
    }
    return text;
}
} // namespace

Core::Expected<void, std::string> OpenThePersonalShop(Kernel::WorldState& world, Crt::CrtRandom& random, Ui::PanelId id,
                                                      const Social::GamePacketSender& send)
{
    Social::PersonalShopState& shops = world.Runtime().social.personalShop;
    if (shops.mode != Social::NoShopView)
    {
        // 0x65ac75 and 0x65b347: the log 0x6d67dc and the C1 04 F3 10 with the XOR32, in C3.
        if (!send)
            return Core::Unexpected(std::format("{}: el C1 04 F3 10 al abrir la tienda sin el cable",
                                                id == Ui::PersonalShopPanel ? "0x65ac87" : "0x65b359"));
        random.Discard(C3Randoms);
        send(Shop::WriteInventoryRequest());
    }
    // 0x65b194..0x65b210 and 0x65b888..0x65ba2a: the two 0x10-byte std::maps, only if both pointers are at 0.
    if (!shops.pricesBuilt)
    {
        shops.ownPrices.clear();
        shops.otherPrices.clear();
        shops.pricesBuilt = true;
    }
    shops.mode = id == Ui::PersonalShopPanel ? Social::OwnShopView : Social::OtherShopView;
    return {};
}

void ResetThePersonalShops(Social::PersonalShopState& shops)
{
    shops.ownOpen = 0;
    shops.mode = Social::NoShopView;
    shops.dialogMode = 0;
    shops.ownTitle.clear();
    shops.titles.clear();
    shops.ownPrices.clear();
    shops.otherPrices.clear();
    shops.pricesBuilt = false;
}

Core::Expected<void, std::string> CloseThePersonalShop(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                       const Social::GamePacketSender& send)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    Social::PersonalShopState& shops = runtime.social.personalShop;
    if (shops.mode == Social::OtherShopView)
    {
        // 0x5eb052..0x5eb066: the own grid comes back from its copy.
        world.Inventory().secondCells = shops.ownGridBackup;
        // 0x5eb069..0x5eb0e5: the title of the hero's sign (0x743a170) or empty.
        const auto title = shops.titles.find(runtime.hero);
        shops.ownTitle = Social::HasShopTitle(shops, runtime.hero) ? title->second.title : std::string{};
        // 0x5eb0ec..0x5eb90f: with the owner's key, C1 10 3F 07 with their key and name, in C3.
        if (shops.ownerKey != 0)
        {
            if (!send)
                return Core::Unexpected(std::string("0x5eb0fa: el 3F 07 al cerrar la tienda de otro sin el cable"));
            random.Discard(C3Randoms);
            send(Shop::WriteViewClosed(shops.ownerKey, shops.ownerName));
        }
    }
    Ui::DialogState& dialogs = runtime.dialogs;
    if (dialogs.current == ShopPriceDialog ||
        (dialogs.current == ShopWarningDialog && runtime.social.trade.warningKind == ShopWarningKind))
        Ui::PushDialog(dialogs, 0);
    shops.mode = Social::NoShopView;
    shops.ownerKey = 0;
    shops.ownerName = {};
    return {};
}

Core::Expected<void, std::string> ApplyPriceResult(const Shop::PriceResult& result,
                                                   const Scenes::WorldEntryResources& resources,
                                                   const Social::GamePacketSender& send)
{
    Kernel::WorldRuntime& runtime = resources.world.Runtime();
    Social::PersonalShopState& shops = runtime.social.personalShop;
    if (result.result == 1 || shops.mode != Social::OwnShopView)
        return {};
    if (runtime.cursorItemCount > 0)
    {
        if (auto returned = ReturnTheCursor(resources); !returned.HasValue())
            return returned;
    }
    // 0x4db16b..0x4db19d: pkt[5]'s slot leaves the own shop's map.
    if (shops.pricesBuilt)
        shops.ownPrices.erase(result.slot);
    if (!send)
        return Core::Unexpected(std::string("0x4db1a2: el C1 04 F3 10 del 3F 01 sin el cable"));
    resources.cloth.random.Discard(C3Randoms);
    send(Shop::WriteInventoryRequest());
    return {};
}

Core::Expected<void, std::string> ApplyShopItemList(const Shop::ShopItemList& list,
                                                    const Scenes::WorldEntryResources& resources,
                                                    const Social::GamePacketSender& send)
{
    if (list.result == ListRefused)
        return Social::AddSocialNotice(resources, ShopListRefusedText, Ui::ErrorMessage, "0x4dbb2f");
    // 0x4dbb04..0x4dbb14: another result, the log 0x6c8578.
    if (list.result != Shop::ListedResult)
        return {};
    // The entries that are placed (up to the first price of 0 or less), before closing or opening anything.
    const auto unpriced =
        std::ranges::find_if(list.entries, [](const Shop::ShopListEntry& entry) { return entry.price <= 0; });
    if (const auto past = AnEntryPastTheGrid(std::span(list.entries.begin(), unpriced), resources))
    {
        resources.log.Warning("se descarta el 3F 05: {}", *past);
        return {};
    }
    Kernel::WorldState& world = resources.world;
    Social::PersonalShopState& shops = world.Runtime().social.personalShop;
    Hud::GamePanels panels(world, resources.cloth.random);
    // Opening someone else's shop (0x65b319) closes the rest: the trade with the 3D and the quests with the 31.
    CastleSiege::LendTheCastlePanelServices(panels, resources, send);
    Ui::PanelRegistry& registry = panels.Registry();
    // 0x4db8a7..0x4db8e8: the warehouse and the inventory, if they are open (IsOpen of both without rand()).
    for (const Ui::PanelId id : {Ui::WarehousePanel, Ui::InventoryPanel})
    {
        if (!registry.IsOpen(id))
            continue;
        auto closed = registry.Close(id);
        if (!closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
    }
    shops.ownerKey = 0;
    shops.ownerName = {};
    // 0x4db8f9..0x4db91e: the copy of the own grid, unless the other's is already open.
    if (!registry.IsOpen(Ui::OtherShopPanel))
        shops.ownGridBackup = world.Inventory().secondCells;
    shops.ownTitle = TitleOf(list.title);
    ClearTheShopGrid(world);
    if (auto opened = registry.Open(Ui::OtherShopPanel); !opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    // 0x4db965..0x4db9ca: the other's map emptied (clear or the erase of each, the same).
    if (shops.pricesBuilt)
        shops.otherPrices.clear();
    for (const Shop::ShopListEntry& entry : list.entries)
    {
        // 0x4db9e2..0x4db9e7 and 0x4dbabf..0x4dbaf4: a price of 0 or less closes both and leaves, without the owner's
        // copy.
        if (entry.price <= 0)
            return CloseBothShops(resources, send);
        if (auto placed = PlaceTheEntry(entry, resources); !placed.HasValue())
            return placed;
    }
    // 0x4dba76..0x4dbab6: the copy of the owner's record (0x438 bytes) if 0x5110d0 finds it.
    const std::size_t owner = Kernel::ActorWithKey(world, list.key);
    if (owner >= KeysSearched)
        return {};
    const Kernel::ActorState& actor = world.Actors()[owner];
    shops.ownerKey = actor.key;
    std::ranges::copy(actor.name, shops.ownerName.begin());
    return {};
}

Core::Expected<void, std::string> ApplyShopPurchase(const Shop::ShopPurchase& purchase,
                                                    const Scenes::WorldEntryResources& resources,
                                                    const Social::GamePacketSender& send)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Social::PersonalShopState& shops = runtime.social.personalShop;
    if (purchase.result == Bought)
    {
        if (const auto past =
                AnEntryPastTheGrid(std::array{Shop::ShopListEntry{purchase.slot, purchase.item, 0}}, resources))
        {
            resources.log.Warning("se descarta el 3F 06: {}", *past);
            return {};
        }
        if (shops.mode == Social::OtherShopView)
        {
            // 0x4dbccf..0x4dbd14: 0x78a6758's slot leaves the map and the grid (0x5bd750 with 0x78a9bb0).
            if (shops.pricesBuilt)
                shops.otherPrices.erase(runtime.slot78a6758);
            const Items::InventoryServices services = Items::InventoryServicesOf(resources);
            if (auto removed = Items::RemoveInventoryItem(
                    Items::InventoryMoveContext{.world = world, .random = resources.cloth.random, .services = services},
                    runtime.slot78a6758);
                !removed.HasValue())
                return removed;
        }
        else if (shops.pricesBuilt)
        {
            // 0x4dbd3c..0x4dbdc2
            shops.otherPrices.clear();
        }
        // 0x4dbd19..0x4dbd2e and 0x4dbdc5..0x4dbdda
        return Items::PlaceMovedItem(purchase.slot, purchase.item, resources);
    }
    if (purchase.result == ShopGone)
    {
        if (auto shown = Social::AddSocialNotice(resources, ShopGoneText, Ui::ErrorMessage, "0x4dbdf9");
            !shown.HasValue())
            return shown;
        return CloseBothShops(resources, send);
    }
    // 0x4dbe2b..0x4dbe8b: 7 and 8 with their text; another, the log 0x6c85e0. All three, 0x5be150.
    if (purchase.result == PurchaseSeven || purchase.result == PurchaseEight)
    {
        const std::size_t text = purchase.result == PurchaseSeven ? PurchaseSevenText : PurchaseEightText;
        if (auto shown = Social::AddSocialNotice(resources, text, Ui::ErrorMessage, "0x4dbe64"); !shown.HasValue())
            return shown;
    }
    return ReturnTheCursor(resources);
}

Core::Expected<void, std::string> ApplyShopRefresh(const Shop::ShopRefresh& refresh,
                                                   const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Social::PersonalShopState& shops = world.Runtime().social.personalShop;
    // 0x4dbc8f: otherwise, the log 0x6c85b0.
    if (refresh.result != Shop::ListedResult || shops.mode != Social::OtherShopView)
        return {};
    if (const auto past = AnEntryPastTheGrid(refresh.entries, resources))
    {
        resources.log.Warning("se descarta el 3F 0x13: {}", *past);
        return {};
    }
    ClearTheShopGrid(world);
    if (shops.pricesBuilt)
        shops.otherPrices.clear();
    for (const Shop::ShopListEntry& entry : refresh.entries)
    {
        if (auto placed = PlaceTheEntry(entry, resources); !placed.HasValue())
            return placed;
    }
    return {};
}
} // namespace Client::PersonalShop
