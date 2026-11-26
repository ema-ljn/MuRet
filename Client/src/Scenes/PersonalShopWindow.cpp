#include "Scenes/PersonalShopWindow.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Protocol/Social/Trade.h"
#include "Render/Text/FontSet.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemPrice.h"
#include "Scenes/NpcWindowSteps.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/ZenText.h"
#include "UI/ChatInput.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/MessageArea.h"
#include "UI/NameChecks.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

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

namespace Client::Scenes
{
namespace
{
namespace Shop = Protocol::PersonalShop;

// El relleno y el serial de un C3 (0x69fd70 y el cierre de 0x40b3f0 o de 0x40b0a0).
constexpr std::uint32_t C3Randoms = 2;
// 0x5eba84 y 0x5ebb2a: el diálogo del título y del precio.
constexpr std::int32_t ShopDialogId = 0x91;
// 0x5ebab3 y 0x5ebb5c: el largo del campo (0x6c8e78) del título y del precio.
constexpr std::int32_t TitleFieldLimit = 0x1c;
constexpr std::int32_t PriceFieldLimit = 8;
// 0x5ebac3..0x5ebb1e y 0x5ebb68..0x5ebbbb: el EDIT de [0x5763504] de cada modo. +0x4c en 0xc (el fondo negro del
// dibujo, 8, y un 4 que la subclase no mira) o 9 (además, solo cifras), el tope (la vtable + 0x3c), el tamaño (+ 8) y
// el lugar (0x45a9d0).
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
// 0x626112..0x626145 y 0x626200..0x626227: el OK y el cancelar del 0x91, de 0xff a 0x133 y de 0x14a a 0x17e en x, y de
// 0xa0 a 0xb0 en y, con los bordes (jl y jg).
constexpr std::int32_t ShopOkLeft = 0xff;
constexpr std::int32_t ShopOkRight = 0x133;
constexpr std::int32_t ShopCancelLeft = 0x14a;
constexpr std::int32_t ShopCancelRight = 0x17e;
constexpr std::int32_t ShopButtonTop = 0xa0;
constexpr std::int32_t ShopButtonBottom = 0xb0;
// 0x6261cc: el título que 0x5ebbd0 no deja (0x770fe14), del tipo 1.
constexpr std::size_t BadTitleText = 1130;
// 0x471701 y 0x6260d1: la caja de confirmación.
constexpr std::int32_t ConfirmDialog = 0x9c;
// 0x4717a8: un pedazo se corta al pasar de 24 bytes.
constexpr std::size_t ConfirmLineBytes = 0x18;
// 0x47179d: un byte de 0x80 o más cuenta como dos.
constexpr unsigned char DoubleByteMark = 0x80;
// 0x471969..0x471972: la altura de un renglón es el paso de la letra + 2.
constexpr std::int32_t ConfirmLineGap = 2;
// 0x6b86c8 (120,0f), 0x6b86d0 (20,0f) y 0x6b885c (36,0f): los botones van de h + 140 a h + 156.
constexpr double ConfirmBoxTop = 120.0;
constexpr double ConfirmButtonTop = 20.0;
constexpr double ConfirmButtonBottom = 36.0;
// 0x471989..0x471a2f y 0x471a66..0x471a9e: las x de los botones, con los bordes.
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
// 0x6c932e: sin respuesta.
constexpr std::uint8_t NoAnswer = 0xff;

// 0x5ea324..0x5ea32e: la esquina de la ventana.
constexpr std::int32_t ShopWindowX = 0x104;
constexpr std::int32_t ShopWindowY = 0;
// 0x5ea31e..0x5ea35d: el botón de abrir, en x absolutas.
constexpr std::int32_t OpenButtonLeft = 0x122;
constexpr std::int32_t OpenButtonRight = 0x15a;
constexpr std::int32_t FootButtonTop = 0x18c;
constexpr std::int32_t FootButtonBottom = 0x1a4;
// 0x5eaa54..0x5eaa6a: el de cerrar, desde la x de la ventana.
constexpr std::int32_t CloseButtonLeft = 0x69;
constexpr std::int32_t CloseButtonWidth = 0x38;
// 0x5eafc8..0x5eaff9: el título.
constexpr std::int32_t TitleBoxLeft = 0x14;
constexpr std::int32_t TitleBoxWidth = 0x96;
constexpr std::int32_t TitleBoxTop = 0x41;
constexpr std::int32_t TitleBoxHeight = 0x18;
// 0x5ea37a..0x5ea3d5: las 32 casillas de 0x78ab0b8, de 8 columnas, y su lugar desde 0x4c.
constexpr std::int32_t ShopColumns = 8;
// 0x5eaa3b: el texto 1119 (0x770f130), con el tipo 2; 0x5ea9c8: el 1131 (0x770ff40) de la caja.
constexpr std::size_t MissingPriceText = 1119;
constexpr std::size_t OpenShopText = 1131;
constexpr std::int32_t ErrorType = 2;

// 0x5c5181, 0x5dc7e9 y 0x5d8dee..: los avisos que esperan respuesta.
constexpr std::int32_t BuyWarningKind = 5;
constexpr std::int32_t PriceWarningKind = 6;
constexpr std::int32_t TradeWarningKind = 3;
// 0x5dc7d5..0x5dc7df: el aviso del precio menor que el valor, 0x633300(2, 1132, 0x99).
constexpr std::int32_t PriceWarningLines = 2;
constexpr std::int32_t PriceWarningText = 1132;
// 0x5dc7ab: 0x54d0f0 con el modo 2.
constexpr std::int32_t PriceValueMode = 2;

const InventoryServices& ServicesOf(const GameUiContext& context)
{
    static const InventoryServices None{};
    return context.inventory != nullptr ? *context.inventory : None;
}

bool InRect(const GameUiContext& context, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
{
    return context.mouseX >= left && context.mouseX < right && context.mouseY >= top && context.mouseY < bottom;
}

// Los dos rand() del C3 y el paquete por el cable. Sin el cable, el error con `where`.
Core::Expected<void, std::string> SendInC3(const GameUiContext& context, Protocol::Outgoing outgoing,
                                           std::string_view where)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::format("{}: el paquete sin el cable", where));
    context.random.Discard(C3Randoms);
    context.requests->SendGamePacket(std::move(outgoing));
    return {};
}

// Los 36 bytes de 0x78aba64 que copia el MOVSD × 9. MuRet guarda el título como texto: detrás del terminador van
// ceros (Q-C965).
std::array<std::byte, Shop::TitleSize> TitleBytes(const std::string& title)
{
    std::array<std::byte, Shop::TitleSize> bytes{};
    const std::size_t count = std::min(title.size(), bytes.size());
    for (std::size_t at = 0; at < count; ++at)
        bytes[at] = static_cast<std::byte>(title[at]);
    return bytes;
}

// 0x5f8110(modo): el mapa de la propia (1) o de la de otro (2); con otro modo o sin armar, ninguno.
std::map<std::int32_t, std::int32_t>* PricesOf(PersonalShopState& shops)
{
    if (!shops.pricesBuilt)
        return nullptr;
    if (shops.mode == OwnShopView)
        return &shops.ownPrices;
    if (shops.mode == OtherShopView)
        return &shops.otherPrices;
    return nullptr;
}

// 0x5ea37f..0x5ea3f4: cada ítem de la grilla con su precio de más de 0, alguno y el título.
bool TheShopIsReady(WorldState& world)
{
    PersonalShopState& shops = world.Runtime().social.personalShop;
    bool empty = true;
    for (const World::ItemState& cell : world.Inventory().secondCells)
    {
        if (std::bit_cast<std::int16_t>(cell.id) < 0)
            continue;
        empty = false;
        const auto* prices = PricesOf(shops);
        if (prices == nullptr)
            return false;
        const std::int32_t place =
            cell.gridColumn + cell.gridRow * ShopColumns + static_cast<std::int32_t>(FirstSecondGridSlot);
        const auto found = prices->find(place);
        if (found == prices->end() || found->second <= 0)
            return false;
    }
    if (empty)
        return false;
    // 0x5ea3e5..0x5ea3f4: strlen del título.
    const std::string& title = shops.ownTitle;
    return !title.empty() && title.front() != '\0';
}

// 0x5ea3a3..0x5eaa4f: el botón de abrir.
Core::Expected<void, std::string> PressTheOpenButton(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    PersonalShopState& shops = runtime.social.personalShop;
    runtime.movement.pendingClick = false;
    if (!TheShopIsReady(context.world))
    {
        // 0x5eaa3b: 0x555190 con el nombre de 0x78abb7c, que nadie escribe (*inferido*, 08 P-C72).
        const InventoryServices& services = ServicesOf(context);
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
        if (auto closed = panels.Close(UI::InventoryPanel); !closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
        return {};
    }
    // 0x5ea9b8..0x5eaa34: la caja 0x9c con el texto 1131 (un std::string con su copia) y dos botones; lo que devuelve
    // 0x4716e0 no se mira.
    if (context.texts == nullptr)
        return Core::Unexpected(std::string("0x5ea9c8: el texto 1131 sin Text.bmd"));
    const auto text = context.texts->Text(OpenShopText);
    if (!text)
        return Core::Unexpected(std::string("0x5ea9c8: sin el texto 1131 de Text.bmd"));
    static_cast<void>(
        OpenTheConfirmBox(runtime.social.confirmBox, runtime.dialogs, OpenShopConfirm, *text, TwoButtons));
    return {};
}

// 0x5dc807.. y 0x5da5ac..: el precio escrito al lugar del ítem.
Core::Expected<void, std::string> SetThePrice(const GameUiContext& context, const InventoryMoveContext& moves)
{
    WorldRuntime& runtime = context.world.Runtime();
    PersonalShopState& shops = runtime.social.personalShop;
    SocialState& social = runtime.social;
    // 0x5dc807..0x5dcd38: con la tienda abierta, el C1 04 3F 03.
    if (shops.ownOpen != 0)
    {
        if (auto sent = SendInC3(context, Shop::WriteCloseShop(), "0x5dc81c"); !sent.HasValue())
            return sent;
    }
    const auto slot = static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff);
    const auto price = static_cast<std::uint32_t>(social.typedMoney);
    // 0x5dcd3d..0x5dcd45: con un ítem en el cursor, el 3F 01, el 24 y el precio en el lugar de destino.
    std::int32_t place = runtime.slot78a6758;
    if (runtime.cursorItemCount > 0)
    {
        if (auto sent = SendInC3(context, Shop::WriteSetPrice(slot, price), "0x5dcd4b"); !sent.HasValue())
            return sent;
        if (runtime.ui.field78abb39 == 0)
        {
            if (auto moved = SendTheCursorItem(moves, static_cast<std::uint8_t>(runtime.itemMoveTarget & 0xff),
                                               InventoryGridFlag, ShopGridFlag);
                !moved.HasValue())
                return moved;
        }
        place = runtime.itemMoveTarget;
    }
    else if (auto sent = SendInC3(context, Shop::WriteSetPrice(slot, price), "0x5de30d"); !sent.HasValue())
        return sent;
    // 0x5de297.. y 0x5dc701..: 0x5f7f90(lugar, precio) en el mapa del modo.
    if (auto* prices = PricesOf(shops); prices != nullptr)
        (*prices)[place] = social.typedMoney;
    social.typedMoney = 0;
    return {};
}

// 0x5dc79e..0x5deac0: la respuesta del diálogo del precio.
Core::Expected<bool, std::string> AnswerThePriceDialog(const GameUiContext& context, const InventoryMoveContext& moves)
{
    WorldRuntime& runtime = context.world.Runtime();
    TradeState& trade = runtime.social.trade;
    PersonalShopState& shops = runtime.social.personalShop;
    if (trade.warningByte == Yes)
    {
        shops.dialogMode = 0;
        trade.warningByte = NoAnswer;
        if (moves.services.items == nullptr)
            return Core::Unexpected(std::string("0x5dc7c3: 0x54d0f0 sin Item.bmd"));
        // 0x5dc7c3: el valor del ítem de 0x78a59c8; el cuervo y el caballo, sin los niveles de 0x723968, cortan.
        auto value = ItemPrice(runtime.dialogItem, *moves.services.items, PriceValueMode, std::nullopt);
        if (!value.HasValue())
            return Core::Unexpected(std::move(value).Error());
        if (static_cast<std::uint32_t>(runtime.social.typedMoney) < value.Value())
        {
            // 0x5dc7d5: 0x633300(2, 1132, 0x99), con el zen tipeado escrito por 0x5b1130.
            auto warned = UI::OpenTheMoneyTextDialog(runtime.dialogBox, runtime.dialogs, context.texts,
                                                     PriceWarningLines, PriceWarningText,
                                                     FormatZen(static_cast<std::uint32_t>(runtime.social.typedMoney)));
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
        if (auto returned = ReturnTheCursorItem(moves); !returned.HasValue())
            return Core::Unexpected(std::move(returned).Error());
    }
    return false;
}

// 0x5d8e00..0x5dc790: el aviso `kind` (de 1 a 6) con su respuesta.
Core::Expected<bool, std::string> AnswerTheWarning(const GameUiContext& context, const InventoryMoveContext& moves,
                                                   std::int32_t kind)
{
    WorldRuntime& runtime = context.world.Runtime();
    TradeState& trade = runtime.social.trade;
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
        // La venta (0x5d8e0d y 0x5d931f), la mezcla (0x5d934f) y el del panel 0x2000 (0x5d9b82), de las ventanas de NPC
        // (CL9 hito 6).
        if (auto answered = AnswerTheNpcWarning(context, kind); !answered.HasValue())
            return Core::Unexpected(std::move(answered).Error());
        return false;
    case TradeWarningKind:
    {
        if (answer == No)
        {
            clear();
            return false;
        }
        // 0x5d966b..0x5d9b74: el OK propio da vuelta, 0x78abaa5 en 1 y el C1 04 3C con el nuevo, en C3.
        clear();
        trade.ownOk = trade.ownOk == 0 ? 1 : 0;
        trade.changed = 1;
        if (auto sent = SendInC3(context, Protocol::Trade::WriteTradeOkButton(trade.ownOk), "0x5d968e");
            !sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        return false;
    }
    case BuyWarningKind:
    {
        clear();
        if (answer == No)
            return false;
        // 0x5d9c82..0x5da58d: con la clave del dueño (su WORD bajo), el 3F 06.
        const PersonalShopState& shops = runtime.social.personalShop;
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

void OpenTheShopDialog(WorldRuntime& runtime, std::int32_t mode)
{
    if (mode != ShopTitleMode && mode != ShopPriceMode)
        return;
    UI::PushDialog(runtime.dialogs, ShopDialogId);
    runtime.social.personalShop.dialogMode = mode;
    runtime.ui.chatOpen = false;
    runtime.social.moneyField = mode == ShopPriceMode ? 1 : 0;
    UI::ResetTheTextBuffers(runtime.chat.input, false);
    runtime.social.dialogFieldLimit = mode == ShopPriceMode ? PriceFieldLimit : TitleFieldLimit;
    // Con 0x6c7b8c en 1: la vtable + 4 con 0 (a la vista), +0x4c, la + 0x44 con (0, 0, 0, 0) (el fondo), el tope, el
    // tamaño y el lugar. El foco y el dibujo son del dibujo del 0x91 (0x6305c9).
    const bool price = mode == ShopPriceMode;
    UI::OldEdit& edit = runtime.chat.input.dialogEdit;
    edit.base.state = UI::ShownWindow;
    edit.filter = price ? PriceEditFilter : TitleEditFilter;
    edit.backgroundColor = 0;
    edit.text.SetLimit(price ? PriceEditLimit : TitleEditLimit);
    edit.base.width = price ? PriceEditWidth : TitleEditWidth;
    edit.base.height = ShopEditHeight;
    edit.base.x = price ? PriceEditX : TitleEditX;
    edit.base.y = ShopEditY;
}

Core::Expected<void, std::string> StepTheShopDialog(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::ChatInputState& input = runtime.chat.input;
    PersonalShopState& shops = runtime.social.personalShop;
    SocialState& social = runtime.social;
    bool& click = runtime.movement.pendingClick;
    const auto in = [&context](std::int32_t left, std::int32_t right)
    {
        return context.mouseX >= left && context.mouseY >= ShopButtonTop && context.mouseX <= right &&
               context.mouseY <= ShopButtonBottom;
    };
    // 0x626102..0x6261f7: el OK con el clic lee el EDIT (la vtable + 0x50, hasta 0xff bytes, y su largo a 0x7758590) y
    // con el título lo guarda si 0x5ebbd0 lo deja (si no, el 1130 del tipo 1 con el nombre de 0x7d3a15c, una cadena
    // vacía de la .bss); con el precio, el atoi a 0x77f3b6c.
    std::int32_t pressed = -1;
    if (click && in(ShopOkLeft, ShopOkRight))
    {
        pressed = 1;
        click = false;
        UI::CopyIntoTheBuffer(input.messageBuffer, input.dialogEdit.text.Value());
        input.messageLength = static_cast<std::int32_t>(UI::CStringOf(input.messageBuffer).size());
        const std::string_view typed = UI::CStringOf(input.messageBuffer);
        if (shops.dialogMode == ShopTitleMode)
        {
            const std::span<const std::string> filter =
                context.chat != nullptr ? context.chat->filter : std::span<const std::string>{};
            if (UI::IsAShopTitle(typed, filter))
                shops.ownTitle = std::string(typed);
            else
            {
                const std::optional<std::string_view> text = UI::TextAt(context.texts, BadTitleText);
                if (!text)
                    return Core::Unexpected(std::format("0x6261cc: sin el texto {} de Text.bmd", BadTitleText));
                if (context.measure == nullptr)
                    return Core::Unexpected(std::string("0x6261d6: el mensaje del texto 1130 sin medidor"));
                if (auto said = AddGameMessage(runtime, *context.measure, context.screen, {}, *text, UI::SystemMessage);
                    !said.HasValue())
                    return said;
            }
        }
        else if (shops.dialogMode == ShopPriceMode)
            social.typedMoney = AtoiLikeTheCrt(typed);
    }
    // 0x6261f7..0x626230: el cancelar, con el clic que quede.
    if (click && in(ShopCancelLeft, ShopCancelRight))
    {
        click = false;
        pressed = 0;
    }
    // 0x626232..0x626293: Enter (0x5763588) vale como el OK, sin leer el EDIT (lo leyó el WndProc); si no, sin botón
    // nada, y el cancelar responde 2.
    bool ok = pressed == 1;
    if (runtime.ui.enterPressed)
    {
        runtime.ui.enterPressed = false;
        ok = true;
    }
    else if (pressed < 0)
        return {};
    // 0x626251..0x626291: el OK responde 1 con un título no vacío (0x78aba64) o un precio mayor que 0 (con signo);
    // si no, 2. 0x6c932e es la respuesta que miran el paso de la ventana (0x5ea2f1) y el del precio (0x5dc79e).
    TradeState& trade = social.trade;
    trade.warningByte = static_cast<std::uint8_t>(No);
    if (ok && ((shops.dialogMode == ShopTitleMode && !shops.ownTitle.empty()) ||
               (shops.dialogMode == ShopPriceMode && social.typedMoney > 0)))
        trade.warningByte = static_cast<std::uint8_t>(Yes);
    // 0x62629a..0x6262d0: los campos de texto vacíos (0x553570(0), con el EDIT), el campo del chat cerrado, el foco a
    // la ventana del juego y el diálogo que espera abierto.
    UI::ResetTheTextBuffers(input, false);
    runtime.ui.chatOpen = false;
    input.focus = UI::ChatFocus::Game;
    runtime.dialogs.current = runtime.dialogs.pending;
    runtime.dialogs.pending = 0;
    return {};
}

bool OpenTheConfirmBox(ConfirmBoxState& box, UI::DialogState& dialogs, std::int32_t id, std::string_view text,
                       std::int32_t type)
{
    if (dialogs.current == ConfirmDialog)
        return false;
    UI::PushDialog(dialogs, ConfirmDialog);
    if (type <= 0 || type > TwoButtons)
        return false;
    box.id = id;
    box.type = type;
    box.lines.clear();
    // 0x471777..0x4718fc: `start` y `length` son esi y edi.
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

Core::Expected<void, std::string> StepTheConfirmBox(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    bool& click = runtime.movement.pendingClick;
    if (runtime.dialogs.current != ConfirmDialog || !click)
        return {};
    const ConfirmBoxState& box = runtime.social.confirmBox;
    // 0x471956..0x471978: el alto, entero, y su float.
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
        // 0x4719f5: el otro botón, sin volver a mirar el clic.
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

Core::Expected<void, std::string> AnswerTheConfirmBox(const GameUiContext& context, std::int32_t id,
                                                      std::int32_t answer)
{
    if (id != OpenShopConfirm || (answer != Yes && answer != No))
        return {};
    WorldRuntime& runtime = context.world.Runtime();
    if (answer == Yes)
    {
        const std::string& title = runtime.social.personalShop.ownTitle;
        if (auto sent = SendInC3(context, Shop::WriteOpenShop(TitleBytes(title)), "0x62378b"); !sent.HasValue())
            return sent;
        // 0x623daa: 0x657a90, con lo que cierra mandando lo suyo (el 3D, el 3F 07 y el 31).
        GamePanels panels(context.world, context.random);
        panels.LendTheSession(context.world, context.random, context.requests);
        if (auto closed = panels.Registry().CloseAll(); !closed.HasValue())
            return closed;
    }
    // 0x623daf..0x623dc7 y 0x623ddd..0x623ded.
    runtime.dialogs.current = runtime.dialogs.pending;
    runtime.dialogs.pending = 0;
    return {};
}

Core::Expected<void, std::string> StepThePersonalShopWindow(const GameUiContext& context, UI::PanelRegistry& panels)
{
    if (!panels.IsOpen(UI::PersonalShopPanel) && !panels.IsOpen(UI::OtherShopPanel))
        return {};
    WorldRuntime& runtime = context.world.Runtime();
    PersonalShopState& shops = runtime.social.personalShop;
    TradeState& trade = runtime.social.trade;
    if (shops.mode != OwnShopView)
        return {};
    // 0x5ea2f1..0x5ea310.
    if (shops.dialogMode == ShopTitleMode && (trade.warningByte == Yes || trade.warningByte == No))
    {
        shops.dialogMode = 0;
        trade.warningByte = NoAnswer;
    }
    shops.windowX = ShopWindowX;
    shops.windowY = ShopWindowY;
    bool& click = runtime.movement.pendingClick;
    if (InRect(context, OpenButtonLeft, FootButtonTop, OpenButtonRight, FootButtonBottom) && click)
    {
        if (auto pressed = PressTheOpenButton(context, panels); !pressed.HasValue())
            return pressed;
    }
    // 0x5eaa54..0x5eafb2: el de cerrar gasta el clic aunque la tienda esté cerrada.
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

Core::Expected<bool, std::string> StepTheWarningAnswers(const GameUiContext& context, const InventoryMoveContext& moves)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t kind = runtime.social.trade.warningKind;
    if (kind >= 1 && kind <= PriceWarningKind)
        return AnswerTheWarning(context, moves, kind);
    if (runtime.social.personalShop.dialogMode == ShopPriceMode)
        return AnswerThePriceDialog(context, moves);
    return true;
}
} // namespace Client::Scenes
