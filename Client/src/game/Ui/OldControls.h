#pragma once

#include <string>

#include "Ui/OldWindowBase.h"
#include "Ui/TextField.h"
#include "Audio/SoundId.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

// The old windows' controls that CL8 left unported (CL9 milestone 9; 06 § HUD y ventanas, "Las ventanas viejas"):
// the list template of 0x45adf0..0x4a7280 and the EDIT of 0x4674c0.
//
// The list is an MSVC6 STL template with a std::deque of elements: the chat log (0x45b620, elements of 0x11c, already
// ported in Chat/ChatLogWindow), the friend list (0x460970, 0x14), the window list (0x462010, 0x4c), the letter list
// (0x463190, 0x138) and a letter's body (0x464620, 0x3f0). The instances are identical byte for byte except for the
// element size (0x47b0f0, 0x47e010, 0x47f7b0 and 0x47c870 are the same mouse step; 0x461830 and 0x4625c0, the same
// rows change). MuRet keeps the elements in their owner and the list gets how many there are; what each instance does
// with a row (the click, the draw) belongs to its user.
//
// The CL9.4 ones (P-C216, *verified* against the ASM: the same instructions as the friends one, except for the
// offsets and the element size), by vtable slot. The letters' (0x6b92a0 and 0x6b9340): + 0x28 0x47f090 (= 0x47a9d0),
// + 0x34 0x47efd0 (= 0x47a910), + 0x3c 0x472340 (= 0x4720c0), + 0x40 0x472350 (= 0x4720d0), + 0x44 0x47f060
// (= 0x47a9a0), + 0x4c 0x47f2a0 (= 0x47abe0), + 0x50 0x47f350 (= 0x47ac90), + 0x5c 0x47f5e0 (= 0x47af20), + 0x60
// 0x47f640 (= 0x47af80), + 0x64 0x47f690 (= 0x47afd0), + 0x6c 0x47fea0 (= 0x47b7e0) and + 0x70 0x480080
// (= 0x47b9c0). A letter body's, of 0x3f0 (0x6b93d4 and 0x6b9474): 0x47c150, 0x47c090, 0x472200, 0x472210, 0x47c120,
// 0x47c2c0 (+ 0x48, = 0x47ab40), 0x47c360, 0x47c410, 0x472220 (+ 0x54, = 0x4720e0), 0x47c670, 0x47c6a0, 0x47c700,
// 0x47c750, 0x47cf60 and 0x47d140, in the same slots. What is each one's own (*inferred* from the vtable and what they
// call, without comparing instruction by instruction): the letters', + 0x2c 0x47eac0 and + 0x94..+ 0x98 0x4636b0 and
// 0x463960 (copying the letters' deque), + 0x40 0x463d50 (the rows); the body's, + 0x2c 0x47bba0 (with 0x47bf00, its
// iterator's +=) and + 0x90..+ 0x98 0x465270, 0x464ae0 and 0x465450 (adding a row and splitting it): they are in
// Messenger/LetterWindows, over the window's two std::deques (OldLetterPart). The draws (+ 0x14 0x464d90, + 0x7c..+
// 0x80 0x464e90 and 0x465770, + 0x9c 0x464190 and 0x465710) are in Messenger/LetterWindowsDraw and
// Messenger/FriendWindowsDraw (P-C213); nothing calls 0x47bf90.
namespace Client::Ui
{
// The list (0x45e2e0 and its derived ones), over the base window: the rectangle hangs from its bottom edge (+0x78 at
// 2, 0x47bb80).
struct OldList
{
    OldWindowBase base;
    // +0xbc: takes the arrow keys (0x47b0f0) and the 0xe picks the last one (0x47a9d0). 1 in the CL9.4 ones.
    std::int32_t keyboard = 1;
    // +0xc0: how many steps an arrow key has been held.
    std::int32_t keyRepeat = 0;
    // +0xc4: counting from 1; 0, none.
    std::int32_t selection = 0;
    // +0xc8 and +0xf8: with 1, the count is +0xf8 and not the deque's size (0x4720e0). The CL9.4 lists leave them at 0
    // (0x460970, 0x462010); the chat rows and letter body ones set +0xc8 to 1 (0x45f240, 0x464620), and +0xf8 is the
    // size of their second deque, the split rows' (Messenger/LetterWindows).
    std::int32_t fixedCount = 0;
    std::int32_t fixedCountValue = 0;
    // +0xfc.
    std::int32_t maxEntries = 0x96;
    // +0x100: how many are skipped before the first shown.
    std::int32_t scroll = 0;
    // +0x104
    std::int32_t rows = 6;
    // +0x108..+0x118: the bar (0x47b9c0).
    float barTop = 0.0f;
    float barBottom = 0.0f;
    float thumbY = 0.0f;
    float thumbWidth = 13.0f;
    float thumbHeight = 0.0f;
    // +0x11c: where the mouse grabbed the bar, from its top edge.
    float dragOffset = 0.0f;
    // +0x120 and +0x124: how many steps a bar arrow or the track has been held. At 16, it repeats each step.
    std::int32_t arrowRepeat = 0;
    std::int32_t trackRepeat = 0;
    // +0x128: with 1, the 9 keeps what shows at the bottom when the rows change. 0 in the CL9.4 ones.
    std::int32_t keepBottom = 0;
    // +0x12c: 1, the window lists' bar (edges of 0xc); 0, the chat log's.
    std::int32_t barMode = 1;
    // +0x130: the headers' draw (0x461890); the friends tab sets it to 1 (0x49c8b0).
    std::int32_t headers = 0;
    // The friend list's +0x134 and +0x138: the width of "ZZZZZZZZZZ" and that of text 1022, + 8 (0x460970).
    std::int32_t nameWidth = 0;
    std::int32_t placeWidth = 0;
    // The letter list's +0x13c (0x463190): the width of text 1030, + 8. In it, +0x130 is the mark column's width (0x19)
    // and +0x134 and +0x138, those of texts 1028 and 1029 (CL9 milestone 10).
    std::int32_t subjectWidth = 0;
    // The friend list's +0x144: 0x49ddd0 sets it to 1 before copying the model. Nobody reads it in milestone 9
    // (*static*: no other write or read of +0x144 in the vtable 0x6b90cc).
    std::int32_t refreshed = 0;
};

// 0x4720e0, vtable + 0x54: the ones it has (+0xa8), or +0xf8 with +0xc8 at 1. 0x4722c0, the window list's (0x6b916c +
// 0x54 and 0x6b920c + 0x54), is the same. 0x4722b0 (+0x104 = rows), the + 0x40 of the window list's intermediate
// vtable (0x6b920c, which 0x462010 sets at 0x4620f1 and replaces with 0x6b916c at 0x462170 without calling it), is
// dead: the built list has 0x4625c0's (*static*).
[[nodiscard]] std::int32_t ListCount(const OldList& list, std::size_t size);

// 0x47a910, vtable + 0x34: with fewer than the rows (unsigned), nothing; otherwise, scrolls `lines` toward the start
// (negative, toward the end) and clamps it to [0, count − rows].
void ScrollTheList(OldList& list, std::size_t size, std::int32_t lines);

// 0x461830 and 0x4625c0, vtable + 0x40: fewer rows than before and than the count sets them and scrolls one;
// otherwise, more than the count resets the scroll to 0, and sets them.
void SetTheListRows(OldList& list, std::size_t size, std::int32_t rows);

// 0x47a9a0, vtable + 0x44: 3 rows per step, at least 3.
void GrowTheListRows(OldList& list, std::int32_t steps);

// 0x47aef0, vtable + 0x58: up to the count.
void SelectInTheList(OldList& list, std::size_t size, std::int32_t selection);

// 0x47af20 and 0x47af80, vtable + 0x5c and + 0x60: empty, the selection at 0; with one selected, `steps` more (up to
// the count) or fewer (down to 1).
void SelectTheNext(OldList& list, std::size_t size, std::int32_t steps);
void SelectThePrevious(OldList& list, std::size_t size, std::int32_t steps);

// 0x47afd0, vtable + 0x64: the selected one's place in the deque, or none (the end) with no selection or one past the
// end. Without elements, it also resets the selection to 0.
[[nodiscard]] std::optional<std::size_t> SelectedInTheList(OldList& list, std::size_t size);

// 0x47bb00, vtable + 0x74: the scroll's one (with +0xc8 at 0, from the start).
[[nodiscard]] std::size_t FirstShownInTheList(const OldList& list);

// 0x461c70 and 0x462a00, vtable + 0x9c: the y of row `row` (from 0, the first shown), counted from the top edge: the
// first ends at the bottom and the rest go up by 13.
[[nodiscard]] std::int32_t ListRowY(const OldList& list, std::size_t size, std::int32_t row);

// 0x47a9d0, vtable + 0x28: the 9 (with a parent) recounts the rows with the height before measuring (ftol(h · 1/13 −
// 0.5); the chat rows and letter ones run their vtable + 0x90, which measures, before), the 0xe goes to the end (and
// picks the last with +0xbc), and the 0xf and the 0x10 move the selection up or down (with +0xbc) and keep it in
// view. Always returns 0: 0x45abc0 runs after.
void HandleTheListMessage(OldList& list, std::size_t size);

// 0x47b9c0, vtable + 0x70: the track's edges and the size and place of the bar's thumb, like the chat log's 0x47a230
// with the list's count.
void PlaceTheListBar(OldList& list, std::size_t size);

// The globals a list's mouse step reads and writes.
struct OldListGlobals
{
    // 0x7d366d4: the left click's edge.
    bool& leftClick;
    // 0x7d366d8: in steps of 120.
    std::int32_t& wheel;
    // 0x5762eb4: the window that grabbed the mouse.
    std::int32_t& capture;
    // 0x5762ec0: the list that takes the arrow keys.
    std::int32_t& focusedList;
    // 0x775857c.
    std::uint8_t& mouseOnUi;
    // 0x41ed10: the last sound.
    std::optional<Audio::SoundId>& sound;
    // The manager's +0x40 (0x5763514): 1 after the draw. The arrow and track counts of a list with a parent only go up
    // with it (0x47b2b0, 0x47b427).
    bool drawn = false;
    // 0x5762ebc: the modal box. With one, the step does not look at the rows (0x47b71b).
    std::int32_t modal = 0;
    std::function<void(Audio::SoundId)> soundRequest{};

