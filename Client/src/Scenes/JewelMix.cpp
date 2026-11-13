#include "Scenes/JewelMix.h"

#include "Core/Expected.h"
#include "Protocol/Connection/Logout.h"
#include "Protocol/JewelMix.h"
#include "Protocol/Outgoing.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
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
// 0x41ed10(0x19, 0, 0) y 0x6c8dcc: el sonido de los botones de los diálogos y el máximo de MouseUpdateTime.
constexpr std::int32_t DialogSound = 0x19;
constexpr std::int32_t DialogMouseUpdateTimeMax = 6;
// 0x6e5794: juntar (el 0xa2) o deshacer (el 0xa4).
constexpr std::int32_t MixMode = 0;
constexpr std::int32_t UnmixMode = 1;
constexpr std::int32_t ChoiceDialog = 0xa2;
constexpr std::int32_t UnmixDialog = 0xa4;
// 0x6284e5: el que espera el BC después del sí del 0xa3.
constexpr std::int32_t WaitingDialog = 0xa5;
// 0x6c62e0 y 0x6c62e1 sin elegir.
constexpr std::uint8_t Unchosen = 0xff;
// 0x6c932e: las respuestas del 0xa3 (0x6284b6 y 0x628490).
constexpr std::uint8_t YesAnswer = 1;
constexpr std::uint8_t NoAnswer = 2;

// 0x402bc0: las joyas (0x1c0d bendición, 0x1c0e alma) y los paquetes (0x181e y 0x181f).
constexpr std::uint16_t BlessJewel = 0x1c0d;
constexpr std::uint16_t SoulJewel = 0x1c0e;
constexpr std::uint16_t BlessBundle = 0x181e;
constexpr std::uint16_t SoulBundle = 0x181f;
constexpr std::uint8_t BlessKind = 0;
constexpr std::uint8_t SoulKind = 1;
// 0x402c61, 0x402cc9 y 0x402cdf: por qué no.
constexpr std::uint8_t MissingJewels = 2;
constexpr std::uint8_t NoBundle = 4;
constexpr std::uint8_t OtherRefusal = 5;
// 0x402cb2: lo que deja con la mezcla posible.
constexpr std::uint8_t Possible = 100;
// 0x402c7e: la casilla del paquete, de 0 a 0x3f.
constexpr std::int32_t BundleCells = 0x40;
// 0x402cf9 y 0x402d04..0x402d1a: el precio, 1.000.000 al deshacer y las joyas · 5⁵ · 16 al juntar.
constexpr std::int32_t UnmixPrice = 1000000;
constexpr std::int32_t PricePerJewel = 50000;
// 0x402b8c y 0x402b95: los avisos de 0x402b70 (Text.bmd), de error.
constexpr std::size_t MissingJewelsText = 1817;
constexpr std::size_t NoBundleText = 1818;
// 0x401f55: la casilla del paquete pasa a lugar del inventario.
constexpr std::int32_t InventorySlotBase = 12;
// 0x402500: el paquete del BC 00, de a 10.
constexpr std::int32_t JewelsPerBundle = 10;

// Los botones, de 0x6259df..0x625b07 (0xa1), 0x625b3c..0x625c7e (0xa2) y 0x625fad..0x626002 (0xa6), con los
// bordes.
constexpr std::int32_t ButtonLeft = 245;
constexpr std::int32_t ButtonRight = 395;
constexpr std::int32_t MenuFirstTop = 190;
constexpr std::int32_t MenuStep = 65;
constexpr std::int32_t ButtonHeight = 35;
constexpr std::int32_t KindTop = 185;
constexpr std::int32_t KindHeight = 25;
constexpr std::int32_t KindWidth = 80;
constexpr std::int32_t KindRowStep = 40;
constexpr std::int32_t CountStep = 45;
constexpr std::array<std::uint8_t, 3> Counts{10, 20, 30};
constexpr std::int32_t CloseLeft = 279;
constexpr std::int32_t CloseRight = 360;
constexpr std::int32_t MenuCloseTop = 320;
constexpr std::int32_t MenuCloseBottom = 345;
constexpr std::int32_t ChoiceCloseTop = 365;
constexpr std::int32_t ChoiceCloseBottom = 390;
constexpr std::int32_t ResultLeft = 287;
constexpr std::int32_t ResultRight = 352;
constexpr std::int32_t ResultTop = 170;
constexpr std::int32_t ResultBottom = 195;

