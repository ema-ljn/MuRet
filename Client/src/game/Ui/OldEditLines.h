#pragma once

#include "Core/PixelSize.h"
#include "Render/Text/FontSet.h"
#include "Ui/OldControls.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Client::Render::Text
{
class ITextMeasure;
}

// The old windows' multiline EDIT (CL9 milestone 10; 06 § HUD y ventanas, "Las cartas y el chat con un amigo"): the
// body of the letter being written (+0x958 of 0x497f50) is the only EDIT with +0xc4 at 1. The original leaves the
// rows, the scroll and the caret to the Windows EDIT window (ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, without
// ES_AUTOHSCROLL), and reads them with EM_GETLINECOUNT, GetScrollPos and the bitmap it paints (0x468320); MuRet has no
// such window and builds them here. What belongs to Windows is *inferred* and to be checked against the VM (08,
// P-C217): the word wrapping, the text area's width (the window's minus its bar's 16) and the scroll limits.
namespace Client::Ui
{
// WM_SETFONT with the normal one ([0x5763554], 0x49812c).
inline constexpr Render::Text::FontKind EditFont = Render::Text::FontKind::Normal;

// The margin the SBCS EDIT's bitmap leaves before the text, observed on XP. With +0xc4 at 1, the area where the rows
// wrap loses it on each side (*inferred*: the right one equal to the left, P-C217).
inline constexpr std::int32_t EditTextMargin = 3;

// 0x467fe0 with +0xc4 at 1 (0x4675e0 with 1 before): +0xd0, ftol(h · ry / cy), with cy the GetTextExtentPointA height
// of "1" (0x6c7af0) in the DC; and the text area's width: the window's, ftol((w + ftol(−5 · rx)) · rx), minus its
// Windows bar.
void MakeTheEditMultiline(OldEdit& edit, const Render::Text::ITextMeasure& measure, Render::Text::FontKind dcFont,
                          Core::PixelSize screen);

// The EDIT's rows: just one without +0xc4; with +0xc4 at 1, cut at each "\r\n" and, where they do not fit in the text
// area's width, at the last space that fits (extra spaces stay in the row) or, without a space, at the last letter
// that fits. An empty EDIT or one ending in "\r\n" has an empty row at the end. The views are into the EDIT's text
// and are valid until it changes.
[[nodiscard]] std::vector<std::string_view> EditLines(const OldEdit& edit, const Render::Text::ITextMeasure& measure);

// EM_GETLINECOUNT
[[nodiscard]] std::int32_t EditLineCount(const OldEdit& edit, const Render::Text::ITextMeasure& measure);

// EM_LINESCROLL(0, lines): the first row in view, shifted, between 0 and rows − the ones that fit.
void ScrollTheEditLines(OldEdit& edit, std::int32_t lineCount, std::int32_t lines);

// EM_SCROLL: SB_LINEUP (0) and SB_LINEDOWN (1) scroll one; SB_PAGEUP (2) and SB_PAGEDOWN (3), as many as fit.
inline constexpr std::int32_t EditLineUp = 0;
inline constexpr std::int32_t EditLineDown = 1;
inline constexpr std::int32_t EditPageUp = 2;
inline constexpr std::int32_t EditPageDown = 3;
void ScrollTheEdit(OldEdit& edit, std::int32_t lineCount, std::int32_t code);

// The caret's row (TextField::Cursor): the last one that starts at it or before. With the caret at a row break, the
// lower one (*inferred*, P-C217). `lines` are EditLines' for the same text.
[[nodiscard]] std::int32_t EditCaretLine(const OldEdit& edit, std::span<const std::string_view> lines);

// ES_AUTOVSCROLL: after a text change, the caret's row in view (without `caretLine`, the last one).
void FollowTheEditCaret(OldEdit& edit, std::int32_t lineCount, std::int32_t caretLine = -1);

// WM_CHAR with backspace or Enter (0x467b10): +0x80 at 0 (0x467b9d) and, to the Windows EDIT. Enter only arrives with
// +0xc4 at 1 (0x467c8f), and is a "\r\n" if it fits whole. A character is Ui::TypeInTheEdit (Ui/OldControls.h). With
// +0xc4 at 1, backspace erases the whole "\r\n" before the caret and Delete the one after (*inferred*, P-C217).
void BackspaceInTheEdit(OldEdit& edit);
void DeleteInTheEdit(OldEdit& edit);
void BreakTheEditLine(OldEdit& edit);

// The keys the Windows EDIT uses to move the caret with +0xc4 at 1 (*inferred*, P-C217): the side arrows skip the
// whole "\r\n"; up and down go to the adjacent row, to the letter closest to the same x; Home and End, to the start
// and the end of the row; PgUp and PgDn, as many rows as fit, and scroll the view the same. With Shift, they select.
// Then the caret's row stays in view (ES_AUTOVSCROLL).
enum class EditCaretKey : std::uint8_t
{
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
};
void MoveTheEditCaret(OldEdit& edit, const Render::Text::ITextMeasure& measure, EditCaretKey key, bool shift);

// WM_PASTE in the Windows EDIT with +0xc4 at 1 (*inferred*, P-C217): the clipboard text in CP1252 up to the first
// NUL, with each line end ("\r\n", "\n" or "\r") as "\r\n", up to the limit.
void PasteInTheEdit(OldEdit& edit, std::string_view utf8);
} // namespace Client::Ui
