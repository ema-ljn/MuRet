#include "Npc/NpcWindowSteps.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Protocol/Npc/NpcWindows.h"
#include "Protocol/Session/Logout.h"
#include "Protocol/Outgoing.h"
#include "Models/LogicalAccess.h"
#include "ChaosMix/ChaosMix.h"
#include "Chat/GameMessages.h"
#include "Hud/GamePanels.h"
#include "Ui/GameUiContext.h"
#include "Items/HudItems.h"
#include "Items/InventoryMoves.h"
#include "Items/ItemPrice.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameText.h"
#include "Crt/SprintfFormat.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"
#include "Chat/ChatRuntime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Npc
{
namespace
{
namespace Npc = Protocol::Npc;

// 0x41ed10(0x19, 0, 0): the buttons' sound.
constexpr Audio::SoundId ButtonSound = Sound::ButtonClickSound;
// 0x6c8dcc: the maximum of MouseUpdateTime that the buttons leave.
constexpr std::int32_t ButtonMouseUpdateTimeMax = 6;
// The padding rand() (0x69fd70) and the serial number's (0x40b0a0, or 0x40b240 and 0x40b3f0 inline) of a C3.
constexpr std::size_t C3SendRandoms = 2;
constexpr std::int32_t ButtonSize = 0x18;
// 0x5ee0cd..0x5ee0f3 and 0x5feb22..0x5feb48: the close button, in the corner of the inventory's foot.
constexpr std::int32_t CloseButtonX = 0x19;
constexpr std::int32_t CloseButtonY = 0x18b;
// 0x5ed9b5..0x5eda97: the shop's three repair buttons, at y + 0x16d.
constexpr std::int32_t RepairButtonsY = 0x16d;
constexpr std::int32_t RepairCursorButtonX = 0x19;
constexpr std::int32_t RepairModeButtonX = 0x55;
constexpr std::int32_t RepairAllButtonX = 0x73;
// 0x5ed9fc..0x5eda71: 0x78abb27 at 2 with the repair mode on.
constexpr std::uint8_t RepairCursorOn = 2;
// 0x5ea090..0x5ea0ba: the warehouse's four buttons.
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
// 0x5ea127 and 0x5ea169: the warehouse zen dialog.
constexpr std::int32_t MoneyDialog = 0x73;
// 0x5ea150..0x5ea15f: the dialog field's length and the Tab fields with the 0x73 open.
constexpr std::int32_t MoneyDialogFieldLimit = 8;
constexpr std::int32_t MoneyDialogFieldCount = 1;
// 0x5ea1aa..0x5ea1b3: button 2's keypad, at 4 without the lock and at 3 with it.
constexpr std::int32_t UnlockedKeypadMode = 4;
// 0x5cb43c and 0x627c33: the password keypad that the warehouse waits for.
constexpr std::int32_t PendingKeypadMode = 1;
// 0x5fe64f..0x5fe67d: the chaos box's mix button.
constexpr std::int32_t MixButtonX = 0x4b;
constexpr std::int32_t MixButtonY = 300;
constexpr std::int32_t MixButtonWidth = 0x2c;
constexpr std::int32_t MixButtonHeight = 0x20;
// 0x5fea4f..0x5fea6c: 0x633300(1, 0x21b, 0x98), the mix dialog.
constexpr std::int32_t MixDialogLines = 1;
constexpr std::int32_t MixDialogText = 0x21b;
// The mix button's warnings (0x5fe982..0x5feb06) and their type (0x555190 with 2).
constexpr std::size_t NotEnoughZenText = 596;
constexpr std::size_t LevelTooLowText = 1171;
constexpr std::size_t NotTheSameText = 600;
constexpr std::size_t NothingToMixText = 580;
constexpr std::size_t MixingText = 591;
constexpr std::size_t NoRoomText = 581;
constexpr std::int32_t ErrorMessageType = 2;
// 0x5feaab: the hero level that the cape requires, the 10 of the wsprintfA.
constexpr std::int32_t CapeLevel = 10;
// 0x5decd9..0x5ded0e: the shop grid, where one sells.
constexpr std::int32_t ShopGridLeft = 0xf;
constexpr std::int32_t ShopGridRight = 0xc3;
constexpr std::int32_t ShopGridTop = 0x32;
constexpr std::int32_t ShopGridBottom = 0x172;
// 0x5df417..0x5df430: 0x633300(3, 0x218, 0x98), the dialog for selling something valuable.
constexpr std::int32_t SellDialogLines = 3;
constexpr std::int32_t SellDialogText = 0x218;
constexpr std::int32_t ConfirmDialog = 0x98;
// 0x78abb14: what the dialog of the cursor's item waits for.
constexpr std::int32_t SellChoice = 1;
constexpr std::int32_t MixChoice = 2;
// 0x5d9b82: the panel 0x2000's step leaves the 4 (0x5e75fc, with the 0x98 of text 0x1c0). The 3, the 5 and the 6 are
// the trade's and the personal shops' (PersonalShop/PersonalShopWindow.h).
constexpr std::int32_t KeypadChoice = 4;
// 0x5d9c19: the warehouse keypad that the 4's yes opens.
constexpr std::int32_t KeypadChoiceMode = 6;
// 0x6c932e: the 0x98's yes (0x62a779) and none.
constexpr std::uint8_t YesAnswer = 1;
constexpr std::uint8_t NoAnswerYet = 0xff;
// 0x5d935c: the mix requested.
constexpr std::int32_t MixRequested = 1;
// 0x627bd9 and 0x628004: the zen does not go past 2000000000; without the zen, the 0x74.
constexpr std::uint32_t MaxZen = 2'000'000'000;
constexpr std::int32_t NotEnoughMoneyDialog = 0x74;
// 0x627c13..0x627c2d: what the password waits for to withdraw zen: −1, type 1, the zen and 0x77f3cc8.
constexpr std::int32_t PendingMoney = -1;
constexpr std::int32_t WithdrawType = 1;
constexpr std::uint8_t DepositType = 0;
// 0x5b17f2..0x5b1856 and 0x5ded19..0x5dee79: the types, signed.
constexpr std::uint32_t LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;
constexpr std::int32_t LastCheapLevel = 4;

// 0x5e74f0: the body uses the fixed margin; the buttons read the corner held from 0x78a5a14/10.
constexpr std::int32_t Panel2000Left = 450;
constexpr std::int32_t Panel2000Right = 640;
constexpr std::int32_t Panel2000Bottom = 433;
constexpr std::int32_t AcceptanceLeft = 25;
constexpr std::int32_t AcceptanceTop = 240;
constexpr std::int32_t AcceptanceSize = 16;
constexpr std::int32_t Panel2000ButtonLeft = 35;
constexpr std::int32_t Panel2000ButtonWidth = 120;
constexpr std::int32_t Panel2000AcceptTop = 320;
constexpr std::int32_t Panel2000BackTop = 350;
constexpr std::int32_t Panel2000ButtonHeight = 24;
constexpr std::int32_t Panel2000QuestionText = 0x1c0;

bool InRect(const Ui::GameUiContext& context, std::int32_t left, std::int32_t top, std::int32_t right,
            std::int32_t bottom)
{
    return context.mouseX >= left && context.mouseX < right && context.mouseY >= top && context.mouseY < bottom;
}

bool OnButton(const Ui::GameUiContext& context, std::int32_t left, std::int32_t top)
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

std::int32_t LevelOf(const Kernel::ItemState& item)
{
    return static_cast<std::int32_t>((item.scalar1Low >> LevelShift) & LevelMask);
}

const Items::InventoryServices& ServicesOf(const Ui::GameUiContext& context)
{
    static const Items::InventoryServices None{};
    return context.inventory != nullptr ? *context.inventory : None;
}

Items::InventoryMoveContext MovesOf(const Ui::GameUiContext& context)
{
    return Items::InventoryMoveContext{
        .world = context.world, .random = context.random, .services = ServicesOf(context)};
}

Core::Expected<void, std::string> Send(const Ui::GameUiContext& context, Protocol::Outgoing packet,
                                       std::string_view what)
{
    if (!context.sendNpcWindow)
        return Core::Unexpected(std::format("{} sin la sesión", what));
    return context.sendNpcWindow(std::move(packet));
}

// A C1 with the XOR32 and in C3: the send's two rand(), before the packet.
Core::Expected<void, std::string> SendInC3(const Ui::GameUiContext& context, Protocol::Outgoing packet,
                                           std::string_view what)
{
    context.random.Discard(C3SendRandoms);
    return Send(context, std::move(packet), what);
}

// 0x62da70 and its copies: the waiting one becomes open.
void CloseTheDialog(Ui::DialogState& dialogs)
{
    dialogs.current = dialogs.pending;
    dialogs.pending = 0;
}

// 0x5b17f2..0x5b1856: what repair all does not count.
bool IsLeftOutOfTheRepair(std::uint16_t id)
{
    const std::int32_t type = Signed(id);
    return InRange(type, 0x1a00, 0x1a05) || type == 0x1a0a || type == 0x1a1f || type == 0x807 || type == 0x80f ||
           type >= 0x1c00 || InRange(type, 0x1807, 0x1813) || InRange(type, 0x1a0e, 0x1a13) || type == 0x1c15 ||
           type == 0x1a14 || type == 0x1a25;
}

// 0x5b17bf..0x5b1884: what a worn item adds to the repair-all price. 0x5b1ba0 runs before looking at the type.
Core::Expected<void, std::string> AddTheRepairOf(Kernel::WorldState& world, const Kernel::ItemState& item,
                                                 const Tables::ItemTable& items)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    const auto record = items.Record(item.id);
    if (!record)
        return Core::Unexpected(
            std::format("0x5b17d3: el tipo {:#x} lee fuera de Item.bmd ([0x7758550] + tipo · 0x54)", item.id));
    const std::uint16_t maximum = Items::MaxDurability(item, *record, LevelOf(item));
    if (IsLeftOutOfTheRepair(item.id) || item.durability >= maximum)
        return {};
    const Items::PetPriceLevels pets{.raven = runtime.pets[Kernel::RavenPetStats].level,
                                     .horse = runtime.pets[Kernel::DarkHorsePetStats].level};
    auto price = Items::ItemPrice(item, items, Items::RepairPriceMode, pets);
    if (!price.HasValue())
        return Core::Unexpected(std::move(price).Error());
    const Items::RepairPrice repair =
        Items::RepairPriceOf(price.Value(), item.durability, maximum, runtime.field78abb10);
    // 0x5b1879..0x5b1884: the sum in the int at 0x78ababc, which wraps around.
    NpcWindowState& npc = runtime.npcWindows;
    npc.repairAllPrice = static_cast<std::int32_t>(static_cast<std::uint32_t>(npc.repairAllPrice) +
                                                   static_cast<std::uint32_t>(repair.gold));
    return {};
}

// 0x5df417 and 0x5ded14..0x5dee3d: what selling asks to confirm.
bool IsValuable(const Kernel::ItemState& item)
{
    const std::int32_t type = Signed(item.id);
    const std::int32_t level = LevelOf(item);
    const std::uint32_t kind = item.flags1c & 3u;
    // 0x402b30 gives 2 or more only with the Bless (0x181e) and Soul (0x181f) bundles.
    const bool bundle = type == 0x181e || type == 0x181f;
    return InRange(type, 0x1a00, 0x1a03) || type == 0x1a25 || type == 0x1c0d || type == 0x1c0e || type == 0x1c10 ||
           type == 0x1c16 || type == 0x1c1f || bundle || InRange(type, 0x1800, 0x1806) || type == 0x1a04 ||
           type == 0x1a05 || type == 0x1a1e || kind == 1 || kind == 2 || type == 0x13 || type == 0xa0a ||
           type == 0x812 || type == 0x40d || type == 0x180f || type == 0x1a0e || type == 0x1a0f ||
           (level > LastCheapLevel && type < 0x1800) || (item.flags1b & 0x3f) != 0;
}

// 0x5dee43..0x5dee79: what the shop does not buy.
bool IsUnsellable(const Kernel::ItemState& item)
{
    const std::int32_t type = Signed(item.id);
    const std::int32_t level = LevelOf(item);
    return InRange(type, 0x1c17, 0x1c1a) || type == 0x1c0b || type == 0x1a13 ||
           (type == 0x1c14 && level >= 1 && level <= 5);
}

// 0x555190(0x6e57a4, text, 2): the error warning, with no name.
Core::Expected<void, std::string> ErrorMessage(const Ui::GameUiContext& context, std::string_view text,
                                               std::string_view where)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::format("{}: el aviso (0x555190) sin el medidor de los textos", where));
    return Chat::AddGameMessage(context.world.Runtime(), *context.measure, context.screen, {}, text, ErrorMessageType);
}

