#include "Scenes/PersonalShopLists.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Protocol/PersonalShop.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GamePanels.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/PersonalShopTitles.h"
#include "Scenes/SocialEvents.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace Client::Scenes
{
namespace
{
namespace Shop = Protocol::PersonalShop;

// El relleno y el serial de un C3 (0x40b0a0).
constexpr std::uint32_t C3Randoms = 2;
// 0x5eb910..0x5eb92a: los diálogos de la tienda que cierra 0x5eb020 (el 0x94 con 0x78abb14 en 5).
constexpr std::int32_t ShopPriceDialog = 0x91;
constexpr std::int32_t ShopWarningDialog = 0x94;
constexpr std::int32_t ShopWarningKind = 5;
// Los textos de Text.bmd ((dirección − 0x76bd1dc) / 300).
constexpr std::size_t ShopListRefusedText = 1120; // 0x770f25c, el 3F 05 con 3
constexpr std::size_t ShopGoneText = 1166;        // 0x7712844, el 3F 06 con 6
constexpr std::size_t PurchaseSevenText = 423;    // 0x76dc190, el 3F 06 con 7
constexpr std::size_t PurchaseEightText = 375;    // 0x76d8950, el 3F 06 con 8
// 0x4db8a1 y 0x4dbcc0: los resultados.
constexpr std::uint8_t ListRefused = 3;
constexpr std::uint8_t Bought = 1;
constexpr std::uint8_t ShopGone = 6;
constexpr std::uint8_t PurchaseSeven = 7;
constexpr std::uint8_t PurchaseEight = 8;
// 0x4dba89..0x4dba94: el personaje del dueño, del 0 al 399.
constexpr std::size_t KeysSearched = 400;

// 0x5be150 con lo que el original lee del mundo.
Core::Expected<void, std::string> ReturnTheCursor(const WorldEntryResources& resources)
{
    const InventoryServices services = InventoryServicesOf(resources);
    return ReturnTheCursorItem(
        InventoryMoveContext{.world = resources.world, .random = resources.cloth.random, .services = services});
}

// 0x4db933..0x4db951 y 0x4dbb64..0x4dbb82: en las 32 casillas de 0x78ab0b8, el tipo en −1, +0x3c y +0x1b en 0.
void ClearTheShopGrid(WorldState& world)
{
    for (World::ItemState& cell : world.Inventory().secondCells)
    {
        cell.id = EmptyOwnItemId;
        cell.occupied = 0;
        cell.flags1b = 0;
    }
}

// 0x4db9e2..0x4dba5a y 0x4dbc01..0x4dbc6e: 0x5bc740(0x78a9bb0, 8, 8, lugar, ítem, 0) y, con el mapa, el precio en su
// lugar (find, erase y el insert de 0x4e7440: queda el último).
Core::Expected<void, std::string> PlaceTheEntry(const Shop::ShopListEntry& entry, const WorldEntryResources& resources)
{
    if (auto placed = PlaceMovedItem(entry.slot, entry.item, resources); !placed.HasValue())
        return placed;
    PersonalShopState& shops = resources.world.Runtime().social.personalShop;
    if (shops.pricesBuilt)
        shops.otherPrices[entry.slot] = entry.price;
    return {};
}

// 0x5bc740 con 0x78a9bb0 escribe la huella sin tope (Q-C200): el aviso de la primera de `entries` que pasaría su
// grilla, mirado antes de escribir nada. Sin el registro de Item.bmd no hay aviso: la puesta corta en su lugar, después
// de lo que el original hace antes.
std::optional<std::string> AnEntryPastTheGrid(std::span<const Shop::ShopListEntry> entries,
                                              const WorldEntryResources& resources)
{
    for (const Shop::ShopListEntry& entry : entries)
    {
        auto past = InventoryPlacementPastTheGrid(entry.slot, InventoryItemBytes(entry.item), resources);
        if (!past.HasValue())
            return std::nullopt;
        if (past.Value())
            return past.Value();
    }
    return std::nullopt;
}

// Close(0x800, 0) y Close(0x20000, 0), como 0x4dbad2..0x4dbaf4 y 0x4dbdfe..0x4dbe20.
Core::Expected<void, std::string> CloseBothShops(const WorldEntryResources& resources, const GamePacketSender& send)
{
    GamePanels panels(resources.world, resources.cloth.random);
    panels.LendThePersonalShops(resources.world, resources.cloth.random, send);
    for (const UI::PanelId id : {UI::PersonalShopPanel, UI::OtherShopPanel})
    {
        auto closed = panels.Registry().Close(id);
        if (!closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
    }
    return {};
}

// strncpy(0x78aba64, pkt + 0x12, 0x24): hasta el primer 0. Sin ninguno en los 36 bytes, el original no termina el
// texto y lo que sigue en la .bss (0x78aba88..) entra en él (*inferido*, 07 Q-C962): MuRet se queda con los 36.
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

Core::Expected<void, std::string> OpenThePersonalShop(WorldState& world, Render::Models::CrtRandom& random,
                                                      UI::PanelId id, const GamePacketSender& send)
{
    PersonalShopState& shops = world.Runtime().social.personalShop;
    if (shops.mode != NoShopView)
    {
        // 0x65ac75 y 0x65b347: el log 0x6d67dc y el C1 04 F3 10 con el XOR32, en C3.
        if (!send)
            return Core::Unexpected(std::format("{}: el C1 04 F3 10 al abrir la tienda sin el cable",
                                                id == UI::PersonalShopPanel ? "0x65ac87" : "0x65b359"));
        random.Discard(C3Randoms);
        send(Shop::WriteInventoryRequest());
    }
    // 0x65b194..0x65b210 y 0x65b888..0x65ba2a: los dos std::map de 0x10 bytes, solo si los dos punteros están en 0.
    if (!shops.pricesBuilt)
    {
        shops.ownPrices.clear();
        shops.otherPrices.clear();
        shops.pricesBuilt = true;
    }
    shops.mode = id == UI::PersonalShopPanel ? OwnShopView : OtherShopView;
    return {};
}

Core::Expected<void, std::string> CloseThePersonalShop(WorldState& world, Render::Models::CrtRandom& random,
                                                       const GamePacketSender& send)
{
    WorldRuntime& runtime = world.Runtime();
    PersonalShopState& shops = runtime.social.personalShop;
    if (shops.mode == OtherShopView)
    {
        // 0x5eb052..0x5eb066: la grilla propia vuelve de su copia.
        world.Inventory().secondCells = shops.ownGridBackup;
        // 0x5eb069..0x5eb0e5: el título del cartel del héroe (0x743a170) o vacío.
        const auto title = shops.titles.find(runtime.hero);
        shops.ownTitle = HasShopTitle(shops, runtime.hero) ? title->second.title : std::string{};
        // 0x5eb0ec..0x5eb90f: con la clave del dueño, C1 10 3F 07 con su clave y su nombre, en C3.
        if (shops.ownerKey != 0)
        {
            if (!send)
                return Core::Unexpected(std::string("0x5eb0fa: el 3F 07 al cerrar la tienda de otro sin el cable"));
            random.Discard(C3Randoms);
            send(Shop::WriteViewClosed(shops.ownerKey, shops.ownerName));
        }
    }
    UI::DialogState& dialogs = runtime.dialogs;
    if (dialogs.current == ShopPriceDialog ||
        (dialogs.current == ShopWarningDialog && runtime.social.trade.warningKind == ShopWarningKind))
        UI::PushDialog(dialogs, 0);
    shops.mode = NoShopView;
    shops.ownerKey = 0;
    shops.ownerName = {};
    return {};
}

Core::Expected<void, std::string> ApplyPriceResult(const Shop::PriceResult& result,
                                                   const WorldEntryResources& resources, const GamePacketSender& send)
{
    WorldRuntime& runtime = resources.world.Runtime();
    PersonalShopState& shops = runtime.social.personalShop;
    if (result.result == 1 || shops.mode != OwnShopView)
        return {};
    if (runtime.cursorItemCount > 0)
    {
        if (auto returned = ReturnTheCursor(resources); !returned.HasValue())
            return returned;
    }
    // 0x4db16b..0x4db19d: el lugar de pkt[5] sale del mapa de la tienda propia.
    if (shops.pricesBuilt)
        shops.ownPrices.erase(result.slot);
    if (!send)
        return Core::Unexpected(std::string("0x4db1a2: el C1 04 F3 10 del 3F 01 sin el cable"));
    resources.cloth.random.Discard(C3Randoms);
    send(Shop::WriteInventoryRequest());
    return {};
}

Core::Expected<void, std::string> ApplyShopItemList(const Shop::ShopItemList& list,
                                                    const WorldEntryResources& resources, const GamePacketSender& send)
{
    if (list.result == ListRefused)
        return AddSocialNotice(resources, ShopListRefusedText, UI::ErrorMessage, "0x4dbb2f");
    // 0x4dbb04..0x4dbb14: otro resultado, el log 0x6c8578.
    if (list.result != Shop::ListedResult)
        return {};
    // Las entradas que se ponen (hasta el primer precio de 0 o menos), antes de cerrar o abrir nada.
    const auto unpriced =
        std::ranges::find_if(list.entries, [](const Shop::ShopListEntry& entry) { return entry.price <= 0; });
    if (const auto past = AnEntryPastTheGrid(std::span(list.entries.begin(), unpriced), resources))
    {
        resources.log.Warning("se descarta el 3F 05: {}", *past);
        return {};
    }
    WorldState& world = resources.world;
    PersonalShopState& shops = world.Runtime().social.personalShop;
    GamePanels panels(world, resources.cloth.random);
    panels.LendThePersonalShops(world, resources.cloth.random, send);
    UI::PanelRegistry& registry = panels.Registry();
    // 0x4db8a7..0x4db8e8: el almacén y el inventario, si están abiertos (IsOpen de los dos sin rand()).
    for (const UI::PanelId id : {UI::WarehousePanel, UI::InventoryPanel})
    {
        if (!registry.IsOpen(id))
            continue;
        auto closed = registry.Close(id);
        if (!closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
    }
    shops.ownerKey = 0;
    shops.ownerName = {};
    // 0x4db8f9..0x4db91e: la copia de la grilla propia, salvo con la de otro ya abierta.
    if (!registry.IsOpen(UI::OtherShopPanel))
        shops.ownGridBackup = world.Inventory().secondCells;
    shops.ownTitle = TitleOf(list.title);
    ClearTheShopGrid(world);
    if (auto opened = registry.Open(UI::OtherShopPanel); !opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    // 0x4db965..0x4db9ca: el mapa de la de otro vacío (clear o el erase de cada uno, lo mismo).
    if (shops.pricesBuilt)
        shops.otherPrices.clear();
    for (const Shop::ShopListEntry& entry : list.entries)
    {
        // 0x4db9e2..0x4db9e7 y 0x4dbabf..0x4dbaf4: un precio de 0 o menos cierra las dos y sale, sin la copia del
        // dueño.
        if (entry.price <= 0)
            return CloseBothShops(resources, send);
        if (auto placed = PlaceTheEntry(entry, resources); !placed.HasValue())
            return placed;
    }
    // 0x4dba76..0x4dbab6: la copia del registro del dueño (0x438 bytes) si 0x5110d0 lo encuentra.
    const std::size_t owner = ActorWithKey(world, list.key);
    if (owner >= KeysSearched)
        return {};
    const ActorState& actor = world.Actors()[owner];
    shops.ownerKey = actor.key;
    std::ranges::copy(actor.name, shops.ownerName.begin());
    return {};
}

Core::Expected<void, std::string> ApplyShopPurchase(const Shop::ShopPurchase& purchase,
                                                    const WorldEntryResources& resources, const GamePacketSender& send)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    PersonalShopState& shops = runtime.social.personalShop;
    if (purchase.result == Bought)
    {
        if (const auto past =
                AnEntryPastTheGrid(std::array{Shop::ShopListEntry{purchase.slot, purchase.item, 0}}, resources))
        {
            resources.log.Warning("se descarta el 3F 06: {}", *past);
            return {};
        }
        if (shops.mode == OtherShopView)
        {
            // 0x4dbccf..0x4dbd14: el lugar de 0x78a6758 sale del mapa y de la grilla (0x5bd750 con 0x78a9bb0).
            if (shops.pricesBuilt)
                shops.otherPrices.erase(runtime.slot78a6758);
            const InventoryServices services = InventoryServicesOf(resources);
            if (auto removed = RemoveInventoryItem(
                    InventoryMoveContext{.world = world, .random = resources.cloth.random, .services = services},
                    runtime.slot78a6758);
                !removed.HasValue())
                return removed;
        }
        else if (shops.pricesBuilt)
        {
            // 0x4dbd3c..0x4dbdc2: el mapa de la de otro vacío.
            shops.otherPrices.clear();
        }
        // 0x4dbd19..0x4dbd2e y 0x4dbdc5..0x4dbdda: el ítem comprado en su lugar.
        return PlaceMovedItem(purchase.slot, purchase.item, resources);
    }
    if (purchase.result == ShopGone)
    {
        if (auto shown = AddSocialNotice(resources, ShopGoneText, UI::ErrorMessage, "0x4dbdf9"); !shown.HasValue())
            return shown;
        return CloseBothShops(resources, send);
    }
    // 0x4dbe2b..0x4dbe8b: 7 y 8 con su texto; otro, el log 0x6c85e0. Los tres, 0x5be150.
    if (purchase.result == PurchaseSeven || purchase.result == PurchaseEight)
    {
        const std::size_t text = purchase.result == PurchaseSeven ? PurchaseSevenText : PurchaseEightText;
        if (auto shown = AddSocialNotice(resources, text, UI::ErrorMessage, "0x4dbe64"); !shown.HasValue())
            return shown;
    }
    return ReturnTheCursor(resources);
}

Core::Expected<void, std::string> ApplyShopRefresh(const Shop::ShopRefresh& refresh,
                                                   const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    PersonalShopState& shops = world.Runtime().social.personalShop;
    // 0x4dbc8f: si no, el log 0x6c85b0.
    if (refresh.result != Shop::ListedResult || shops.mode != OtherShopView)
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
} // namespace Client::Scenes
