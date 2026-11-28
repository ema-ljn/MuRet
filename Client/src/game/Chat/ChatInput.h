#pragma once

#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/TextField.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

// The game's chat input (CL8 milestone 3; 06 § El chat). With 0x6c7b8c at 1 (.data), the chat writes in two Windows
// EDITs of the object [0x57634f8] (0x330 bytes, vtable 0x6ba0f0, built by 0x4691e0): the message one at +4 and the
// whisper one at +0xf0 (the class 0x4674c0, vtable 0x6b9550), with the history of what was said at +0x2e4. The WndProc
// (0x4af4e0) and the step (0x59ff10) share the field's globals with it: the buffers 0x7798bf8 (the message) and
// 0x7798cf8 (the whisper name) with their lengths (0x7758590 and 0x7758594), the chosen field (0x77f3b70), the
// whisper names (0x77f31cc), the chat brakes (0x5767940 and 0x57676e4), the F3 mode (0x6c8dd0) and the macros
// (0x77f1db0). /whisper off and on (0x77f3b98), the last recipient (0x57678c4) and those who whispered (0x77997f8)
// are shared with the incoming chat and live in ChatRuntime. *Static*.
//
// The EDITs share OldEdit and XP's Western Windows-1252 profile. The conversion, the visible preedit and EDIT's exact
// horizontal scrolling still need an XP comparison (P-C181). The text does not go to the log.
namespace Client::Chat
{
// 0x4691e0: EM_LIMITTEXT of the message (0x32) and of the whisper (10).
inline constexpr std::size_t ChatMessageLimit = 0x32;
inline constexpr std::size_t ChatWhisperLimit = 10;
// 0x7798bf8 and 0x7798cf8: two of the ten 0x100 buffers that 0x553570 empties.
inline constexpr std::size_t ChatBufferSize = 0x100;
// 0x469a80(0): the history keeps the ten newest.
inline constexpr std::size_t ChatHistoryLimit = 10;
// 0x77f31cc..0x77f36cc: five names of 0x100; 0x6c8ea8 (.data 2) is the current one and goes from 0 to 4.
inline constexpr std::size_t WhisperNameSlots = 5;
inline constexpr std::int32_t FirstWhisperNameSlot = 2;
inline constexpr std::int32_t LastWhisperNameSlot = 4;
// 0x5a01bc and 0x5a0244: strncpy(…, 0xb), the name and its '\0'.
inline constexpr std::size_t WhisperNameCopy = 0xb;
// 0x77f1db0..0x77f27b0: ten macros of 0x100, Alt + 1's first and Alt + 0's last.
inline constexpr std::size_t MacroCount = 10;
// 0x5a1ea1: what a macro that hit the brake leaves in 0x77f3b74 (HudState::macroTime).
inline constexpr std::int32_t MacroLockSteps = 0x64;
// 0x77f3b70: the chosen field. Tab moves it between 0 and 0x6c8e6c − 1; the odd ones are the whisper's.
inline constexpr std::int32_t MessageField = 0;
inline constexpr std::int32_t WhisperField = 1;
// 0x4bb01b/0x4bb02c → 0x4674c0 → 0x45a210 → 0x459ef0: WinMain's first two controls.
// The ids follow the same inferred order as DialogFieldId and Chat::ChatLogId.
inline constexpr std::int32_t ChatMessageEditId = 1;
inline constexpr std::int32_t ChatWhisperEditId = 2;
// 0x6c8e6c: how many fields Tab walks, 2 from the first game step and on opening.
inline constexpr std::int32_t ChatFieldCount = 2;
// [0x5763504] and [0x5763508]: the limits of the dialogs' two EDITs before anyone sets another: WinMain builds them
// with +0x2c([0x5763544], 0x32, 0) (0x4bbaaa) and +0x2c([0x5763544], 9, 1) (0x4bbad7), and 0x467fe0 passes the second
// argument to the vtable + 0x3c (EM_LIMITTEXT, 0x46811e) and with the third at 1 sets ES_PASSWORD and +0xb0
// (0x468005). The guild's window 2 sets the first to 8 in each draw of page 1 (0x655202) and the dialog 0x7d, the
// second to 7 (0x630b2b). *Static*.
inline constexpr std::size_t DialogFieldLimit = 0x32;
inline constexpr std::size_t NumberFieldLimit = 9;
// +0x40 of both (the counter 0x5762eb0, 0x459ef0): WinMain first builds the six EDITs of [0x57634f8], [0x57634fc] and
// [0x5763500], which take 1 to 6, and then the chat log (Chat::ChatLogId, 9). *Inferred*, like that one.
inline constexpr std::int32_t DialogFieldId = 7;
inline constexpr std::int32_t NumberFieldId = 8;
// 0x469790(10000): back to the newest.
inline constexpr std::int32_t NewestInTheHistory = 10000;

// The dialogs' EDIT, hidden as WinMain leaves it (+4(3) at 0x4bbabc); the rest, as 0x4674c0 leaves it.
[[nodiscard]] inline Ui::OldEdit DialogFieldEdit()
{
    Ui::OldEdit edit;
    edit.base.id = DialogFieldId;
    edit.base.state = Ui::HiddenWindow;
    edit.text = Ui::TextField{DialogFieldLimit, false, Ui::TextField::Charset::Windows1252Xp};
    return edit;
}

// The dialogs' second EDIT, with ES_PASSWORD (TextField::Masked, +0xb0) and hidden (+4(3) at 0x4bbae9); the rest, as
// 0x4674c0 leaves it. WinMain gives both the normal font (+0x34 with [0x5763554], 0x4bbb42 and 0x4bbb59).
[[nodiscard]] inline Ui::OldEdit NumberFieldEdit()
{
    Ui::OldEdit edit;
    edit.base.id = NumberFieldId;
    edit.base.state = Ui::HiddenWindow;
    edit.text = Ui::TextField{NumberFieldLimit, true, Ui::TextField::Charset::Windows1252Xp};
    return edit;
}

// Which window has the keyboard focus (GetFocus()): the game's ([0x5763544]) or one of the two EDITs.
enum class ChatFocus : std::uint8_t
{
    Game,
    Message,
    Whisper,
    // An old window's EDIT (the add-a-friend box, 0x4682a0): OldWindowManager::focusedEdit.
    OldWindow,
    // The EDIT of [0x5763504] (ChatInputState::dialogEdit), which takes the focus with the vtable + 0x30 (0x4682a0).
    Dialog,
    // The second one, [0x5763508] (ChatInputState::numberField, the number of the game's dialog 0x7d, CL9 milestone 8).
    NumberField,
};

using ChatBuffer = std::array<char, ChatBufferSize>;

// The history: the deque<char*> of +0x2e4 (the newest first), the iterator of +0x310 (0, the first; all of `said`,
// the end), and what was being typed before going up (+0x1dc and the buffer of +0x1e0).
struct ChatHistory
{
    std::deque<std::string> said;
    std::size_t at = 0;
    bool saved = false;
    std::string typing;
};

// 0x4691e0: two instances of 0x4674c0/0x467fe0, without a filter or a parent, in their fixed rectangles.
[[nodiscard]] inline Ui::OldEdit ChatFieldEdit(bool whisper)
{
    Ui::OldEdit edit;
    edit.base.id = whisper ? ChatWhisperEditId : ChatMessageEditId;
    edit.text =
        Ui::TextField{whisper ? ChatWhisperLimit : ChatMessageLimit, false, Ui::TextField::Charset::Windows1252Xp};
    edit.base.x = whisper ? 376 : 193;
    edit.base.y = 422;
    edit.base.width = whisper ? 50 : 180;
    edit.base.height = 14;
    edit.base.state = Ui::HiddenWindow;
    edit.textColor = whisper ? 0xffc8c8c8u : 0xffd2e6ffu;
    edit.backgroundColor = whisper ? 0x00191919u : 0xff000000u;
    return edit;
}

struct ChatInputState
{
    Ui::OldEdit message = ChatFieldEdit(false);
    Ui::OldEdit whisper = ChatFieldEdit(true);
    // +0x48 of the two EDITs: with 3, hidden (ShowWindow(SW_HIDE), 0x468260). 0x469450 sets them together.
    bool shown = false;
    ChatFocus focus = ChatFocus::Game;
    std::int32_t field = MessageField;
    std::int32_t fieldCount = ChatFieldCount;
    ChatBuffer messageBuffer{};
    ChatBuffer whisperBuffer{};
    std::int32_t messageLength = 0;
    std::int32_t whisperLength = 0;
    ChatHistory history;
    std::array<ChatBuffer, WhisperNameSlots> whisperNames{};
    std::int32_t whisperNameAt = FirstWhisperNameSlot;
    // 0x77f3b69: another input's text field (the one of the panels' dialog, milestone 6). With 1, Enter neither opens
    // nor closes and 0x5aa9a0 draws the box. MuRet does not set it.
    bool otherInput = false;
    // [0x5763504]: the dialogs' EDIT (the class 0x4674c0 that WinMain builds at 0x4bb31b..0x4bb344), which in the game
    // is used by the dialog 0x73 (milestone 6), the serial number input (0x5f6550, CL9 milestone 11), the personal
    // shop's title and price (0x91, milestone 7) and the guild's window 2 for the name (0x655130 shows it, places it
    // and draws it with 0x468320; 0x654f80 reads it and hides it; CL9 milestone 8). Its text is read by Enter with the
    // chat field closed (0x4b540a). Windows-1252 like the chat's; what is typed goes in with the focus on
    // ChatFocus::Dialog. What is seen of it (+0x4c, the colors, the place, the draws and the state of +0x48, in view
    // with other than 3) is the old windows' one (OldEdit).
    Ui::OldEdit dialogEdit = DialogFieldEdit();
    // [0x5763508]: the dialogs' second EDIT (the class 0x4674c0 that WinMain builds at 0x4bb395), with ES_PASSWORD and
    // the filter its draw gives it. In the game the dialog 0x7d uses it (0x630af0 shows it, places it, gives it the
    // focus and draws it; the yes of 0x62a878 reads it and empties it, and nobody hides it: Q-C1295). What is typed
    // goes in with the focus on ChatFocus::NumberField; in view, with its +0x48 (`numberField.base.state`).
    Ui::OldEdit numberField = NumberFieldEdit();
    // 0x77f3b68: the dialog field turned on (the Guild Master's 01, 0x4e3450, and the Create button, 0x655d70; 0x6570b0
    // turns it off). With it, Enter is not the chat's (0x4b5115) and there are no keyboard shortcuts (0x6578e0).
    bool dialogFieldOn = false;
    // 0x6c8dd0 (.data 1): F3 and the box's button. With 0, the whisper name does not count: all goes to the chat.
    bool whisperMode = true;
    // 0x5767940 and 0x57676e4: the chat brake. Each message sets it to 0x46 and 0x637d2c lowers it by one per step;
    // with more than 0x32 nothing goes out, and with more than 0 the same as the previous one does not go out.
    std::int32_t brake = 0;
    std::string lastSaid;
    // 0x77f1db0: the macros, as Data\Macro.txt left them (0x61c1c0) and /1../0 changes them (0x56f0b0).
    std::array<std::string, MacroCount> macros{};
};

// The focused EDIT of the chat or of its dialogs; the old manager's one is resolved in FriendWindows.
[[nodiscard]] Ui::OldEdit* ChatEditWithFocus(ChatInputState& input);

// A buffer's text up to its '\0'.
[[nodiscard]] std::string_view CStringOf(const ChatBuffer& buffer);
// strcpy: `text` (up to its '\0') and the '\0', without touching what is left behind. What does not fit is cut.
void CopyIntoTheBuffer(ChatBuffer& buffer, std::string_view text);

// 0x553570(second): the chosen field at 0, the buffers 0 and, with `second`, 1 zeroed and their lengths at 0, and with
// 0x6c7b8c at 1 (the .data) SetText(0) in the dialogs' two EDITs (0x5535bd..0x5535db: `dialogEdit` and
// `numberField`). MuRet does not have the other eight buffers, their lengths, or the marks of 0x77f31c0 and 0x6c8e74.
void ResetTheTextBuffers(ChatInputState& input, bool second);

// 0x469420: SetText(0) in the two EDITs.
void ClearTheChatFields(ChatInputState& input);

// 0x469370(field): with the EDITs in view, the focus to the whisper one (odd) or to the message one, with everything
// selected (EM_SETSEL(0, −1)).
void FocusTheChatField(ChatInputState& input, std::int32_t field, std::int32_t* focusedList = nullptr);

// 0x4682a0(1) on the EDIT of the field `field` (the click on it, 0x468d50): the focus and the cursor at the end, with
// nothing selected (EM_SETSEL(−2, −1)). It also leaves the EDIT's id in 0x5762ec0 (`focusedList`).
void ClickTheChatField(ChatInputState& input, std::int32_t field, std::int32_t* focusedList = nullptr);

// 0x469450(state): 3 hides them; another shows them, and 0 also gives the focus to the message one (0x4682a0 with 0:
// without changing the selection). On hiding them, if one of the two had the focus, it goes back to the game window
// (*inferred*: MuRet has no hidden EDIT that takes keys); another EDIT's (the dialog's, the number's, an old window's)
// stays, because ShowWindow(SW_HIDE) only touches the chat's (0x469450..0x469485).
void ShowTheChatFields(ChatInputState& input, std::int32_t state, std::int32_t* focusedList = nullptr);

// 0x469320: the chosen field at 0, the focus to the message one (0x469370), both empty (0x469420) and the history
// empty (0x469a80(1)).
void ResetTheChatInput(ChatInputState& input, std::int32_t* focusedList = nullptr);

// 0x4693e0(0x7798bf8, 0x7798cf8): GetText of the two EDITs into their buffers, up to 0xff bytes.
void ReadTheChatFields(ChatInputState& input);

// 0x4694c0(a, text a, b, text b): SetText in the message one and in the whisper one, each if given. 0x467ee0 does not
// set a text of 0x100 bytes or more.
void SetTheChatFields(ChatInputState& input, std::optional<std::string_view> message,
                      std::optional<std::string_view> whisper);

// 0x469540 (the vtable + 8): what was said at the start of the history if not empty, the iterator at the start,
// without the saved text, and 0x469a80(0).
void RememberTheMessage(ChatInputState& input, std::string_view said);

// 0x469a80(all): with `all` it empties it; if not, it keeps the ChatHistoryLimit newest.
void TrimTheHistory(ChatHistory& history, bool all);

// 0x469790(n) (the vtable + 0xc): with n < 0 it goes up to the oldest (saving what was being typed the first time),
// with n > 0 down to the newest (and from the first, to the saved text), and with 10000 back to the first. It puts the
// text in the message EDIT with the cursor at the end. With the history empty it does nothing; the original also asks
// for the IME context of the EDIT's window and its mode without the 0x40, which MuRet takes as good (08, P-C85). It
// returns whether the 0x19 sounded.
bool RecallFromTheHistory(ChatInputState& input, std::int32_t steps);

// 0x4b4f12..0x4b4fb2: Tab in the game window (the WM_CHAR 9 the subclass 0x467b10 passes it). With the chat field
// open and two or more fields, it moves to the next one (0x469370 in the chat; in the login EDIT, [0x57634fc], which in
// the game is hidden, nothing); with one, if its length is 0, it flips 0x77f3b69. It returns whether the 0x19 sounded.
bool PressTabInTheChat(ChatInputState& input, bool chatOpen, std::int32_t* focusedList = nullptr);

// 0x468260(state) on the dialogs' EDIT (the vtable + 4): 3 hides it, and the keyboard focus goes back to the game
// window if it had it (*inferred*, like ShowTheChatFields); another shows it.
void ShowTheDialogField(ChatInputState& input, std::int32_t state);

// 0x4682a0(0) on the dialogs' EDIT (the vtable + 0x30): the focus, without changing the selection. Hidden, nothing
// (SetFocus on a hidden window does not take it, *inferred*).
void FocusTheDialogField(ChatInputState& input);

// 0x467ec0(buffer, 0xff) on the dialogs' EDIT (the vtable + 0x50): GetText, up to 0xff bytes and the '\0'.
void ReadTheDialogField(const ChatInputState& input, ChatBuffer& buffer);

// The same on the dialogs' second EDIT ([0x5763508]): 0x468260 (+4), 0x4682a0(0) (+0x30) and 0x467ec0(buffer, 0xff)
// (+0x50), and 0x467ee0(0) (+0x4c, SetText with nothing).
void ShowTheNumberField(ChatInputState& input, std::int32_t state);
void FocusTheNumberField(ChatInputState& input);
void ReadTheNumberField(const ChatInputState& input, ChatBuffer& buffer);
void ClearTheNumberField(ChatInputState& input);

// What is typed into the focused EDIT, as the WM_CHAR messages Windows gives it: the bytes go at the end (or replace
// the selection) and BackspaceChar erases. Without a focused EDIT, the game window drops them (0x4b88ee). In the
// dialogs' two EDITs it first goes through its subclass's filter (Ui::TypeInTheEdit, 0x467bd1..0x467c64,
// OldEdit::filter): with bit 0, only the digits; if not, with bit 1, the digits and the letters, lowercase to
// uppercase. The rest it swallows (returns 0 without calling the EDIT's WndProc). The control ones filter 1 lets
// through (3, 0x16 and 0x18: copy, paste with 0x467a90 and cut) do not reach MuRet as text.
void TypeInTheChat(ChatInputState& input, std::string_view typed);
void BackspaceInTheChat(ChatInputState& input);

// 0x5a01ac..0x5a026f (up, −1) and 0x5a02f8..0x5a03bd (down, +1): the name in the whisper buffer stays in the current
// slot, the buffer empties, the slot moves (from 0 to LastWhisperNameSlot), the whisper EDIT takes the new slot's name
// (and the focus with everything selected if the field is the odd one) and the buffer too. Past the edges, nothing.
// It returns whether the 0x19 sounded.
bool StepTheWhisperName(ChatInputState& input, std::int32_t direction, std::int32_t* focusedList = nullptr);

// 0x59ffe0..0x5a0083: strcpy of the name to the whisper buffer and its length; the EDIT does not change (Q-C530).
void PutTheWhisperName(ChatInputState& input, std::string_view name);
} // namespace Client::Chat