Core::Expected<std::string_view, std::string> TextOf(const Ui::GameUiContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

Core::Expected<void, std::string> TextMessage(const Ui::GameUiContext& context, std::size_t index,
                                              std::string_view where)
{
    auto text = TextOf(context, index, where);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    return ErrorMessage(context, text.Value(), where);
}

// 0x5fe690..0x5feb0e: the mix button. The own record's zen (+0xecc, opened and closed: 3848 rand() without other
// accesses) against the price the drawing left (0x7d2dca0, unsigned): with the price equal or greater, warning 596.
// Otherwise, what the mix kind (0x7d2dc98) asks for: the warnings of the negatives and the 0, or, with room in the
// inventory (0x5fd200) and no item on the cursor, 0x78abb14 at 2 and dialog 0x98 with text 0x21b. The click is spent
// and 0x19 sounds, except when the zen is not enough, which does not sound.
Core::Expected<void, std::string> PressTheMixButton(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    std::uint32_t zen = 0;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        zen = context.world.OwnCharacter().zen;
    }
    if (npc.mixPrice >= zen)
    {
        // 0x5feafa..0x5feb0e.
        if (auto shown = TextMessage(context, NotEnoughZenText, "0x5feb06"); !shown.HasValue())
            return shown;
        runtime.mouse.pendingClick = false;
        return {};
    }
    runtime.mouse.pendingClick = false;
    switch (ChaosMix::MixButtonActionFor(npc.mixKind))
    {
    case ChaosMix::MixButtonAction::LevelTooLow:
    {
        // 0x5feaab..0x5feae2: wsprintfA with text 1171 and 10.
        auto format = TextOf(context, LevelTooLowText, "0x5feab6");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        if (auto shown = ErrorMessage(context, Crt::Sprintf(format.Value(), std::int32_t{CapeLevel}), "0x5feae2");
            !shown.HasValue())
            return shown;
        break;
    }
    case ChaosMix::MixButtonAction::NotTheSame:
        // 0x5fea87..0x5fea93.
        if (auto shown = TextMessage(context, NotTheSameText, "0x5fea93"); !shown.HasValue())
            return shown;
        break;
    case ChaosMix::MixButtonAction::NothingToMix:
    {
        // 0x5feac4..0x5feae2: sprintf with text 580 and 591.
        auto format = TextOf(context, NothingToMixText, "0x5fead2");
        auto mixing = TextOf(context, MixingText, "0x5fead2");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        if (!mixing.HasValue())
            return Core::Unexpected(mixing.Error());
        if (auto shown = ErrorMessage(context, Crt::Sprintf(format.Value(), mixing.Value()), "0x5feae2");
            !shown.HasValue())
            return shown;
        break;
    }
    case ChaosMix::MixButtonAction::Mix:
    {
        // 0x5fe9bd..0x5fea82.
        const ChaosMix::MixRoom room = ChaosMix::MixRoomFor(npc, npc.mixKind);
        if (!ChaosMix::HasRoomFor(context.world.Inventory().cells, NpcGridColumns, NpcGridColumns, room.width,
                                  room.height))
        {
            if (auto shown = TextMessage(context, NoRoomText, "0x5fea27"); !shown.HasValue())
                return shown;
            break;
        }
        if (runtime.cursorItemCount > 0)
            break;
        runtime.social.trade.warningKind = MixChoice;
        runtime.social.trade.warningByte = NoAnswerYet;
        if (auto opened = Ui::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.texts, MixDialogLines,
                                             MixDialogText, ConfirmDialog);
            !opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        break;
    }
    case ChaosMix::MixButtonAction::Nothing:
        break;
    }
    runtime.RequestSound(ButtonSound);
    return {};
}

} // namespace

