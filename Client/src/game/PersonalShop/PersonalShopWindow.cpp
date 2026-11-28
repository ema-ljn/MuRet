#include "PersonalShop/PersonalShopWindow.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Protocol/Social/Trade.h"
#include "Render/Text/FontSet.h"
#include "Chat/GameChat.h"
#include "Chat/GameMessages.h"
#include "Hud/GamePanels.h"
#include "Ui/GameUiContext.h"
#include "Items/InventoryMoves.h"
#include "Items/ItemPrice.h"
#include "Npc/NpcWindowSteps.h"
#include "Items/OwnItemPlacement.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/ZenText.h"
#include "Chat/ChatInput.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameText.h"
#include "Ui/GameUiState.h"
#include "Ui/MessageArea.h"
#include "FrontEnd/NameChecks.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::PersonalShop
{
namespace
{
namespace Shop = Protocol::PersonalShop;

// The padding and the serial of a C3 (0x69fd70 and the close of 0x40b3f0 or of 0x40b0a0).
constexpr std::uint32_t C3Randoms = 2;
// 0x5eba84 and 0x5ebb2a: the title and price dialog.
constexpr std::int32_t ShopDialogId = 0x91;
// 0x5ebab3 and 0x5ebb5c: the field length (0x6c8e78) of the title and the price.
constexpr std::int32_t TitleFieldLimit = 0x1c;
constexpr std::int32_t PriceFieldLimit = 8;
// 0x5ebac3..0x5ebb1e and 0x5ebb68..0x5ebbbb: the EDIT of [0x5763504] in each mode. +0x4c at 0xc (the draw's black
// background, 8, and a 4 the subclass does not look at) or 9 (also, digits only), the cap (the vtable + 0x3c), the
// size (+ 8) and the position (0x45a9d0).
constexpr std::int32_t TitleEditFilter = 0xc;
constexpr std::int32_t PriceEditFilter = 9;
constexpr std::size_t TitleEditLimit = 0x1c;
constexpr std::size_t PriceEditLimit = 8;
constexpr std::int32_t TitleEditWidth = 0x85;
constexpr std::int32_t PriceEditWidth = 0x3c;
constexpr std::int32_t ShopEditHeight = 0xa;
constexpr std::int32_t TitleEditX = 0xfd;
constexpr std::int32_t PriceEditX = 0x125;
constexpr std::int32_t ShopEditY = 0x8a;
// 0x626112..0x626145 and 0x626200..0x626227: the 0x91's OK and cancel, from 0xff to 0x133 and from 0x14a to 0x17e in
// x, and from 0xa0 to 0xb0 in y, with the edges (jl and jg).
constexpr std::int32_t ShopOkLeft = 0xff;
constexpr std::int32_t ShopOkRight = 0x133;
constexpr std::int32_t ShopCancelLeft = 0x14a;
constexpr std::int32_t ShopCancelRight = 0x17e;
constexpr std::int32_t ShopButtonTop = 0xa0;
constexpr std::int32_t ShopButtonBottom = 0xb0;
// 0x6261cc: the title that 0x5ebbd0 rejects (0x770fe14), of type 1.
constexpr std::size_t BadTitleText = 1130;
// 0x471701 and 0x6260d1
constexpr std::int32_t ConfirmDialog = 0x9c;
// 0x4717a8: a piece is cut when it passes 24 bytes.
constexpr std::size_t ConfirmLineBytes = 0x18;
// 0x47179d: a byte of 0x80 or more counts as two.
constexpr unsigned char DoubleByteMark = 0x80;
// 0x471969..0x471972: a row's height is the letter step + 2.
constexpr std::int32_t ConfirmLineGap = 2;
// 0x6b86c8 (120.0f), 0x6b86d0 (20.0f) and 0x6b885c (36.0f): the buttons go from h + 140 to h + 156.
constexpr double ConfirmBoxTop = 120.0;
constexpr double ConfirmButtonTop = 20.0;
constexpr double ConfirmButtonBottom = 36.0;
// 0x471989..0x471a2f and 0x471a66..0x471a9e: the buttons' x, with the edges.
constexpr std::int32_t YesLeft = 0xff;
constexpr std::int32_t YesRight = 0x133;
constexpr std::int32_t NoLeft = 0x14a;
constexpr std::int32_t NoRight = 0x17e;
constexpr std::int32_t OkLeft = 0x127;
constexpr std::int32_t OkRight = 0x15b;
constexpr std::int32_t TwoButtons = 2;
constexpr std::int32_t OneButton = 1;
constexpr std::int32_t Yes = 1;
constexpr std::int32_t No = 2;
// 0x6c932e
constexpr std::uint8_t NoAnswer = 0xff;

// 0x5ea324..0x5ea32e
constexpr std::int32_t ShopWindowX = 0x104;
constexpr std::int32_t ShopWindowY = 0;
// 0x5ea31e..0x5ea35d: the open button, in absolute x.
constexpr std::int32_t OpenButtonLeft = 0x122;
constexpr std::int32_t OpenButtonRight = 0x15a;
constexpr std::int32_t FootButtonTop = 0x18c;
constexpr std::int32_t FootButtonBottom = 0x1a4;
// 0x5eaa54..0x5eaa6a: the close one, from the window's x.
constexpr std::int32_t CloseButtonLeft = 0x69;
constexpr std::int32_t CloseButtonWidth = 0x38;
// 0x5eafc8..0x5eaff9
constexpr std::int32_t TitleBoxLeft = 0x14;
constexpr std::int32_t TitleBoxWidth = 0x96;
constexpr std::int32_t TitleBoxTop = 0x41;
constexpr std::int32_t TitleBoxHeight = 0x18;
// 0x5ea37a..0x5ea3d5: the 32 cells of 0x78ab0b8, of 8 columns, and their slot from 0x4c.
constexpr std::int32_t ShopColumns = 8;
// 0x5eaa3b: text 1119 (0x770f130), with type 2; 0x5ea9c8: the box's 1131 (0x770ff40).
constexpr std::size_t MissingPriceText = 1119;
constexpr std::size_t OpenShopText = 1131;
constexpr std::int32_t ErrorType = 2;

// 0x5c5181, 0x5dc7e9 and 0x5d8dee..: the warnings that wait for an answer.
constexpr std::int32_t BuyWarningKind = 5;
constexpr std::int32_t PriceWarningKind = 6;
constexpr std::int32_t TradeWarningKind = 3;
// 0x5dc7d5..0x5dc7df: the warning of the price below the value, 0x633300(2, 1132, 0x99).
constexpr std::int32_t PriceWarningLines = 2;
constexpr std::int32_t PriceWarningText = 1132;
// 0x5dc7ab: 0x54d0f0 with mode 2.
constexpr std::int32_t PriceValueMode = 2;

const Items::InventoryServices& ServicesOf(const Ui::GameUiContext& context)
{
    static const Items::InventoryServices None{};
    return context.inventory != nullptr ? *context.inventory : None;
}

bool InRect(const Ui::GameUiContext& context, std::int32_t left, std::int32_t top, std::int32_t right,
            std::int32_t bottom)
{
    return context.mouseX >= left && context.mouseX < right && context.mouseY >= top && context.mouseY < bottom;
}

// The C3's two rand() and the packet through the wire. Without the wire, the error with `where`.
Core::Expected<void, std::string> SendInC3(const Ui::GameUiContext& context, Protocol::Outgoing outgoing,
                                           std::string_view where)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::format("{}: el paquete sin el cable", where));
    context.random.Discard(C3Randoms);
    context.requests->SendGamePacket(std::move(outgoing));
    return {};
}