// x en [left, right] e y en [top, bottom], con los bordes: los FCOM de los floats con el mouse entero.
bool Within(const GameUiContext& context, std::int32_t left, std::int32_t right, std::int32_t top, std::int32_t bottom)
{
    return context.mouseX >= left && context.mouseX <= right && context.mouseY >= top && context.mouseY <= bottom;
}

void LeaveTheMouseTime(WorldRuntime& runtime)
{
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = DialogMouseUpdateTimeMax;
}

// El diálogo que espera pasa a abierto (0x6236f0(0) y los MOV de 0x625a49, 0x625cbc y 0x6284ce).
void CloseTheDialog(UI::DialogState& dialogs)
{
    dialogs.current = dialogs.pending;
    dialogs.pending = 0;
}

// Abrir `id` como lo hacen 0x625a5f..0x625a97 y 0x6284e3: esperando si hay uno abierto.
void OpenAfter(UI::DialogState& dialogs, std::int32_t id)
{
    if (dialogs.current != 0)
        dialogs.pending = id;
    else
        dialogs.current = id;
}

Core::Expected<void, std::string> Send(const GameUiContext& context, Protocol::Outgoing packet, std::string_view what)
{
    if (!context.sendNpcWindow)
        return Core::Unexpected(std::format("{} sin la sesión", what));
    return context.sendNpcWindow(std::move(packet));
}

// 0x402e10: todo a su valor de arranque.
void ResetTheJewelMix(NpcWindowState& windows)
{
    JewelMixState& jewels = windows.jewels;
    jewels.mode = MixMode;
    windows.field6e5798 = 0;
    jewels.refusal = 0;
    jewels.kind = Unchosen;
    jewels.count = Unchosen;
    jewels.counted = 0;
    jewels.price = -1;
    jewels.field6e57a0 = 0;
    jewels.cell = -1;
    jewels.level = -1;
}

// 0x402eb0: 0x402e10, el diálogo cerrado y el C1 03 31 en claro.
Core::Expected<void, std::string> CloseTheJewelMix(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    ResetTheJewelMix(runtime.npcWindows);
    UI::PushDialog(runtime.dialogs, 0);
    return Send(context, Protocol::Connection::WriteWindowClose(), "0x402f91: el C1 03 31");
}

// 0x402e50: de vuelta a elegir (el 0xa2 o el 0xa4), o todo cerrado con otro modo.
Core::Expected<void, std::string> ReturnToTheChoice(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    NpcWindowState& windows = runtime.npcWindows;
    if (windows.field6e5798 == 1)
        windows.field6e5798 = 0;
    UI::PushDialog(runtime.dialogs, 0);
    windows.jewels.refusal = 0;
    windows.jewels.kind = Unchosen;
    if (windows.jewels.mode == MixMode)
        UI::PushDialog(runtime.dialogs, ChoiceDialog);
    else if (windows.jewels.mode == UnmixMode)
        UI::PushDialog(runtime.dialogs, UnmixDialog);
    else
        return CloseTheJewelMix(context);
    return {};
}

// 0x402cf0: el precio.
void PriceTheMix(JewelMixState& jewels)
{
    if (jewels.mode != MixMode)
        jewels.price = UnmixPrice;
    else
        jewels.price = static_cast<std::int8_t>(jewels.count) * PricePerJewel;
}