void OpenTheKeypad(Kernel::WorldRuntime& runtime, Crt::CrtRandom& random, std::int32_t mode)
{
    NpcWindowState& npc = runtime.npcWindows;
    Items::ShuffleTheKeypad(random, npc.keypad);
    npc.typedNumber.fill(0);
    runtime.ui.field78abb20 = mode;
}

Core::Expected<void, std::string> StepTheNpcShop(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::MouseState& mouse = runtime.mouse;
    Ui::PanelState& state = runtime.panels;
    if (!panels.IsOpen(Ui::NpcShopPanel))
        return {};
    if (state.field78abb09 != 0)
    {
        const std::int32_t x = runtime.ui.npcWindowX;
        const std::int32_t top = runtime.ui.npcWindowY + RepairButtonsY;
        // 0x5ed9c1..0x5eda20: the repair cursor.
        if (OnButton(context, x + RepairCursorButtonX, top) && mouse.pendingClick)
        {
            mouse.pendingClick = false;
            const bool on = state.repairCursor == 0;
            state.repairCursor = on ? RepairCursorOn : std::uint8_t{0};
            state.repairMode = on ? 1 : 0;
        }
        // 0x5eda2e..0x5eda71: the repair mode, toggled.
        if (OnButton(context, x + RepairModeButtonX, top) && mouse.pendingClick)
        {
            mouse.pendingClick = false;
            state.repairMode ^= 1;
            state.repairCursor = state.repairMode != 0 ? RepairCursorOn : std::uint8_t{0};
        }
        // 0x5eda76..0x5ee0b9: repair all, C1 05 34 FF 00 in C3, without spending the click.
        if (OnButton(context, x + RepairAllButtonX, top) && mouse.pendingClick)
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
    if (!OnButton(context, x + CloseButtonX, y + CloseButtonY) || !mouse.pendingClick)
        return {};
    mouse.pendingClick = false;
    mouse.updateTime = 0;
    mouse.updateTimeMax = ButtonMouseUpdateTimeMax;
    if (auto closed = panels.Close(Ui::InventoryPanel); !closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}

Core::Expected<void, std::string> PriceTheRepairOfEverything(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                             const Tables::ItemTable* items)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    runtime.npcWindows.repairAllPrice = 0;
    const Models::LogicalAccess own(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    const auto table = [items]() -> Core::Expected<const Tables::ItemTable*, std::string>
    {
        if (items == nullptr)
            return Core::Unexpected(std::string("0x5b17ce: reparar todo sin Item.bmd ([0x7758550])"));
        return items;
    };
    // 0x5b1793..0x5b1893: the twelve equipment slots with a type and +0x3c.
    for (const Kernel::ItemState& worn : world.OwnCharacter().equipment)
    {
        if (worn.id == Kernel::EmptyOwnItemId || worn.occupied == 0)
            continue;
        auto read = table();
        if (!read.HasValue())
            return Core::Unexpected(std::move(read).Error());
        if (auto added = AddTheRepairOf(world, worn, *read.Value()); !added.HasValue())
            return added;
    }
    // 0x5b1899..0x5b198a: the 64 cells with +0x3c, without looking at the type.
    for (const Kernel::ItemState& cell : world.Inventory().cells)
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

Core::Expected<void, std::string> StepTheWarehouse(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::MouseState& mouse = runtime.mouse;
    if (!panels.IsOpen(Ui::WarehousePanel))
        return {};
    std::optional<std::int32_t> pressed{};
    for (std::size_t button = 0; button < WarehouseButtons.size(); ++button)
    {
        if (OnButton(context, runtime.ui.npcWindowX + WarehouseButtons[button].x,
                     runtime.ui.npcWindowY + WarehouseButtons[button].y))
        {
            pressed = static_cast<std::int32_t>(button);
            break;
        }
    }
    if (!pressed || !mouse.pendingClick)
        return {};
    mouse.pendingClick = false;
    runtime.RequestSound(ButtonSound);
    NpcWindowState& npc = runtime.npcWindows;
    Social::SocialState& social = runtime.social;
    switch (*pressed)
    {
    case DepositButton:
    case WithdrawButton:
        // 0x5ea127..0x5ea1a5.
        Ui::PushDialog(runtime.dialogs, MoneyDialog);
        Ui::ResetTheTextBuffers(runtime.chat.input, false);
        runtime.ui.chatOpen = false;
        social.typedMoney = 0;
        social.trade.field78abacc = *pressed;
        social.dialogFieldLimit = MoneyDialogFieldLimit;
        runtime.chat.input.fieldCount = MoneyDialogFieldCount;
        social.moneyField = 1;
        return {};
    case KeypadButton:
        // 0x5ea1aa..0x5ea24c: the keypad at 4, or at 3 with the lock.
        OpenTheKeypad(runtime, context.random, UnlockedKeypadMode - (npc.warehouseLocked != 0 ? 1 : 0));
        return {};
    case CloseWarehouseButton:
        // 0x5ea24d..0x5ea261.
        if (auto closed = panels.Close(Ui::WarehousePanel); !closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
        mouse.updateTime = 0;
        mouse.updateTimeMax = ButtonMouseUpdateTimeMax;
        return {};
    default:
        return {};
    }
}

Core::Expected<void, std::string> StepTheChaosBox(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::MouseState& mouse = runtime.mouse;
    if (!panels.IsOpen(Ui::ChaosBoxPanel))
        return {};
    // 0x5fe62e..0x5fe68a.
    const std::int32_t left = runtime.ui.npcWindowX + MixButtonX;
    const std::int32_t top = runtime.ui.npcWindowY + MixButtonY;
    if (runtime.npcWindows.chaosMixState == 0 &&
        InRect(context, left, top, left + MixButtonWidth, top + MixButtonHeight) && mouse.pendingClick)
    {
        if (auto mixed = PressTheMixButton(context); !mixed.HasValue())
            return mixed;
    }
    // 0x5feb15..0x5feb69: closing the box, without MouseUpdateTime.
    const std::int32_t x = runtime.hud.inventoryX;
    const std::int32_t y = runtime.hud.inventoryY;
    if (!OnButton(context, x + CloseButtonX, y + CloseButtonY) || !mouse.pendingClick)
        return {};
    mouse.pendingClick = false;
    if (auto closed = panels.Close(Ui::ChaosBoxPanel); !closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}

Core::Expected<void, std::string> AnswerTheNpcWarning(const Ui::GameUiContext& context, std::int32_t kind)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    Social::TradeState& trade = runtime.social.trade;
    const bool yes = trade.warningByte == YesAnswer;
    trade.warningKind = 0;
    trade.warningByte = NoAnswerYet;
    if (kind == SellChoice)
    {
        if (yes)
        {
            // 0x5d8e0d..0x5d905d: C1 04 33 with [0x78a6758] in C3.
            if (auto sent = SendInC3(context, Npc::WriteSell(static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff)),
                                     "0x5d8e1a: el C1 04 33");
                !sent.HasValue())
                return sent;
            return {};
        }
        // 0x5d9327..0x5d9334 → 0x5e0709.
        return Items::ReturnTheCursorItem(MovesOf(context));
    }
    if (kind == MixChoice)
    {
        // 0x5d934f..0x5d9633: the mix requested and the C1 04 86 with the XOR32, in the clear; the no
        // (0x5d9634..0x5d9659), nothing.
        if (!yes)
            return {};
        npc.chaosMixState = MixRequested;
        return Send(context, Npc::WriteChaosMix(static_cast<std::uint8_t>(npc.mixKind & 0xff)),
                    "0x5d9362: el C1 04 86");
    }
    if (kind == KeypadChoice)
    {
        // 0x5d9b8f..0x5d9c23: the warehouse keypad shuffled (twenty times two rand() % 10), what was typed
        // (0x78ab9fc..0x78aba06) at 0 and 0x78abb20 at 6. The no (0x5d9c47..0x5d9c56), 0x78abb08, the byte of panel
        // 0x2000, at 0.
        if (yes)
            OpenTheKeypad(runtime, context.random, KeypadChoiceMode);
        else
            runtime.panels.field78abb08 = 0;
        return {};
    }
    return Core::Unexpected(std::format("0x5d8dee: el aviso {} de 0x78abb14 no es de las ventanas de NPC", kind));
}

Core::Expected<bool, std::string> SellTheCursorItem(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Kernel::ItemState& cursor = runtime.cursorItem;
    // 0x5deca7..0x5ded0e: the level is read before the IsOpen, which draws its rand().
    if (!panels.IsOpen(Ui::NpcShopPanel))
        return false;
    const std::int32_t x = runtime.ui.npcWindowX;
    const std::int32_t y = runtime.ui.npcWindowY;
    if (!InRect(context, x + ShopGridLeft, y + ShopGridTop, x + ShopGridRight, y + ShopGridBottom))
        return false;
    if (IsValuable(cursor))
    {
        // 0x5df417..0x5df430.
        runtime.social.trade.warningKind = SellChoice;
        runtime.social.trade.warningByte = NoAnswerYet;
        if (auto opened = Ui::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.texts, SellDialogLines,
                                             SellDialogText, ConfirmDialog);
            !opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        return true;
    }
    if (IsUnsellable(cursor))
        return true;
    // 0x5dee7f..0x5df416.
    runtime.social.trade.warningByte = NoAnswerYet;
    runtime.social.trade.warningKind = 0;
    if (auto sent = SendInC3(context, Npc::WriteSell(static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff)),
                             "0x5dee8e: el C1 04 33");
        !sent.HasValue())
        return Core::Unexpected(std::move(sent).Error());
    return true;
}

Core::Expected<void, std::string> MoveTheWarehouseMoney(const Ui::GameUiContext& context)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    Ui::DialogState& dialogs = runtime.dialogs;
    const std::int32_t money = runtime.social.typedMoney;
    // 0x6270ed..0x62820a: with the own record open, which closes at the end (3848 rand()).
    const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    const Kernel::OwnCharacterRecord& record = world.OwnCharacter();
    const auto amount = static_cast<std::uint32_t>(money);
    if (runtime.social.trade.field78abacc == 0)
    {
        // 0x627100..0x6278f3: depositing zen. The zen against the record's, unsigned.
        CloseTheDialog(dialogs);
        if (amount > record.zen)
            Ui::PushDialog(dialogs, NotEnoughMoneyDialog);
        else if (auto sent = Send(context, Npc::WriteWarehouseMoney(DepositType, amount), "0x627229: el C1 08 81");
                 !sent.HasValue())
            return sent;
        return {};
    }
    // 0x627bb9..0x628030: withdrawing zen. More than the warehouse's (signed) or more than 2000000000 in the inventory
    // (unsigned) is the error, which only warns without passing that cap.
    const std::uint32_t sum = record.zen + amount;
    CloseTheDialog(dialogs);
    if (money > static_cast<std::int32_t>(record.warehouseZen) || sum > MaxZen)
    {
        if (sum <= MaxZen)
            Ui::PushDialog(dialogs, NotEnoughMoneyDialog);
    }
    else if (npc.warehouseLocked != 0 && npc.field78abb1c == 0)
    {
        // 0x627bf7..0x627c33: waits for the password.
        npc.pendingMove = PendingWarehouseMove{
            .slot = PendingMoney, .second = WithdrawType, .third = money, .fourth = runtime.itemMoveTarget};
        OpenTheKeypad(runtime, context.random, PendingKeypadMode);
    }
    else if (auto sent = Send(context, Npc::WriteWarehouseMoney(WithdrawType, amount), "0x627c46: el C1 08 81");
             !sent.HasValue())
        return sent;
    return {};
}
namespace
{
Core::Expected<void, std::string> LeaveThePanel2000(const Ui::GameUiContext& context, Ui::PanelRegistry& panels,
                                                    bool cross)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    runtime.mouse.pendingClick = false;
    if (cross)
        runtime.panels.field78abb00 = 0;
    else
        runtime.mouse.leftHeld = false;
    runtime.mouse.updateTime = 0;
    runtime.mouse.updateTimeMax = ButtonMouseUpdateTimeMax;
    if (auto sent = Send(context, Protocol::Session::WriteWindowClose(), "0x5e7738/0x5e7992: el C1 03 31");
        !sent.HasValue())
        return sent;
    return panels.CloseAll();
}
} // namespace