// The 36 bytes of 0x78aba64 that the MOVSD × 9 copies. MuRet keeps the title as text: zeros follow its end (Q-C965).
std::array<std::byte, Shop::TitleSize> TitleBytes(const std::string& title)
{
    std::array<std::byte, Shop::TitleSize> bytes{};
    const std::size_t count = std::min(title.size(), bytes.size());
    for (std::size_t at = 0; at < count; ++at)
        bytes[at] = static_cast<std::byte>(title[at]);
    return bytes;
}

// 0x5f8110(mode): the own one's map (1) or the other's (2); with another mode or not built, none.
std::map<std::int32_t, std::int32_t>* PricesOf(Social::PersonalShopState& shops)
{
    if (!shops.pricesBuilt)
        return nullptr;
    if (shops.mode == Social::OwnShopView)
        return &shops.ownPrices;
    if (shops.mode == Social::OtherShopView)
        return &shops.otherPrices;
    return nullptr;
}

// 0x5ea37f..0x5ea3f4: each grid item with its price above 0, at least one, and the title.
bool TheShopIsReady(Kernel::WorldState& world)
{
    Social::PersonalShopState& shops = world.Runtime().social.personalShop;
    bool empty = true;
    for (const Kernel::ItemState& cell : world.Inventory().secondCells)
    {
        if (std::bit_cast<std::int16_t>(cell.id) < 0)
            continue;
        empty = false;
        const auto* prices = PricesOf(shops);
        if (prices == nullptr)
            return false;
        const std::int32_t place =
            cell.gridColumn + cell.gridRow * ShopColumns + static_cast<std::int32_t>(Items::FirstSecondGridSlot);
        const auto found = prices->find(place);
        if (found == prices->end() || found->second <= 0)
            return false;
    }
    if (empty)
        return false;
    // 0x5ea3e5..0x5ea3f4: the title's strlen.
    const std::string& title = shops.ownTitle;
    return !title.empty() && title.front() != '\0';
}

