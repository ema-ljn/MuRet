#include "Scenes/NpcWindowSteps.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Protocol/NpcWindows.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ChaosMix.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/HudItems.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemPrice.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameText.h"
#include "UI/SprintfFormat.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
namespace Npc = Protocol::NpcWindows;

// 0x41ed10(0x19, 0, 0): el sonido de los botones.
constexpr std::int32_t ButtonSound = 0x19;
// 0x6c8dcc: el máximo de MouseUpdateTime que dejan los botones.
constexpr std::int32_t ButtonMouseUpdateTimeMax = 6;
// El rand() del relleno (0x69fd70) y el del número de serie (0x40b0a0, o 0x40b240 y 0x40b3f0 en línea) de un C3.
constexpr std::size_t C3SendRandoms = 2;
// Los botones de 0x18 × 0x18 de las ventanas y del pie del inventario.
constexpr std::int32_t ButtonSize = 0x18;
// 0x5ee0cd..0x5ee0f3 y 0x5feb22..0x5feb48: el botón de cerrar, en la esquina del pie del inventario.
constexpr std::int32_t CloseButtonX = 0x19;
constexpr std::int32_t CloseButtonY = 0x18b;
// 0x5ed9b5..0x5eda97: los tres botones de reparar de la tienda, en y + 0x16d.
constexpr std::int32_t RepairButtonsY = 0x16d;
constexpr std::int32_t RepairCursorButtonX = 0x19;
constexpr std::int32_t RepairModeButtonX = 0x55;
constexpr std::int32_t RepairAllButtonX = 0x73;
// 0x5ed9fc..0x5eda71: 0x78abb27 en 2 con el modo de reparar prendido.
constexpr std::uint8_t RepairCursorOn = 2;
// 0x5ea090..0x5ea0ba: los cuatro botones del almacén.
struct WarehouseButton
{
    std::int32_t x;
    std::int32_t y;
};
constexpr std::array<WarehouseButton, 4> WarehouseButtons{{{0x1a, 0x186}, {0x4c, 0x186}, {0x7e, 0x186}, {0xd7, 0x18b}}};
constexpr std::int32_t DepositButton = 0;
constexpr std::int32_t WithdrawButton = 1;
constexpr std::int32_t KeypadButton = 2;
constexpr std::int32_t CloseWarehouseButton = 3;
// 0x5ea127 y 0x5ea169: el diálogo del zen del almacén.
constexpr std::int32_t MoneyDialog = 0x73;
// 0x5ea150..0x5ea15f y 0x62821f..0x628233: la entrada sin EDIT y los campos con el 0x73 abierto y al cerrarlo.
constexpr std::int32_t MoneyDialogInputMode = 8;
constexpr std::int32_t ClosedInputMode = 0x2a;
constexpr std::int32_t MoneyDialogFieldCount = 1;
constexpr std::int32_t ClosedFieldCount = 2;
// 0x5ea1aa..0x5ea1b3: el teclado del botón 2, en 4 sin la traba y en 3 con ella.
constexpr std::int32_t UnlockedKeypadMode = 4;
// 0x5cb43c y 0x627c33: el teclado de la contraseña que espera el almacén.
constexpr std::int32_t PendingKeypadMode = 1;
// 0x5fe64f..0x5fe67d: el botón de mezclar de la caja del caos.
constexpr std::int32_t MixButtonX = 0x4b;
constexpr std::int32_t MixButtonY = 300;
constexpr std::int32_t MixButtonWidth = 0x2c;
constexpr std::int32_t MixButtonHeight = 0x20;
// 0x5fea4f..0x5fea6c: 0x633300(1, 0x21b, 0x98), el diálogo de mezclar.
constexpr std::int32_t MixDialogLines = 1;
constexpr std::int32_t MixDialogText = 0x21b;
// Los avisos del botón de mezclar (0x5fe982..0x5feb06) y su tipo (0x555190 con 2).
constexpr std::size_t NotEnoughZenText = 596;
constexpr std::size_t LevelTooLowText = 1171;
constexpr std::size_t NotTheSameText = 600;
constexpr std::size_t NothingToMixText = 580;
constexpr std::size_t MixingText = 591;
constexpr std::size_t NoRoomText = 581;
constexpr std::int32_t ErrorMessageType = 2;
// 0x5feaab: el nivel del héroe que pide la capa, el 10 del wsprintfA.
constexpr std::int32_t CapeLevel = 10;
// 0x5decd9..0x5ded0e: la grilla de la tienda, donde se vende.
constexpr std::int32_t ShopGridLeft = 0xf;
constexpr std::int32_t ShopGridRight = 0xc3;
constexpr std::int32_t ShopGridTop = 0x32;
constexpr std::int32_t ShopGridBottom = 0x172;
// 0x5df417..0x5df430: 0x633300(3, 0x218, 0x98), el diálogo de vender algo valioso.
constexpr std::int32_t SellDialogLines = 3;
constexpr std::int32_t SellDialogText = 0x218;
constexpr std::int32_t ConfirmDialog = 0x98;
// 0x78abb14: lo que espera el diálogo del ítem del cursor.
constexpr std::int32_t SellChoice = 1;
constexpr std::int32_t MixChoice = 2;
// 0x5d965a: el 3, el OK del comercio (hito 7). 0x5d9b82: el 4 lo deja el paso del panel 0x2000 (0x5e75fc, con el 0x98
// del texto 0x1c0); el 5, levantar un ítem de la tienda personal propia (0x5c5181, hito 7); el 6, el precio de la
// tienda personal menor que lo que vale el ítem (0x5da592, hito 7).
constexpr std::int32_t TradeChoice = 3;
constexpr std::int32_t KeypadChoice = 4;
constexpr std::int32_t PersonalShopChoice = 5;
constexpr std::int32_t ShopPriceChoice = 6;
// 0x5d9c19: el teclado del almacén que abre el sí del 4.
constexpr std::int32_t KeypadChoiceMode = 6;
// 0x6c932e: las respuestas del 0x98 (0x62a779) y nada.
constexpr std::uint8_t YesAnswer = 1;
constexpr std::uint8_t NoAnswer = 2;
constexpr std::uint8_t NoAnswerYet = 0xff;
// 0x5d935c: la mezcla pedida.
constexpr std::int32_t MixRequested = 1;
// 0x62646a: más de 100.000.000 abre el 0x75; 0x627bd9 y 0x628004: el zen no pasa de 2.000.000.000.
constexpr std::int32_t MaxTypedMoney = 100'000'000;
constexpr std::uint32_t MaxZen = 2'000'000'000;
constexpr std::int32_t TooMuchMoneyDialog = 0x75;
constexpr std::int32_t NotEnoughMoneyDialog = 0x74;
// 0x627c13..0x627c2d: lo que espera la contraseña para sacar zen: −1, el tipo 1, el zen y 0x77f3cc8.
constexpr std::int32_t PendingMoney = -1;
constexpr std::int32_t WithdrawType = 1;
constexpr std::uint8_t DepositType = 0;
// 0x5b17f2..0x5b1856 y 0x5ded19..0x5dee79: los tipos, con signo.
constexpr std::uint32_t LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;
constexpr std::int32_t LastCheapLevel = 4;

