#include "Scenes/SiegeBattleUi.h"

#include "Core/Expected.h"
#include "Protocol/SiegeControls.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "UI/SiegePanelState.h"
#include "Math/Ftol.h"

#include <cstdint>
#include <bit>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t MapInsetX = 23;
constexpr std::int32_t MapInsetY = 45;
constexpr std::int32_t MapSize = 128;
constexpr std::int32_t RowStride = 17;
constexpr std::uint8_t NoCommand = 3;

bool Inside(std::int32_t x, std::int32_t y, std::int32_t left, std::int32_t top, std::int32_t width,
            std::int32_t height)
{
    return left <= x && x <= left + width && top <= y && y <= top + height;
}
void ConsumeLeft(WorldRuntime& runtime)
{
    runtime.movement.pendingClick = false;
    runtime.movement.leftHeld = false;
}
Core::Expected<void, std::string> SendCommand(const GameUiContext& context)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::string("0x4108a7: sin cable para la orden del minimapa"));
    const UI::SiegePanelState& panel = context.world.Runtime().siegePanel;
    context.requests->SendGamePacket(
        Protocol::SiegeControls::WriteCommand(panel.selected, panel.targetX, panel.targetY, panel.command));
    return {};
}

// 0x40f6d0: el mismo prefijo de las tres vtables.
bool StepBase(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::SiegePanelState& panel = runtime.siegePanel;
    // 0x40f6df..0x40f6f6: primero 0x6578e0, sólo sin campo/foco ajeno consulta y consume Tab.
    if (!GameTextInputBusy(runtime) && OldWindowKeyEdge(runtime.ui, context.input, 9))
        panel.visible ^= 1;
    if (panel.visible == 0)
        return false;
    const std::int32_t x = context.mouseX;
    const std::int32_t y = context.mouseY;
    if (panel.x < x && x <= panel.x + 151 && panel.y < y && y < panel.y + 173)
        runtime.ui.mouseOnUi = 1;
    if (!runtime.movement.pendingClick)
        return true;
    if (Inside(x, y, panel.x + 127, panel.y + 13, 24, 22))
    {
        panel.page = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(panel.page) + 1u);
        if (panel.page >= 3)
            panel.page = 1;
        else if (panel.page < 1)
            panel.page = 2;
    }
    else
    {
        if (!Inside(x, y, panel.x + 5, panel.y + 28, 38, 16))
            return true;
        const double sum = static_cast<double>(panel.alpha) + static_cast<double>(0.1f);
        panel.alpha = static_cast<float>(sum);
        if (sum > static_cast<double>(1.1f))
            panel.alpha = 0.5f;
        // 40f81a..40f827: FCOMP activaC0 también si el resultado esunordered; en ambos casos vuelve a1.
        else if (!(sum >= 0.5))
            panel.alpha = 1.0f;
    }
    ConsumeLeft(runtime);
    return true;
}

// 0x41066f..0x4107df: elegir una fila y una de sus tres órdenes. El derecho sobre una fila vuelve sin consumirlo.
bool ChooseCommand(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::SiegePanelState& panel = runtime.siegePanel;
    const std::int32_t x = context.mouseX;
    const std::int32_t y = context.mouseY;
    for (std::uint8_t row = 0; row < 7; ++row)
    {
        if (!Inside(x, y, panel.x + 3, panel.y + 49 + RowStride * row, 19, 18))
            continue;
        panel.command = NoCommand;
        if (runtime.ui.rightClick)
        {
            panel.selected = 0;
            return false;
        }
        panel.selected = static_cast<std::uint8_t>(row + 1);
        break;
    }
    if (panel.selected == 0 || panel.command != NoCommand)
        return true;
    std::int32_t top = panel.y + 54 + (panel.selected - 1) * RowStride - 5;
    if (panel.selected == 6)
        top -= 18;
    else if (panel.selected == 7)
        top -= 36;
    for (std::uint8_t command = 0; command < 3; ++command)
    {
        if (Inside(x, y, panel.x + 22, top + command * 18, 30, 18))
        {
            panel.command = command;
            ConsumeLeft(runtime);
            break;
        }
    }
    return true;
}

Core::Expected<void, std::string> PlaceCommand(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::SiegePanelState& panel = runtime.siegePanel;
    if (!Inside(context.mouseX, context.mouseY, panel.x + MapInsetX, panel.y + MapInsetY, MapSize, MapSize) ||
        panel.selected == 0 || panel.selected >= 8 || panel.command == NoCommand)
        return {};
    if (runtime.movement.pendingClick)
    {
        // 0x41085e..0x4108a3: coordenadas en el mapa, truncadas por _ftol y después al byte bajo.
        const double x = static_cast<double>(panel.offsetX + context.mouseX) - static_cast<double>(panel.x + MapInsetX);
        const double y = static_cast<double>(panel.offsetY + context.mouseY) - static_cast<double>(panel.y + MapInsetY);
        panel.targetX = static_cast<std::uint8_t>(Math::OriginalFtolLow(x * panel.page).value_or(0));
        panel.targetY = static_cast<std::uint8_t>(Math::OriginalFtolLow(256.0 - y * panel.page).value_or(0));
        panel.targetTtl = 100;
        if (auto sent = SendCommand(context); !sent.HasValue())
            return sent;
        panel.command = NoCommand;
        panel.selected = 0;
        ConsumeLeft(runtime);
        return {};
    }
    if (runtime.ui.rightClick)
    {
        panel.command = NoCommand;
        panel.targetX = 0;
        panel.targetY = 0;
        if (auto sent = SendCommand(context); !sent.HasValue())
            return sent;
        panel.selected = 0;
        runtime.ui.rightClick = false;
        runtime.ui.rightDown = false;
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> StepTheBattlePanel(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (!runtime.siegePanelKind || !StepBase(context))
        return {};
    if (*runtime.siegePanelKind != 2 || (!runtime.movement.pendingClick && !runtime.ui.rightClick))
        return {};
    if (!ChooseCommand(context))
        return {};
    return PlaceCommand(context);
}
} // namespace Client::Scenes