Core::Expected<void, std::string> StepThePanel2000(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    if (!panels.IsOpen(Ui::Panel2000))
        return {};
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const auto x = runtime.hud.inventoryX;
    const auto y = runtime.hud.inventoryY;
    if (InRect(context, Panel2000Left, 0, Panel2000Right, Panel2000Bottom))
        runtime.ui.mouseOnUi = 1;
    if (!runtime.mouse.pendingClick)
        return {};
    if (InRect(context, x + AcceptanceLeft, AcceptanceTop, x + AcceptanceLeft + AcceptanceSize,
               AcceptanceTop + AcceptanceSize))
    {
        runtime.mouse.pendingClick = false;
        runtime.mouse.leftHeld = false;
        runtime.panels.field78abb08 ^= 1;
        return {};
    }
    const auto on = [&](std::int32_t top)
    {
        return InRect(context, x + Panel2000ButtonLeft, top, x + Panel2000ButtonLeft + Panel2000ButtonWidth,
                      top + Panel2000ButtonHeight);
    };
    if (runtime.panels.field78abb08 != 0 && on(Panel2000AcceptTop))
    {
        runtime.mouse.pendingClick = false;
        runtime.mouse.leftHeld = false;
        runtime.social.trade.warningKind = KeypadChoice;
        runtime.social.trade.warningByte = NoAnswerYet;
        return Ui::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.texts, 1, Panel2000QuestionText,
                                  ConfirmDialog);
    }
    if (on(Panel2000BackTop))
        return LeaveThePanel2000(context, panels, false);
    if (OnButton(context, x + CloseButtonX, y + CloseButtonY))
        return LeaveThePanel2000(context, panels, true);
    return {};
}
} // namespace Client::Npc
