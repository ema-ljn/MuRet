#include "Scenes/GameSocialSteps.h"

#include "Core/Expected.h"
#include "Protocol/Duel.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Trade.h"
#include "Render/Models/Cloth.h"
#include "Scenes/GameUi.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace Client::Scenes
{
namespace
{
// 0x5ece12..0x5ed4c6: los botones del pie, de 0x18 × 0x18 en y + 0x186.
constexpr std::int32_t ButtonTop = 0x186;
constexpr std::int32_t ButtonSize = 0x18;
constexpr std::int32_t MoneyButtonLeft = 0x1a;
constexpr std::int32_t OkButtonLeft = 0x61;
constexpr std::int32_t CancelButtonLeft = 0x89;
constexpr std::int32_t ButtonSound = 0x19;
// 0x5ece66..0x5ece9a: el diálogo del zen y lo que deja.
constexpr std::int32_t MoneyDialog = 0x73;
constexpr std::int32_t MoneyFieldLimit = 8;
constexpr std::int32_t MoneyFieldCount = 1;
constexpr std::int32_t MoneyButtonMark = 2;
// 0x5ecf5b..0x5ecf71: el aviso del OK con la grilla del otro cambiada.
constexpr std::int32_t ChangedWarningKind = 3;
constexpr std::uint8_t ChangedWarningByte = 0xff;
constexpr std::int32_t ChangedWarningLines = 4;
constexpr std::int32_t ChangedWarningText = 0x173;
constexpr std::int32_t ChangedWarningDialog = 0x94;
// 0x5ed92c..0x5ed936.
constexpr std::int32_t ButtonMouseUpdateTimeMax = 6;
// 0x5ebcdf..0x5ebcf8: la caja del pedido de duelo.
constexpr std::int32_t DuelBoxLeft = 0xd5;
constexpr std::int32_t DuelBoxRight = 0x1aa;
constexpr std::int32_t DuelBoxTop = 0x6e;
constexpr std::int32_t DuelBoxBottom = 0x118;
// 0x5ebd01..0x5ebd2b y 0x5ec522..: sí y no.
constexpr std::int32_t DuelYesLeft = 0xed;
constexpr std::int32_t DuelYesRight = 0x133;
constexpr std::int32_t DuelNoLeft = 0x14c;
constexpr std::int32_t DuelNoRight = 0x192;
constexpr std::int32_t DuelButtonTop = 0xfa;
constexpr std::int32_t DuelButtonBottom = 0x10f;
// El relleno y el serial de un C3 (rand() en 0x5ed19a o 0x5ec1e4, y 0x40b3f0 al cerrar la entrada del serial).
constexpr std::uint32_t C3Randoms = 2;

bool InRange(const GameUiContext& context, std::int32_t left, std::int32_t right, std::int32_t top, std::int32_t bottom)
{
    return context.mouseX >= left && context.mouseX < right && context.mouseY >= top && context.mouseY < bottom;
}

bool OnTheButton(const GameUiContext& context, std::int32_t x, std::int32_t y, std::int32_t left)
{
    return InRange(context, x + left, x + left + ButtonSize, y + ButtonTop, y + ButtonTop + ButtonSize);
}

Core::Expected<void, std::string> SendInC3(const GameUiContext& context, Protocol::Outgoing outgoing, const char* where)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::string(where) + ": el paquete sin el cable");
    context.random.Discard(C3Randoms);
    context.requests->SendGamePacket(std::move(outgoing));
    return {};
}

// 0x5ece4c..0x5eceae.
void PressTheMoneyButton(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    SocialState& social = runtime.social;
    runtime.movement.pendingClick = false;
    UI::PushDialog(runtime.dialogs, MoneyDialog);
    UI::ResetTheTextBuffers(runtime.chat.input, false);
    social.dialogFieldLimit = MoneyFieldLimit;
    runtime.chat.input.fieldCount = MoneyFieldCount;
    runtime.ui.chatOpen = false;
    social.moneyField = 1;
    social.typedMoney = 0;
    social.trade.field78abacc = MoneyButtonMark;
    runtime.lastSoundId = ButtonSound;
}

