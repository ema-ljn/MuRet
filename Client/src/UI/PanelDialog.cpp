#include "UI/PanelDialog.h"

#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "UI/ChatLogWindow.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindows.h"
#include "UI/Widget.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::UI
{
namespace
{
// 0x6600d0: el ancho fijo, lo que suman al alto los botones, el campo y el reloj, y el espacio de cada renglón.
constexpr std::int32_t DialogWidth = 0xd5;
constexpr std::int32_t BareExtraHeight = 0x2b;
constexpr std::int32_t ButtonsExtraHeight = 0x3f;
constexpr std::int32_t FieldExtraHeight = 0x11;
constexpr std::int32_t LineGap = 2;
constexpr std::uint32_t AnyButton = PanelDialogOk | PanelDialogYesNo | PanelDialogConfirm;
// 0x6605c3..0x6605f2 y 0x6576a0: el centro de la pantalla de 640 × 480 que usan el paso y la caja.
constexpr std::int32_t CenterX = 0x140;
constexpr std::int32_t CenterY = 0x82;
// 0x6576a0: la x de la caja, su y más arriba y la que usa con +0xa24.
constexpr std::int32_t BoxLeft = 0xd6;
constexpr float BoxMinimumTop = 40.0f;
constexpr std::int32_t PlacedTop = 10;
// 0x660830: el primer renglón, 15 por renglón (0x6b87bc), 7 y 10 por el campo y por el reloj (0x6b8878 y 0x6b87a8) y
// 9 hasta los botones (0x6b887c).
constexpr std::int32_t FirstLineOffset = 0x14;
constexpr float LineStep = 15.0f;
constexpr float SectionTop = 7.0f;
constexpr float SectionBottom = 10.0f;
constexpr float ButtonsOffset = 9.0f;
// 0x660bc8..0x660c54: la x de un botón solo y la de los dos.
constexpr std::int32_t SingleButtonX = 0x127;
constexpr std::int32_t LeftButtonX = 0x104;
constexpr std::int32_t RightButtonX = 0x14a;
// 0x65feb0: el tamaño de los botones.
constexpr std::int32_t ButtonWidth = 0x32;
constexpr std::int32_t ButtonHeight = 0x12;
// 0x660410: las teclas.
constexpr std::int32_t EscapeKey = 0x1b;
constexpr std::int32_t EnterKey = 0xd;
// 0x45b200: el estado apagado, el modo del rectángulo y el sonido del soltado (0x41ed10, CL10).
constexpr std::int32_t TopLeftRect = 1;
constexpr std::int32_t ButtonSound = 0x19;

// Hasta el primer '\0', como lo copia strcpy, recortado al buffer de 0x100.
std::string LineOf(std::string_view text)
{
    const std::size_t end = text.find('\0');
    std::string_view line = end == std::string_view::npos ? text : text.substr(0, end);
    if (line.size() >= PanelDialogLineBytes)
        line = line.substr(0, PanelDialogLineBytes - 1);
    return std::string(line);
}

// Lo común de 0x6603c0, 0x660410 y 0x6605b0 con un motivo: sin función, se cierra; con ella, se cierra si no devuelve
// 0, y antes pone +4 en 0.
Core::Expected<void, std::string> Answer(PanelDialogState& dialog, std::uint32_t reason,
                                         const PanelDialogAnswer& answer)
{
    if (dialog.owner != PanelDialogOwner::None)
    {
        if (!answer)
            return Core::Unexpected(std::string("0x6600d0: el diálogo de los paneles responde sin quien lo abrió"));
        auto closes = answer(dialog.owner, reason);
        if (!closes.HasValue())
            return Core::Unexpected(std::move(closes).Error());
        if (!closes.Value())
            return {};
        dialog.owner = PanelDialogOwner::None;
    }
    return CloseThePanelDialog(dialog);
}
} // namespace

bool ClickTheButton(OldButton& button, const OldWindowGlobals& globals, std::int32_t mouseX, std::int32_t mouseY)
{
    OldWindowBase& base = button.base;
    if (base.state == DisabledWindow)
        return false;
    if (MouseInRect(mouseX, mouseY, base.x, base.y, base.width, base.height, TopLeftRect))
    {
        globals.mouseOnUi = 1;
        if (globals.leftClick && globals.pressedButton == 0)
        {
            globals.pressedButton = base.id;
            button.pressed = 1;
        }
        if (globals.releasedClick && globals.pressedButton == base.id)
        {
            globals.releasedClick = false;
            globals.sound = ButtonSound;
            button.pressed = 0;
            globals.pressedButton = 0;
            return true;
        }
        return false;
    }
    if (button.pressed == 1)
    {
        globals.leftHeld = false;
        globals.leftClick = false;
        globals.releasedClick = false;
        globals.pressedButton = 0;
        button.pressed = 0;
    }
    return false;
}

std::array<OldButton, PanelDialogButtonCount> PanelDialogState::DefaultButtons()
{
    std::array<OldButton, PanelDialogButtonCount> buttons{};
    for (std::size_t i = 0; i < buttons.size(); ++i)
    {
        buttons[i].base.id = -static_cast<std::int32_t>(i) - 1;
        buttons[i].base.width = ButtonWidth;
        buttons[i].base.height = ButtonHeight;
        buttons[i].command = static_cast<std::int32_t>(i);
    }
    return buttons;
}

void ResetThePanelDialog(PanelDialogState& dialog)
{
    dialog.owner = PanelDialogOwner::None;
    dialog.hooks = PanelDialogHooks::None;
    dialog.opened = 0;
    dialog.lineCount = 0;
    dialog.flags = 0;
    for (std::string& line : dialog.lines)
        line.clear();
}

PanelDialogOpening OpenThePanelDialog(PanelDialogState& dialog, const PanelDialogRequest& request,
                                      const Render::Text::ITextMeasure& measure, Render::Text::FontKind dcFont,
                                      std::uint32_t now)
{
    if (request.lines.size() > PanelDialogMaxLines)
        return PanelDialogOpening::TooManyLines;
    if (dialog.opened != 0)
        return PanelDialogOpening::Busy;
    ResetThePanelDialog(dialog);
    std::int32_t height = 0;
    for (std::size_t i = 0; i < request.lines.size(); ++i)
    {
        dialog.lines[i] = LineOf(request.lines[i]);
        if (dialog.lines[i].empty())
            continue;
        height += measure.Measure(dcFont, dialog.lines[i]).height + LineGap;
    }
    dialog.lineCount = static_cast<std::int32_t>(request.lines.size());
    dialog.owner = request.owner;
    dialog.flags = request.flags;
    dialog.width = DialogWidth;
    dialog.placed = request.placed;
    dialog.height = height + ((request.flags & AnyButton) != 0 ? ButtonsExtraHeight : BareExtraHeight);
    if ((request.flags & PanelDialogTextField) != 0)
        dialog.height += FieldExtraHeight;
    if ((request.flags & PanelDialogTimed) != 0)
        dialog.height += FieldExtraHeight;
    dialog.timerStart = now;
    dialog.opened = now;
    dialog.timerEnd = dialog.timerMs + now;
    return PanelDialogOpening::Opened;
}

Core::Expected<void, std::string> CloseThePanelDialog(PanelDialogState& dialog)
{
    if (dialog.opened == 0)
        return {};
    if ((dialog.flags & PanelDialogTextField) != 0)
        return Core::Unexpected(
            std::string("0x660350: cerrar el diálogo con el campo de texto vacía el EDIT (0x5763504) "
                        "o el buffer del chat, que ningún diálogo de MuRet abre"));
    dialog.opened = 0;
    return {};
}

Core::Expected<void, std::string> CancelThePanelDialog(PanelDialogState& dialog, const PanelDialogAnswer& answer)
{
    if (dialog.opened == 0)
        return {};
    if (dialog.owner != PanelDialogOwner::None)
    {
        std::uint32_t reason = PanelAnswerEnter;
        if ((dialog.flags & PanelDialogOk) != 0)
            reason = PanelAnswerOk;
        else if ((dialog.flags & PanelDialogYesNo) != 0)
            reason = PanelAnswerNo;
        else if ((dialog.flags & PanelDialogConfirm) != 0)
            reason = PanelAnswerCancel;
        if (!answer)
            return Core::Unexpected(std::string("0x6603c0: el diálogo de los paneles responde sin quien lo abrió"));
        // 0x6603f6: lo que devuelve no se mira.
        auto answered = answer(dialog.owner, reason);
        if (!answered.HasValue())
            return Core::Unexpected(std::move(answered).Error());
        dialog.owner = PanelDialogOwner::None;
    }
    return CloseThePanelDialog(dialog);
}

Core::Expected<bool, std::string> ThePanelDialogKey(PanelDialogState& dialog, std::int32_t virtualKey,
                                                    const PanelDialogAnswer& answer)
{
    if (dialog.opened == 0)
        return false;
    std::uint32_t reason = 0;
    if (virtualKey == EscapeKey && (dialog.flags & PanelDialogOk) != 0)
        reason = PanelAnswerEscapeOk;
    else if (virtualKey == EscapeKey && (dialog.flags & PanelDialogYesNo) != 0)
        reason = PanelAnswerEscapeYesNo;
    else if (virtualKey == EscapeKey && (dialog.flags & PanelDialogConfirm) != 0)
        reason = PanelAnswerEscapeConfirm;
    else if (virtualKey == EscapeKey)
        reason = PanelAnswerEscape;
    else if (virtualKey != EnterKey || (dialog.flags & PanelDialogTextField) == 0)
        return false;
    else if ((dialog.flags & (PanelDialogOk | PanelDialogYesNo)) != 0)
        reason = PanelAnswerOk;
    else if ((dialog.flags & PanelDialogConfirm) != 0)
        reason = PanelAnswerConfirm;
    else
        reason = PanelAnswerEnter;
    if (auto answered = Answer(dialog, reason, answer); !answered.HasValue())
        return Core::Unexpected(std::move(answered).Error());
    return true;
}

bool ThePanelDialogTimedOut(const PanelDialogState& dialog, std::uint32_t now)
{
    if ((dialog.flags & PanelDialogTimed) == 0)
        return false;
    return dialog.timerEnd <= now;
}

Core::Expected<void, std::string> StepThePanelDialog(PanelDialogState& dialog, const OldWindowGlobals& globals,
                                                     std::int32_t mouseX, std::int32_t mouseY, std::uint32_t now,
                                                     const PanelDialogAnswer& answer)
{
    if (dialog.opened == 0)
        return {};
    // 0x6605c3..0x660615: la división de C (CDQ, SUB, SAR) redondea hacia el 0.
    if (MouseInRect(mouseX, mouseY, CenterX - dialog.width / 2, CenterY - dialog.height / 2, dialog.width,
                    dialog.height, TopLeftRect))
        globals.mouseOnUi = 1;
    // 0x66061c: +8, el paso propio que pone 0x6602b0 (0x6504d4, la ventana de los cargos del gremio), en lugar de lo
    // demás. Lo llama Scenes después de este.
    if (dialog.hooks != PanelDialogHooks::None)
        return {};
    const bool timedOut = ThePanelDialogTimedOut(dialog, now);
    std::optional<std::uint32_t> reason;
    if (timedOut)
    {
        reason = PanelAnswerTimeout;
    }
    else if ((dialog.flags & PanelDialogOk) != 0)
    {
        if (ClickTheButton(dialog.buttons[PanelOkButton], globals, mouseX, mouseY))
            reason = PanelAnswerOk;
    }
    else if ((dialog.flags & PanelDialogYesNo) != 0)
    {
        if (ClickTheButton(dialog.buttons[PanelOkButton], globals, mouseX, mouseY))
            reason = PanelAnswerOk;
        else if (ClickTheButton(dialog.buttons[PanelNoButton], globals, mouseX, mouseY))
            reason = PanelAnswerNo;
    }
    else if ((dialog.flags & PanelDialogConfirm) != 0)
    {
        if (ClickTheButton(dialog.buttons[PanelConfirmButton], globals, mouseX, mouseY))
            reason = PanelAnswerConfirm;
        else if (ClickTheButton(dialog.buttons[PanelCancelButton], globals, mouseX, mouseY))
            reason = PanelAnswerCancel;
    }
    if (!reason)
        return {};
    return Answer(dialog, *reason, answer);
}

UI::Point PlaceThePanelDialog(PanelDialogState& dialog)
{
    std::int32_t top = PlacedTop;
    if (dialog.placed == 0)
    {
        // 0x6576a0 con −1: 0x82 − alto / 2 en float, con 40 de mínimo.
        const std::int32_t centered = CenterY - dialog.height / 2;
        top = static_cast<float>(centered) < BoxMinimumTop ? static_cast<std::int32_t>(BoxMinimumTop) : centered;
    }
    // 0x6608be..0x660bb2: la suma de la x87 guardada en float; todos los sumandos son enteros chicos.
    float y = static_cast<float>(top + FirstLineOffset);
    y += LineStep * static_cast<float>(std::max(dialog.lineCount, 0));
    if ((dialog.flags & PanelDialogTextField) != 0)
        y += SectionTop + SectionBottom;
    if ((dialog.flags & PanelDialogTimed) != 0)
        y += SectionTop + SectionBottom;
    const auto buttonsY = static_cast<std::int32_t>(y + ButtonsOffset);
    const auto place = [buttonsY](OldButton& button, std::int32_t x)
    {
        button.base.x = x;
        button.base.y = buttonsY;
    };
    if ((dialog.flags & PanelDialogOk) != 0)
        place(dialog.buttons[PanelOkButton], SingleButtonX);
    else if ((dialog.flags & PanelDialogYesNo) != 0)
    {
        place(dialog.buttons[PanelOkButton], LeftButtonX);
        place(dialog.buttons[PanelNoButton], RightButtonX);
    }
    else if ((dialog.flags & PanelDialogConfirm) != 0)
    {
        place(dialog.buttons[PanelConfirmButton], LeftButtonX);
        place(dialog.buttons[PanelCancelButton], RightButtonX);
    }
    return UI::Point{BoxLeft, top};
}
} // namespace Client::UI