bool InRect(const GameUiContext& context, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
{
    return context.mouseX >= left && context.mouseX < right && context.mouseY >= top && context.mouseY < bottom;
}

bool OnButton(const GameUiContext& context, std::int32_t left, std::int32_t top)
{
    return InRect(context, left, top, left + ButtonSize, top + ButtonSize);
}

std::int32_t Signed(std::uint16_t id)
{
    return static_cast<std::int16_t>(id);
}

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

std::int32_t LevelOf(const World::ItemState& item)
{
    return static_cast<std::int32_t>((item.scalar1Low >> LevelShift) & LevelMask);
}

const InventoryServices& ServicesOf(const GameUiContext& context)
{
    static const InventoryServices None{};
    return context.inventory != nullptr ? *context.inventory : None;
}

InventoryMoveContext MovesOf(const GameUiContext& context)
{
    return InventoryMoveContext{.world = context.world, .random = context.random, .services = ServicesOf(context)};
}

Core::Expected<void, std::string> Send(const GameUiContext& context, Protocol::Outgoing packet, std::string_view what)
{
    if (!context.sendNpcWindow)
        return Core::Unexpected(std::format("{} sin la sesión", what));
    return context.sendNpcWindow(std::move(packet));
}

// Un C1 con el XOR32 y en C3: los dos rand() del envío, antes del paquete.
Core::Expected<void, std::string> SendInC3(const GameUiContext& context, Protocol::Outgoing packet,
                                           std::string_view what)
{
    context.random.Discard(C3SendRandoms);
    return Send(context, std::move(packet), what);
}

// 0x62da70 y sus copias: el que espera pasa a abierto.
void CloseTheDialog(UI::DialogState& dialogs)
{
    dialogs.current = dialogs.pending;
    dialogs.pending = 0;
}

// 0x5b17f2..0x5b1856: lo que reparar todo no cuenta.
bool IsLeftOutOfTheRepair(std::uint16_t id)
{
    const std::int32_t type = Signed(id);
    return InRange(type, 0x1a00, 0x1a05) || type == 0x1a0a || type == 0x1a1f || type == 0x807 || type == 0x80f ||
           type >= 0x1c00 || InRange(type, 0x1807, 0x1813) || InRange(type, 0x1a0e, 0x1a13) || type == 0x1c15 ||
           type == 0x1a14 || type == 0x1a25;
}

// 0x5b17bf..0x5b1884: lo que suma un ítem gastado al precio de reparar todo. 0x5b1ba0 corre antes de mirar el tipo.
Core::Expected<void, std::string> AddTheRepairOf(WorldState& world, const World::ItemState& item,
                                                 const Data::Tables::ItemTable& items)
{
    WorldRuntime& runtime = world.Runtime();
    const auto record = items.Record(item.id);
    if (!record)
        return Core::Unexpected(
            std::format("0x5b17d3: el tipo {:#x} lee fuera de Item.bmd ([0x7758550] + tipo · 0x54)", item.id));
    const std::uint16_t maximum = MaxDurability(item, *record, LevelOf(item));
    if (IsLeftOutOfTheRepair(item.id) || item.durability >= maximum)
        return {};
    const PetPriceLevels pets{.raven = runtime.ravenLevel, .horse = runtime.darkHorseLevel};
    auto price = ItemPrice(item, items, RepairPriceMode, pets);
    if (!price.HasValue())
        return Core::Unexpected(std::move(price).Error());
    const RepairPrice repair = RepairPriceOf(price.Value(), item.durability, maximum, runtime.field78abb10);
    // 0x5b1879..0x5b1884: la suma en el int de 0x78ababc, que da la vuelta.
    NpcWindowState& npc = runtime.npcWindows;
    npc.repairAllPrice = static_cast<std::int32_t>(static_cast<std::uint32_t>(npc.repairAllPrice) +
                                                   static_cast<std::uint32_t>(repair.gold));
    return {};
}

// 0x5df417 y 0x5ded14..0x5dee3d: lo que vender pide confirmar.
bool IsValuable(const World::ItemState& item)
{
    const std::int32_t type = Signed(item.id);
    const std::int32_t level = LevelOf(item);
    const std::uint32_t kind = item.flags1c & 3u;
    // 0x402b30 da 2 o más solo con los paquetes de Bless (0x181e) y de Soul (0x181f).
    const bool bundle = type == 0x181e || type == 0x181f;
    return InRange(type, 0x1a00, 0x1a03) || type == 0x1a25 || type == 0x1c0d || type == 0x1c0e || type == 0x1c10 ||
           type == 0x1c16 || type == 0x1c1f || bundle || InRange(type, 0x1800, 0x1806) || type == 0x1a04 ||
           type == 0x1a05 || type == 0x1a1e || kind == 1 || kind == 2 || type == 0x13 || type == 0xa0a ||
           type == 0x812 || type == 0x40d || type == 0x180f || type == 0x1a0e || type == 0x1a0f ||
           (level > LastCheapLevel && type < 0x1800) || (item.flags1b & 0x3f) != 0;
}

// 0x5dee43..0x5dee79: lo que la tienda no compra.
bool IsUnsellable(const World::ItemState& item)
{
    const std::int32_t type = Signed(item.id);
    const std::int32_t level = LevelOf(item);
    return InRange(type, 0x1c17, 0x1c1a) || type == 0x1c0b || type == 0x1a13 ||
           (type == 0x1c14 && level >= 1 && level <= 5);
}

// 0x555190(0x6e57a4, texto, 2): el aviso de error, sin nombre.
Core::Expected<void, std::string> ErrorMessage(const GameUiContext& context, std::string_view text,
                                               std::string_view where)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::format("{}: el aviso (0x555190) sin el medidor de los textos", where));
    return AddGameMessage(context.world.Runtime(), *context.measure, context.screen, {}, text, ErrorMessageType);
}

