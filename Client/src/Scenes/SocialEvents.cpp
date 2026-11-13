#include "Scenes/SocialEvents.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Protocol/Duel.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Party.h"
#include "Protocol/PersonalShop.h"
#include "Protocol/Trade.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GamePanels.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/PersonalShopTitles.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/Dialogs.h"
#include "UI/GameUiState.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"
#include "UI/SprintfFormat.h"
#include "World/ItemState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
using World::ItemState;

// 0x6236f0: los diálogos que abren los paquetes.
constexpr std::int32_t TradeRequestDialog = 0x78;
constexpr std::int32_t PartyRequestDialog = 0x77;
constexpr std::int32_t MoneyDialog = 0x73;
constexpr std::int32_t DuelBusyDialog = 0x9a;
// Los textos de Text.bmd ((dirección − 0x76bd1dc) / 300).
constexpr std::size_t TradeRefusedText = 492;   // 0x76e126c
constexpr std::size_t TradeBusyText = 493;      // 0x76e1398
constexpr std::size_t TradeCancelledText = 494; // 0x76e14c4
constexpr std::size_t TradeNoRoomText = 495;    // 0x76e15f0
constexpr std::size_t TradeTimeText = 496;      // 0x76e171c
constexpr std::size_t PartyLeftText = 502;      // 0x76e1e24
constexpr std::size_t DuelStartedText = 912;    // 0x76ffe9c
constexpr std::size_t DuelRefusedText = 913;    // 0x76fffc8
constexpr std::size_t DuelEndedText = 914;      // 0x77000f4
// 0x4e6090: los textos del 41, del 0 al 5 (0x76e1848, 0x76e1974, 0x76e1aa0, 0x76e1bcc, 0x76e1cf8 y 0x76e44d0).
constexpr std::array<std::size_t, 6> PartyResultTexts{497, 498, 499, 500, 501, 535};
// Los sonidos (0x41ed10).
constexpr std::int32_t TradeItemSound = 0x1d;
constexpr std::int32_t TradeOkSound = 0x19;
constexpr std::int32_t DuelStartSound = 0x8d;
constexpr std::int32_t DuelRequestSound = 0x8c;
// 0x4e3e0a: el 3C del GS.
constexpr std::uint8_t OtherOkOff = 0;
constexpr std::uint8_t OtherOkOn = 1;
constexpr std::uint8_t OwnOkOff = 2;
// 0x4d5570 y 0x4d57f0.
constexpr std::uint8_t TradeRefused = 0;
constexpr std::uint8_t TradeAccepted = 1;
constexpr std::uint8_t TradeBusy = 2;
constexpr std::uint8_t TradeCancelled = 0;
constexpr std::uint8_t TradeNoRoom = 2;
constexpr std::uint8_t TradeTime = 3;
// 0x4d58da..0x4d58f5: lo que deja el cierre del diálogo 0x73.
constexpr std::int32_t ClosedDialogFieldLimit = 0x2a;
// 0x4dae80, 0x4dafe0 y 0x4e4ec2: el personaje de la clave, del 0 al 399 (0x5110d0 da 400 sin ninguno).
constexpr std::size_t ShopKeysSearched = 400;
// 0x4e4f8d: sprintf(EBP − 0x7c, Text[1122], nombre), con 0x70 bytes hasta el nombre (EBP − 0xc).
constexpr std::size_t ShopNoticeText = 1122;
constexpr std::size_t ShopNoticeBufferSize = 0x70;
// 0x4e4fcc..0x4e5009: con 0x6c931c en 2, el texto 1126 (0x770f964) con el tipo 2.
constexpr std::int32_t ViewingAnotherShop = 2;
constexpr std::size_t ShopOwnerLeftText = 1126;
// 0x4e41a9..0x4e41b6: los décimos del 44, hasta 10.
constexpr std::uint8_t MaxLifeTenths = 10;
// 0x4e4d58: el valor de 0x5767814 con que el AB pone 0x5767816 en 0.
constexpr std::uint8_t DuelEndClearsWord = 2;
// El relleno y el serial de un C3 (0x40b0a0).
constexpr std::uint32_t C3Randoms = 2;
// 0x5bc470: 0x5bc260 con 1 (el ítem está como en la copia) o con 99 (cambió), y la lista de su pila, de 10.
constexpr std::uint8_t SameAsTheCopy = 1;
constexpr std::size_t ChangedListSize = 10;
// 0x5bc300.
constexpr std::int32_t OtherType = 2;
constexpr std::uint8_t FirstLuckOption = 0x50;
constexpr std::uint8_t LastLuckOption = 0x53;
// 0x5bc5e0: lo que no se copia.
constexpr std::int16_t FirstPet = 0x1a00;
constexpr std::int16_t LastPet = 0x1a04;
constexpr std::array<std::int16_t, 3> GuardedTypes{0x1c0d, 0x1c0e, 0x1c10};
constexpr std::int16_t FirstLateType = 0x1c1f;
constexpr std::int16_t FirstJewelPack = 0x181e;
constexpr std::int16_t SecondJewelPack = 0x181f;
constexpr std::int16_t FirstWing = 0x1800;
constexpr std::int16_t LastWing = 0x1806;
constexpr std::int16_t FirstLateWing = 0x1a1e;
constexpr std::int16_t ThirdJewel = 0x180f;
constexpr std::uint32_t LevelMask = 0x78;
constexpr std::uint32_t HighestPlainLevel = 0x20;
constexpr std::uint8_t ExcellentMask = 0x3f;