// 0x5ea3a3..0x5eaa4f
Core::Expected<void, std::string> PressTheOpenButton(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Social::PersonalShopState& shops = runtime.social.personalShop;
    runtime.mouse.pendingClick = false;
    if (!TheShopIsReady(context.world))
    {
        // 0x5eaa3b: 0x555190 with the name of 0x78abb7c, which nobody writes (*inferred*, 08 P-C72).
        const Items::InventoryServices& services = ServicesOf(context);
        if (!services.systemMessage)
            return Core::Unexpected(std::string("0x5eaa47: 0x555190 con el texto 1119 sin el chat"));
        return services.systemMessage(MissingPriceText, ErrorType);
    }
    if (shops.ownOpen != 0)
    {
        if (auto sent = SendInC3(context, Shop::WriteOpenShop(TitleBytes(shops.ownTitle)), "0x5ea407");
            !sent.HasValue())
            return sent;
        // 0x5ea99a..0x5ea9ae: 0x65cae0(0x4000000, 0).
        if (auto closed = panels.Close(Ui::InventoryPanel); !closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
        return {};
    }
    // 0x5ea9b8..0x5eaa34: the 0x9c box with text 1131 (a std::string with its copy) and two buttons; what 0x4716e0
    // returns is not checked.
    if (context.texts == nullptr)
        return Core::Unexpected(std::string("0x5ea9c8: el texto 1131 sin Text.bmd"));
    const auto text = context.texts->Text(OpenShopText);
    if (!text)
        return Core::Unexpected(std::string("0x5ea9c8: sin el texto 1131 de Text.bmd"));
    static_cast<void>(
        OpenTheConfirmBox(runtime.social.confirmBox, runtime.dialogs, OpenShopConfirm, *text, TwoButtons));
    return {};
}

// 0x5dc807.. and 0x5da5ac..: the typed price to the item's slot.
Core::Expected<void, std::string> SetThePrice(const Ui::GameUiContext& context,
                                              const Items::InventoryMoveContext& moves)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Social::PersonalShopState& shops = runtime.social.personalShop;
    Social::SocialState& social = runtime.social;
    // 0x5dc807..0x5dcd38: with the shop open, the C1 04 3F 03.
    if (shops.ownOpen != 0)
    {
        if (auto sent = SendInC3(context, Shop::WriteCloseShop(), "0x5dc81c"); !sent.HasValue())
            return sent;
    }
    const auto slot = static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff);
    const auto price = static_cast<std::uint32_t>(social.typedMoney);
    // 0x5dcd3d..0x5dcd45: with an item on the cursor, the 3F 01, the 24 and the price at the target slot.
    std::int32_t place = runtime.slot78a6758;
    if (runtime.cursorItemCount > 0)
    {
        if (auto sent = SendInC3(context, Shop::WriteSetPrice(slot, price), "0x5dcd4b"); !sent.HasValue())
            return sent;
        if (runtime.ui.field78abb39 == 0)
        {
            if (auto moved = Items::SendTheCursorItem(moves, static_cast<std::uint8_t>(runtime.itemMoveTarget & 0xff),
                                                      Items::InventoryGridFlag, Items::ShopGridFlag);
                !moved.HasValue())
                return moved;
        }
        place = runtime.itemMoveTarget;
    }
    else if (auto sent = SendInC3(context, Shop::WriteSetPrice(slot, price), "0x5de30d"); !sent.HasValue())
        return sent;
    // 0x5de297.. and 0x5dc701..: 0x5f7f90(slot, price) in the mode's map.
    if (auto* prices = PricesOf(shops); prices != nullptr)
        (*prices)[place] = social.typedMoney;
    social.typedMoney = 0;
    return {};
}