// 0x5ecf2d..0x5ed476.
Core::Expected<void, std::string> PressTheOkButton(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    TradeState& trade = runtime.social.trade;
    runtime.movement.pendingClick = false;
    runtime.lastSoundId = ButtonSound;
    if (runtime.cursorItemCount != 0)
        return {};
    if (trade.changed != 0 && trade.ownOk == 0)
    {
        trade.warningKind = ChangedWarningKind;
        trade.warningByte = ChangedWarningByte;
        return UI::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.texts, ChangedWarningLines,
                                  ChangedWarningText, ChangedWarningDialog);
    }
    trade.ownOk = trade.ownOk != 0 ? 0 : 1;
    trade.changed = 1;
    return SendInC3(context, Protocol::Trade::WriteTradeOkButton(trade.ownOk), "0x5ed1a0");
}

// 0x5ed4cc..0x5ed936.
Core::Expected<void, std::string> PressTheCancelButton(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    runtime.movement.pendingClick = false;
    runtime.lastSoundId = ButtonSound;
    if (runtime.cursorItemCount != 0)
        return {};
    runtime.social.trade.changed = 0;
    if (auto sent = SendInC3(context, Protocol::Trade::WriteTradeCancel(), "0x5ed525"); !sent.HasValue())
        return sent;
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
    return {};
}

Protocol::Duel::Name RivalName(const DuelState& duel)
{
    Protocol::Duel::Name name{};
    std::ranges::copy(std::span(duel.rivalName).first(name.size()), name.begin());
    return name;
}
} // namespace

Core::Expected<void, std::string> StepTheTradeWindow(const GameUiContext& context, UI::PanelRegistry& panels)
{
    if (!panels.IsOpen(UI::TradePanel))
        return {};
    WorldRuntime& runtime = context.world.Runtime();
    TradeState& trade = runtime.social.trade;
    const std::int32_t x = runtime.hud.tradeX;
    const std::int32_t y = runtime.hud.tradeY;
    if (OnTheButton(context, x, y, MoneyButtonLeft) && runtime.movement.pendingClick)
        PressTheMoneyButton(context);
    if (trade.okWait > 0)
        --trade.okWait;
    if (trade.okWait == 0 && runtime.cursorItemCount == 0 && OnTheButton(context, x, y, OkButtonLeft) &&
        runtime.movement.pendingClick)
    {
        if (auto pressed = PressTheOkButton(context); !pressed.HasValue())
            return pressed;
    }
    if (OnTheButton(context, x, y, CancelButtonLeft) && runtime.movement.pendingClick)
        return PressTheCancelButton(context);
    return {};
}

Core::Expected<void, std::string> StepTheDuelRequest(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field78abad9 == 0)
        return {};
    if (InRange(context, DuelBoxLeft, DuelBoxRight, DuelBoxTop, DuelBoxBottom))
        runtime.ui.mouseOnUi = 1;
    const DuelState& duel = runtime.social.duel;
    const auto answer = [&](std::uint8_t accepted, const char* where) -> Core::Expected<void, std::string>
    {
        const auto key = static_cast<std::uint16_t>(duel.rivalKey & 0xffff);
        if (auto sent = SendInC3(context, Protocol::Duel::WriteDuelAnswer(accepted, key, RivalName(duel)), where);
            !sent.HasValue())
            return sent;
        runtime.ui.field78abad9 = 0;
        return {};
    };
    if (InRange(context, DuelYesLeft, DuelYesRight, DuelButtonTop, DuelButtonBottom) && runtime.movement.pendingClick)
    {
        if (auto yes = answer(1, "0x5ebd3f"); !yes.HasValue())
            return yes;
    }
    if (InRange(context, DuelNoLeft, DuelNoRight, DuelButtonTop, DuelButtonBottom) && runtime.movement.pendingClick)
        return answer(0, "0x5ec55d");
    return {};
}
} // namespace Client::Scenes