std::int16_t SignedType(const ItemState& item)
{
    return static_cast<std::int16_t>(item.id);
}

bool IsEmpty(const ItemState& item)
{
    return item.id == EmptyOwnItemId;
}

void Copy(std::span<std::byte> target, std::span<const std::byte> name)
{
    std::ranges::copy(name.first(std::min(name.size(), target.size())), target.begin());
}

// strncpy(destino, origen, 10): hasta el primer 0 y ceros hasta el final.
void CopyUpToTheZero(std::span<std::byte> target, std::span<const std::byte> name)
{
    bool ended = false;
    for (std::size_t at = 0; at < name.size() && at < target.size(); ++at)
    {
        ended = ended || name[at] == std::byte{0};
        target[at] = ended ? std::byte{0} : name[at];
    }
}

// Un nombre de la .bss como texto, hasta el primer 0.
std::string NameText(std::span<const std::byte> name)
{
    std::string text;
    for (const std::byte b : name)
    {
        if (b == std::byte{0})
            break;
        text.push_back(static_cast<char>(std::to_integer<unsigned char>(b)));
    }
    return text;
}

Core::Expected<void, std::string> MessageText(const WorldEntryResources& resources, std::string_view text,
                                              std::int32_t type, std::string_view where)
{
    if (resources.textMeasure == nullptr)
        return Core::Unexpected(std::format("{}: el mensaje sin el medidor de los textos", where));
    const App::PixelSize screen = resources.screen ? resources.screen() : App::PixelSize{640, 480};
    return AddGameMessage(resources.world.Runtime(), *resources.textMeasure, screen, {}, text, type);
}