// 0x5dc79e..0x5deac0
Core::Expected<bool, std::string> AnswerThePriceDialog(const Ui::GameUiContext& context,
                                                       const Items::InventoryMoveContext& moves)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Social::TradeState& trade = runtime.social.trade;
    Social::PersonalShopState& shops = runtime.social.personalShop;
    if (trade.warningByte == Yes)
    {
        shops.dialogMode = 0;
        trade.warningByte = NoAnswer;
        if (moves.services.items == nullptr)
            return Core::Unexpected(std::string("0x5dc7c3: 0x54d0f0 sin Item.bmd"));
        // 0x5dc7c3: the value of 0x78a59c8's item; the raven and the horse, without 0x723968's levels, stop.
        auto value = Items::ItemPrice(runtime.dialogItem, *moves.services.items, PriceValueMode, std::nullopt);
        if (!value.HasValue())
            return Core::Unexpected(std::move(value).Error());
        if (static_cast<std::uint32_t>(runtime.social.typedMoney) < value.Value())
        {
            // 0x5dc7d5: 0x633300(2, 1132, 0x99), with the typed zen written by 0x5b1130.
            auto warned = Ui::OpenTheMoneyTextDialog(
                runtime.dialogBox, runtime.dialogs, context.texts, PriceWarningLines, PriceWarningText,
                Ui::FormatZen(static_cast<std::uint32_t>(runtime.social.typedMoney)));
            if (!warned.HasValue())
                return Core::Unexpected(std::move(warned).Error());
            trade.warningKind = PriceWarningKind;
            trade.warningByte = NoAnswer;
            return false;
        }
        if (auto priced = SetThePrice(context, moves); !priced.HasValue())
            return Core::Unexpected(std::move(priced).Error());
        return false;
    }
    if (trade.warningByte == No)
    {
        shops.dialogMode = 0;
        trade.warningByte = NoAnswer;
        if (auto returned = Items::ReturnTheCursorItem(moves); !returned.HasValue())
            return Core::Unexpected(std::move(returned).Error());
    }
    return false;
}

// 0x5d8e00..0x5dc790: the warning `kind` (from 1 to 6) with its answer.
Core::Expected<bool, std::string> AnswerTheWarning(const Ui::GameUiContext& context,
                                                   const Items::InventoryMoveContext& moves, std::int32_t kind)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Social::TradeState& trade = runtime.social.trade;
    const std::uint8_t answer = trade.warningByte;
    if (answer != Yes && answer != No)
        return false;
    const auto clear = [&trade]
    {
        trade.warningKind = 0;
        trade.warningByte = NoAnswer;
    };
    switch (kind)
    {
    case 1:
    case 2:
    case 4:
        // The sale (0x5d8e0d and 0x5d931f), the mix (0x5d934f) and the 0x2000 panel's (0x5d9b82), from the NPC windows
        // (CL9 milestone 6).
        if (auto answered = Npc::AnswerTheNpcWarning(context, kind); !answered.HasValue())
            return Core::Unexpected(std::move(answered).Error());
        return false;
    case TradeWarningKind:
    {
        if (answer == No)
        {
            clear();
            return false;
        }
        // 0x5d966b..0x5d9b74: the own OK flips, 0x78abaa5 at 1 and the C1 04 3C with the new one, in C3.
        clear();
        trade.ownOk = trade.ownOk == 0 ? 1 : 0;
        trade.changed = 1;
        if (auto sent = SendInC3(context, Protocol::Social::WriteTradeOkButton(trade.ownOk), "0x5d968e");
            !sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        return false;
    }
    case BuyWarningKind:
    {
        clear();
        if (answer == No)
            return false;
        // 0x5d9c82..0x5da58d: with the owner's key (its low WORD), the 3F 06.
        const Social::PersonalShopState& shops = runtime.social.personalShop;
        if (shops.ownerKey == 0)
            return false;
        const auto slot = static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff);
        if (auto sent = SendInC3(context, Shop::WriteBuyItem(shops.ownerKey, shops.ownerName, slot), "0x5d9c9e");
            !sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        return false;
    }
    case PriceWarningKind:
        clear();
        if (answer == No)
        {
            // 0x5dc765..0x5dc778.
            OpenTheShopDialog(runtime, ShopPriceMode);
            return false;
        }
        if (auto priced = SetThePrice(context, moves); !priced.HasValue())
            return Core::Unexpected(std::move(priced).Error());
        return false;
    default:
        break;
    }
    return Core::Unexpected(std::format("0x5d8dee: el aviso {} de 0x78abb14", kind));
}
} // namespace