// 0x402bc0: si alcanza para mezclar. Al juntar cuenta las joyas del tipo en las 64 casillas, fila por fila, y alcanza
// en cuanto la cuenta (un byte) llega a 0x6c62e1 con signo; al deshacer mira el paquete de la casilla 0x6c62d8.
Core::Expected<bool, std::string> CheckTheJewels(WorldState& world)
{
    JewelMixState& jewels = world.Runtime().npcWindows.jewels;
    const auto& cells = world.Inventory().cells;
    jewels.field6e57a0 = 0;
    jewels.counted = 0;
    const auto possible = [&]
    {
        jewels.field6e57a0 = Possible;
        PriceTheMix(jewels);
        return true;
    };
    if (jewels.mode == MixMode)
    {
        const std::int32_t needed = static_cast<std::int8_t>(jewels.count);
        for (const World::ItemState& cell : cells)
        {
            if ((cell.id == SoulJewel && jewels.kind == SoulKind) ||
                (cell.id == BlessJewel && jewels.kind == BlessKind))
                ++jewels.counted;
            if (jewels.counted == needed)
                return possible();
        }
        if (jewels.counted >= needed && jewels.count != Unchosen)
        {
            jewels.refusal = OtherRefusal;
            return false;
        }
        jewels.field6e57a0 = 0;
        jewels.refusal = MissingJewels;
        return false;
    }
    if (jewels.mode != UnmixMode)
    {
        jewels.refusal = OtherRefusal;
        return false;
    }
    // 0x402c74..0x402c81: −1 y desde 0x40 no; otro negativo leería antes de la grilla.
    if (jewels.cell < -1)
        return Core::Unexpected(std::format("0x402c83: la casilla {} lee antes del inventario", jewels.cell));
    if (jewels.cell != -1 && jewels.cell < BundleCells)
    {
        const std::uint16_t id = cells[static_cast<std::size_t>(jewels.cell)].id;
        if ((id == BlessBundle && jewels.kind == BlessKind) || (id == SoulBundle && jewels.kind == SoulKind))
        {
            jewels.counted = 1;
            return possible();
        }
    }
    jewels.refusal = NoBundle;
    jewels.field6e57a0 = 0;
    return false;
}

// 0x402b70: si alcanza; si no, el aviso (2 y 4) y la vuelta de 0x402e50.
Core::Expected<bool, std::string> AskForTheJewels(const GameUiContext& context)
{
    auto enough = CheckTheJewels(context.world);
    if (!enough.HasValue() || enough.Value())
        return enough;
    WorldRuntime& runtime = context.world.Runtime();
    const std::uint8_t refusal = runtime.npcWindows.jewels.refusal;
    if (refusal == MissingJewels || refusal == NoBundle)
    {
        const std::size_t index = refusal == MissingJewels ? MissingJewelsText : NoBundleText;
        const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
        if (!text)
            return Core::Unexpected(std::format("0x402b9f: sin el texto {} de Text.bmd", index));
        if (context.measure == nullptr)
            return Core::Unexpected(std::format("0x402b9f: el mensaje del texto {} sin medidor", index));
        // 0x555190(0x6e57a4, texto, 2): el nombre es una cadena vacía de la .bss.
        if (auto added = AddGameMessage(runtime, *context.measure, context.screen, {}, *text, UI::ErrorMessage);
            !added.HasValue())
            return Core::Unexpected(std::move(added).Error());
    }
    if (auto back = ReturnToTheChoice(context); !back.HasValue())
        return Core::Unexpected(std::move(back).Error());
    return false;
}

// 0x4029e0: una vez, y sin un "no" pendiente, el BC.
Core::Expected<void, std::string> SendTheJewelMix(const GameUiContext& context)
{
    NpcWindowState& windows = context.world.Runtime().npcWindows;
    JewelMixState& jewels = windows.jewels;
    if (windows.field6e5798 == 1 || jewels.refusal != 0)
        return {};
    windows.field6e5798 = 1;
    if (jewels.mode == MixMode)
    {
        // 0x40276b..0x402794: el paquete con signo / 10, hacia el cero, menos 1, en un byte.
        const auto bundle = static_cast<std::uint8_t>(static_cast<std::int8_t>(jewels.count) / JewelsPerBundle - 1);
        return Send(context, Protocol::JewelMix::WriteJewelMix(jewels.kind, bundle), "0x402500: el BC 00");
    }
    // 0x401f4e..0x401f5a: la casilla queda como lugar en el global.
    jewels.cell += InventorySlotBase;
    return Send(context,
                Protocol::JewelMix::WriteJewelUnmix(jewels.kind, static_cast<std::uint8_t>(jewels.level),
                                                    static_cast<std::uint8_t>(jewels.cell)),
                "0x401f30: el BC 01");
}

