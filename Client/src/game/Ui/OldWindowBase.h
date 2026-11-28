#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>

// The old windows' base window (CL8 milestone 7; 06 § HUD y ventanas, "Las ventanas viejas"), 0x45a210 (vtable
// 0x6b8d6c), and what they all share: the states, the messages and the button (0x45ad80). They are here and not in
// Ui/OldWindows.h so that the controls of Ui/OldControls.h (the lists and the EDIT) use them without the manager.
namespace Client::Ui
{
// +0x48 (0x45a9b0 and 0x45a9c0): shown, resizing, hidden, dragged, minimized (nobody sets it in CL8) and disabled
// (the button, 0x45b200; nobody sets it in CL8).
inline constexpr std::int32_t ShownWindow = 0;
inline constexpr std::int32_t ResizedWindow = 1;
inline constexpr std::int32_t HiddenWindow = 3;
inline constexpr std::int32_t DraggedWindow = 4;
inline constexpr std::int32_t MinimizedWindow = 5;
inline constexpr std::int32_t DisabledWindow = 6;

// The manager's (0x490b70) and the windows' (0x45abc0, 0x4a3b80) messages: raise and focus, minimize, stretch (vtable
// + 0x38, 0x4930f0), close and send to the back; place and size against the parent; a button's command, and the
// notice to a message box's owner. The manager sends the 6 to the one it raises: the friends window (0x4a2be0) and
// the add-a-friend one (0x4a3850) handle it.
inline constexpr std::int32_t RaiseMessage = 1;
inline constexpr std::int32_t MinimizeMessage = 2;
inline constexpr std::int32_t StretchMessage = 3;
inline constexpr std::int32_t CloseMessage = 4;
inline constexpr std::int32_t SendBackMessage = 5;
inline constexpr std::int32_t RaisedMessage = 6;
// 0x467b10: Enter in an old window's EDIT sends it to its parent.
inline constexpr std::int32_t EnterMessage = 7;
inline constexpr std::int32_t PlaceMessage = 8;
inline constexpr std::int32_t SizeMessage = 9;
inline constexpr std::int32_t CommandMessage = 10;
// 0x4a37e0: the name typed in the add-a-friend box, to the tab that opened it (OldMessage::text).
inline constexpr std::int32_t NameMessage = 11;
inline constexpr std::int32_t AnswerMessage = 12;
// The lists' (0x47a9d0): the row with a double click (to the parent, 0x461f00) or a click (0x462bd0), go to the end,
// and the up and down arrow keys (0x47b0f0).
inline constexpr std::int32_t ChosenRowMessage = 13;
inline constexpr std::int32_t ToTheEndMessage = 14;
inline constexpr std::int32_t ArrowUpMessage = 15;
inline constexpr std::int32_t ArrowDownMessage = 16;

// 0x459f70: a 12-byte message (the type and two arguments) at the end of the queue (+0x08..+0x30, a std::deque);
// 0x45a140 takes the first out to +0x34..+0x3c. The message deque (stride 12, blocks of 0xffc) uses 0x472d30; it is
// not the 20-byte FriendEntry deque.
struct OldMessage
{
    std::int32_t type = 0;
    std::int32_t a = 0;
    std::int32_t b = 0;
    // The 11 carries in b a pointer to a 0x100-byte buffer (malloc at 0x4a37e0, free at 0x49d6d6): MuRet carries the
    // text.
    std::string text;

    friend bool operator==(const OldMessage&, const OldMessage&) = default;
};

// 0x45a210: what they all share.
struct OldWindowBase
{
    // +0x40: from the counter 0x5762eb0 (0x459ef0), which increments before handing out.
    std::int32_t id = 0;
    // +0x44: the window it hangs from (0, none).
    std::int32_t parent = 0;
    // +0x48.
    std::int32_t state = ShownWindow;
    // +0x4c.
    std::int32_t flags = 0;
    // +0x50..+0x5c, in 640 × 480. 0x45a210 leaves it at (0, 0), 100 × 100.
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 100;
    std::int32_t height = 100;
    // +0x60, +0x68 and +0x6c (0x45aa10): how message 8 places it against the parent's inner area (0, from the top left;
    // 1, 2 and 3, from the right, from the bottom or from both).
    std::int32_t placeMode = 0;
    std::int32_t placeX = 0;
    std::int32_t placeY = 0;
    // +0x64, +0x70 and +0x74 (0x45aa30): the same for the size with message 9.
    std::int32_t sizeMode = 0;
    std::int32_t sizeX = 0;
    std::int32_t sizeY = 0;
    // +0x78: 0x459f00's mode for the mouse (1, from the top left).
    std::int32_t rectMode = 1;
    std::deque<OldMessage> queue;
    OldMessage current;
};

// 0x45ad80 (vtable 0x6b8d9c): 0x46 × 0x14 with win_button.tga.
struct OldButton
{
    OldWindowBase base;
    // +0x7c: the command it sends to the parent (0x45b150).
    std::int32_t command = 0;
    // +0x80: the text (strdup of 0x45b170), or none.
    std::optional<std::string> label;
    // +0x84: the click started over it.
    std::int32_t pressed = 0;
};

} // namespace Client::Ui