void OpenTheShopDialog(Kernel::WorldRuntime& runtime, std::int32_t mode)
{
    if (mode != ShopTitleMode && mode != ShopPriceMode)
        return;
    Ui::PushDialog(runtime.dialogs, ShopDialogId);
    runtime.social.personalShop.dialogMode = mode;
    runtime.ui.chatOpen = false;
    runtime.social.moneyField = mode == ShopPriceMode ? 1 : 0;
    Ui::ResetTheTextBuffers(runtime.chatInput, false);
    runtime.social.dialogFieldLimit = mode == ShopPriceMode ? PriceFieldLimit : TitleFieldLimit;
    // With 0x6c7b8c at 1: the vtable + 4 with 0 (in view), +0x4c, the + 0x44 with (0, 0, 0, 0) (the background), the
    // cap, the size and the position. The focus and the drawing belong to the 0x91's draw (0x6305c9).
    const bool price = mode == ShopPriceMode;
    Ui::OldEdit& edit = runtime.chatInput.dialogEdit;
    edit.base.state = Ui::ShownWindow;
    edit.filter = price ? PriceEditFilter : TitleEditFilter;
    edit.backgroundColor = 0;
    edit.text.SetLimit(price ? PriceEditLimit : TitleEditLimit);
    edit.base.width = price ? PriceEditWidth : TitleEditWidth;
    edit.base.height = ShopEditHeight;
    edit.base.x = price ? PriceEditX : TitleEditX;
    edit.base.y = ShopEditY;
}

