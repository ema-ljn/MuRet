#include "Ui/PanelDialog.h"

#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Chat/ChatLogWindow.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindows.h"
#include "Ui/Widget.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Ui
{
namespace
{
// 0x6600d0: the fixed width, what the buttons, the field and the timer add to the height, and each line's spacing.
constexpr std::int32_t DialogWidth = 0xd5;
constexpr std::int32_t BareExtraHeight = 0x2b;
constexpr std::int32_t ButtonsExtraHeight = 0x3f;
constexpr std::int32_t FieldExtraHeight = 0x11;
constexpr std::int32_t LineGap = 2;
constexpr std::uint32_t AnyButton = PanelDialogOk | PanelDialogYesNo | PanelDialogConfirm;
// 0x6605c3..0x6605f2 and 0x6576a0: the 640 × 480 screen centre the step and the box use.
constexpr std::int32_t CenterX = 0x140;
constexpr std::int32_t CenterY = 0x82;
// 0x6576a0: the box's x, its topmost y and the one it uses with +0xa24.
constexpr std::int32_t BoxLeft = 0xd6;
constexpr float BoxMinimumTop = 40.0f;
constexpr std::int32_t PlacedTop = 10;
// 0x660830: the first line, 15 per line (0x6b87bc), 7 and 10 for the field and for the timer (0x6b8878 and 0x6b87a8)
// and 9 down to the buttons (0x6b887c).
constexpr std::int32_t FirstLineOffset = 0x14;
constexpr float LineStep = 15.0f;
constexpr float SectionTop = 7.0f;
constexpr float SectionBottom = 10.0f;
constexpr float ButtonsOffset = 9.0f;
// 0x660bc8..0x660c54
constexpr std::int32_t SingleButtonX = 0x127;
constexpr std::int32_t LeftButtonX = 0x104;
constexpr std::int32_t RightButtonX = 0x14a;
// 0x65feb0
constexpr std::int32_t ButtonWidth = 0x32;
constexpr std::int32_t ButtonHeight = 0x12;
// 0x660410
constexpr std::int32_t EscapeKey = 0x1b;
constexpr std::int32_t EnterKey = 0xd;
// 0x45b200: the disabled state, the rectangle mode and the release sound (0x41ed10, CL10).
constexpr std::int32_t TopLeftRect = 1;
constexpr Audio::SoundId ButtonSound = Sound::ButtonClickSound;

// Up to the first '\0', as strcpy copies it.
std::string_view LineOf(std::string_view text)
{
    const std::size_t end = text.find('\0');
    return end == std::string_view::npos ? text : text.substr(0, end);
}

// 0x660163..0x66017b: strcpy into a 0x100-byte slot; with 0x100 or more (and its '\0') it runs into the next one
// (Q-C1305).
bool FitsInItsLine(std::string_view text)
{
    return LineOf(text).size() < PanelDialogLineBytes;
}

// Shared by 0x6603c0, 0x660410 and 0x6605b0 with a reason: without a function, it closes; with one, it closes if it
// does not return 0, and before that sets +4 to 0.
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
    if (Chat::MouseInRect(mouseX, mouseY, base.x, base.y, base.width, base.height, TopLeftRect))
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
            globals.RequestSound(ButtonSound);
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
    dialog.closeField = {};
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
    if (!std::ranges::all_of(request.lines, FitsInItsLine))
        return PanelDialogOpening::LineTooLong;
    ResetThePanelDialog(dialog);
    std::int32_t height = 0;
    for (std::size_t i = 0; i < request.lines.size(); ++i)
    {
        dialog.lines[i] = std::string(LineOf(request.lines[i]));
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
    {
        if (!dialog.closeField)
            return Core::Unexpected(std::string("0x660350: el diálogo con campo de texto sin el EDIT compartido"));
        dialog.closeField();
    }
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
        // 0x6603f6: what it returns is not looked at.
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
    // 0x6605c3..0x660615: C division (CDQ, SUB, SAR) rounds toward 0.
    if (Chat::MouseInRect(mouseX, mouseY, CenterX - dialog.width / 2, CenterY - dialog.height / 2, dialog.width,
                          dialog.height, TopLeftRect))
        globals.mouseOnUi = 1;
    // 0x66061c: +8, the own step 0x6602b0 sets (0x6504d4, the guild positions window), instead of the rest. Guild calls
    // it after this one.
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

Ui::Point PlaceThePanelDialog(PanelDialogState& dialog)
{
    std::int32_t top = PlacedTop;
    if (dialog.placed == 0)
    {
        // 0x6576a0 with −1: 0x82 − height / 2 in float, with a minimum of 40.
        const std::int32_t centered = CenterY - dialog.height / 2;
        top = static_cast<float>(centered) < BoxMinimumTop ? static_cast<std::int32_t>(BoxMinimumTop) : centered;
    }
    // 0x6608be..0x660bb2: the x87 sum stored as float; all the addends are small integers.
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
    return Ui::Point{BoxLeft, top};
}
} // namespace Client::Ui