    void RequestSound(Audio::SoundId id) const
    {
        if (soundRequest)
            soundRequest(id);
        else
            sound = id;
    }
};

// What the step reads from the game.
struct OldListView
{
    // 0x7d3682c and 0x7d36828, in 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // GetAsyncKeyState(vk) with the high byte at 0x80: the key down, without edge.
    std::function<bool(std::uint8_t)> keyDown;
};

// Each instance's vtable + 0x88 (0x461f00, 0x462bd0): row `row` with the deque's element `at`. Returns what the step
// does with the count: negative moves it further, 0 repeats the row and positive goes on.
using OldListRowHover = std::function<std::int32_t(std::int32_t row, std::size_t at)>;

// 0x47b0f0 (and its equals), vtable + 0x20: the arrow keys (with +0xbc and the list with the focus), the wheel, the
// bar's arrows and track, grabbing the bar or the border, the drag, and with the modal box closed the mouse over each
// row shown. Returns 1 if the mouse was over the list (0x47bb80).
[[nodiscard]] bool StepTheListMouse(OldList& list, std::size_t size, const OldListGlobals& globals,
                                    const OldListView& view, const OldListRowHover& hover);

// The old windows' EDIT, 0x4674c0 (vtable 0x6b9550): a Windows EDIT child of the game window, with its subclass
// 0x467b10. The model keeps the caret and the selection in bytes and the preedit apart. The input conversion, the
// selection/composition bitmap, the scroll and the full IME follow P-C181 (11-edit-win32 and 12-codificacion).
// 0x468d50 handles focus/bar; character editing belongs to the native EDIT.
struct OldEdit
{
    OldWindowBase base;
    // +0x4c: 1 digits only, 2 letters and digits in uppercase (0x467b10). The add-a-friend box, 8, which does not
    // filter and gives the draw the black background (0x468477).
    std::int32_t filter = 0;
    TextField text{10, false, TextField::Charset::Windows1252Xp};
    // +0x80: the draws (0x468320 adds one at the end of each); the caret shows with the remainder mod 24 below 12.
    std::int32_t draws = 0;
    // The SDL_TEXTEDITING preedit does not change `text` until SDL_TEXTINPUT confirms its bytes.
    std::string compositionText;
    std::int32_t compositionStart = 0;
    std::int32_t compositionLength = 0;
    // +0x9c and +0xa0 (0x4674c0; vtable + 0x40 and + 0x44): the text and background colors of the EDIT's bitmap
    // (0x468320), as CL8 reads them (Hud/GameHud, 0x4691e0). The middle pixels (+0xa4, 0xff969696) are set by MuRet's
    // text draw.
    std::uint32_t textColor = 0xffffffffu;
    std::uint32_t backgroundColor = 0xff000000u;
    // +0xac: the height of "Q" in pixels (0x6c7af4), which 0x468320 measures the first time (0 until then).
    float caretHeight = 0.0f;
    // +0xb4 (vtable + 0x58, 0x4676c0, and + 0x5c, 0x4676d0; 0 in the constructor, 0x46755f): 1 with the chat with a
    // friend closed. Only 0x495630 writes it, and 0x4944c0's 10 and 2 (does not invite) and Enter in 0x467b10 (goes to
    // the Windows EDIT, without the 7 to the parent) read it. It is neither the focus nor the state (+0x48).
    std::int32_t closed = 0;
    // +0x98 (vtable + 0x54, 0x4676b0): the id of the EDIT Tab passes the focus to (0x467d07), or 0. Only the
    // letter-writing window links them (0x49817a..0x498192: to, subject, body and to again).
    std::int32_t next = 0;
    // CL9 milestone 10, the multiline EDIT (Ui/OldEditLines.h). +0xc4: 1 with ES_MULTILINE | ES_AUTOVSCROLL |
    // WS_VSCROLL (0x4675e0, vtable + 0x38, before creating it: only the letter body, 0x4980ef).
    std::int32_t multiline = 0;
    // +0xc8 and +0xcc: 0x468d50's arrow and track counts, like the list's.
    std::int32_t arrowRepeat = 0;
    std::int32_t trackRepeat = 0;
    // +0xd0: the rows that fit, ftol(h · ry / cy of "1") on creation (0x467fe0, with +0xc4 at 1; otherwise 0).
    std::int32_t visibleLines = 0;
    // +0xd4..+0xe4: 0x468970's bar (the width, 13; the track from y + 9 to y + h − 9; the thumb's height and y) and
    // +0xe8, the mouse minus the thumb's y when grabbed (0x468d50).
    float barWidth = 0.0f;
    float barTop = 0.0f;
    float barBottom = 0.0f;
    float thumbHeight = 0.0f;
    float thumbY = 0.0f;
    float dragOffset = 0.0f;
    // MuRet: what the Windows EDIT keeps and the original asks for with messages. The first row in view (GetScrollPos
    // of SB_VERT, EM_SCROLL and EM_LINESCROLL), the text area's width in pixels (0x467fe0's window's minus the Windows
    // bar and the margins, *inferred*) and whether the last change of the text or the caret asks to show the caret
    // (ES_AUTOVSCROLL).
    std::int32_t firstLine = 0;
    std::int32_t textWidth = 0;
    bool followCaret = false;
};

// The EDIT's +0x4c: its subclass's filters.
inline constexpr std::int32_t DigitsOnlyEdit = 1;
inline constexpr std::int32_t UppercaseLettersAndDigitsEdit = 2;

// What subclass 0x467b10 lets through from a WM_CHAR to the EDIT (0x467c2a..0x467cd0) and how: Esc and 0x19..0x1c
// never; with +0x4c & 1, digits only (and erase, 3 and 0x18; paste, 0x16, is checked by 0x467a90 and MuRet lacks
// it); otherwise, with +0x4c & 2, digits and uppercase, and lowercase turned uppercase; without a filter, everything.
// A byte of 0x80 or more is negative as a char and passes neither filter.
[[nodiscard]] std::optional<char> FilteredByTheEdit(std::int32_t filter, char typed);

// WM_CHAR to the EDIT with the focus: +0x80 at 0 (0x467b9d) and each letter of `typed` through the +0x4c filter, and
// what passes into the text, with its limit. In the multiline one, the caret stays in view (followCaret,
// Ui/OldEditLines.h).
void TypeInTheEdit(OldEdit& edit, std::string_view typed);

// The EDIT's vtable + 0x3c (0x467f30): EM_LIMITTEXT with SendMessageA, which goes through subclass 0x467b10; with an
// arrow down (0x467b35..0x467b69) it leaves +0x80 at 0. The draws that call it each frame (the 0x7d at 0x630af0, the
// 0x73 at 0x6308c6, the guild's window 2 at 0x655130 and the serial number at 0x5f696d) do so before 0x468320 looks
// at the caret: with the arrow, the caret shows from the first frame.
void LimitTheEdit(OldEdit& edit, std::size_t limit, bool arrowHeld);
} // namespace Client::Ui