Core::Expected<void, std::string> StepTheShopDialog(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::ChatInputState& input = runtime.chatInput;
    Social::PersonalShopState& shops = runtime.social.personalShop;
    Social::SocialState& social = runtime.social;
    bool& click = runtime.mouse.pendingClick;
    const auto in = [&context](std::int32_t left, std::int32_t right)
    {
        return context.mouseX >= left && context.mouseY >= ShopButtonTop && context.mouseX <= right &&
               context.mouseY <= ShopButtonBottom;
    };
    // 0x626102..0x6261f7: the OK with the click reads the EDIT (the vtable + 0x50, up to 0xff bytes, and its length to
    // 0x7758590) and with the title keeps it if 0x5ebbd0 allows it (otherwise, the 1130 of type 1 with the name of
    // 0x7d3a15c, an empty .bss string); with the price, the atoi to 0x77f3b6c.
    std::int32_t pressed = -1;
    if (click && in(ShopOkLeft, ShopOkRight))
    {
        pressed = 1;
        click = false;
        Ui::CopyIntoTheBuffer(input.messageBuffer, input.dialogEdit.text.Value());
        input.messageLength = static_cast<std::int32_t>(Ui::CStringOf(input.messageBuffer).size());
        const std::string_view typed = Ui::CStringOf(input.messageBuffer);
        if (shops.dialogMode == ShopTitleMode)
        {
            const std::span<const std::string> filter =
                context.chat != nullptr ? context.chat->filter : std::span<const std::string>{};
            if (Ui::IsAShopTitle(typed, filter))
                shops.ownTitle = std::string(typed);
            else
            {
                const std::optional<std::string_view> text = Ui::TextAt(context.texts, BadTitleText);
                if (!text)
                    return Core::Unexpected(std::format("0x6261cc: sin el texto {} de Text.bmd", BadTitleText));
                if (context.measure == nullptr)
                    return Core::Unexpected(std::string("0x6261d6: el mensaje del texto 1130 sin medidor"));
                if (auto said =
                        Chat::AddGameMessage(runtime, *context.measure, context.screen, {}, *text, Ui::SystemMessage);
                    !said.HasValue())
                    return said;
            }
        }
        else if (shops.dialogMode == ShopPriceMode)
            social.typedMoney = Chat::AtoiLikeTheCrt(typed);
    }
    // 0x6261f7..0x626230: the cancel, with the click that remains.
    if (click && in(ShopCancelLeft, ShopCancelRight))
    {
        click = false;
        pressed = 0;
    }
    // 0x626232..0x626293: Enter (0x5763588) counts as the OK, without reading the EDIT (the WndProc read it);
    // otherwise, without a button nothing, and the cancel answers 2.
    bool ok = pressed == 1;
    if (runtime.ui.enterPressed)
    {
        runtime.ui.enterPressed = false;
        ok = true;
    }
    else if (pressed < 0)
        return {};
    // 0x626251..0x626291: the OK answers 1 with a non-empty title (0x78aba64) or a price above 0 (signed);
    // otherwise, 2. 0x6c932e is the answer that the window's step (0x5ea2f1) and the price's (0x5dc79e) look at.
    Social::TradeState& trade = social.trade;
    trade.warningByte = static_cast<std::uint8_t>(No);
    if (ok && ((shops.dialogMode == ShopTitleMode && !shops.ownTitle.empty()) ||
               (shops.dialogMode == ShopPriceMode && social.typedMoney > 0)))
        trade.warningByte = static_cast<std::uint8_t>(Yes);
    // 0x62629a..0x6262d0: the text fields emptied (0x553570(0), with the EDIT), the chat field closed, the focus to the
    // game's window and the pending dialog open.
    Ui::ResetTheTextBuffers(input, false);
    runtime.ui.chatOpen = false;
    input.focus = Ui::ChatFocus::Game;
    runtime.dialogs.current = runtime.dialogs.pending;
    runtime.dialogs.pending = 0;
    return {};
}

bool OpenTheConfirmBox(Social::ConfirmBoxState& box, Ui::DialogState& dialogs, std::int32_t id, std::string_view text,
                       std::int32_t type)
{
    if (dialogs.current == ConfirmDialog)
        return false;
    Ui::PushDialog(dialogs, ConfirmDialog);
    if (type <= 0 || type > TwoButtons)
        return false;
    box.id = id;
    box.type = type;
    box.lines.clear();
    // 0x471777..0x4718fc: `start` and `length` are esi and edi.
    std::size_t start = 0;
    std::size_t length = 0;
    while (start + length < text.size())
    {
        const auto byte = static_cast<unsigned char>(text[start + length]);
        length += (byte & DoubleByteMark) != 0 ? 2 : 1;
        if (length <= ConfirmLineBytes && start + length != text.size())
            continue;
        box.lines.emplace_back(text.substr(start, std::min(length, text.size() - start)));
        start += length;
        length = 0;
    }
    return true;
}

Core::Expected<void, std::string> StepTheConfirmBox(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    bool& click = runtime.mouse.pendingClick;
    if (runtime.dialogs.current != ConfirmDialog || !click)
        return {};
    const Social::ConfirmBoxState& box = runtime.social.confirmBox;
    // 0x471956..0x471978: the height, integer, and its float.
    const auto lines = static_cast<std::int32_t>(box.lines.size());
    const std::int32_t height = (Render::Text::FontStepFor(context.screen.width) + ConfirmLineGap) * lines;
    const auto top = static_cast<double>(static_cast<float>(static_cast<double>(height) + ConfirmBoxTop));
    const auto y = static_cast<double>(context.mouseY);
    const bool inRow = top + ConfirmButtonTop <= y && y <= top + ConfirmButtonBottom;
    const auto pressed = [&](std::int32_t left, std::int32_t right)
    { return context.mouseX >= left && inRow && context.mouseX <= right; };
    const std::int32_t id = box.id;
    if (box.type == TwoButtons)
    {
        if (pressed(YesLeft, YesRight))
        {
            click = false;
            if (auto answered = AnswerTheConfirmBox(context, id, Yes); !answered.HasValue())
                return answered;
        }
        // 0x4719f5: the other button, without looking at the click again.
        if (pressed(NoLeft, NoRight))
        {
            click = false;
            return AnswerTheConfirmBox(context, id, No);
        }
        return {};
    }
    if (box.type == OneButton && pressed(OkLeft, OkRight))
    {
        click = false;
        return AnswerTheConfirmBox(context, id, Yes);
    }
    return {};
}

