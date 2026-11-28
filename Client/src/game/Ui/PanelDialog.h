#pragma once

#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindows.h"
#include "Ui/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Render::Text
{
class ITextMeasure;
}

// The panel dialog, [0x7d53704] (0x65feb0, 0x1064 bytes; CL9 milestone 8, 06 § El diálogo de los paneles): a question
// of up to ten lines with one or two buttons, which the packets (the 50 and the E5) open with 0x6600d0 and which
// answers whoever opened it with a reason. The panel registry handles it (the key first, 0x657880; the step, 0x657930;
// closing everything, 0x657a90) and so does the panel drawing (0x6579e0 → 0x660830).
namespace Client::Ui
{
// +0xa18: the buttons and the rest. 1, OK (228); 2, Yes and No (228 and 229); 4, Confirm and Cancel (1037 and 1038);
// 8, the timer; 0x10, the text field.
inline constexpr std::uint32_t PanelDialogOk = 0x1;
inline constexpr std::uint32_t PanelDialogYesNo = 0x2;
inline constexpr std::uint32_t PanelDialogConfirm = 0x4;
inline constexpr std::uint32_t PanelDialogTimed = 0x8;
inline constexpr std::uint32_t PanelDialogTextField = 0x10;

// The reasons it answers with (0x6603c0, 0x660410 and 0x6605b0).
inline constexpr std::uint32_t PanelAnswerEnter = 0x1;   // Enter without buttons
inline constexpr std::uint32_t PanelAnswerOk = 0x2;      // OK or Yes, or Enter with them
inline constexpr std::uint32_t PanelAnswerNo = 0x4;      // No
inline constexpr std::uint32_t PanelAnswerConfirm = 0x8; // Confirm, or Enter with it
inline constexpr std::uint32_t PanelAnswerCancel = 0x10; // Cancelar
inline constexpr std::uint32_t PanelAnswerTimeout = 0x20;
inline constexpr std::uint32_t PanelAnswerEscape = 0x41; // Esc without buttons
inline constexpr std::uint32_t PanelAnswerEscapeOk = 0x42;
inline constexpr std::uint32_t PanelAnswerEscapeYesNo = 0x44;
inline constexpr std::uint32_t PanelAnswerEscapeConfirm = 0x50;

// 0x6600d0
inline constexpr std::size_t PanelDialogMaxLines = 10;
inline constexpr std::size_t PanelDialogLineBytes = 0x100;
// +0xe40, +0xec8, +0xf50 and +0xfd8: OK or Yes (228), No (229), Confirm (1037) and Cancel (1038).
inline constexpr std::size_t PanelDialogButtonCount = 4;
inline constexpr std::size_t PanelOkButton = 0;
inline constexpr std::size_t PanelNoButton = 1;
inline constexpr std::size_t PanelConfirmButton = 2;
inline constexpr std::size_t PanelCancelButton = 3;
// 0x65feb0: +0xe3c.
inline constexpr std::uint32_t PanelDialogTimerMs = 5000;

// +4: whom it answers. The original keeps the function; MuRet, which one it is, and whoever holds the world and the
// wiring calls it (Guild/GuildPanels).
enum class PanelDialogOwner : std::uint8_t
{
    None,
    GuildJoin,    // 0x4d6b60, the guild join request (the 50)
    Relationship, // 0x653c00, the alliance or rivalry request (the E5)
    GuildDismiss, // 0x64def0, removing a member's position (the members tab)
    GuildKick,    // 0x64e550, kicking a member (the members tab)
    GuildDisband, // 0x64de90, disbanding the guild (tab 0)
    UnionKick,    // 0x650830, kicking a guild from the alliance (the alliance tab)
    CastleGiveUp, // 0x64ccd0
    CastleGate,
    CastleGateLife,
    CastleGateDefense,
    CastleStatue,
    CastleStatueLife,
    CastleStatueDefense,
    CastleStatueRecovery,
    CastleTaxes,
    CastleMoney,
    CastleCrownRegistration, // 0x4e00a0: Esc keeps the crown capture dialog.
};

// +8 and +0xc: the own step and drawing 0x6602b0 sets. The step (0x6605b0) calls it after the mouse over the box
// instead of the buttons and the timer; the drawing (0x660830), instead of everything. 0x660090 clears them on opening
// (0x6600d0); closing (0x660350) does not touch them. The original keeps the functions; MuRet, whose they are, and
// Guild calls them.
enum class PanelDialogHooks : std::uint8_t
{
    None,
    GuildPositions, // 0x64e600 and 0x64f1c0, the positions window (button +0x4b4 of the guild window)
};

// If the function returns non-zero the dialog closes (MuRet's return 1).
using PanelDialogAnswer =
    std::function<Core::Expected<bool, std::string>(PanelDialogOwner owner, std::uint32_t reason)>;

struct PanelDialogState
{
    PanelDialogOwner owner = PanelDialogOwner::None; // +4
    PanelDialogHooks hooks = PanelDialogHooks::None; // +8 and +0xc
    // +0x10: open, with the GetTickCount() of when it opened (what 0x6600d0 returns and the windows keep).
    std::uint32_t opened = 0;
    std::int32_t lineCount = 0; // +0x14
    std::array<std::string, PanelDialogMaxLines> lines{};
    std::uint32_t flags = 0;                    // +0xa18
    std::int32_t width = 0;                     // +0xa1c: 0xd5 on opening
    std::int32_t height = 0;                    // +0xa20
    std::int32_t placed = 0;                    // +0xa24: non-zero, the box goes to the top, at y = 10
    std::uint32_t timerStart = 0;               // +0xe34
    std::uint32_t timerEnd = 0;                 // +0xe38
    std::uint32_t timerMs = PanelDialogTimerMs; // +0xe3c
    // +0x1060, 0x660b41: written during the drawing with mode&8 and kept by 0x660090.
    std::uint32_t elapsedSeconds = 0;
    // 0x660250, kept by 0x660090: the EDIT settings the drawing applies with bit 0x10.
    std::int32_t inputWidth = 0;
    std::size_t inputMaxChars = 0;
    std::int32_t inputFilter = 0;
    // The shared EDIT's owner lends its cleanup; it runs after the answer, never during draw.
    std::function<void()> closeField{};
    // 0x65feb0: the four 0x32 × 0x12 buttons with their command (0, 1, 2 and 3). The drawing places them (0x660830).
    // Their ids come from the 0x5762eb0 counter (P-C97): MuRet gives them −1..−4, which do not clash with the old
    // windows' (P-C280).
    std::array<OldButton, PanelDialogButtonCount> buttons = DefaultButtons();

