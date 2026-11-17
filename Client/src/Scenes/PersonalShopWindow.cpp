#include "Scenes/PersonalShopWindow.h"

#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PersonalShop.h"
#include "Protocol/Trade.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemPrice.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameUiState.h"
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
constexpr std::int32_t PriceWarningDialog = 0x99;
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
            auto warned = UI::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.texts, PriceWarningLines,
                                             PriceWarningText, PriceWarningDialog);
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
        if (answer == Yes)
            return Core::Unexpected(std::string("0x5d8e0d: el sí del aviso 1 de 0x78abb14 es de la CL9.3"));
        // 0x5d931f..0x5d9334: 0x5be150.
        clear();
        if (auto returned = ReturnTheCursorItem(moves); !returned.HasValue())
            return Core::Unexpected(std::move(returned).Error());
        return false;
    case 2:
        if (answer == Yes)
            return Core::Unexpected(std::string("0x5d934f: el sí del aviso 2 de 0x78abb14 (0x7d2dc80) es de la CL9.3"));
        clear();
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
    case 4:
        return Core::Unexpected(std::format("0x5d9b82: la respuesta {} del aviso 4 de 0x78abb14 (0x7879fc8 y "
                                            "0x78abb08) es de la CL9.3",
                                            answer));
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
        // 0x623daa: 0x657a90, con las tiendas que cierra mandando lo suyo.
        GamePanels panels(context.world, context.random);
        panels.LendThePersonalShops(context.world, context.random,
                                    [requests = context.requests](Protocol::Outgoing outgoing)
                                    { requests->SendGamePacket(std::move(outgoing)); });
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