Core::Expected<void, std::string> AnswerTheConfirmBox(const Ui::GameUiContext& context, std::int32_t id,
                                                      std::int32_t answer)
{
    if (id != OpenShopConfirm || (answer != Yes && answer != No))
        return {};
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (answer == Yes)
    {
        const std::string& title = runtime.social.personalShop.ownTitle;
        if (auto sent = SendInC3(context, Shop::WriteOpenShop(TitleBytes(title)), "0x62378b"); !sent.HasValue())
            return sent;
        // 0x623daa: 0x657a90, with what closes sending its own (the 3D, the 3F 07 and the 31).
        Hud::GamePanels panels(context.world, context.random);
        panels.LendTheSession(context.world, context.random, context.requests);
        if (auto closed = panels.Registry().CloseAll(); !closed.HasValue())
            return closed;
    }
    // 0x623daf..0x623dc7 and 0x623ddd..0x623ded.
    runtime.dialogs.current = runtime.dialogs.pending;
    runtime.dialogs.pending = 0;
    return {};
}

Core::Expected<void, std::string> StepThePersonalShopWindow(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    if (!panels.IsOpen(Ui::PersonalShopPanel) && !panels.IsOpen(Ui::OtherShopPanel))
        return {};
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Social::PersonalShopState& shops = runtime.social.personalShop;
    Social::TradeState& trade = runtime.social.trade;
    if (shops.mode != Social::OwnShopView)
        return {};
    // 0x5ea2f1..0x5ea310.
    if (shops.dialogMode == ShopTitleMode && (trade.warningByte == Yes || trade.warningByte == No))
    {
        shops.dialogMode = 0;
        trade.warningByte = NoAnswer;
    }
    shops.windowX = ShopWindowX;
    shops.windowY = ShopWindowY;
    bool& click = runtime.mouse.pendingClick;
    if (InRect(context, OpenButtonLeft, FootButtonTop, OpenButtonRight, FootButtonBottom) && click)
    {
        if (auto pressed = PressTheOpenButton(context, panels); !pressed.HasValue())
            return pressed;
    }
    // 0x5eaa54..0x5eafb2: the close button spends the click even if the shop is closed.
    const std::int32_t x = shops.windowX;
    if (InRect(context, x + CloseButtonLeft, FootButtonTop, x + CloseButtonLeft + CloseButtonWidth, FootButtonBottom) &&
        click)
    {
        click = false;
        if (shops.ownOpen != 0)
        {
            if (auto sent = SendInC3(context, Shop::WriteCloseShop(), "0x5eaab1"); !sent.HasValue())
                return sent;
        }
    }
    // 0x5eafc8..0x5eb00b.
    const std::int32_t left = shops.windowX + TitleBoxLeft;
    const std::int32_t top = shops.windowY + TitleBoxTop;
    if (InRect(context, left, top, left + TitleBoxWidth, top + TitleBoxHeight) && click)
        OpenTheShopDialog(runtime, ShopTitleMode);
    return {};
}

Core::Expected<bool, std::string> StepTheWarningAnswers(const Ui::GameUiContext& context,
                                                        const Items::InventoryMoveContext& moves)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t kind = runtime.social.trade.warningKind;
    if (kind >= 1 && kind <= PriceWarningKind)
        return AnswerTheWarning(context, moves, kind);
    if (runtime.social.personalShop.dialogMode == ShopPriceMode)
        return AnswerThePriceDialog(context, moves);
    return true;
}
} // namespace Client::PersonalShop