// 0x625cbc..0x625ce5: cierra y abre el 0xa3 con su texto.
Core::Expected<void, std::string> AskTheQuestion(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const JewelMixState& jewels = runtime.npcWindows.jewels;
    CloseTheDialog(runtime.dialogs);
    const UI::DialogTextArguments arguments{.jewelMode = jewels.mode,
                                            .jewelKind = jewels.kind,
                                            .jewelCount = jewels.counted,
                                            .jewelLevel = jewels.level,
                                            .jewelPrice = jewels.price};
    if (auto opened = UI::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.texts, 0, 0, UI::ServerTimeDialog,
                                         nullptr, &arguments);
        !opened.HasValue())
        return opened;
    LeaveTheMouseTime(runtime);
    return {};
}
} // namespace

Core::Expected<void, std::string> StepTheJewelMenu(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    bool& click = runtime.movement.pendingClick;
    if (!click)
        return {};
    click = false;
    for (std::int32_t button = 0; button < 2; ++button)
    {
        const std::int32_t top = MenuFirstTop + button * MenuStep;
        if (!Within(context, ButtonLeft, ButtonRight, top, top + ButtonHeight))
            continue;
        // 0x625a49..0x625ab1.
        CloseTheDialog(runtime.dialogs);
        OpenAfter(runtime.dialogs, button == 0 ? ChoiceDialog : UnmixDialog);
        runtime.npcWindows.jewels.mode = button == 0 ? MixMode : UnmixMode;
        LeaveTheMouseTime(runtime);
        break;
    }
    // 0x625abb..0x625b0d.
    if (Within(context, CloseLeft, CloseRight, MenuCloseTop, MenuCloseBottom))
        return CloseTheJewelMix(context);
    return {};
}

Core::Expected<void, std::string> StepTheJewelChoice(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    JewelMixState& jewels = runtime.npcWindows.jewels;
    bool& click = runtime.movement.pendingClick;
    if (!click)
        return {};
    click = false;
    // 0x625b42: BL, la joya, que se guarda al final.
    std::uint8_t kind = jewels.kind;
    for (std::int32_t button = 0; button < 2; ++button)
    {
        const std::int32_t left = ButtonLeft + button * KindWidth;
        if (!Within(context, left, left + KindWidth, KindTop, KindTop + KindHeight))
            continue;
        kind = button != 0 ? SoulKind : BlessKind;
        LeaveTheMouseTime(runtime);
        break;
    }
    // 0x625bf9: con BL negativo (sin joya), los paquetes no se miran.
    std::int32_t top = KindTop + KindRowStep;
    for (std::size_t count = 0; count < Counts.size() && static_cast<std::int8_t>(kind) >= 0; ++count)
    {
        if (!Within(context, ButtonLeft, ButtonRight, top, top + ButtonHeight))
        {
            top += CountStep;
            continue;
        }
        // 0x625c83..0x625cef.
        jewels.kind = kind;
        jewels.count = Counts[count];
        auto enough = AskForTheJewels(context);
        if (!enough.HasValue())
            return Core::Unexpected(std::move(enough).Error());
        if (enough.Value())
        {
            if (auto asked = AskTheQuestion(context); !asked.HasValue())
                return asked;
        }
        // 0x625c5f: el cierre, con la joya ya guardada.
        if (Within(context, CloseLeft, CloseRight, ChoiceCloseTop, ChoiceCloseBottom))
            return CloseTheJewelMix(context);
        return {};
    }
    // 0x625c57.
    jewels.kind = kind;
    if (Within(context, CloseLeft, CloseRight, ChoiceCloseTop, ChoiceCloseBottom))
        return CloseTheJewelMix(context);
    return {};
}

Core::Expected<void, std::string> StepTheJewelResult(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    bool& click = runtime.movement.pendingClick;
    if (!click)
        return {};
    click = false;
    if (!Within(context, ResultLeft, ResultRight, ResultTop, ResultBottom))
        return {};
    return ReturnToTheChoice(context);
}

Core::Expected<void, std::string> AnswerTheJewelMix(const GameUiContext& context, bool yes)
{
    WorldRuntime& runtime = context.world.Runtime();
    runtime.npcWindows.dialogAnswer = yes ? YesAnswer : NoAnswer;
    LeaveTheMouseTime(runtime);
    runtime.lastSoundId = DialogSound; // (CL10)
    if (!yes)
        return ReturnToTheChoice(context);
    CloseTheDialog(runtime.dialogs);
    OpenAfter(runtime.dialogs, WaitingDialog);
    return SendTheJewelMix(context);
}
} // namespace Client::Scenes