Core::Expected<std::string_view, std::string> TextOf(const GameUiContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

Core::Expected<void, std::string> TextMessage(const GameUiContext& context, std::size_t index, std::string_view where)
{
    auto text = TextOf(context, index, where);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    return ErrorMessage(context, text.Value(), where);
}

// 0x5fe690..0x5feb0e: el botón de mezclar. El zen del registro propio (+0xecc, abierto y cerrado: 3.848 rand() sin
// otros accesos) contra el precio que dejó el dibujo (0x7d2dca0, sin signo): con el precio igual o mayor, el aviso
// 596. Si no, lo que pide el tipo de la mezcla (0x7d2dc98): los avisos de los negativos y del 0, o, con lugar en el
// inventario (0x5fd200) y sin ítem en el cursor, 0x78abb14 en 2 y el diálogo 0x98 con el texto 0x21b. El clic se
// gasta y suena 0x19, salvo con el zen que no alcanza, que no suena.
Core::Expected<void, std::string> PressTheMixButton(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    std::uint32_t zen = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        zen = context.world.OwnCharacter().zen;
    }
    if (npc.mixPrice >= zen)
    {
        // 0x5feafa..0x5feb0e.
        if (auto shown = TextMessage(context, NotEnoughZenText, "0x5feb06"); !shown.HasValue())
            return shown;
        runtime.movement.pendingClick = false;
        return {};
    }
    runtime.movement.pendingClick = false;
    switch (MixButtonActionFor(npc.mixKind))
    {
    case MixButtonAction::LevelTooLow:
    {
        // 0x5feaab..0x5feae2: wsprintfA con el texto 1171 y 10.
        auto format = TextOf(context, LevelTooLowText, "0x5feab6");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        if (auto shown = ErrorMessage(context, UI::Sprintf(format.Value(), std::int32_t{CapeLevel}), "0x5feae2");
            !shown.HasValue())
            return shown;
        break;
    }
    case MixButtonAction::NotTheSame:
        // 0x5fea87..0x5fea93.
        if (auto shown = TextMessage(context, NotTheSameText, "0x5fea93"); !shown.HasValue())
            return shown;
        break;
    case MixButtonAction::NothingToMix:
    {
        // 0x5feac4..0x5feae2: sprintf con el texto 580 y el 591.
        auto format = TextOf(context, NothingToMixText, "0x5fead2");
        auto mixing = TextOf(context, MixingText, "0x5fead2");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        if (!mixing.HasValue())
            return Core::Unexpected(mixing.Error());
        if (auto shown = ErrorMessage(context, UI::Sprintf(format.Value(), mixing.Value()), "0x5feae2");
            !shown.HasValue())
            return shown;
        break;
    }
    case MixButtonAction::Mix:
    {
        // 0x5fe9bd..0x5fea82.
        const MixRoom room = MixRoomFor(npc, npc.mixKind);
        if (!HasRoomFor(context.world.Inventory().cells, NpcGridColumns, NpcGridColumns, room.width, room.height))
        {
            if (auto shown = TextMessage(context, NoRoomText, "0x5fea27"); !shown.HasValue())
                return shown;
            break;
        }
        if (runtime.cursorItemCount > 0)
            break;
        npc.cursorChoice = MixChoice;
        npc.dialogAnswer = NoAnswerYet;
        if (auto opened = UI::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.texts, MixDialogLines,
                                             MixDialogText, ConfirmDialog);
            !opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        break;
    }
    case MixButtonAction::Nothing:
        break;
    }
    runtime.lastSoundId = ButtonSound;
    return {};
}

} // namespace