Core::Expected<std::string_view, std::string> TextOf(const WorldEntryResources& resources, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(resources.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x555190("", el texto `index`, tipo): los nombres que pasa el original (0x576799c.., 0x57679c8..) son buffers
// vacíos de la .bss, como los de InventoryServices::systemMessage (*inferido*, 08 P-C72).
Core::Expected<void, std::string> Message(const WorldEntryResources& resources, std::size_t index, std::int32_t type,
                                          std::string_view where)
{
    auto text = TextOf(resources, index, where);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    return MessageText(resources, text.Value(), type, where);
}

// El tipo en −1 y +0x3c en 0 en cada entrada.
void ClearTheGrid(std::span<ItemState> grid)
{
    for (ItemState& entry : grid)
    {
        entry.id = EmptyOwnItemId;
        entry.occupied = 0;
    }
}

// 0x5bbdf0: las 32 entradas de 0x77f3dc8.
void ClearTheBackup(TradeState& trade)
{
    ClearTheGrid(trade.backup);
}

// 0x5bc260(lugar, grilla, valor): +0x44 de cada casilla de la huella del ítem de `slot`, desde su columna y su fila,
// con 8 columnas.
Core::Expected<void, std::string> MarkTheFootprint(std::span<ItemState> grid, std::size_t slot, std::uint8_t value,
                                                   const WorldEntryResources& resources)
{
    const ItemState& item = grid[slot];
    const auto footprint = ItemFootprintOf(resources.items, item.id, "0x5bc260");
    if (!footprint.HasValue())
        return Core::Unexpected(footprint.Error());
    for (std::int32_t row = item.gridRow; row < item.gridRow + footprint.Value().height; ++row)
    {
        for (std::int32_t column = item.gridColumn; column < item.gridColumn + footprint.Value().width; ++column)
        {
            const std::int32_t cell = column + row * TradeGridColumns;
            if (cell < 0 || cell >= static_cast<std::int32_t>(grid.size()))
                return Core::Unexpected(
                    std::format("0x5bc260: la huella del tipo {:#x} pasa la casilla {} de la grilla "
                                "del comercio (Q-C200)",
                                item.id, cell));
            grid[static_cast<std::size_t>(cell)].warning = value;
        }
    }
    return {};
}

// 0x5bc470(0x78a5a20): 0x78abaa5 en 0, +0x44 en 1 en las 32 casillas y, por cada ítem del otro con una copia del mismo
// tipo en 0x77f3dc8, la comparación (0x5bc300(copia, ítem)): 1 (la copia era mejor) marca 99 y prende 0x78abaa5 y sigue
// con la copia que viene; otra cosa marca 1 y termina (con 0, el tipo va a la lista). Al final, si la lista tiene algo,
// 0x78abaa5 en 0 y los ítems de esos tipos con 1.
Core::Expected<void, std::string> MarkTheChangedItems(const WorldEntryResources& resources)
{
    TradeState& trade = resources.world.Runtime().social.trade;
    std::span<ItemState> grid = trade.otherGrid;
    trade.changed = 0;
    for (ItemState& cell : grid)
        cell.warning = SameAsTheCopy;
    std::array<std::int32_t, ChangedListSize> same{};
    std::size_t count = 0;
    for (std::size_t slot = 0; slot < grid.size(); ++slot)
    {
        const ItemState item = grid[slot];
        if (IsEmpty(item) || item.occupied == 0)
            continue;
        for (const ItemState& copy : trade.backup)
        {
            if (IsEmpty(copy) || copy.id != item.id)
                continue;
            // 0x5bc4d1..0x5bc4f1: la copia es el primer argumento (la pila de abajo) y el ítem el segundo.
            const std::int32_t compared = CompareTradeItems(copy, grid[slot]);
            if (compared != 1)
            {
                if (compared == 0)
                {
                    if (count == same.size())
                        return Core::Unexpected(std::string("0x5bc52f: más de diez tipos iguales pisan la pila de "
                                                            "0x5bc470 (Q-C961)"));
                    same[count++] = SignedType(copy);
                }
                if (auto marked = MarkTheFootprint(grid, slot, SameAsTheCopy, resources); !marked.HasValue())
                    return marked;
                break;
            }
            trade.changed = 1;
            if (auto marked = MarkTheFootprint(grid, slot, ChangedFromTheCopy, resources); !marked.HasValue())
                return marked;
        }
    }
    if (count == 0)
        return {};
    trade.changed = 0;
    for (std::size_t at = 0; at < count; ++at)
    {
        for (std::size_t slot = 0; slot < grid.size(); ++slot)
        {
            const ItemState& item = grid[slot];
            if (!IsEmpty(item) && SignedType(item) == same[at] && item.occupied != 0)
            {
                if (auto marked = MarkTheFootprint(grid, slot, SameAsTheCopy, resources); !marked.HasValue())
                    return marked;
            }
        }
    }
    return {};
}

// 0x5bc5e6..0x5bc661: lo que 0x5bc5e0 no guarda, con 0x402b30 (2 o 3 solo los paquetes 0x181e y 0x181f).
bool IsNotWorthACopy(const ItemState& item)
{
    const std::int16_t type = SignedType(item);
    const bool pet = type >= FirstPet && type <= LastPet;
    const bool guarded = std::ranges::find(GuardedTypes, type) != GuardedTypes.end();
    if (pet || guarded || type >= FirstLateType)
        return false;
    if (type == FirstJewelPack || type == SecondJewelPack)
        return false;
    if ((type >= FirstWing && type <= LastWing) || type >= FirstLateWing || type == ThirdJewel)
        return false;
    // 0x5bc64c..0x5bc65b: CMP DL,0x20 con signo sobre el nivel · 8.
    const auto level = static_cast<std::int8_t>(item.scalar1Low & LevelMask);
    if (level > static_cast<std::int8_t>(HighestPlainLevel) && type < FirstWing)
        return false;
    return (item.flags1b & ExcellentMask) == 0;
}

// 0x5bc5e0(lugar, 0x78a5a20): el ítem del otro en la copia 0x77f3dc8. Lo que no vale una copia, 0. Si no, contra cada
// copia que no está vacía, 0x5bc300(copia, ítem): 0 termina con 0; −1 (la copia era peor) la reemplaza y termina con
// −1; 1 recuerda que hubo una mejor. Sin
// ninguna mejor, va al primer lugar vacío. Devuelve la última comparación.
std::int32_t KeepACopy(TradeState& trade, std::size_t slot)
{
    const ItemState item = trade.otherGrid[slot];
    if (IsNotWorthACopy(item))
        return 0;
    std::int32_t compared = 0;
    bool better = false;
    for (ItemState& copy : trade.backup)
    {
        if (IsEmpty(copy))
            continue;
        // 0x5bc679..0x5bc691: la copia primero, como en 0x5bc470.
        compared = CompareTradeItems(copy, item);
        if (compared == 0)
            return 0;
        if (compared == -1)
        {
            copy = item;
            return -1;
        }
        if (compared != OtherType)
            better = true;
    }
    if (better)
        return compared;
    const auto free = std::ranges::find_if(trade.backup, IsEmpty);
    if (free != trade.backup.end())
        *free = item;
    return compared;
}

// 0x5bd750(lugar, 0x78a5a20, 8): 0x5bc5e0, y con un ítem, su huella vacía (tipo −1 y +0x3c en 0) y 0x5bc470.
Core::Expected<void, std::string> RemoveFromTheOtherGrid(std::uint8_t slot, const WorldEntryResources& resources)
{
    constexpr std::uint8_t NoSlot = 0xff;
    if (slot == NoSlot)
        return {};
    TradeState& trade = resources.world.Runtime().social.trade;
    if (slot >= trade.otherGrid.size())
        return Core::Unexpected(std::format("0x5bd750: el lugar {} pasa las 32 casillas de 0x78a5a20", slot));
    static_cast<void>(KeepACopy(trade, slot));
    const ItemState item = trade.otherGrid[slot];
    if (IsEmpty(item))
        return {};
    const auto footprint = ItemFootprintOf(resources.items, item.id, "0x5bd750, la grilla de 0x78a5a20");
    if (!footprint.HasValue())
        return Core::Unexpected(footprint.Error());
    for (std::int32_t row = item.gridRow; row < item.gridRow + footprint.Value().height; ++row)
    {
        for (std::int32_t column = item.gridColumn; column < item.gridColumn + footprint.Value().width; ++column)
        {
            const std::int32_t cell = row * TradeGridColumns + column;
            if (cell >= static_cast<std::int32_t>(trade.otherGrid.size()))
                return Core::Unexpected(std::format("0x5bd750: la huella del tipo {:#x} pasa la casilla {} de "
                                                    "0x78a5a20 (Q-C200)",
                                                    item.id, cell));
            trade.otherGrid[static_cast<std::size_t>(cell)].id = EmptyOwnItemId;
            trade.otherGrid[static_cast<std::size_t>(cell)].occupied = 0;
        }
    }
    return MarkTheChangedItems(resources);
}
} // namespace

std::int32_t CompareTradeItems(const ItemState& a, const ItemState& b)
{
    if (a.id != b.id)
        return OtherType;
    // 0x5bc320..0x5bc32b: +9 no lo escribe nadie en las grillas ni en la copia (las dos en 0, *inferido*, Q-C961).
    const std::uint32_t levelA = (a.scalar1Low >> 3) & 0xf;
    const std::uint32_t levelB = (b.scalar1Low >> 3) & 0xf;
    if (levelA != levelB)
        return levelA < levelB ? -1 : 1;
    const std::uint32_t bitA = (a.scalar1Low >> 7) & 1;
    const std::uint32_t bitB = (b.scalar1Low >> 7) & 1;
    if (bitA != bitB)
        return bitA < bitB ? -1 : 1;
    const std::uint32_t excellentA = a.flags1b & ExcellentMask;
    const std::uint32_t excellentB = b.flags1b & ExcellentMask;
    if (excellentA != excellentB)
        return excellentA < excellentB ? -1 : 1;
    if (a.optionCount != b.optionCount)
        return a.optionCount < b.optionCount ? -1 : 1;
    // 0x5bc3df..0x5bc43d: la última opción de 0x50 a 0x53 de cada uno, con la cuenta de `b` (las dos iguales).
    std::uint32_t foundA = 0;
    std::uint32_t foundB = 0;
    std::uint32_t valueA = 0;
    std::uint32_t valueB = 0;
    for (std::size_t at = 0; at < b.optionCount && at < World::MaxItemOptions; ++at)
    {
        const std::uint8_t tagA = a.optionBytes[at];
        if (tagA >= FirstLuckOption && tagA <= LastLuckOption)
        {
            foundA = 1;
            valueA = a.optionBytes[at + World::ItemOptionValueOffset];
        }
        const std::uint8_t tagB = b.optionBytes[at];
        if (tagB >= FirstLuckOption && tagB <= LastLuckOption)
        {
            foundB = 1;
            valueB = b.optionBytes[at + World::ItemOptionValueOffset];
        }
    }
    if (foundA < foundB || valueA < valueB)
        return -1;
    if (foundA != foundB && valueA != valueB)
        return 1;
    if (a.durability < b.durability)
        return -1;
    return a.durability == b.durability ? 0 : 1;
}

Core::Expected<std::optional<Protocol::Outgoing>, std::string>
ApplyTradeRequest(const Protocol::Trade::TradeRequest& request, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    GamePanels panels(world, resources.cloth.random);
    if (panels.Registry().IsOpen(UI::PersonalShopPanel) || panels.Registry().IsOpen(UI::OtherShopPanel))
        return std::optional<Protocol::Outgoing>{Protocol::Trade::WriteTradeResponse(0)};
    TradeState& trade = runtime.social.trade;
    trade.otherName.back() = std::byte{0};
    Copy(trade.otherName, request.name);
    UI::PushDialog(runtime.dialogs, TradeRequestDialog);
    if (runtime.cursorItemCount > 0)
    {
        const InventoryServices services = InventoryServicesOf(resources);
        if (auto returned = ReturnTheCursorItem(InventoryMoveContext{world, resources.cloth.random, services});
            !returned.HasValue())
            return Core::Unexpected(std::move(returned).Error());
    }
    return std::optional<Protocol::Outgoing>{};
}

Protocol::Outgoing ApplyClearTradeRequest(Render::Models::CrtRandom& random)
{
    random.Discard(C3Randoms);
    return Protocol::Trade::WriteClearTradeRequestReport();
}

Core::Expected<void, std::string> ApplyTradeResponse(const Protocol::Trade::TradeResponse& response,
                                                     const WorldEntryResources& resources)
{
    if (response.response == TradeRefused)
        return Message(resources, TradeRefusedText, UI::ErrorMessage, "0x4d56d4");
    if (response.response == TradeBusy)
        return Message(resources, TradeBusyText, UI::ErrorMessage, "0x4d5594");
    if (response.response != TradeAccepted)
        return {};
    WorldState& world = resources.world;
    GamePanels panels(world, resources.cloth.random);
    if (auto opened = panels.Registry().Open(UI::TradePanel); !opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    TradeState& trade = world.Runtime().social.trade;
    // 0x4d55bd..0x4d562b: SetCursorPos con el mouse y la pantalla del momento (08 P-C202).
    trade.cursorMoveRequested = 1;
    trade.otherMoney = 0;
    trade.ownMoney = 0;
    trade.otherOk = 0;
    trade.ownOk = 0;
    trade.field78abac4 = 0;
    trade.okWait = 0;
    // 0x4d5644..0x4d568a: strcmp entre 0x78aba1c y el nombre en la pila, con su 0 en el byte 10.
    std::array<std::byte, TradeNameSize> incoming{};
    Copy(incoming, response.name);
    if (trade.changed == 0 && NameText(trade.otherName) != NameText(incoming))
        ClearTheBackup(trade);
    Copy(trade.otherName, response.name);
    trade.otherGuild = static_cast<std::int32_t>(response.guild);
    trade.changed = 0;
    trade.otherName.back() = std::byte{0};
    trade.otherLevel = response.level;
    return {};
}

Core::Expected<void, std::string> ApplyTradeItemRemoved(const Protocol::Trade::TradeItemRemoved& removed,
                                                        const WorldEntryResources& resources)
{
    if (auto taken = RemoveFromTheOtherGrid(removed.slot, resources); !taken.HasValue())
        return taken;
    resources.world.Runtime().lastSoundId = TradeItemSound;
    return {};
}

Core::Expected<void, std::string> ApplyTradeItemAdded(const Protocol::Trade::TradeItemAdded& added,
                                                      const WorldEntryResources& resources)
{
    TradeState& trade = resources.world.Runtime().social.trade;
    const InventoryItemBytes bytes(added.item);
    if (auto placed = PlaceItemInOtherGrid(trade.otherGrid, TradeGridColumns, added.slot, bytes, resources,
                                           "0x5bc740, la grilla de 0x78a5a20");
        !placed.HasValue())
        return placed;
    // 0x5bd3f2: con 0x78a5a20, 0x5bc470; el último argumento en 1 vuelve ahí. Con el tipo 0x1fff no llega.
    if (InventoryItemType(bytes) != NoInventoryItem)
    {
        if (auto marked = MarkTheChangedItems(resources); !marked.HasValue())
            return marked;
    }
    resources.world.Runtime().lastSoundId = TradeItemSound;
    return {};
}

void ApplyTradeMoneyResult(const Protocol::Trade::TradeMoneyResult& result, WorldState& world)
{
    TradeState& trade = world.Runtime().social.trade;
    trade.ownMoney = result.accepted != 0 ? trade.sentMoney : 0;
}

void ApplyTradeOtherMoney(const Protocol::Trade::TradeOtherMoney& money, WorldState& world)
{
    world.Runtime().social.trade.otherMoney = money.money;
}

void ApplyTradeOkState(const Protocol::Trade::TradeOkState& state, WorldState& world)
{
    WorldRuntime& runtime = world.Runtime();
    TradeState& trade = runtime.social.trade;
    if (state.state == OtherOkOff)
        trade.otherOk = 0;
    else if (state.state == OtherOkOn)
        trade.otherOk = 1;
    else if (state.state == OwnOkOff)
        trade.ownOk = 0;
    runtime.lastSoundId = TradeOkSound;
}

Core::Expected<void, std::string> ApplyTradeResult(const Protocol::Trade::TradeResult& result,
                                                   const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    TradeState& trade = runtime.social.trade;
    if (runtime.ui.field78abb39 != 0)
    {
        // 0x4d57fc..0x4d581f: el primero que llega con la traba se guarda; los demás se pierden.
        if (trade.resultDeferred == 0)
        {
            trade.resultDeferred = 1;
            trade.deferredHead = result.head;
        }
        return {};
    }
    if (result.result == TradeCancelled)
    {
        if (auto shown = Message(resources, TradeCancelledText, UI::ErrorMessage, "0x4d5838"); !shown.HasValue())
            return shown;
        trade.changed = 0;
        for (std::size_t slot = 0; slot < trade.otherGrid.size(); ++slot)
        {
            const ItemState& item = trade.otherGrid[slot];
            if (!IsEmpty(item) && item.occupied != 0)
                static_cast<void>(KeepACopy(trade, slot));
        }
    }
    if (result.result == TradeNoRoom)
    {
        if (auto shown = Message(resources, TradeNoRoomText, UI::ErrorMessage, "0x4d588a"); !shown.HasValue())
            return shown;
    }
    if (result.result == TradeTime)
    {
        if (auto shown = Message(resources, TradeTimeText, UI::ErrorMessage, "0x4d58a6"); !shown.HasValue())
            return shown;
        UI::PushDialog(runtime.dialogs, 0);
    }
    // 0x4d58ba: solo la cuenta (0x7879fbc); el ítem del cursor queda como está.
    runtime.cursorItemCount = 0;
    runtime.cursorItem.occupied = 0;
    if (runtime.dialogs.current == MoneyDialog)
    {
        UI::PushDialog(runtime.dialogs, 0);
        UI::ResetTheTextBuffers(runtime.chat.input, false);
        runtime.social.dialogFieldLimit = ClosedDialogFieldLimit;
        runtime.chat.input.fieldCount = UI::ChatFieldCount;
        runtime.social.moneyField = 0;
        runtime.ui.chatOpen = false;
    }
    GamePanels panels(world, resources.cloth.random);
    auto closed = panels.Registry().Close(UI::TradePanel, true);
    if (!closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}

void ClearTheTradeForSelection(TradeState& trade)
{
    trade.changed = 0;
    ClearTheGrid(trade.otherGrid);
    ClearTheGrid(trade.ownGrid);
    ClearTheGrid(trade.backup);
}

Core::Expected<void, std::string> CloseTheTradeGrids(WorldState& world, Render::Models::CrtRandom& random, bool aux,
                                                     const GamePacketSender& send)
{
    TradeState& trade = world.Runtime().social.trade;
    if (!aux)
    {
        if (!send)
            return Core::Unexpected(std::string("0x657fe1: el C1 03 3D al cerrar el comercio sin el cable"));
        // El relleno y el serial del C3 (0x40b0a0).
        random.Discard(C3Randoms);
        send(Protocol::Trade::WriteTradeCancel());
    }
    for (std::size_t slot = 0; slot < trade.ownGrid.size(); ++slot)
    {
        trade.ownGrid[slot].id = EmptyOwnItemId;
        trade.ownGrid[slot].occupied = 0;
        trade.otherGrid[slot].id = EmptyOwnItemId;
        trade.otherGrid[slot].occupied = 0;
        if (trade.changed == 1)
            ClearTheBackup(trade);
    }
    return {};
}

void ApplyPartyRequest(const Protocol::Party::PartyRequest& request, WorldState& world)
{
    WorldRuntime& runtime = world.Runtime();
    runtime.social.party.requestKey = request.key;
    UI::PushDialog(runtime.dialogs, PartyRequestDialog);
}

Core::Expected<void, std::string> ApplyPartyRequestResult(const Protocol::Party::PartyRequestResult& result,
                                                          const WorldEntryResources& resources)
{
    if (result.result < PartyResultTexts.size())
    {
        if (auto shown = Message(resources, PartyResultTexts[result.result], UI::ErrorMessage, "0x4e3f80");
            !shown.HasValue())
            return shown;
    }
    UI::PushDialog(resources.world.Runtime().dialogs, 0);
    return {};
}

Core::Expected<void, std::string> ApplyPartyList(const Protocol::Party::PartyList& list, WorldState& world)
{
    WorldRuntime& runtime = world.Runtime();
    runtime.hud.partyCount = static_cast<std::int32_t>(list.members.size());
    if (list.members.size() > PartySlots)
        return Core::Unexpected(
            std::format("0x4d67e0: {} miembros pasan los cinco lugares de 0x77f3cd0 (Q-C960)", list.members.size()));
    for (std::size_t at = 0; at < list.members.size(); ++at)
    {
        const Protocol::Party::PartyMember& member = list.members[at];
        PartySlot& slot = runtime.social.party.slots[at];
        Copy(slot.name, member.name);
        slot.name.back() = std::byte{0};
        slot.number = member.number;
        slot.map = member.map;
        slot.x = member.x;
        slot.y = member.y;
        slot.life = member.life;
        slot.maxLife = member.maxLife;
    }
    return {};
}

Core::Expected<void, std::string> ApplyPartyLeft(const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    runtime.hud.partyCount = 0;
    if (auto shown = Message(resources, PartyLeftText, UI::ErrorMessage, "0x4e40b1"); !shown.HasValue())
        return shown;
    // 0x4e40b6..0x4e415e: con la cuenta ya en 0, la búsqueda del nombre no corre y 0x6c8e50 queda en −1.
    if (runtime.social.party.followTarget >= 0)
        runtime.social.party.followTarget = -1;
    return {};
}

Core::Expected<void, std::string> ApplyPartyLife(const Protocol::Party::PartyLife& life, WorldState& world)
{
    PartyState& party = world.Runtime().social.party;
    for (const std::uint8_t entry : life.entries)
    {
        const auto place = static_cast<std::size_t>(entry >> 4);
        if (place >= party.slots.size())
            return Core::Unexpected(std::format("0x4e41bb: el 44 escribe el lugar {} de 0x77f3cd0, fuera de los cinco "
                                                "(Q-C960)",
                                                place));
        party.slots[place].tenths = std::min<std::uint8_t>(entry & 0xf, MaxLifeTenths);
    }
    return {};
}

Core::Expected<void, std::string> ApplyDuelStart(const Protocol::Duel::DuelStart& start,
                                                 const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    DuelState& duel = runtime.social.duel;
    if (start.result != 0)
    {
        duel.active = 1;
        duel.rivalKey = start.key;
    }
    CopyUpToTheZero(duel.rivalName, start.name);
    duel.rivalName.back() = std::byte{0};
    const std::size_t index = start.result != 0 ? DuelStartedText : DuelRefusedText;
    auto format = TextOf(resources, index, "0x4dac90");
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    const std::string rival = NameText(duel.rivalName);
    if (auto shown =
            MessageText(resources, UI::Sprintf(format.Value(), std::string_view(rival)), UI::ErrorMessage, "0x4dac90");
        !shown.HasValue())
        return shown;
    if (start.result != 0)
        runtime.lastSoundId = DuelStartSound;
    return {};
}

Core::Expected<void, std::string> ApplyDuelEnd(const Protocol::Duel::DuelEnd& end, const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    DuelState& duel = runtime.social.duel;
    duel.active = 0;
    duel.rivalKey = end.key;
    CopyUpToTheZero(duel.rivalName, end.name);
    duel.rivalName.back() = std::byte{0};
    if (auto shown = Message(resources, DuelEndedText, UI::ErrorMessage, "0x4e4d4b"); !shown.HasValue())
        return shown;
    if (duel.byte5767814 == DuelEndClearsWord)
        duel.word5767816 = 0;
    return {};
}

void ApplyDuelRequest(const Protocol::Duel::DuelRequest& request, WorldState& world)
{
    WorldRuntime& runtime = world.Runtime();
    if (runtime.dialogs.current == DuelBusyDialog)
        return;
    DuelState& duel = runtime.social.duel;
    duel.rivalKey = request.key;
    CopyUpToTheZero(duel.rivalName, request.name);
    duel.rivalName.back() = std::byte{0};
    runtime.ui.field78abad9 = 1;
    runtime.lastSoundId = DuelRequestSound;
}

void ApplyShopTitles(const Protocol::PersonalShop::ShopTitles& titles, WorldState& world)
{
    for (const Protocol::PersonalShop::ShopTitleEntry& entry : titles.entries)
    {
        const std::size_t index = ActorWithKey(world, entry.key);
        if (index >= ShopKeysSearched)
            continue;
        AddShopTitle(world, entry.key, index, entry.title);
    }
}

void ApplyShopCreated(const Protocol::PersonalShop::ShopCreated& created, WorldState& world)
{
    // 0x4db7ff: "Fault ReceiveCreatePersonalShop" al log con el resultado, nada más.
    if (created.result != 1)
        return;
    WorldRuntime& runtime = world.Runtime();
    PersonalShopState& shops = runtime.social.personalShop;
    shops.ownOpen = 1;
    const std::size_t hero = runtime.hero;
    const std::string title = shops.ownTitle;
    AddShopTitle(world, static_cast<std::int32_t>(static_cast<std::int16_t>(world.Actors()[hero].key)), hero, title);
}

void ApplyShopClosed(const Protocol::PersonalShop::ShopClosed& closed, WorldState& world)
{
    // 0x4e4f0e: el log 0x6c8504 con el resultado, nada más.
    if (closed.result != 1)
        return;
    const std::size_t index = ActorWithKey(world, closed.key);
    if (index >= ShopKeysSearched)
        return;
    WorldRuntime& runtime = world.Runtime();
    if (index == runtime.hero)
        runtime.social.personalShop.ownOpen = 0;
    RemoveShopTitle(runtime.social.personalShop, index);
}

Core::Expected<void, std::string> ApplyShopNotice(const Protocol::PersonalShop::ShopNotice& notice,
                                                  const WorldEntryResources& resources)
{
    auto format = TextOf(resources, ShopNoticeText, "0x4e4f8d");
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    std::string name;
    for (const std::byte letter : notice.name)
    {
        if (letter == std::byte{0})
            break;
        name.push_back(static_cast<char>(std::to_integer<std::uint8_t>(letter)));
    }
    const std::string text = UI::Sprintf(format.Value(), std::string_view(name));
    if (text.size() >= ShopNoticeBufferSize)
        return Core::Unexpected(std::format("0x4e4f93: el texto {} con el nombre no entra en los {} bytes de la pila",
                                            ShopNoticeText, ShopNoticeBufferSize));
    return MessageText(resources, text, UI::SystemMessage, "0x4e4fa3");
}

void ApplyOtherShopTitle(const Protocol::PersonalShop::ShopTitle& title, WorldState& world)
{
    const std::size_t index = ActorWithKey(world, title.key);
    if (index >= ShopKeysSearched)
        return;
    const ActorState& actor = world.Actors()[index];
    // 0x4db04b: strncmp(+0x1d9, pkt + 0x2a, 10).
    for (std::size_t at = 0; at < title.name.size(); ++at)
    {
        if (actor.name[at] != title.name[at])
            return;
        if (actor.name[at] == std::byte{0})
            break;
    }
    AddShopTitle(world, title.key, index, title.title);
}

Core::Expected<void, std::string> ApplyShopOwnerLeft(const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    if (world.Runtime().social.personalShop.mode != ViewingAnotherShop)
        return {};
    GamePanels panels(world, resources.cloth.random);
    for (const UI::PanelId id : {UI::PersonalShopPanel, UI::OtherShopPanel})
    {
        auto closed = panels.Registry().Close(id);
        if (!closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
    }
    return Message(resources, ShopOwnerLeftText, UI::ErrorMessage, "0x4e5009");
}

void ApplyDuelScore(const Protocol::Duel::DuelScore& score, WorldState& world)
{
    DuelState& duel = world.Runtime().social.duel;
    if (duel.rivalKey == score.firstKey)
    {
        duel.rivalScore = score.firstScore;
        duel.ownScore = score.secondScore;
    }
    else if (duel.rivalKey == score.secondKey)
    {
        duel.rivalScore = score.secondScore;
        duel.ownScore = score.firstScore;
    }
}
} // namespace Client::Scenes