    [[nodiscard]] static std::array<OldButton, PanelDialogButtonCount> DefaultButtons();
};

// 0x660090: everything to 0 but the size, the timer and the buttons.
void ResetThePanelDialog(PanelDialogState& dialog);

// 0x6602b0(step, draw): sets +8 and +0xc, open or not.
inline void HookThePanelDialog(PanelDialogState& dialog, PanelDialogHooks hooks)
{
    dialog.hooks = hooks;
}

enum class PanelDialogOpening : std::uint8_t
{
    Opened,
    TooManyLines, // 0x6600d6: more than ten lines, OutputDebugStringA and 0
    Busy,         // 0x6600f9: one is already open: OutputDebugStringA, the notice (0x6d69a8) and 0
    // 0x660163..0x66017b: a line of 0x100 bytes or more, which the strcpy would spill over the next slot (Q-C1305). Not
    // in the original: MuRet does not open it and the caller bails out with the address.
    LineTooLong,
};

// 0x6d69a8: 0x6600f9's notice with another dialog open, in EUC-KR ("이미 팝업창이 떠 있습니다."), without its '\0'.
// 0x555190 sends it with the empty name (0x6e57a4) and type 1.
inline constexpr std::string_view PanelDialogBusyNotice =
    "\xc0\xcc\xb9\xcc \xc6\xcb\xbe\xf7\xc3\xa2\xc0\xcc \xb6\xb0 \xc0\xd6\xbd\xc0\xb4\xcf\xb4\xd9.";
inline constexpr std::int32_t PanelDialogBusyNoticeType = 1;

struct PanelDialogRequest
{
    std::span<const std::string> lines;
    std::uint32_t flags = 0;
    PanelDialogOwner owner = PanelDialogOwner::None;
    std::int32_t placed = 0;
};

// 0x6600d0(lines, count, step, flags, function, top): copies the lines with strcpy (up to their '\0') into the
// 0x100-byte slots, adds the height of each non-empty one (GetTextExtentPoint32A with the DC's font, cy + 2) and the
// rest, and opens it with the `now` timer (GetTickCount()). With a line of 0x100 bytes or more it does not touch the
// dialog (LineTooLong, Q-C1305).
[[nodiscard]] PanelDialogOpening OpenThePanelDialog(PanelDialogState& dialog, const PanelDialogRequest& request,
                                                    const Render::Text::ITextMeasure& measure,
                                                    Render::Text::FontKind dcFont, std::uint32_t now);

// 0x660340: the GetTickCount() it opened with, 0 when closed.
[[nodiscard]] inline std::uint32_t PanelDialogHandle(const PanelDialogState& dialog)
{
    return dialog.opened;
}

// 0x660350. With the text field (0x10) it empties the EDIT or the chat buffer, which no MuRet dialog opens: it bails
// out.
[[nodiscard]] Core::Expected<void, std::string> CloseThePanelDialog(PanelDialogState& dialog);

// 0x6603c0: open, it answers with the cancel reason (1 → 2, 2 → 4, 4 → 0x10, without buttons 1) without looking at
// what it returns, and closes it.
[[nodiscard]] Core::Expected<void, std::string> CancelThePanelDialog(PanelDialogState& dialog,
                                                                     const PanelDialogAnswer& answer);

// 0x660410(vk): open, Esc answers by the buttons (0x42, 0x44, 0x50 or 0x41) and Enter, with the text field, like the
// first button (2, 2, 8 or 1); if the function returns 0 it stays open, otherwise it closes. true if it took the key.
[[nodiscard]] Core::Expected<bool, std::string> ThePanelDialogKey(PanelDialogState& dialog, std::int32_t virtualKey,
                                                                  const PanelDialogAnswer& answer);

// 0x660580: with the timer (8), whether the end has passed.
[[nodiscard]] bool ThePanelDialogTimedOut(const PanelDialogState& dialog, std::uint32_t now);

// 0x45b200, the button's vtable + 0x20 (0x6b8d9c): a click that starts over it presses it (0x5762ec4) and the release
// over it returns 1 with sound 0x19. Before that it sends its command to the parent with 0x490b40(+0x44, 10, …), which
// looks up the id's window in the manager: the dialog's buttons have no parent and those of guild window 2 hang from
// one that is not in it (*inferred*): it reaches nobody. Disabled (6), nothing. With the mouse outside and the button
// held, it releases everything. It is milestone 8's copy, which returns the release (MuRet has no StepTheButtonMouse
// that returns it).
[[nodiscard]] bool ClickTheButton(OldButton& button, const OldWindowGlobals& globals, std::int32_t mouseX,
                                  std::int32_t mouseY);

// 0x6605b0: open, the mouse over the box (0x140 − width / 2, 0x82 − height / 2) belongs to the UI; then the buttons
// (0x45b200) or the timer, and the answer. With the own step (+8), nothing else: whoever set it calls it
// (Guild::LendThePanelDialogStepTo).
[[nodiscard]] Core::Expected<void, std::string> StepThePanelDialog(PanelDialogState& dialog,
                                                                   const OldWindowGlobals& globals, std::int32_t mouseX,
                                                                   std::int32_t mouseY, std::uint32_t now,
                                                                   const PanelDialogAnswer& answer);

// 0x660830, the part the step reads: the box (0x6576a0: x 0xd6 and y 0x82 − height / 2, never under 40, or 10 with
// +0xa24) and the buttons' y (y + 0x14, 15 per line, 17 with the field, 17 with the timer and 9), with their x.
// Returns the box's corner.
Ui::Point PlaceThePanelDialog(PanelDialogState& dialog);
} // namespace Client::Ui