void OpenTheKeypad(WorldRuntime& runtime, Render::Models::CrtRandom& random, std::int32_t mode)
{
    NpcWindowState& npc = runtime.npcWindows;
    ShuffleTheKeypad(random, npc.keypad);
    npc.typedNumber.fill(0);
    runtime.ui.field78abb20 = mode;
}

Core::Expected<void, std::string> StepTheNpcShop(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& movement = runtime.movement;
    UI::PanelState& state = runtime.panels;
    if (!panels.IsOpen(UI::NpcShopPanel))
        return {};
    if (state.field78abb09 != 0)
    {
        const std::int32_t x = runtime.hud.npcWindowX;
        const std::int32_t top = runtime.hud.npcWindowY + RepairButtonsY;
        // 0x5ed9c1..0x5eda20: el cursor de reparar.
        if (OnButton(context, x + RepairCursorButtonX, top) && movement.pendingClick)
        {
            movement.pendingClick = false;
            const bool on = state.repairCursor == 0;
            state.repairCursor = on ? RepairCursorOn : std::uint8_t{0};
            state.repairMode = on ? 1 : 0;
        }
        // 0x5eda2e..0x5eda71: el modo de reparar, al revés.
        if (OnButton(context, x + RepairModeButtonX, top) && movement.pendingClick)
        {
            movement.pendingClick = false;
            state.repairMode ^= 1;
            state.repairCursor = state.repairMode != 0 ? RepairCursorOn : std::uint8_t{0};
        }
        // 0x5eda76..0x5ee0b9: reparar todo, C1 05 34 FF 00 en C3, sin gastar el clic.
        if (OnButton(context, x + RepairAllButtonX, top) && movement.pendingClick)
        {
            if (auto sent = SendInC3(context, Npc::WriteRepair(Npc::RepairEverything, 0), "0x5edaa7: el C1 05 34 FF");
                !sent.HasValue())
                return sent;
        }
        if (auto priced = PriceTheRepairOfEverything(context.world, context.random, ServicesOf(context).items);
            !priced.HasValue())
            return priced;
    }
    // 0x5ee0c1..0x5ee128.
    const std::int32_t x = runtime.hud.inventoryX;
    const std::int32_t y = runtime.hud.inventoryY;
    if (!OnButton(context, x + CloseButtonX, y + CloseButtonY) || !movement.pendingClick)
        return {};
    movement.pendingClick = false;
    movement.mouseUpdateTime = 0;
    movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
    if (auto closed = panels.Close(UI::InventoryPanel); !closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}

Core::Expected<void, std::string> PriceTheRepairOfEverything(WorldState& world, Render::Models::CrtRandom& random,
                                                             const Data::Tables::ItemTable* items)
{
    WorldRuntime& runtime = world.Runtime();
    runtime.npcWindows.repairAllPrice = 0;
    const Render::Models::LogicalAccess own(runtime.ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
    const auto table = [items]() -> Core::Expected<const Data::Tables::ItemTable*, std::string>
    {
        if (items == nullptr)
            return Core::Unexpected(std::string("0x5b17ce: reparar todo sin Item.bmd ([0x7758550])"));
        return items;
    };
    // 0x5b1793..0x5b1893: los doce lugares del equipo con tipo y +0x3c.
    for (const World::ItemState& worn : world.OwnCharacter().equipment)
    {
        if (worn.id == EmptyOwnItemId || worn.occupied == 0)
            continue;
        auto read = table();
        if (!read.HasValue())
            return Core::Unexpected(std::move(read).Error());
        if (auto added = AddTheRepairOf(world, worn, *read.Value()); !added.HasValue())
            return added;
    }
    // 0x5b1899..0x5b198a: las 64 casillas con +0x3c, sin mirar el tipo.
    for (const World::ItemState& cell : world.Inventory().cells)
    {
        if (cell.occupied == 0)
            continue;
        auto read = table();
        if (!read.HasValue())
            return Core::Unexpected(std::move(read).Error());
        if (auto added = AddTheRepairOf(world, cell, *read.Value()); !added.HasValue())
            return added;
    }
    return {};
}

Core::Expected<void, std::string> StepTheWarehouse(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& movement = runtime.movement;
    if (!panels.IsOpen(UI::WarehousePanel))
        return {};
    std::optional<std::int32_t> pressed{};
    for (std::size_t button = 0; button < WarehouseButtons.size(); ++button)
    {
        if (OnButton(context, runtime.hud.npcWindowX + WarehouseButtons[button].x,
                     runtime.hud.npcWindowY + WarehouseButtons[button].y))
        {
            pressed = static_cast<std::int32_t>(button);
            break;
        }
    }
    if (!pressed || !movement.pendingClick)
        return {};
    movement.pendingClick = false;
    runtime.lastSoundId = ButtonSound;
    NpcWindowState& npc = runtime.npcWindows;
    switch (*pressed)
    {
    case DepositButton:
    case WithdrawButton:
        // 0x5ea127..0x5ea1a5.
        UI::PushDialog(runtime.dialogs, MoneyDialog);
        UI::ResetTheTextBuffers(runtime.chat.input, false);
        runtime.ui.chatOpen = false;
        npc.typedMoney = 0;
        npc.warehouseMoneyMode = *pressed;
        npc.inputMode = MoneyDialogInputMode;
        runtime.chat.input.fieldCount = MoneyDialogFieldCount;
        npc.numberField = 1;
        return {};
    case KeypadButton:
        // 0x5ea1aa..0x5ea24c: el teclado en 4, o en 3 con la traba.
        OpenTheKeypad(runtime, context.random, UnlockedKeypadMode - (npc.warehouseLocked != 0 ? 1 : 0));
        return {};
    case CloseWarehouseButton:
        // 0x5ea24d..0x5ea261.
        if (auto closed = panels.Close(UI::WarehousePanel); !closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
        movement.mouseUpdateTime = 0;
        movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
        return {};
    default:
        return {};
    }
}

Core::Expected<void, std::string> StepTheChaosBox(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& movement = runtime.movement;
    if (!panels.IsOpen(UI::ChaosBoxPanel))
        return {};
    // 0x5fe62e..0x5fe68a.
    const std::int32_t left = runtime.hud.npcWindowX + MixButtonX;
    const std::int32_t top = runtime.hud.npcWindowY + MixButtonY;
    if (runtime.npcWindows.chaosMixState == 0 &&
        InRect(context, left, top, left + MixButtonWidth, top + MixButtonHeight) && movement.pendingClick)
    {
        if (auto mixed = PressTheMixButton(context); !mixed.HasValue())
            return mixed;
    }
    // 0x5feb15..0x5feb69: cerrar la caja, sin MouseUpdateTime.
    const std::int32_t x = runtime.hud.inventoryX;
    const std::int32_t y = runtime.hud.inventoryY;
    if (!OnButton(context, x + CloseButtonX, y + CloseButtonY) || !movement.pendingClick)
        return {};
    movement.pendingClick = false;
    if (auto closed = panels.Close(UI::ChaosBoxPanel); !closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}

Core::Expected<bool, std::string> StepTheCursorChoice(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    const std::int32_t choice = npc.cursorChoice;
    if (choice == SellChoice)
    {
        if (npc.dialogAnswer == YesAnswer)
        {
            // 0x5d8e0d..0x5d905d: C1 04 33 con [0x78a6758] en C3.
            npc.cursorChoice = 0;
            npc.dialogAnswer = NoAnswerYet;
            if (auto sent = SendInC3(context, Npc::WriteSell(static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff)),
                                     "0x5d8e1a: el C1 04 33");
                !sent.HasValue())
                return Core::Unexpected(std::move(sent).Error());
            return true;
        }
        if (npc.dialogAnswer == NoAnswer)
        {
            // 0x5d9327..0x5d9334 → 0x5e0709.
            npc.cursorChoice = 0;
            npc.dialogAnswer = NoAnswerYet;
            if (auto returned = ReturnTheCursorItem(MovesOf(context)); !returned.HasValue())
                return Core::Unexpected(std::move(returned).Error());
        }
        return true;
    }
    if (choice == MixChoice)
    {
        if (npc.dialogAnswer == YesAnswer)
        {
            // 0x5d934f..0x5d9633: la mezcla pedida y el C1 04 86 con el XOR32, en claro.
            npc.cursorChoice = 0;
            npc.dialogAnswer = NoAnswerYet;
            npc.chaosMixState = MixRequested;
            if (auto sent = Send(context, Npc::WriteChaosMix(static_cast<std::uint8_t>(npc.mixKind & 0xff)),
                                 "0x5d9362: el C1 04 86");
                !sent.HasValue())
                return Core::Unexpected(std::move(sent).Error());
        }
        else if (npc.dialogAnswer == NoAnswer)
        {
            // 0x5d9634..0x5d9659.
            npc.cursorChoice = 0;
            npc.dialogAnswer = NoAnswerYet;
        }
        return true;
    }
    if (choice == KeypadChoice)
    {
        if (npc.dialogAnswer == YesAnswer)
        {
            // 0x5d9b8f..0x5d9c23: el teclado del almacén barajado (veinte veces dos rand() % 10), lo tecleado
            // (0x78ab9fc..0x78aba06) en 0 y 0x78abb20 en 6.
            npc.cursorChoice = 0;
            npc.dialogAnswer = NoAnswerYet;
            OpenTheKeypad(runtime, context.random, KeypadChoiceMode);
        }
        else if (npc.dialogAnswer == NoAnswer)
        {
            // 0x5d9c47..0x5d9c56: y 0x78abb08, el byte del panel 0x2000, en 0.
            npc.cursorChoice = 0;
            npc.dialogAnswer = NoAnswerYet;
            runtime.panels.field78abb08 = 0;
        }
        return true;
    }
    if (choice == TradeChoice || choice == PersonalShopChoice)
    {
        if (npc.dialogAnswer == YesAnswer)
        {
            if (choice == TradeChoice)
                return Core::Unexpected(std::string("0x5d966b: 0x78abb14 en 3 con el sí, el OK del comercio que da "
                                                    "vuelta 0x78abac1 y manda el C1 04 3C, es del hito 7 (CL9.4)"));
            return Core::Unexpected(std::string("0x5d9c82: 0x78abb14 en 5 con el sí, el C1 3F 06 de la tienda "
                                                "personal, es del hito 7 (CL9.4, 08 P-C339)"));
        }
        if (npc.dialogAnswer == NoAnswer)
        {
            // 0x5d9634..0x5d9659, como el no de la mezcla.
            npc.cursorChoice = 0;
            npc.dialogAnswer = NoAnswerYet;
        }
        return true;
    }
    if (choice == ShopPriceChoice)
    {
        if (npc.dialogAnswer == YesAnswer)
            return Core::Unexpected(std::string("0x5da5ac: 0x78abb14 en 6 con el sí, el precio de la tienda personal "
                                                "con su C1 3F, es del hito 7 (CL9.4)"));
        if (npc.dialogAnswer == NoAnswer)
            return Core::Unexpected(std::string("0x5dc765..0x5dc778: 0x78abb14 en 6 con el no, 0x78abb14 en 0, la "
                                                "respuesta en 0xff y 0x5eba70(2), el diálogo del precio de la tienda "
                                                "personal, es del hito 7 (CL9.4)"));
        return true;
    }
    return false;
}

Core::Expected<bool, std::string> SellTheCursorItem(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    const World::ItemState& cursor = runtime.cursorItem;
    // 0x5deca7..0x5ded0e: el nivel se lee antes del IsOpen, que saca su rand().
    if (!panels.IsOpen(UI::NpcShopPanel))
        return false;
    const std::int32_t x = runtime.hud.npcWindowX;
    const std::int32_t y = runtime.hud.npcWindowY;
    if (!InRect(context, x + ShopGridLeft, y + ShopGridTop, x + ShopGridRight, y + ShopGridBottom))
        return false;
    if (IsValuable(cursor))
    {
        // 0x5df417..0x5df430.
        npc.cursorChoice = SellChoice;
        npc.dialogAnswer = NoAnswerYet;
        if (auto opened = UI::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.texts, SellDialogLines,
                                             SellDialogText, ConfirmDialog);
            !opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        return true;
    }
    if (IsUnsellable(cursor))
        return true;
    // 0x5dee7f..0x5df416.
    npc.dialogAnswer = NoAnswerYet;
    npc.cursorChoice = 0;
    if (auto sent = SendInC3(context, Npc::WriteSell(static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff)),
                             "0x5dee8e: el C1 04 33");
        !sent.HasValue())
        return Core::Unexpected(std::move(sent).Error());
    return true;
}

Core::Expected<void, std::string> ConfirmTheMoneyDialog(const GameUiContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    UI::DialogState& dialogs = runtime.dialogs;
    const std::int32_t money = npc.typedMoney;
    if (money > MaxTypedMoney)
    {
        // 0x62646c..0x626499.
        CloseTheDialog(dialogs);
        UI::PushDialog(dialogs, TooMuchMoneyDialog);
    }
    else
    {
        GamePanels panels(world, context.random, GamePanelTables{.texts = context.texts});
        if (panels.Registry().IsOpen(UI::TradePanel))
            return Core::Unexpected(std::string("0x6264b3: el zen del comercio (0x78a5a1c) es de la CL9.4"));
        // 0x6270ed..0x62820a: con el registro propio abierto, que se cierra al final (3.848 rand()).
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        const OwnCharacterRecord& record = world.OwnCharacter();
        const auto amount = static_cast<std::uint32_t>(money);
        if (npc.warehouseMoneyMode == 0)
        {
            // 0x627100..0x6278f3: meter zen. El zen contra el del registro, sin signo.
            CloseTheDialog(dialogs);
            if (amount > record.zen)
                UI::PushDialog(dialogs, NotEnoughMoneyDialog);
            else if (auto sent = Send(context, Npc::WriteWarehouseMoney(DepositType, amount), "0x627229: el C1 08 81");
                     !sent.HasValue())
                return sent;
        }
        else
        {
            // 0x627bb9..0x628030: sacar zen. Más que el del almacén (con signo) o más de 2.000.000.000 en el
            // inventario (sin signo) es el error, que solo avisa sin pasar ese tope.
            const std::uint32_t sum = record.zen + amount;
            CloseTheDialog(dialogs);
            if (money > static_cast<std::int32_t>(record.warehouseZen) || sum > MaxZen)
            {
                if (sum <= MaxZen)
                    UI::PushDialog(dialogs, NotEnoughMoneyDialog);
            }
            else if (npc.warehouseLocked != 0 && npc.field78abb1c == 0)
            {
                // 0x627bf7..0x627c33: espera la contraseña.
                npc.pendingMove = PendingWarehouseMove{
                    .slot = PendingMoney, .second = WithdrawType, .third = money, .fourth = runtime.itemMoveTarget};
                OpenTheKeypad(runtime, context.random, PendingKeypadMode);
            }
            else if (auto sent = Send(context, Npc::WriteWarehouseMoney(WithdrawType, amount), "0x627c46: el C1 08 81");
                     !sent.HasValue())
                return sent;
        }
    }
    // 0x628215..0x6282c8.
    UI::ResetTheTextBuffers(runtime.chat.input, false);
    npc.inputMode = ClosedInputMode;
    runtime.chat.input.fieldCount = ClosedFieldCount;
    npc.numberField = 0;
    runtime.ui.chatOpen = false;
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
    runtime.lastSoundId = ButtonSound;
    return {};
}
} // namespace Client::Scenes
